/*
 * t9numpad - Logging macros and functions
 * include/log.h
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef T9NUMPAD_LOG_H
#define T9NUMPAD_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_SILENT = 0,
    LOG_ERROR  = 1,
    LOG_WARN   = 2,
    LOG_INFO   = 3,
    LOG_DEBUG  = 4,
} log_level_t;

void log_init(int level, const char *file);
void log_close(void);
void log_write(log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define log_error(...) log_write(LOG_ERROR, __VA_ARGS__)
#define log_warn(...)  log_write(LOG_WARN,  __VA_ARGS__)
#define log_info(...)  log_write(LOG_INFO,  __VA_ARGS__)
#define log_debug(...) log_write(LOG_DEBUG, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* T9NUMPAD_LOG_H */
