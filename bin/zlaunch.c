/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zlaunch: the launchpad. A sheet over the whole work area with every app
 * on it: the programs in /bin that carry an icon, and the executables at
 * the top of each mounted volume (games). Click one to start it (in its
 * own directory) and the sheet goes; Escape or a click on nothing closes
 * it. The dock's first button opens it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <zwm.h>
#include "appicon.h"

#define S zwm_scale()               /* HiDPI: every size below is in these units */
#define CELL_W  (120 * S)
#define CELL_H  (112 * S)
#define ICON    (64 * S)

struct app { char name[64], path[512], dir[512]; struct appicon icon; int own; };
static struct app apps[64];
static int napps, hover = -1;
static zwm_surface *s;
static zwm *c;
static int win;

static const char *hidden[] = { "zwm", "zde", "zpanel", "zlaunch", "zview", "init", "sh", "renpy", NULL };   /* not apps to start bare */

static void add(const char *dir, const char *name, int need_icon)
{
    if (napps >= 64) return;
    for (int i = 0; hidden[i]; i++) if (strcmp(name, hidden[i]) == 0) return;
    char path[768];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || !is_elf(path)) return;
    struct app *a = &apps[napps];
    a->icon = icon_for(path, 0, &a->own);
    if (need_icon && !a->own) return;                   /* a command-line tool, not an app */
    snprintf(a->name, sizeof a->name, "%s", name);
    snprintf(a->path, sizeof a->path, "%s", path);
    snprintf(a->dir, sizeof a->dir, "%s", dir);
    /* zterm -> Terminal etc.: the names the dock uses */
    static const struct { const char *bin, *label; } names[] = {
        { "zterm", "Terminal" }, { "zfiles", "Files" }, { "zclock", "Clock" }, { "zabout", "About" }, { "zview", "Viewer" },
        { "gldemo", "GL Demo" }, { "sdldemo", "SDL Demo" }, { "blitbench", "Blit Bench" }, { "sdltone", "Tone" }, { "sdlrecreate", "Recreate" },
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(name, names[i].bin) == 0) snprintf(a->name, sizeof a->name, "%s", names[i].label);
    napps++;
}

static int cmp(const void *x, const void *y) { return strcasecmp(((const struct app *)x)->name, ((const struct app *)y)->name); }

static void scan(void)
{
    napps = 0;
    DIR *d = opendir("/bin");
    struct dirent *e;
    while (d && (e = readdir(d))) if (e->d_name[0] != '.') add("/bin", e->d_name, 1);
    if (d) closedir(d);
    qsort(apps, (size_t)napps, sizeof apps[0], cmp);
    int nbin = napps;
    d = opendir("/mnt");
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char vol[160];
        snprintf(vol, sizeof vol, "/mnt/%s", e->d_name);
        DIR *v = opendir(vol);
        struct dirent *f;
        while (v && (f = readdir(v))) if (f->d_name[0] != '.') add(vol, f->d_name, 0);
        if (v) closedir(v);
    }
    if (d) closedir(d);
    qsort(apps + nbin, (size_t)(napps - nbin), sizeof apps[0], cmp);
}

static int cols(void) { int n = (s->w - 40 * S) / CELL_W; return n < 1 ? 1 : n; }
static int origin_x(void) { int n = cols(); if (n > napps) n = napps ? napps : 1; return (s->w - n * CELL_W) / 2; }
#define ORIGIN_Y (60 * S)

static void draw(void)
{
    zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_BG);
    zwm_ttext_w(s, (s->w - zwm_ttext_width_w("Applications", 20 * S, ZWM_FONT_MEDIUM)) / 2, 20 * S, "Applications", ZWM_COL_TEXT, 20 * S, ZWM_FONT_MEDIUM);
    int n = cols(), ox = origin_x();
    for (int i = 0; i < napps; i++) {
        int x = ox + (i % n) * CELL_W, y = ORIGIN_Y + (i / n) * CELL_H;
        if (i == hover) zwm_round_rect(s, x + 4 * S, y + 2 * S, CELL_W - 8 * S, CELL_H - 4 * S, 12 * S, ZWM_COL_SURFACE);
        icon_draw_or_box(s, x + (CELL_W - ICON) / 2, y + 10 * S, ICON, &apps[i].icon);
        char label[64];
        snprintf(label, sizeof label, "%s", apps[i].name);
        while (zwm_ttext_width(label, ZWM_UI_PX) > CELL_W - 12 * S && strlen(label) > 4) { size_t k = strlen(label); strcpy(label + k - 4, "..."); }
        zwm_ttext(s, x + (CELL_W - zwm_ttext_width(label, ZWM_UI_PX)) / 2, y + 20 * S + ICON, label, ZWM_COL_TEXT, ZWM_UI_PX);
    }
    if (!napps) zwm_ttext(s, (s->w - zwm_ttext_width("No applications found", ZWM_UI_PX)) / 2, ORIGIN_Y + 20 * S, "No applications found", ZWM_COL_TEXT_DIM, ZWM_UI_PX);
    zwm_flush(c, win, s);
}

static int app_at(int px, int py)
{
    int n = cols(), ox = origin_x();
    if (px < ox || py < ORIGIN_Y) return -1;
    int col = (px - ox) / CELL_W, row = (py - ORIGIN_Y) / CELL_H;
    if (col >= n) return -1;
    int i = row * n + col;
    return i < napps ? i : -1;
}

static void launch(struct app *a)
{
    pid_t pid = fork();
    if (pid == 0) { if (chdir(a->dir) == 0) execl(a->path, a->path, (char *)NULL); _exit(127); }
}

int main(void)
{
    signal(SIGCHLD, SIG_IGN);
    c = zwm_connect();
    if (!c) { perror("zlaunch: zwm"); return 1; }
    struct zwm_m_geom g;
    win = zwm_create(c, 1 << 14, 1 << 14, "Launchpad", ZWM_UNDECORATED | ZWM_FIXED, &g);   /* clamped: the whole work area */
    if (win < 0) return 1;
    s = zwm_window_surface(c, win, g.w, g.h);
    scan();
    draw();
    for (;;) {
        zwm_event ev;
        int r = zwm_next_event(c, &ev, 1);
        if (r < 0) break;
        switch (ev.type) {
        case ZWM_S_MOUSE: {
            int a = app_at(ev.mouse.x, ev.mouse.y);
            if (ev.mouse.kind == ZWM_MOUSE_PRESS && (ev.mouse.buttons & ZWM_BTN_LEFT)) {
                if (a >= 0) launch(&apps[a]);
                zwm_destroy(c, win);
                return 0;
            }
            if (a != hover) { hover = a; draw(); }
            break;
        }
        case ZWM_S_KEY:
            if (ev.key.down && ev.key.sym == 27) { zwm_destroy(c, win); return 0; }
            break;
        case ZWM_S_RESIZE: zwm_surface_resize(s, ev.geom.w, ev.geom.h); draw(); break;
        case ZWM_S_FOCUS: if (!ev.focus.focused) { zwm_destroy(c, win); return 0; } break;
        case ZWM_S_CLOSE: zwm_destroy(c, win); return 0;
        }
    }
    return 0;
}
