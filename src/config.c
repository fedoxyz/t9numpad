/*
 * t9numpad - Configuration parser and auto-detection
 * src/config.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#include "config.h"
#include "log.h"

/* ------------------------------------------------------------------ */
/* Defaults                                                             */
/* ------------------------------------------------------------------ */

void config_init_defaults(t9numpad_config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);

    /* device_path is left empty; autodetect fills it */
    strncpy(cfg->dict_path,    "/usr/share/t9numpad/en.dict",    CFG_PATH_MAX - 1);
    strncpy(cfg->learned_path, "/var/lib/t9numpad/learned.dict", CFG_PATH_MAX - 1);

    /* Key map — your requested layout */
    strncpy(cfg->key_map[0], " ",    CFG_KEY_MAP_LEN - 1); /* 0 = space  */
    strncpy(cfg->key_map[1], "abc",  CFG_KEY_MAP_LEN - 1);
    strncpy(cfg->key_map[2], "def",  CFG_KEY_MAP_LEN - 1);
    strncpy(cfg->key_map[3], ".,!?", CFG_KEY_MAP_LEN - 1); /* 3 = symbols */
    strncpy(cfg->key_map[4], "ghi",  CFG_KEY_MAP_LEN - 1);
    strncpy(cfg->key_map[5], "jkl",  CFG_KEY_MAP_LEN - 1);
    strncpy(cfg->key_map[6], "mno",  CFG_KEY_MAP_LEN - 1);
    strncpy(cfg->key_map[7], "pqrs", CFG_KEY_MAP_LEN - 1);
    strncpy(cfg->key_map[8], "tuv",  CFG_KEY_MAP_LEN - 1);
    strncpy(cfg->key_map[9], "wxyz", CFG_KEY_MAP_LEN - 1);

    cfg->commit_timeout_ms = 1500;
    cfg->multitap_enabled  = 0;
    cfg->layout_flipped = 0;
    cfg->symbol_key     = 3;
    cfg->log_level         = 3;
}

/* ------------------------------------------------------------------ */
/* Minimal TOML parser                                                  */
/* ------------------------------------------------------------------ */

static char *ltrim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    return s;
}

static void rtrim(char *s)
{
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
}

static void strip_comment(char *s)
{
    int in_str = 0;
    for (char *p = s; *p; p++) {
        if (*p == '"') in_str = !in_str;
        if (!in_str && *p == '#') { *p = '\0'; break; }
    }
}

static char *unquote(char *s)
{
    size_t l = strlen(s);
    if (l >= 2 && s[0] == '"' && s[l-1] == '"') {
        s[l-1] = '\0';
        return s + 1;
    }
    return s;
}

int config_load(t9numpad_config_t *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT)
            log_error("config_load: fopen(%s): %s", path, strerror(errno));
        return -1;
    }

    char line[512];
    char section[64] = "";

    while (fgets(line, sizeof(line), f)) {
        strip_comment(line);
        char *l = ltrim(line);
        rtrim(l);
        if (!*l) continue;

        if (l[0] == '[') {
            char *end = strchr(l, ']');
            if (end) { *end = '\0'; strncpy(section, ltrim(l + 1), sizeof(section) - 1); }
            continue;
        }

        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = ltrim(l);  rtrim(key);
        char *val = ltrim(eq + 1); rtrim(val);
        val = unquote(val);

#define MATCH(s, k)  (strcmp(section, (s)) == 0 && strcmp(key, (k)) == 0)
        if (MATCH("", "device") || MATCH("input", "device"))
            strncpy(cfg->device_path, val, CFG_PATH_MAX - 1);
        else if (MATCH("dict", "path") || MATCH("", "dict"))
            strncpy(cfg->dict_path, val, CFG_PATH_MAX - 1);
        else if (MATCH("dict", "learned"))
            strncpy(cfg->learned_path, val, CFG_PATH_MAX - 1);
        else if (MATCH("t9", "commit_timeout_ms"))
            cfg->commit_timeout_ms = (int)strtol(val, NULL, 10);
        else if (MATCH("t9", "multitap"))
            cfg->multitap_enabled = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (MATCH("t9", "flip_layout"))
            cfg->layout_flipped = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (MATCH("t9", "symbol_key"))
            cfg->symbol_key = (int)strtol(val, NULL, 10);
        else if (MATCH("log", "level"))
            cfg->log_level = (int)strtol(val, NULL, 10);
        else if (MATCH("log", "file"))
            strncpy(cfg->log_file, val, CFG_PATH_MAX - 1);
        else if (strncmp(section, "keymap", 6) == 0) {
            int digit = (int)strtol(key, NULL, 10);
            if (digit >= 0 && digit <= 9)
                strncpy(cfg->key_map[digit], val, CFG_KEY_MAP_LEN - 1);
        }
#undef MATCH
    }

    fclose(f);
    log_info("Loaded config from %s", path);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Auto-detect numpad                                                   */
/* ------------------------------------------------------------------ */

/*
 * Scoring heuristic:
 *   +3  has KEY_KP1..KEY_KP9 (strong numpad signal)
 *   +2  device name contains "numpad", "num pad", "keypad"
 *   +1  device name contains "asus" (Asus touchpad numpad)
 *   -1  device name contains "touchpad" without numpad keyword
 *         (touchpad-only devices score lower)
 *
 * Pick the device with the highest score.
 */
static int score_device(int fd, const char *name_hint)
{
    int score = 0;

    unsigned long evbits[(EV_MAX + 1) / (8 * sizeof(unsigned long)) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return -1;

    /* Must emit key events */
    int has_ev_key = (evbits[EV_KEY / (8*sizeof(unsigned long))]
                      >> (EV_KEY % (8*sizeof(unsigned long)))) & 1;
    if (!has_ev_key) return -1;

    unsigned long keybits[(KEY_MAX + 1) / (8 * sizeof(unsigned long)) + 1] = {0};
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);

    /* Count how many of KP1-KP9 are present */
    int kp_count = 0;
    for (int k = KEY_KP1; k <= KEY_KP9; k++) {
        if ((keybits[k / (8*sizeof(unsigned long))]
             >> (k % (8*sizeof(unsigned long)))) & 1)
            kp_count++;
    }
    if (kp_count < 5) return -1;  /* not a numpad */
    score += 3;

    /* Name heuristics */
    if (!name_hint) return score;
    char lower[256] = {0};
    strncpy(lower, name_hint, sizeof(lower) - 1);
    for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);

    if (strstr(lower, "numpad") || strstr(lower, "num pad") || strstr(lower, "keypad"))
        score += 2;
    if (strstr(lower, "asus"))
        score += 1;
    if (strstr(lower, "touchpad") && !strstr(lower, "numpad"))
        score -= 1;

    return score;
}

void config_autodetect(t9numpad_config_t *cfg)
{
    DIR *d = opendir("/dev/input");
    if (!d) return;

    int  best_score = -1;
    char best_path[CFG_PATH_MAX] = {0};

    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;

        char path[CFG_PATH_MAX + 16];
        snprintf(path, sizeof(path), "/dev/input/%.220s", de->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        /* Read device name */
        char name[256] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);

        int s = score_device(fd, name);
        close(fd);

        if (s > best_score) {
            best_score = s;
            strncpy(best_path, path, CFG_PATH_MAX - 1);
            log_info("  Candidate: %s (%s) score=%d", path, name, s);
        }
    }
    closedir(d);

    if (best_score >= 0) {
        strncpy(cfg->device_path, best_path, CFG_PATH_MAX - 1);
        log_info("Auto-detected numpad at %s (score=%d)", best_path, best_score);
    } else {
        log_warn("No numpad found. Set [input] device = /dev/input/eventN in config.");
    }
}

void config_apply_layout(t9numpad_config_t *cfg)
{
    if (!cfg->layout_flipped) return;
    char tmp[CFG_KEY_MAP_LEN];
    int pairs[][2] = {{1,7},{2,8},{3,9},{-1,-1}};
    for (int i = 0; pairs[i][0] >= 0; i++) {
        int a = pairs[i][0], b = pairs[i][1];
        memcpy(tmp,              cfg->key_map[a], CFG_KEY_MAP_LEN);
        memcpy(cfg->key_map[a], cfg->key_map[b], CFG_KEY_MAP_LEN);
        memcpy(cfg->key_map[b], tmp,              CFG_KEY_MAP_LEN);
    }
    log_info("Layout flipped to phone style");
}
