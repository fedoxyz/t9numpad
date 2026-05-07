/*
 * t9numpad - Configuration structures and constants
 * include/config.h
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef T9NUMPAD_CONFIG_H
#define T9NUMPAD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define T9NUMPAD_VERSION "0.1.0"

#define CFG_PATH_MAX     256
#define CFG_KEY_MAP_LEN  8   /* max letters per digit + NUL */

typedef struct {
    /* Input device */
    char device_path[CFG_PATH_MAX];

    /* Dictionary / learned frequencies */
    char dict_path[CFG_PATH_MAX];
    char learned_path[CFG_PATH_MAX];

    /* T9 engine */
    char key_map[10][CFG_KEY_MAP_LEN];
    int  commit_timeout_ms;     /* ms of inactivity before auto-commit  */
    int  multitap_enabled;      /* allow old-school multi-tap fallback  */
    int  layout_flipped;        /* 1 = phone style (abc=1), 0 = numpad physical (abc=7) */
    int  symbol_key;            /* which digit key cycles symbols, default 3 */

    /* Logging */
    int  log_level;             /* 0=silent 1=error 2=warn 3=info 4=debug */
    char log_file[CFG_PATH_MAX];/* empty = stderr                        */
} t9numpad_config_t;

/* Populate *cfg with compiled-in defaults */
void config_init_defaults(t9numpad_config_t *cfg);

/*
 * Load a TOML config file.
 * Unrecognised keys are silently ignored for forward-compatibility.
 *
 * @return 0 on success, -1 on fatal parse / IO error
 */
int config_load(t9numpad_config_t *cfg, const char *path);

/*
 * Try common locations to find the input device automatically.
 * Updates cfg->device_path if a numpad is found.
 */
void config_autodetect(t9numpad_config_t *cfg);

void config_apply_layout(t9numpad_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* T9NUMPAD_CONFIG_H */
