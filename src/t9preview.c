/*
 * t9preview — lightweight X11 overlay for t9numpad
 *
 * Reads /tmp/t9numpad_preview and shows a compact HUD window:
 *
 *   ┌─────────────────────────────────┐
 *   │ TEXT │ hello_                   │
 *   └─────────────────────────────────┘
 *
 * Always-on-top, no decorations, click-through.
 * Position: bottom-right corner by default (configurable via args).
 *
 * Build:
 *   gcc -O2 -o t9preview t9preview.c -lX11 -lXext
 *
 * Usage:
 *   t9preview [x y] [&]
 *   t9preview 0 0        # top-left
 *   t9preview -1 -1      # bottom-right (default)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

/* ── Appearance ─────────────────────────────────────────────────── */
#define WIN_W           320
#define WIN_H            36
#define PADDING          10
#define FONT_NAME       "-*-terminus-bold-r-*-*-16-*-*-*-*-*-iso8859-*"
#define FONT_FALLBACK   "fixed"

/* Colours (X11 named or hex #rrggbb) */
#define COL_BG          "#1a1a2e"   /* deep navy                  */
#define COL_BORDER      "#e94560"   /* vivid red accent           */
#define COL_MODE_TEXT   "#e94560"   /* mode badge — same red      */
#define COL_MODE_NUM    "#f5a623"   /* number mode — amber        */
#define COL_MODE_TAP    "#50fa7b"   /* multitap mode — green      */
#define COL_WORD        "#eaeaea"   /* prediction text            */
#define COL_DIM         "#555577"   /* dimmed / empty             */

#define PREVIEW_FILE    "/tmp/t9numpad_preview"
#define POLL_MS         80          /* how often to re-read (ms)  */

/* ── Helpers ─────────────────────────────────────────────────────── */

static unsigned long parse_color(Display *dpy, const char *name)
{
    XColor c, exact;
    if (XAllocNamedColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)),
                         name, &c, &exact))
        return c.pixel;
    return WhitePixel(dpy, DefaultScreen(dpy));
}

static void ms_sleep(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Read one line from PREVIEW_FILE into buf (max len).
 * Returns 1 if content changed, 0 if same, -1 on error/missing. */
static int read_preview(char *buf, int len, char *prev)
{
    FILE *f = fopen(PREVIEW_FILE, "r");
    if (!f) {
        if (prev[0]) { prev[0] = '\0'; strncpy(buf, "", len); return 1; }
        return -1;
    }
    char tmp[512] = {0};
    fgets(tmp, sizeof(tmp), f);
    fclose(f);

    /* Strip newline */
    char *nl = strchr(tmp, '\n');
    if (nl) *nl = '\0';

    if (strcmp(tmp, prev) == 0) return 0;
    strncpy(prev, tmp, len - 1);
    strncpy(buf,  tmp, len - 1);
    return 1;
}

/* Parse "[MODE] text" from preview line */
static void parse_line(const char *line, char *mode, int mode_len,
                        char *word, int word_len)
{
    mode[0] = word[0] = '\0';
    if (line[0] == '[') {
        const char *close = strchr(line, ']');
        if (close) {
            int mlen = (int)(close - line - 1);
            if (mlen > 0 && mlen < mode_len) {
                strncpy(mode, line + 1, (size_t)mlen);
                mode[mlen] = '\0';
            }
            const char *rest = close + 1;
            while (*rest == ' ') rest++;
            strncpy(word, rest, (size_t)(word_len - 1));
            return;
        }
    }
    strncpy(word, line, (size_t)(word_len - 1));
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int want_x = -1, want_y = -1;   /* -1 = bottom/right anchor */
    if (argc >= 3) {
        want_x = atoi(argv[1]);
        want_y = atoi(argv[2]);
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "t9preview: cannot open display\n"); return 1; }

    int scr    = DefaultScreen(dpy);
    int scr_w  = DisplayWidth(dpy, scr);
    int scr_h  = DisplayHeight(dpy, scr);

    /* Resolve position */
    int win_x = (want_x < 0) ? scr_w - WIN_W - 8 : want_x;
    int win_y = (want_y < 0) ? scr_h - WIN_H - 8 : want_y;

    /* Colours */
    unsigned long col_bg     = parse_color(dpy, COL_BG);
    unsigned long col_border = parse_color(dpy, COL_BORDER);
    unsigned long col_text   = parse_color(dpy, COL_MODE_TEXT);
    unsigned long col_num    = parse_color(dpy, COL_MODE_NUM);
    unsigned long col_tap    = parse_color(dpy, COL_MODE_TAP);
    unsigned long col_word   = parse_color(dpy, COL_WORD);
    unsigned long col_dim    = parse_color(dpy, COL_DIM);

    /* Window */
    XSetWindowAttributes attrs = {0};
    attrs.background_pixel  = col_bg;
    attrs.border_pixel       = col_border;
    attrs.override_redirect  = True;   /* no WM decorations / management */
    attrs.event_mask         = ExposureMask;

    Window win = XCreateWindow(dpy, RootWindow(dpy, scr),
        win_x, win_y, WIN_W, WIN_H, 2,
        DefaultDepth(dpy, scr), InputOutput,
        DefaultVisual(dpy, scr),
        CWBackPixel | CWBorderPixel | CWOverrideRedirect | CWEventMask,
        &attrs);

    /* Stay on top via EWMH */
    Atom wm_state     = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom wm_top       = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    Atom wm_skip_task = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom wm_skip_pag  = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    XChangeProperty(dpy, win, wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char[]){wm_top, wm_skip_task, wm_skip_pag}, 3);

    /* Window type: utility (no focus steal) */
    Atom wm_type     = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom wm_type_util= XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(dpy, win, wm_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&wm_type_util, 1);

    /* Font */
    XFontStruct *font = XLoadQueryFont(dpy, FONT_NAME);
    if (!font) font   = XLoadQueryFont(dpy, FONT_FALLBACK);
    if (!font) { fprintf(stderr, "t9preview: no usable font\n"); return 1; }

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetFont(dpy, gc, font->fid);

    XMapWindow(dpy, win);
    XFlush(dpy);

    /* Make the fd non-blocking for polling */
    int x11_fd = ConnectionNumber(dpy);
    (void)x11_fd;

    char prev[512] = {0};
    char line[512] = {0};
    char mode[16]  = {0};
    char word[256] = {0};

    int fh = font->ascent + font->descent;
    int baseline = (WIN_H + font->ascent - font->descent) / 2;

    for (;;) {
        /* Drain X events */
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            /* Redraw on expose */
            if (ev.type == Expose && ev.xexpose.count == 0)
                prev[0] = '\xff'; /* force redraw */
        }

        int changed = read_preview(line, sizeof(line), prev);
        if (changed != 0) {
            parse_line(line, mode, sizeof(mode), word, sizeof(word));

            /* Clear */
            XSetForeground(dpy, gc, col_bg);
            XFillRectangle(dpy, win, gc, 0, 0, WIN_W, WIN_H);

            /* Mode badge */
            unsigned long badge_col = col_dim;
            if      (strcmp(mode, "TEXT") == 0) badge_col = col_text;
            else if (strcmp(mode, "NUM")  == 0) badge_col = col_num;
            else if (strcmp(mode, "TAP")  == 0) badge_col = col_tap;

            /* Draw mode pill */
            int badge_w = XTextWidth(font, mode[0] ? mode : "---",
                                     mode[0] ? (int)strlen(mode) : 3);
            int pill_w  = badge_w + 12;
            int pill_x  = PADDING;
            int pill_y  = (WIN_H - fh) / 2;

            XSetForeground(dpy, gc, badge_col);
            XFillRectangle(dpy, win, gc, pill_x, pill_y, pill_w, fh);

            XSetForeground(dpy, gc, col_bg);
            XDrawString(dpy, win, gc,
                        pill_x + 6, baseline,
                        mode[0] ? mode : "---",
                        mode[0] ? (int)strlen(mode) : 3);

            /* Separator */
            XSetForeground(dpy, gc, badge_col);
            XDrawLine(dpy, win, gc,
                      pill_x + pill_w + 6, WIN_H / 4,
                      pill_x + pill_w + 6, WIN_H * 3 / 4);

            /* Word / prediction */
            int text_x = pill_x + pill_w + 14;
            if (word[0]) {
                XSetForeground(dpy, gc, col_word);
                /* Draw cursor block at end */
                int tw = XTextWidth(font, word, (int)strlen(word));
                XDrawString(dpy, win, gc, text_x, baseline,
                            word, (int)strlen(word));
                XSetForeground(dpy, gc, badge_col);
                XFillRectangle(dpy, win, gc,
                               text_x + tw + 2, pill_y, 2, fh);
            } else {
                XSetForeground(dpy, gc, col_dim);
                XDrawString(dpy, win, gc, text_x, baseline,
                            "ready", 5);
            }

            XFlush(dpy);
        }

        ms_sleep(POLL_MS);
    }

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
