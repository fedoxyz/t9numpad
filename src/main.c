/*
 * t9numpad - Program entry point, event loop, startup/shutdown
 * src/main.c
 *
 * Handles:
 *  - Standard USB numpads (EV_KEY, KEY_KP*)
 *  - Asus Touchpad/Numpad (also sends EV_KEY when numpad layer is active)
 *  - Preview output: prints current prediction to stderr so you can see it
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include "config.h"
#include "log.h"
#include "t9.h"
#include "uinput.h"

#define MAX_EVENTS 16

static volatile sig_atomic_t running = 1;

/* ------------------------------------------------------------------ */
/* Signals                                                              */
/* ------------------------------------------------------------------ */

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static int setup_signals(void)
{
    struct sigaction sa = { .sa_handler = handle_signal, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        log_error("sigaction: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Preview line                                                          */
/* ------------------------------------------------------------------ */

/*
 * Print a live preview line to stderr so the user can see the current
 * T9 prediction while typing.  Uses carriage-return to overwrite the line.
 *
 * Format:
 *   [TEXT]  ▶ hello_    (pending prediction)
 *   [NUM ]  123          (number mode)
 *   [TEXT]  (empty)      (nothing buffered)
 */
static void print_preview(t9_ctx_t *t9, const char *text)
{
    const char *mode;
    if (t9_get_mode(t9) == T9_MODE_NUMBER)
        mode = "NUM ";
    else if (t9_get_multitap(t9))
        mode = "TAP ";
    else
        mode = "TEXT";

    fprintf(stderr, "\r[%s] ▶ %-40s", mode, text ? text : "");
    fflush(stderr);
    FILE *pf = fopen("/tmp/t9numpad_preview", "w");
    if (pf) { fprintf(pf, "[%s] %s\n", mode, text ? text : ""); fclose(pf); }
}

static void print_mode_banner(t9_mode_t mode)
{
    if (mode == T9_MODE_NUMBER)
        fprintf(stderr, "\r*** NUMBER MODE — type digits directly  (KP* to switch back) ***\n");
    else
        fprintf(stderr, "\r*** TEXT MODE   — T9 prediction active  (KP* to switch to numbers) ***\n");
    fflush(stderr);
}

/* ------------------------------------------------------------------ */
/* Device opening                                                       */
/* ------------------------------------------------------------------ */

static int open_input_device(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        log_error("open(%s): %s", path, strerror(errno));
        return -1;
    }

    /*
     * Grab the device so KEY events don't ALSO go to the compositor/terminal.
     * Without this, every numpad keypress would type its default character
     * AND our predicted text — resulting in doubled / garbled output.
     */
    if (ioctl(fd, EVIOCGRAB, (void *)1) < 0) {
        log_warn("EVIOCGRAB(%s): %s — events may leak to other apps", path, strerror(errno));
    } else {
        log_info("Grabbed %s exclusively", path);
    }
    return fd;
}

/* ------------------------------------------------------------------ */
/* Event processing                                                     */
/* ------------------------------------------------------------------ */

static void process_event(t9_ctx_t *t9, uinput_dev_t *vkbd,
                           const struct input_event *ev)
{
    /* We only care about key events */
    if (ev->type != EV_KEY)
        return;

    t9_result_t result;
    if (t9_handle_key(t9, ev->code, ev->value, &result) != 0)
        return;

    switch (result.action) {

    case T9_NOOP:
        break;

    case T9_PREVIEW:
        print_preview(t9, result.text);
        break;

    case T9_EMIT_STRING:
        /* Clear the preview line before emitting */
        fprintf(stderr, "\r%-60s\r", "");
        fflush(stderr);
        uinput_emit_string(vkbd, result.text);
        /* Boost frequency for the committed word */
        break;

    case T9_EMIT_KEY:
        fprintf(stderr, "\r%-60s\r", "");
        fflush(stderr);
        uinput_emit_key(vkbd, result.keycode, 1);
        uinput_emit_key(vkbd, result.keycode, 0);
        break;

    case T9_BACKSPACE:
        fprintf(stderr, "\r%-60s\r", "");
        fflush(stderr);
        uinput_emit_key(vkbd, KEY_BACKSPACE, 1);
        uinput_emit_key(vkbd, KEY_BACKSPACE, 0);
        break;

    case T9_MODE_CHANGED:
        print_mode_banner(result.mode);
        print_preview(t9, NULL);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                  */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    const char *cfg_path = NULL;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0)
            && i + 1 < argc) {
            cfg_path = argv[++i];
        } else if (strcmp(argv[i], "--version") == 0) {
            puts("t9numpad " T9NUMPAD_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-c CONFIG]\n\n"
                   "Key layout (TEXT mode):\n"
                   "  1=abc  2=def  3=symbols\n"
                   "  4=ghi  5=jkl  6=mno\n"
                   "  7=pqrs 8=tuv  9=wxyz\n"
                   "  0=SPC  -=DEL  *=toggle NUM/TEXT  ENTER=commit+newline\n",
                   argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    /* 1. Config */
    t9numpad_config_t cfg;
    config_init_defaults(&cfg);
    static const char *DEFAULT_CFG_PATHS[] = {
        "/etc/t9numpad/t9numpad.toml",
        "/etc/t9numpad.toml",
        NULL
    };
    if (cfg_path) {
        if (config_load(&cfg, cfg_path) != 0) {
            fprintf(stderr, "Failed to load config: %s\n", cfg_path);
            return 1;
        }
    } else {
        int loaded = 0;
        for (int i = 0; DEFAULT_CFG_PATHS[i]; i++) {
            if (config_load(&cfg, DEFAULT_CFG_PATHS[i]) == 0) {
                loaded = 1;
                break;
            }
        }
        /* If config didn't set a device, autodetect */
        if (!cfg.device_path[0])
            config_autodetect(&cfg);
    }

    /* 2. Logging */
    log_init(cfg.log_level, cfg.log_file);
    log_info("t9numpad " T9NUMPAD_VERSION " starting");

    if (setup_signals() != 0) return 1;

    /* 3. T9 engine */
    t9_ctx_t *t9 = t9_create(&cfg);
    if (!t9) { log_error("Failed to init T9 engine"); return 1; }

    if (t9_load_dictionary(t9, cfg.dict_path) != 0)
        log_warn("No dictionary — prediction disabled, fallback to first-letter");

    /* 4. Virtual keyboard */
    uinput_dev_t *vkbd = uinput_create("t9numpad-vkbd");
    if (!vkbd) {
        log_error("Failed to create uinput device — is uinput loaded?  "
                  "Run: sudo modprobe uinput");
        t9_destroy(t9);
        return 1;
    }

    /* 5. Open physical device */
    int input_fd = open_input_device(cfg.device_path);
    if (input_fd < 0) {
        uinput_destroy(vkbd);
        t9_destroy(t9);
        return 1;
    }

    /* 6. Print startup banner */
    fprintf(stderr,
        "\nt9numpad " T9NUMPAD_VERSION " running\n"
        "  Device : %s\n"
        "  Mode   : TEXT (KP* to toggle numbers)\n"
        "  Keys   : 1-9=letters  0=space  -=delete  *=mode  ENTER=commit\n"
        "─────────────────────────────────────────────────────────────────\n",
        cfg.device_path);
    print_preview(t9, NULL);

    /* 7. epoll loop */
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        log_error("epoll_create1: %s", strerror(errno));
        close(input_fd); uinput_destroy(vkbd); t9_destroy(t9);
        return 1;
    }
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = input_fd };
    epoll_ctl(epfd, EPOLL_CTL_ADD, input_fd, &ev);

    struct epoll_event events[MAX_EVENTS];

    while (running) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            log_error("epoll_wait: %s", strerror(errno));
            break;
        }
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd != input_fd) continue;
            struct input_event iev;
            ssize_t r;
            while ((r = read(input_fd, &iev, sizeof(iev))) == sizeof(iev))
                process_event(t9, vkbd, &iev);
            if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                log_error("Input device disconnected");
                running = 0;
            }
        }
    }

    /* 8. Clean shutdown */
    fprintf(stderr, "\n");
    log_info("Shutting down");

    /* Flush any pending sequence */
    t9_result_t final;
    if (t9_flush(t9, &final) == 0 && final.text[0])
        uinput_emit_string(vkbd, final.text);

    /* Save learned frequencies */
    if (cfg.learned_path[0])
        t9_save_learned(t9, cfg.learned_path);

    close(epfd);
    close(input_fd);
    uinput_destroy(vkbd);
    t9_destroy(t9);
    log_close();
    return 0;
}
