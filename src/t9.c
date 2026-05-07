/*
 * t9numpad - T9 prediction engine
 * src/t9.c
 *
 * Key layout (your design):
 *
 *   7=pqrs   8=tuv    9=wxyz
 *   4=ghi    5=jkl    6=mno
 *   1=abc    2=def    3=symbols(.,!?)
 *   0=SPACE  -=DEL    *=MODE  ENTER=↵
 *
 * TEXT mode:  type digit sequence → shows best matching word → ENTER commits.
 *             0 inserts space after commit, - deletes last digit or backspaces.
 * NUMBER mode: each key immediately types the digit. Toggle with *.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <linux/input-event-codes.h>

#include "t9.h"
#include "config.h"
#include "log.h"

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define DICT_HASH_SIZE  (1 << 17)
#define MAX_WORD_LEN    64
#define SEQUENCE_MAX    32

/* ------------------------------------------------------------------ */
/* Types                                                                */
/* ------------------------------------------------------------------ */

typedef struct dict_entry {
    char              word[MAX_WORD_LEN];
    uint32_t          freq;
    struct dict_entry *next;
} dict_entry_t;

struct t9_ctx {
    /* Key map: digit 0-9 → letters string */
    char key_map[10][8];

    /* Current input state */
    uint8_t  sequence[SEQUENCE_MAX];
    int      seq_len;

    /* Mode */
    t9_mode_t mode;

    /* After committing a word, should we append a space? */
    int pending_space;

    /* Symbol multi-tap */
    int symbol_key;
    int symbol_tap_count;

    /* Multi-tap / spell mode */
    int  multitap_enabled;
    int  multitap_count;
    uint8_t multitap_digit;
    struct timespec multitap_last;
    char spell_buf[MAX_WORD_LEN];
    int  spell_mode;
    int multitap_letter_pending;

    /* Dictionary */
    dict_entry_t **hash;
    int            dict_loaded;

    /* Config */
    int commit_timeout_ms;
};

/* ------------------------------------------------------------------ */
/* Default key map (your requested layout)                             */
/* ------------------------------------------------------------------ */

static const char *DEFAULT_KEY_MAP[10] = {
    " ",     /* 0 → space (committed immediately in text mode)  */
    "abc",   /* 1 */
    "def",   /* 2 */
    ".,!?",  /* 3 → punctuation / symbols                       */
    "ghi",   /* 4 */
    "jkl",   /* 5 */
    "mno",   /* 6 */
    "pqrs",  /* 7 */
    "tuv",   /* 8 */
    "wxyz",  /* 9 */
};

/* Symbols emitted by key 3 in order of press count (multi-tap) */
static const char *SYMBOLS_3[] = { ".", ",", "!", "?", "-", "'", NULL };

/* ------------------------------------------------------------------ */
/* Hash                                                                 */
/* ------------------------------------------------------------------ */

static uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    unsigned char c;
    while ((c = (unsigned char)*s++))
        h = ((h << 5) + h) ^ c;
    return h;
}

static dict_entry_t *dict_find(t9_ctx_t *ctx, const char *word)
{
    if (!ctx->dict_loaded) return NULL;
    uint32_t idx = djb2(word) & (DICT_HASH_SIZE - 1);
    for (dict_entry_t *e = ctx->hash[idx]; e; e = e->next)
        if (strcmp(e->word, word) == 0)
            return e;
    return NULL;
}

static void dict_insert(t9_ctx_t *ctx, const char *word, uint32_t freq)
{
    if (strlen(word) >= MAX_WORD_LEN) return;
    dict_entry_t *e = dict_find(ctx, word);
    if (e) { e->freq += freq; return; }
    e = malloc(sizeof *e);
    if (!e) return;
    strncpy(e->word, word, sizeof(e->word) - 1);
    e->word[sizeof(e->word) - 1] = '\0';
    e->freq = freq;
    uint32_t idx = djb2(word) & (DICT_HASH_SIZE - 1);
    e->next = ctx->hash[idx];
    ctx->hash[idx] = e;
}

/* ------------------------------------------------------------------ */
/* Prediction                                                           */
/* ------------------------------------------------------------------ */

static int word_matches(t9_ctx_t *ctx, const char *word,
                        const uint8_t *seq, int slen)
{
    if ((int)strlen(word) != slen) return 0;
    for (int i = 0; i < slen; i++) {
        int d = seq[i];
        if (d < 0 || d > 9) return 0;
        if (!strchr(ctx->key_map[d], tolower((unsigned char)word[i])))
            return 0;
    }
    return 1;
}

static const char *predict(t9_ctx_t *ctx)
{
    if (!ctx->dict_loaded || ctx->seq_len == 0) return NULL;
    const char *best = NULL;
    uint32_t    best_freq = 0;
    for (int b = 0; b < DICT_HASH_SIZE; b++) {
        for (dict_entry_t *e = ctx->hash[b]; e; e = e->next) {
            if (word_matches(ctx, e->word, ctx->sequence, ctx->seq_len)
                && e->freq > best_freq) {
                best_freq = e->freq;
                best = e->word;
            }
        }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Fallback: spell out the sequence as letters (first letter per key) */
/* ------------------------------------------------------------------ */

static void sequence_to_fallback(t9_ctx_t *ctx, char *out, int outsz)
{
    int pos = 0;
    for (int i = 0; i < ctx->seq_len && pos < outsz - 1; i++) {
        int d = ctx->sequence[i];
        if (d >= 0 && d <= 9 && ctx->key_map[d][0])
            out[pos++] = ctx->key_map[d][0];
    }
    out[pos] = '\0';
}

/* ------------------------------------------------------------------ */
/* Keycode → digit                                                      */
/* ------------------------------------------------------------------ */

static int kc_to_digit(unsigned int kc)
{
    switch (kc) {
    case KEY_KP0: return 0;
    case KEY_KP1: case KEY_END:      return 1;
    case KEY_KP2: case KEY_DOWN:     return 2;
    case KEY_KP3: case KEY_PAGEDOWN: return 3;
    case KEY_KP4: case KEY_LEFT:     return 4;
    case KEY_KP5:                    return 5;
    case KEY_KP6: case KEY_RIGHT:    return 6;
    case KEY_KP7: case KEY_HOME:     return 7;
    case KEY_KP8: case KEY_UP:       return 8;
    case KEY_KP9: case KEY_PAGEUP:   return 9;
    default:                         return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

t9_ctx_t *t9_create(const t9numpad_config_t *cfg)
{
    t9_ctx_t *ctx = calloc(1, sizeof *ctx);
    if (!ctx) return NULL;

    ctx->hash = calloc(DICT_HASH_SIZE, sizeof(dict_entry_t *));
    if (!ctx->hash) { free(ctx); return NULL; }

    for (int i = 0; i < 10; i++) {
        if (cfg->key_map[i][0])
            strncpy(ctx->key_map[i], cfg->key_map[i], sizeof(ctx->key_map[i]) - 1);
        else
            strncpy(ctx->key_map[i], DEFAULT_KEY_MAP[i], sizeof(ctx->key_map[i]) - 1);
    }

    ctx->symbol_key       = (cfg->symbol_key >= 0 && cfg->symbol_key <= 9)
                             ? cfg->symbol_key : 3;
    ctx->multitap_enabled = cfg->multitap_enabled;

    ctx->commit_timeout_ms = cfg->commit_timeout_ms > 0
                             ? cfg->commit_timeout_ms : 1500;
    ctx->mode = T9_MODE_TEXT;
    ctx->multitap_letter_pending = 0;
    return ctx;
}

void t9_destroy(t9_ctx_t *ctx)
{
    if (!ctx) return;
    t9_unload_dictionary(ctx);
    free(ctx->hash);
    free(ctx);
}

int t9_load_dictionary(t9_ctx_t *ctx, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn("t9_load_dictionary: cannot open %s: %s", path, strerror(errno));
        return -1;
    }
    char     line[256];
    unsigned long loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char     word[MAX_WORD_LEN] = {0};
        uint32_t freq = 1;
        if (sscanf(line, "%63s %u", word, &freq) >= 1) {
            dict_insert(ctx, word, freq);
            loaded++;
        }
    }
    fclose(f);
    ctx->dict_loaded = 1;
    log_info("Loaded %lu words from %s", loaded, path);
    return 0;
}

void t9_unload_dictionary(t9_ctx_t *ctx)
{
    if (!ctx->dict_loaded) return;
    for (int b = 0; b < DICT_HASH_SIZE; b++) {
        dict_entry_t *e = ctx->hash[b];
        while (e) { dict_entry_t *n = e->next; free(e); e = n; }
        ctx->hash[b] = NULL;
    }
    ctx->dict_loaded = 0;
}

t9_mode_t t9_get_mode(const t9_ctx_t *ctx) { return ctx->mode; }
int       t9_pending_length(const t9_ctx_t *ctx) { return ctx->seq_len; }

/* Commit whatever is buffered */
int t9_flush(t9_ctx_t *ctx, t9_result_t *result)
{
    memset(result, 0, sizeof *result);

    /* Flush a spelled word (multi-tap) */
   if (ctx->spell_mode && (ctx->spell_buf[0] || ctx->multitap_digit != 0)) {
        if (ctx->multitap_digit != 0 && ctx->multitap_letter_pending) {
            const char *letters = ctx->key_map[ctx->multitap_digit];
            int nletters = (int)strlen(letters);
            if (nletters > 0) {
                int li = (ctx->multitap_count - 1) % nletters;
                int slen = (int)strlen(ctx->spell_buf);
                if (slen < MAX_WORD_LEN - 1) {
                    ctx->spell_buf[slen]   = letters[li];
                    ctx->spell_buf[slen+1] = '\0';
                }
            }
        }
        ctx->multitap_letter_pending = 0;
        strncpy(result->text, ctx->spell_buf, T9_MAX_TEXT - 1);
        result->action = T9_EMIT_STRING;
        dict_insert(ctx, ctx->spell_buf, 5);
        log_info("Learned new word: '%s'", ctx->spell_buf);
        /* reset all multitap state */
        memset(ctx->spell_buf, 0, sizeof ctx->spell_buf);
        ctx->spell_mode     = 0;
        ctx->multitap_digit = 0;
        ctx->multitap_count = 0;
        ctx->seq_len        = 0;
        ctx->pending_space  = 1;
        return 0;
    }

    if (ctx->seq_len == 0) { result->action = T9_NOOP; return -1; }

    const char *word = predict(ctx);
    if (word) {
        strncpy(result->text, word, T9_MAX_TEXT - 1);
    } else {
        sequence_to_fallback(ctx, result->text, T9_MAX_TEXT);
    }
    result->action = T9_EMIT_STRING;
    ctx->seq_len       = 0;
    ctx->pending_space = 1;
    return 0;
}

int t9_handle_key(t9_ctx_t *ctx, unsigned int keycode, int value,
                  t9_result_t *result)
{
    memset(result, 0, sizeof *result);
    result->action = T9_NOOP;

    /* Ignore key releases */
    if (value == 0) return -1;

    /* ── MODE TOGGLE: KP* or KPSLASH ───────────────────────────────── */
    if (keycode == KEY_KPSLASH) {
            ctx->multitap_enabled = !ctx->multitap_enabled;
            if (ctx->seq_len > 0) {
                t9_flush(ctx, result);
                return 0;
            }
            result->action = T9_MODE_CHANGED;   /* was T9_NOOP */
            result->mode   = ctx->mode;         /* TEXT or NUMBER, doesn't matter */
            return 0;
        }

    /* KP* : toggle TEXT / NUMBER mode */
    if (keycode == KEY_KPASTERISK) {
        /* Flush any pending sequence first */
        if (ctx->seq_len > 0) {
            t9_result_t flush;
            t9_flush(ctx, &flush);
            /* We can only return one action; emit the word, mode change
               will be logged. Caller will see the string; next keypress
               will reflect new mode. */
            ctx->mode = (ctx->mode == T9_MODE_TEXT) ? T9_MODE_NUMBER : T9_MODE_TEXT;
            log_info("Mode → %s", ctx->mode == T9_MODE_TEXT ? "TEXT" : "NUMBER");
            strncpy(result->text, flush.text, T9_MAX_TEXT - 1);
            result->action = T9_EMIT_STRING;
            return 0;
        }
        ctx->mode = (ctx->mode == T9_MODE_TEXT) ? T9_MODE_NUMBER : T9_MODE_TEXT;
        result->action = T9_MODE_CHANGED;
        result->mode   = ctx->mode;
        log_info("Mode → %s", ctx->mode == T9_MODE_TEXT ? "TEXT" : "NUMBER");
        return 0;
    }

    /* ── NUMBER MODE ─────────────────────────────────────────────────── */
    if (ctx->mode == T9_MODE_NUMBER) {
        int digit = kc_to_digit(keycode);
        if (digit >= 0) {
            result->text[0] = '0' + digit;
            result->text[1] = '\0';
            result->action  = T9_EMIT_STRING;
            return 0;
        }
        /* Also pass KP- as backspace in number mode */
        if (keycode == KEY_KPMINUS) {
            result->action = T9_BACKSPACE;
            return 0;
        }
        if (keycode == KEY_KPENTER || keycode == KEY_ENTER) {
            result->action  = T9_EMIT_KEY;
            result->keycode = KEY_ENTER;
            return 0;
        }
        return -1;
    }

    /* ── TEXT MODE ───────────────────────────────────────────────────── */

    /* KP- : delete last digit in buffer, or backspace if buffer empty */
    if (keycode == KEY_KPMINUS) {
        if (ctx->seq_len > 0) {
            ctx->seq_len--;
            /* Update preview */
            if (ctx->seq_len > 0) {
                const char *w = predict(ctx);
                if (w) strncpy(result->text, w, T9_MAX_TEXT - 1);
                else   sequence_to_fallback(ctx, result->text, T9_MAX_TEXT);
                result->action = T9_PREVIEW;
            } else {
                result->action = T9_PREVIEW; /* empty preview */
            }
        } else {
            result->action = T9_BACKSPACE;
        }
        return 0;
    }

    /* KP0 : space — commit current sequence then emit space */
    if (keycode == KEY_KP0) {
        if (ctx->symbol_tap_count > 0) {
            static const char *syms[] = {".", ",", "!", "?", "-", "'", NULL};
            int nsyms = 0; while (syms[nsyms]) nsyms++;
            int idx = (ctx->symbol_tap_count - 1) % nsyms;
            result->text[0] = syms[idx][0];
            result->text[1] = '\0';
            result->action  = T9_EMIT_STRING;
            ctx->symbol_tap_count = 0;
            return 0;
        }
        if (ctx->spell_mode && (ctx->spell_buf[0] || ctx->multitap_digit)) {
            t9_result_t flush;
            t9_flush(ctx, &flush);
            /* append space */
            int len = (int)strlen(flush.text);
            if (len < T9_MAX_TEXT - 1) {
                flush.text[len]   = ' ';
                flush.text[len+1] = '\0';
            }
            memcpy(result, &flush, sizeof *result);
            return 0;
        }
        if (ctx->seq_len > 0) {
            /* commit word + space */
            const char *word = predict(ctx);
            const char *src  = word ? word : "";
            char tmp[T9_MAX_TEXT];
            if (!word) sequence_to_fallback(ctx, tmp, T9_MAX_TEXT), src = tmp;
            int len = (int)strlen(src);
            if (len < T9_MAX_TEXT - 1) {
                memcpy(result->text, src, (size_t)len);
                result->text[len]   = ' ';
                result->text[len+1] = '\0';
            } else {
                strncpy(result->text, src, T9_MAX_TEXT - 1);
            }
            ctx->seq_len      = 0;
            ctx->pending_space = 0;
            result->action    = T9_EMIT_STRING;
        } else {
            /* buffer empty: just a space */
            result->text[0] = ' ';
            result->text[1] = '\0';
            result->action  = T9_EMIT_STRING;
        }
        return 0;
    }

    /* ENTER / KP_ENTER : commit word + newline */
    if (keycode == KEY_KPENTER || keycode == KEY_ENTER) {
        if (ctx->symbol_tap_count > 0) {
            static const char *syms[] = {".", ",", "!", "?", "-", "'", NULL};
            int nsyms = 0; while (syms[nsyms]) nsyms++;
            int idx = (ctx->symbol_tap_count - 1) % nsyms;
            result->text[0] = syms[idx][0];
            result->text[1] = '\0';
            result->action  = T9_EMIT_STRING;
            ctx->symbol_tap_count = 0;
            return 0;
        }
        if (ctx->seq_len > 0 ||
            (ctx->spell_mode && (ctx->spell_buf[0] || ctx->multitap_digit))) {
            t9_flush(ctx, result);
            int len = (int)strlen(result->text);
            if (len < T9_MAX_TEXT - 1) {
                result->text[len]   = '\n';
                result->text[len+1] = '\0';
            }
            ctx->pending_space = 0;
        } else {
            result->action  = T9_EMIT_KEY;
            result->keycode = KEY_ENTER;
        }
        return 0;
    }

    /* KP. / KP_COMMA : commit without newline */
    if (keycode == KEY_KPDOT || keycode == KEY_KPCOMMA) {
        if (ctx->seq_len > 0) {
            t9_flush(ctx, result);
        }
        return 0;
    }

    /* Digit keys 1-9 */
    int digit = kc_to_digit(keycode);
    if (digit >= 1 && digit <= 9) {

        /* ── Symbol key: multi-tap cycling, never enters T9 sequence ── */
        if (digit == ctx->symbol_key) {
            if (ctx->seq_len > 0) {
                /* Commit pending T9 word first, symbol on next press */
                t9_flush(ctx, result);
                ctx->symbol_tap_count = 0;
                return 0;
            }
            static const char *syms[] = {".", ",", "!", "?", "-", "'", NULL};
            int nsyms = 0;
            while (syms[nsyms]) nsyms++;
            ctx->symbol_tap_count++;
            int idx = (ctx->symbol_tap_count - 1) % nsyms;
            result->text[0] = syms[idx][0];
            result->text[1] = '\0';
            result->action  = T9_PREVIEW;  /* commit with KP0 or ENTER */
            return 0;
        }

        /* Pressing a non-symbol key commits any pending symbol */
        if (ctx->symbol_tap_count > 0) {
            static const char *syms[] = {".", ",", "!", "?", "-", "'", NULL};
            int nsyms = 0; while (syms[nsyms]) nsyms++;
            int idx = (ctx->symbol_tap_count - 1) % nsyms;
            /* Emit the symbol, then fall through to process this digit next
               call — we can only return one action, so reset and return */
            result->text[0] = syms[idx][0];
            result->text[1] = '\0';
            result->action  = T9_EMIT_STRING;
            ctx->symbol_tap_count = 0;
            /* Re-queue this keypress by NOT consuming it — but since we
               can't re-queue, just reset; user presses the digit again */
            return 0;
        }

        /* ── Multi-tap mode ── */
        if (ctx->multitap_enabled) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long ms = (now.tv_sec  - ctx->multitap_last.tv_sec)  * 1000
                    + (now.tv_nsec - ctx->multitap_last.tv_nsec) / 1000000;

            if (digit == (int)ctx->multitap_digit && ms < ctx->commit_timeout_ms) {
                /* Same key within timeout: cycle to next letter */
                ctx->multitap_count++;
            } else {
            /* commit previous letter into spell_buf */
            if (ctx->multitap_digit != 0 && ctx->multitap_letter_pending) {
                const char *letters = ctx->key_map[ctx->multitap_digit];
                int nletters = (int)strlen(letters);
                if (nletters > 0) {
                    int li = (ctx->multitap_count - 1) % nletters;
                    int slen = (int)strlen(ctx->spell_buf);
                    if (slen < MAX_WORD_LEN - 1) {
                        ctx->spell_buf[slen]   = letters[li];
                        ctx->spell_buf[slen+1] = '\0';
                    }
                }
            }
            ctx->multitap_count          = 1;
            ctx->multitap_digit          = (uint8_t)digit;
            ctx->multitap_letter_pending = 1;  /* new key is now cycling */
            ctx->spell_mode              = 1;
            }
            ctx->multitap_last = now;

            const char *letters = ctx->key_map[digit];
            int nletters = (int)strlen(letters);
            char cur = nletters > 0
                ? letters[(ctx->multitap_count - 1) % nletters] : '?';
            snprintf(result->text, T9_MAX_TEXT, "%s%c", ctx->spell_buf, cur);
            result->action = T9_PREVIEW;
            return 0;
        }

        /* ── Normal T9 ── */
        if (ctx->seq_len < SEQUENCE_MAX)
            ctx->sequence[ctx->seq_len++] = (uint8_t)digit;

        const char *word = predict(ctx);
        if (word)
            strncpy(result->text, word, T9_MAX_TEXT - 1);
        else
            sequence_to_fallback(ctx, result->text, T9_MAX_TEXT);
        result->action = T9_PREVIEW;
        return 0;
    }

    return -1;
}

int t9_save_learned(const t9_ctx_t *ctx, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("t9_save_learned: %s: %s", path, strerror(errno));
        return -1;
    }
    for (int b = 0; b < DICT_HASH_SIZE; b++)
        for (dict_entry_t *e = ctx->hash[b]; e; e = e->next)
            fprintf(f, "%s %u\n", e->word, e->freq);
    fclose(f);
    return 0;
}

int t9_get_multitap(const t9_ctx_t *ctx) { return ctx->multitap_enabled; }
