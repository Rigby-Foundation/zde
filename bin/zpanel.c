/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zpanel: the dock at the bottom. A rounded tray of icons: the launchers
 * (a dot under the ones that have a window open), then any other window.
 * Click an icon to bring its window up, or back from minimized; click the
 * one in front to put it away; click a launcher with nothing open to start
 * it. The name of what is under the mouse shows above the tray (the clock
 * is in the bar at the top). Re-spawned by zde if it dies. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <zwm.h>
#include "appicon.h"

#define S zwm_scale()               /* HiDPI: every size below is in these units */
#define PANEL_H  (74 * S)           /* the strip: a label row, then the tray */
#define TRAY_H   (50 * S)
#define TRAY_Y   (PANEL_H - TRAY_H - 4 * S)
#define CELL     (44 * S)           /* one icon's slot */
#define ICON     (28 * S)
#define TRAY_PAD (6 * S)
#define SEP_W    (12 * S)

/* The launchers: their icons come from the programs themselves (the
 * .zicon section, appicon.h), the first one is the launchpad. */
static const struct { const char *label, *path; } launchers[] = {
    { "Launchpad", "/bin/zlaunch" },
    { "Terminal", "/bin/zterm" },
    { "Files",    "/bin/zfiles" },
    { "Clock",    "/bin/zclock" },
    { "About",    "/bin/zabout" },
};
#define NL (int)(sizeof launchers / sizeof launchers[0])
static struct appicon launcher_icons[NL];

static zwm_surface *s;
static struct zwm_m_window windows[32];
static int nwindows;

/* What the tray shows, left to right: launchers, then windows that are not one of them. */
struct item { const char *label; struct appicon *icon; int launcher; int win; int running, focused, minimized; };
static struct item items[NL + 32];
static int nitems, hover = -1, pressed = -1;

/* A launcher's window: the same program behind it; failing that (no
 * /proc/self/exe) a title of "Label" or "Label: ...". */
static int is_app(const struct zwm_m_window *w, int l)
{
    if (w->exe[0]) return strcmp(w->exe, launchers[l].path) == 0;
    size_t n = strlen(launchers[l].label);
    return strncmp(w->title, launchers[l].label, n) == 0 && (w->title[n] == 0 || w->title[n] == ':');
}

/* Icons of the other windows' programs, by path; kept for the session. */
static struct { char exe[128]; struct appicon icon; } icon_cache[32];
static int nicon_cache;

static struct appicon *icon_of(const struct zwm_m_window *w)
{
    if (!w->exe[0]) return generic_icon(ICON_APP);
    for (int i = 0; i < nicon_cache; i++)
        if (strcmp(icon_cache[i].exe, w->exe) == 0) return &icon_cache[i].icon;
    if (nicon_cache == 32) return generic_icon(ICON_APP);
    int own;
    snprintf(icon_cache[nicon_cache].exe, sizeof icon_cache[0].exe, "%s", w->exe);
    icon_cache[nicon_cache].icon = icon_for(w->exe, 0, &own);
    return &icon_cache[nicon_cache++].icon;
}

static void build_items(void)
{
    nitems = 0;
    for (int l = 0; l < NL; l++) {
        struct item *it = &items[nitems++];
        *it = (struct item){ launchers[l].label, &launcher_icons[l], l, -1, 0, 0, 0 };
        for (int i = 0; i < nwindows; i++)              /* the topmost window of this app wins */
            if (is_app(&windows[i], l)) {
                it->running = 1; it->win = (int)windows[i].id;
                it->focused = windows[i].state & ZWM_WIN_FOCUSED; it->minimized = windows[i].state & ZWM_WIN_MINIMIZED;
            }
    }
    for (int i = nwindows - 1; i >= 0 && nitems < NL + 32; i--) {      /* top to bottom */
        int l;
        for (l = 0; l < NL && !is_app(&windows[i], l); l++) ;
        if (l < NL) continue;
        items[nitems++] = (struct item){ windows[i].title, icon_of(&windows[i]), -1, (int)windows[i].id, 1,
                                         windows[i].state & ZWM_WIN_FOCUSED, windows[i].state & ZWM_WIN_MINIMIZED };
    }
}

static int tray_w(void) { return 2 * TRAY_PAD + nitems * CELL + (nitems > NL ? SEP_W : 0); }
static int tray_x(void) { return (s->w - tray_w()) / 2; }
static int item_x(int i) { return tray_x() + TRAY_PAD + i * CELL + (i >= NL ? SEP_W : 0); }

static void draw(void)
{
    zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_PANEL);
    int tx = tray_x(), tw = tray_w();
    zwm_round_rect(s, tx, TRAY_Y, tw, TRAY_H, 14 * S, ZWM_COL_WINDOW);
    zwm_round_rect_border(s, tx, TRAY_Y, tw, TRAY_H, 14 * S, ZWM_COL_BORDER);
    if (nitems > NL) zwm_fill(s, item_x(NL) - SEP_W / 2, TRAY_Y + 12 * S, S, TRAY_H - 24 * S, ZWM_COL_BORDER);
    for (int i = 0; i < nitems; i++) {
        int x = item_x(i), y = TRAY_Y + (TRAY_H - CELL) / 2;
        if (i == hover || i == pressed) zwm_round_rect(s, x + 2 * S, y + 2 * S, CELL - 4 * S, CELL - 4 * S, 9 * S, i == pressed ? ZWM_COL_ACCENT_DIM : ZWM_COL_SURFACE);
        icon_draw_or_box(s, x + (CELL - ICON) / 2, y + (CELL - ICON) / 2 - S, ICON, items[i].icon);
        if (items[i].minimized)                                     /* put away: faded */
            for (int j = 0; j < ICON; j++) for (int k = 0; k < ICON; k++)
                zwm_blend(s, x + (CELL - ICON) / 2 + k, y + (CELL - ICON) / 2 - S + j, ZWM_COL_WINDOW, 130);
        if (items[i].running) zwm_disc(s, x + CELL / 2, TRAY_Y + TRAY_H - 5 * S, (items[i].focused ? 2 : 1) * S, items[i].focused ? ZWM_COL_ACCENT : ZWM_COL_TEXT_DIM);
    }
    if (hover >= 0 && hover < nitems) {                 /* the name, above the tray */
        const char *label = items[hover].label;
        int lw = zwm_ttext_width(label, ZWM_UI_PX) + 16 * S, lx = item_x(hover) + CELL / 2 - lw / 2;
        if (lx < 4 * S) lx = 4 * S;
        if (lx + lw > s->w - 4 * S) lx = s->w - 4 * S - lw;
        zwm_round_rect(s, lx, 0, lw, 20 * S, 6 * S, ZWM_COL_SURFACE);
        zwm_ttext(s, lx + 8 * S, (20 * S - zwm_ttext_height(ZWM_UI_PX)) / 2, label, ZWM_COL_TEXT, ZWM_UI_PX);
    }
}

static int item_at(int x, int y)
{
    if (y < TRAY_Y || y >= TRAY_Y + TRAY_H) return -1;
    for (int i = 0; i < nitems; i++)
        if (x >= item_x(i) && x < item_x(i) + CELL) return i;
    return -1;
}

static void launch(const char *path)
{
    pid_t pid = fork();
    if (pid == 0) { execl(path, path, (char *)NULL); _exit(127); }
}

int main(void)
{
    signal(SIGCHLD, SIG_IGN);
    zwm *c = zwm_connect();
    if (!c) { perror("zpanel: zwm"); return 1; }
    struct zwm_m_geom g;
    int win = zwm_create(c, 100, PANEL_H, "panel", ZWM_DOCK_BOTTOM | ZWM_TASKBAR, &g);
    if (win < 0) return 1;
    s = zwm_window_surface(c, win, g.w, g.h);
    for (int i = 0; i < NL; i++) { int own; launcher_icons[i] = icon_for(launchers[i].path, 0, &own); }
    build_items();
    draw();
    zwm_flush(c, win, s);
    for (;;) {
        struct pollfd pf = { zwm_fd(c), POLLIN, 0 };
        poll(&pf, 1, 1000);
        int st;
        while (waitpid(-1, &st, WNOHANG) > 0) ;
        zwm_event ev;
        int r;
        while ((r = zwm_next_event(c, &ev, 0)) > 0) {
            if (ev.type == ZWM_S_MOUSE) {
                int b = item_at(ev.mouse.x, ev.mouse.y);
                if (ev.mouse.kind == ZWM_MOUSE_PRESS && (ev.mouse.buttons & ZWM_BTN_LEFT)) pressed = b;
                else if (ev.mouse.kind == ZWM_MOUSE_RELEASE) {
                    if (pressed >= 0 && pressed == b) {
                        struct item *it = &items[b];
                        if (!it->running || it->launcher == 0) launch(launchers[it->launcher].path);
                        else if (it->focused && !it->minimized) zwm_minimize(c, it->win);   /* the front window: put it away */
                        else zwm_activate(c, it->win);
                    }
                    pressed = -1;
                }
                hover = b;
            } else if (ev.type == ZWM_S_RESIZE) {
                zwm_surface_resize(s, ev.geom.w, ev.geom.h);
            } else if (ev.type == ZWM_S_WINDOWS) {
                nwindows = ev.windows.count;
                memcpy(windows, ev.windows.w, sizeof windows[0] * (size_t)nwindows);
                build_items();
                if (hover >= nitems) hover = -1;
                if (pressed >= nitems) pressed = -1;
            }
        }
        if (r < 0) break;
        draw();
        zwm_flush(c, win, s);
    }
    return 0;
}
