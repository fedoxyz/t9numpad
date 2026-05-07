/*
 * t9numpad - Logging implementation
 * src/log.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log.h"

static log_level_t current_level = LOG_INFO;
static FILE       *log_fp        = NULL;   /* NULL → stderr */
static int         own_fp        = 0;

void log_init(int level, const char *file)
{
    current_level = (log_level_t)level;

    if (file && file[0]) {
        FILE *f = fopen(file, "a");
        if (f) {
            log_fp  = f;
            own_fp  = 1;
        } else {
            fprintf(stderr, "log_init: cannot open %s: %s\n",
                    file, strerror(errno));
        }
    }
}

void log_close(void)
{
    if (own_fp && log_fp) {
        fclose(log_fp);
        log_fp = NULL;
        own_fp = 0;
    }
}

void log_write(log_level_t level, const char *fmt, ...)
{
    if (level > current_level)
        return;

    static const char *labels[] = {
        [LOG_SILENT] = "SILENT",
        [LOG_ERROR]  = "ERROR ",
        [LOG_WARN]   = "WARN  ",
        [LOG_INFO]   = "INFO  ",
        [LOG_DEBUG]  = "DEBUG ",
    };

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    FILE *out = log_fp ? log_fp : stderr;
    fprintf(out, "[%s.%03ld] %s ", tbuf, ts.tv_nsec / 1000000L,
            level < 5 ? labels[level] : "?????");

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fputc('\n', out);
    fflush(out);
}
