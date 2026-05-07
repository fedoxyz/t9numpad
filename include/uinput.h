/*
 * t9numpad - Linux virtual keyboard (uinput) API
 * include/uinput.h
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef T9NUMPAD_UINPUT_H
#define T9NUMPAD_UINPUT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct uinput_dev uinput_dev_t;

/*
 * Create a virtual keyboard via /dev/uinput.
 * @param name  Name reported to the kernel (max 80 chars)
 * @return      Opaque handle, or NULL on failure
 */
uinput_dev_t *uinput_create(const char *name);

/* Destroy the virtual keyboard and release the uinput fd */
void uinput_destroy(uinput_dev_t *dev);

/*
 * Emit a single key event (press or release).
 * @param dev     Handle from uinput_create()
 * @param keycode Linux KEY_* code
 * @param value   1 = press, 0 = release
 */
void uinput_emit_key(uinput_dev_t *dev, unsigned int keycode, int value);

/*
 * Type a NUL-terminated UTF-8 string by synthesising individual
 * key-press / key-release pairs.  Only ASCII printable characters
 * and newline are supported without an X/Wayland compositor.
 */
void uinput_emit_string(uinput_dev_t *dev, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* T9NUMPAD_UINPUT_H */
