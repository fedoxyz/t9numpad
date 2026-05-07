/*
 * t9numpad - T9 prediction engine API
 * include/t9.h
 *
 * Layout (your requested mapping):
 *
 *   [KP7=pqrs] [KP8=tuv]  [KP9=wxyz]
 *   [KP4=ghi]  [KP5=jkl]  [KP6=mno]
 *   [KP1=abc]  [KP2=def]  [KP3=symbols]
 *   [KP0=SPC]  [KP-=DEL]  [KP*=MODE]  [ENTER=↵]
 *
 * Modes:
 *   T9_MODE_TEXT   — digit sequence → predicted word, commit on ENTER/KP.
 *   T9_MODE_NUMBER — each key emits the raw digit immediately.
 *   Toggle with KP* (asterisk key).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef T9NUMPAD_T9_H
#define T9NUMPAD_T9_H

#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define T9_MAX_TEXT 256

typedef enum {
    T9_MODE_TEXT   = 0,
    T9_MODE_NUMBER = 1,
} t9_mode_t;

typedef enum {
    T9_NOOP        = 0,
    T9_EMIT_STRING,
    T9_EMIT_KEY,
    T9_BACKSPACE,
    T9_MODE_CHANGED,
    T9_PREVIEW,
} t9_action_t;

typedef struct {
    t9_action_t  action;
    char         text[T9_MAX_TEXT];
    unsigned int keycode;
    t9_mode_t    mode;
} t9_result_t;

typedef struct t9_ctx t9_ctx_t;

t9_ctx_t *t9_create(const t9numpad_config_t *cfg);
void      t9_destroy(t9_ctx_t *ctx);

int  t9_load_dictionary(t9_ctx_t *ctx, const char *path);
void t9_unload_dictionary(t9_ctx_t *ctx);

t9_mode_t t9_get_mode(const t9_ctx_t *ctx);
int       t9_pending_length(const t9_ctx_t *ctx);

int t9_handle_key(t9_ctx_t *ctx, unsigned int keycode, int value,
                  t9_result_t *result);
int t9_flush(t9_ctx_t *ctx, t9_result_t *result);
int t9_save_learned(const t9_ctx_t *ctx, const char *path);

int t9_get_multitap(const t9_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* T9NUMPAD_T9_H */
