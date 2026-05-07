/*
 * t9numpad - Linux virtual keyboard (uinput) implementation
 * src/uinput.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

#include "uinput.h"
#include "log.h"

#define UINPUT_PATH "/dev/uinput"

struct uinput_dev {
    int fd;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void emit(int fd, int type, int code, int val)
{
    struct input_event ev = {
        .type  = (uint16_t)type,
        .code  = (uint16_t)code,
        .value = val,
    };
    /* Kernel ≥ 5.11 ignores the time field; older kernels want it set */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ev.time.tv_sec  = ts.tv_sec;
    ev.time.tv_usec = ts.tv_nsec / 1000;

    if (write(fd, &ev, sizeof ev) < 0)
        log_warn("uinput write: %s", strerror(errno));
}

/* ------------------------------------------------------------------ */
/* ASCII → KEY_* lookup (a-z, 0-9, space, newline, common punctuation) */
/* ------------------------------------------------------------------ */

typedef struct { char c; unsigned int kc; int shift; } key_entry_t;

static const key_entry_t ASCII_MAP[] = {
    /* lowercase */
    {'a', KEY_A, 0}, {'b', KEY_B, 0}, {'c', KEY_C, 0}, {'d', KEY_D, 0},
    {'e', KEY_E, 0}, {'f', KEY_F, 0}, {'g', KEY_G, 0}, {'h', KEY_H, 0},
    {'i', KEY_I, 0}, {'j', KEY_J, 0}, {'k', KEY_K, 0}, {'l', KEY_L, 0},
    {'m', KEY_M, 0}, {'n', KEY_N, 0}, {'o', KEY_O, 0}, {'p', KEY_P, 0},
    {'q', KEY_Q, 0}, {'r', KEY_R, 0}, {'s', KEY_S, 0}, {'t', KEY_T, 0},
    {'u', KEY_U, 0}, {'v', KEY_V, 0}, {'w', KEY_W, 0}, {'x', KEY_X, 0},
    {'y', KEY_Y, 0}, {'z', KEY_Z, 0},
    /* uppercase */
    {'A', KEY_A, 1}, {'B', KEY_B, 1}, {'C', KEY_C, 1}, {'D', KEY_D, 1},
    {'E', KEY_E, 1}, {'F', KEY_F, 1}, {'G', KEY_G, 1}, {'H', KEY_H, 1},
    {'I', KEY_I, 1}, {'J', KEY_J, 1}, {'K', KEY_K, 1}, {'L', KEY_L, 1},
    {'M', KEY_M, 1}, {'N', KEY_N, 1}, {'O', KEY_O, 1}, {'P', KEY_P, 1},
    {'Q', KEY_Q, 1}, {'R', KEY_R, 1}, {'S', KEY_S, 1}, {'T', KEY_T, 1},
    {'U', KEY_U, 1}, {'V', KEY_V, 1}, {'W', KEY_W, 1}, {'X', KEY_X, 1},
    {'Y', KEY_Y, 1}, {'Z', KEY_Z, 1},
    /* digits */
    {'0', KEY_0, 0}, {'1', KEY_1, 0}, {'2', KEY_2, 0}, {'3', KEY_3, 0},
    {'4', KEY_4, 0}, {'5', KEY_5, 0}, {'6', KEY_6, 0}, {'7', KEY_7, 0},
    {'8', KEY_8, 0}, {'9', KEY_9, 0},
    /* punctuation */
    {' ', KEY_SPACE,  0}, {'\n', KEY_ENTER,  0}, {'\t', KEY_TAB,  0},
    {'.', KEY_DOT,    0}, {',', KEY_COMMA,   0}, {'\'', KEY_APOSTROPHE, 0},
    {'-', KEY_MINUS,  0}, {'_', KEY_MINUS,   1},
    /* shifted punctuation */
    {'!', KEY_1,         1},
    {'?', KEY_SLASH,     1},
    {'\'',KEY_APOSTROPHE,0},
    {'-', KEY_MINUS,     0},
    {'_', KEY_MINUS,     1},
    {'(', KEY_9,         1},
    {')', KEY_0,         1},
    {':', KEY_SEMICOLON, 1},
    {';', KEY_SEMICOLON, 0},
    {'@', KEY_2,         1},
    {0, 0, 0} /* sentinel */
};

static const key_entry_t *char_to_key(char c)
{
    for (const key_entry_t *e = ASCII_MAP; e->c; e++)
        if (e->c == c)
            return e;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

uinput_dev_t *uinput_create(const char *name)
{
    int fd = open(UINPUT_PATH, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        log_error("open(%s): %s  (is uinput module loaded? is user in 'input' group?)",
                  UINPUT_PATH, strerror(errno));
        return NULL;
    }

    /* Enable EV_KEY, EV_SYN */
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    /* Register every KEY_* we might emit */
    for (const key_entry_t *e = ASCII_MAP; e->c; e++)
        ioctl(fd, UI_SET_KEYBIT, e->kc);

    ioctl(fd, UI_SET_KEYBIT, KEY_BACKSPACE);
    ioctl(fd, UI_SET_KEYBIT, KEY_LEFTSHIFT);
    ioctl(fd, UI_SET_KEYBIT, KEY_ENTER);

    struct uinput_setup us = {
        .id = {
            .bustype = BUS_VIRTUAL,
            .vendor  = 0x1d6b,   /* Linux Foundation */
            .product = 0x0001,
            .version = 1,
        },
    };
    strncpy(us.name, name, sizeof(us.name) - 1);

    if (ioctl(fd, UI_DEV_SETUP, &us) < 0) {
        log_error("UI_DEV_SETUP: %s", strerror(errno));
        close(fd);
        return NULL;
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        log_error("UI_DEV_CREATE: %s", strerror(errno));
        close(fd);
        return NULL;
    }

    uinput_dev_t *dev = malloc(sizeof *dev);
    if (!dev) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        return NULL;
    }
    dev->fd = fd;
    log_info("Created virtual keyboard '%s' via %s", name, UINPUT_PATH);
    return dev;
}

void uinput_destroy(uinput_dev_t *dev)
{
    if (!dev)
        return;
    ioctl(dev->fd, UI_DEV_DESTROY);
    close(dev->fd);
    free(dev);
}

void uinput_emit_key(uinput_dev_t *dev, unsigned int keycode, int value)
{
    emit(dev->fd, EV_KEY, (int)keycode, value);
    emit(dev->fd, EV_SYN, SYN_REPORT, 0);
}

void uinput_emit_string(uinput_dev_t *dev, const char *text)
{
    for (const char *p = text; *p; p++) {
        const key_entry_t *ke = char_to_key(*p);
        if (!ke) {
            log_warn("uinput_emit_string: no mapping for char 0x%02x", (unsigned char)*p);
            continue;
        }
        if (ke->shift)
            uinput_emit_key(dev, KEY_LEFTSHIFT, 1);

        uinput_emit_key(dev, ke->kc, 1);
        uinput_emit_key(dev, ke->kc, 0);

        if (ke->shift)
            uinput_emit_key(dev, KEY_LEFTSHIFT, 0);
    }
}
