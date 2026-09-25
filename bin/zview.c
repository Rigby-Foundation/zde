/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zview: shows a text file. Arrow keys, PgUp/PgDn, Home/End scroll. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zwm.h>

#define S zwm_scale()
#define LINE_H (18 * S)
#define PAD (10 * S)

static char **lines;
static int nlines, top;
static zwm_surface *s;
static zwm *c;
static int win;

static void load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { lines = malloc(sizeof *lines); lines[0] = strdup("(cannot open file)"); nlines = 1; return; }
    char buf[1024];
    int cap = 0;
    while (fgets(buf, sizeof buf, f)) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        /* expand tabs, drop control characters */
        char out[1200]; size_t o = 0;
        for (size_t i = 0; i < n && o < sizeof out - 8; i++) {
            if (buf[i] == '\t') { do out[o++] = ' '; while (o % 8); }
            else if ((unsigned char)buf[i] >= 0x20) out[o++] = buf[i];
        }
        out[o] = 0;
        if (nlines == cap) lines = realloc(lines, sizeof *lines * (size_t)(cap = cap ? cap * 2 : 64));
        lines[nlines++] = strdup(out);
    }
    fclose(f);
    if (!nlines) { lines = malloc(sizeof *lines); lines[0] = strdup("(empty)"); nlines = 1; }
}

static void draw(void)
{
    zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_WINDOW);
    int visible = (s->h - 2 * PAD) / LINE_H;
    for (int i = 0; i < visible && top + i < nlines; i++)
        zwm_ttext(s, PAD, PAD + i * LINE_H, lines[top + i], ZWM_COL_TEXT, ZWM_UI_PX);
    /* scrollbar */
    if (nlines > visible) {
        int bar_h = s->h * visible / nlines, bar_y = (s->h - bar_h) * top / (nlines - visible);
        zwm_fill(s, s->w - 6 * S, 0, 6 * S, s->h, ZWM_COL_SURFACE);
        zwm_fill(s, s->w - 6 * S, bar_y, 6 * S, bar_h < 8 * S ? 8 * S : bar_h, ZWM_COL_TEXT_DIM);
    }
    zwm_flush(c, win, s);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/etc/motd";
    load(path);
    c = zwm_connect();
    if (!c) { perror("zview: zwm"); return 1; }
    struct zwm_m_geom g;
    const char *base = strrchr(path, '/');
    win = zwm_create(c, 560 * S, 380 * S, base ? base + 1 : path, 0, &g);
    if (win < 0) return 1;
    s = zwm_window_surface(c, win, g.w, g.h);
    draw();
    for (;;) {
        zwm_event ev;
        if (zwm_next_event(c, &ev, 1) < 0) break;
        int visible = (s->h - 2 * PAD) / LINE_H, max = nlines - visible > 0 ? nlines - visible : 0;
        switch (ev.type) {
        case ZWM_S_KEY:
            if (!ev.key.down) break;
            switch (ev.key.sym) {
            case ZWM_KEY_DOWN: top++; break;
            case ZWM_KEY_UP: top--; break;
            case ZWM_KEY_PGDN: case ' ': top += visible; break;
            case ZWM_KEY_PGUP: top -= visible; break;
            case ZWM_KEY_HOME: top = 0; break;
            case ZWM_KEY_END: top = max; break;
            case 'q': zwm_destroy(c, win); return 0;
            default: continue;
            }
            if (top > max) top = max;
            if (top < 0) top = 0;
            draw();
            break;
        case ZWM_S_MOUSE:
            if (ev.mouse.kind == ZWM_MOUSE_PRESS) {
                top += (ev.mouse.buttons & ZWM_BTN_LEFT) ? visible / 2 : -visible / 2;
                if (top > max) top = max;
                if (top < 0) top = 0;
                draw();
            }
            break;
        case ZWM_S_RESIZE: zwm_surface_resize(s, ev.geom.w, ev.geom.h); draw(); break;
        case ZWM_S_CLOSE: zwm_destroy(c, win); return 0;
        }
    }
    return 0;
}
