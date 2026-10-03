/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zkbd: the keyboard on the screen, for a phone. A dock along the bottom
 * (docks never take the focus, so what is typed goes to the window in
 * front, through the server: zwm_type_key). Letters, then symbols behind
 * "?123"; Shift and Ctrl apply to the next key (Shift twice: caps lock);
 * Backspace and the arrows repeat while held. zbar's keyboard button
 * starts and stops it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <zwm.h>

#define S zwm_scale()

enum { K_CHAR, K_BACK, K_ENTER, K_SHIFT, K_CTRL, K_LAYER, K_TAB, K_ESC, K_SPACE, K_LEFT, K_RIGHT, K_UP, K_DOWN };
struct key { int kind; char c; float w; };    /* c: the character (K_CHAR); w: width in key units */
#define CH(x) { K_CHAR, x, 1 }

#define NROWS 5
#define MAXK  12
static const struct key layout[2][NROWS][MAXK] = {
    {   /* letters (Shift makes them capitals) */
        { CH('1'), CH('2'), CH('3'), CH('4'), CH('5'), CH('6'), CH('7'), CH('8'), CH('9'), CH('0'), { K_BACK, 0, 1.5f } },
        { CH('q'), CH('w'), CH('e'), CH('r'), CH('t'), CH('y'), CH('u'), CH('i'), CH('o'), CH('p') },
        { CH('a'), CH('s'), CH('d'), CH('f'), CH('g'), CH('h'), CH('j'), CH('k'), CH('l'), { K_ENTER, 0, 1.5f } },
        { { K_SHIFT, 0, 1.5f }, CH('z'), CH('x'), CH('c'), CH('v'), CH('b'), CH('n'), CH('m'), CH(','), CH('.') },
        { { K_ESC, 0, 1 }, { K_CTRL, 0, 1.25f }, { K_LAYER, 0, 1.25f }, { K_SPACE, 0, 3.5f }, { K_TAB, 0, 1 },
          { K_LEFT, 0, 0.875f }, { K_DOWN, 0, 0.875f }, { K_UP, 0, 0.875f }, { K_RIGHT, 0, 0.875f } },
    },
    {   /* symbols */
        { CH('1'), CH('2'), CH('3'), CH('4'), CH('5'), CH('6'), CH('7'), CH('8'), CH('9'), CH('0'), { K_BACK, 0, 1.5f } },
        { CH('!'), CH('@'), CH('#'), CH('$'), CH('%'), CH('^'), CH('&'), CH('*'), CH('('), CH(')') },
        { CH('-'), CH('_'), CH('='), CH('+'), CH('['), CH(']'), CH('{'), CH('}'), CH(';'), CH('`'), { K_ENTER, 0, 1.5f } },
        { { K_LAYER, 0, 1.5f }, CH(':'), CH('\''), CH('"'), CH('/'), CH('\\'), CH('|'), CH('<'), CH('>'), CH('?'), CH('~') },
        { { K_ESC, 0, 1 }, { K_CTRL, 0, 1.25f }, { K_LAYER, 0, 1.25f }, { K_SPACE, 0, 3.5f }, { K_TAB, 0, 1 },
          { K_LEFT, 0, 0.875f }, { K_DOWN, 0, 0.875f }, { K_UP, 0, 0.875f }, { K_RIGHT, 0, 0.875f } },
    },
};
#define UNITS 11.5f                         /* the widest row */

static zwm *c;
static int win;
static zwm_surface *s;
static int layer, shift, caps, ctrl;        /* shift and ctrl: for the next key */
static int kw, kh, gap;
static int down_row = -1, down_col = -1;    /* the key being held */
static uint32_t down_sym, down_mods;
static struct timespec down_at, repeat_at;

static int row_len(int r) { int n = 0; while (n < MAXK && layout[layer][r][n].w > 0) n++; return n; }   /* the rest are zero */

static float row_units(int r)
{
    float u = 0;
    for (int i = 0; i < row_len(r); i++) u += layout[layer][r][i].w;
    return u;
}

/* Where key (r, i) is: rows centred. */
static void key_rect(int r, int i, int *x, int *y, int *w, int *h)
{
    float x0 = (s->w - row_units(r) * kw) / 2;
    for (int k = 0; k < i; k++) x0 += layout[layer][r][k].w * kw;
    *x = (int)x0 + gap / 2; *y = gap + r * kh + gap / 2;
    *w = (int)(layout[layer][r][i].w * kw) - gap; *h = kh - gap;
}

static const char *label(const struct key *k, char *buf)
{
    switch (k->kind) {
    case K_CHAR:  buf[0] = layer == 0 && (shift || caps) && k->c >= 'a' && k->c <= 'z' ? (char)(k->c - 32) : k->c; buf[1] = 0; return buf;
    case K_BACK:  return "Del";
    case K_ENTER: return "Enter";
    case K_SHIFT: return caps ? "CAPS" : "Shift";
    case K_CTRL:  return "Ctrl";
    case K_LAYER: return layer ? "abc" : "?123";
    case K_TAB:   return "Tab";
    case K_ESC:   return "Esc";
    case K_SPACE: return "";
    default:      return NULL;              /* the arrows are drawn */
    }
}

static void arrow(int cx, int cy, int size, int dir, uint32_t col)     /* 0 left, 1 right, 2 up, 3 down */
{
    for (int t = 0; t < size; t++) {
        int half = t * 2 / 3;
        switch (dir) {
        case 0: zwm_fill(s, cx - size / 2 + t, cy - half, 1, 2 * half + 1, col); break;
        case 1: zwm_fill(s, cx + size / 2 - t, cy - half, 1, 2 * half + 1, col); break;
        case 2: zwm_fill(s, cx - half, cy - size / 2 + t, 2 * half + 1, 1, col); break;
        case 3: zwm_fill(s, cx - half, cy + size / 2 - t, 2 * half + 1, 1, col); break;
        }
    }
}

static void draw(void)
{
    zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_PANEL);
    zwm_hline(s, 0, 0, s->w, ZWM_COL_BORDER);
    int px = kh * 2 / 5 > 40 * S ? 40 * S : kh * 2 / 5, spx = px * 3 / 4;
    for (int r = 0; r < NROWS; r++)
        for (int i = 0; i < row_len(r); i++) {
            const struct key *k = &layout[layer][r][i];
            int x, y, w, h;
            key_rect(r, i, &x, &y, &w, &h);
            int on = (k->kind == K_SHIFT && (shift || caps)) || (k->kind == K_CTRL && ctrl);
            uint32_t bg = r == down_row && i == down_col ? ZWM_COL_ACCENT_DIM : on ? ZWM_COL_ACCENT : k->kind == K_CHAR || k->kind == K_SPACE ? ZWM_COL_SURFACE : ZWM_COL_WINDOW;
            uint32_t fg = on ? ZWM_COL_ACCENT_TEXT : ZWM_COL_TEXT;
            zwm_round_rect(s, x, y, w, h, 6 * S, bg);
            char buf[2];
            const char *t = label(k, buf);
            if (t) {
                int p = k->kind == K_CHAR ? px : spx;
                zwm_ttext(s, x + (w - zwm_ttext_width(t, p)) / 2, y + (h - zwm_ttext_height(p)) / 2, t, fg, p);
            } else arrow(x + w / 2, y + h / 2, h / 3, k->kind == K_LEFT ? 0 : k->kind == K_RIGHT ? 1 : k->kind == K_UP ? 2 : 3, fg);
        }
}

static int key_at(int mx, int my, int *row, int *col)
{
    for (int r = 0; r < NROWS; r++)
        for (int i = 0; i < row_len(r); i++) {
            int x, y, w, h;
            key_rect(r, i, &x, &y, &w, &h);
            if (mx >= x - gap / 2 && mx < x + w + gap / 2 && my >= y - gap / 2 && my < y + h + gap / 2) { *row = r; *col = i; return 1; }
        }
    return 0;
}

static int repeats(int kind) { return kind == K_BACK || kind == K_LEFT || kind == K_RIGHT || kind == K_UP || kind == K_DOWN; }
static long ms_since(const struct timespec *t)
{
    struct timespec n;
    clock_gettime(CLOCK_MONOTONIC, &n);
    return (n.tv_sec - t->tv_sec) * 1000 + (n.tv_nsec - t->tv_nsec) / 1000000;
}

static void press(int r, int i)
{
    const struct key *k = &layout[layer][r][i];
    uint32_t sym = 0, mods = ctrl ? ZWM_MOD_CTRL : 0;
    switch (k->kind) {
    case K_SHIFT: if (caps) caps = shift = 0; else if (shift) { caps = 1; shift = 0; } else shift = 1; return;
    case K_CTRL:  ctrl = !ctrl; return;
    case K_LAYER: layer = !layer; shift = 0; return;
    case K_CHAR:
        sym = (unsigned char)k->c;
        if (layer == 0 && (shift || caps) && k->c >= 'a' && k->c <= 'z' && !ctrl) { sym -= 32; mods |= ZWM_MOD_SHIFT; }
        break;
    case K_BACK:  sym = '\b'; break;
    case K_ENTER: sym = '\n'; break;
    case K_TAB:   sym = '\t'; break;
    case K_ESC:   sym = 27; break;
    case K_SPACE: sym = ' '; break;
    case K_LEFT:  sym = ZWM_KEY_LEFT; break;
    case K_RIGHT: sym = ZWM_KEY_RIGHT; break;
    case K_UP:    sym = ZWM_KEY_UP; break;
    case K_DOWN:  sym = ZWM_KEY_DOWN; break;
    }
    zwm_type_key(c, sym, mods, 1);
    down_sym = sym; down_mods = mods;
    clock_gettime(CLOCK_MONOTONIC, &down_at); repeat_at = down_at;
    shift = 0; ctrl = 0;                    /* used up (caps stays) */
}

static void release(void)
{
    if (down_row >= 0 && down_sym) zwm_type_key(c, down_sym, down_mods, 0);
    down_row = down_col = -1; down_sym = 0;
}

int main(void)
{
    c = zwm_connect();
    if (!c) { perror("zkbd: zwm"); return 1; }
    struct zwm_m_geom g;
    /* keys about a finger wide on a phone, no more than a fifteenth of the screen tall */
    int probe = zwm_create(c, 1, 1, "keyboard", ZWM_UNDECORATED, &g);
    int sw = g.screen_w, sh = g.screen_h;
    if (probe >= 0) zwm_destroy(c, probe);
    kw = (int)(sw / UNITS);
    kh = kw * 6 / 5;
    if (kh > sh / 15) kh = sh / 15;
    if (kh < 30 * S) kh = 30 * S;
    gap = kh / 10 > 2 ? kh / 10 : 2;
    win = zwm_create(c, sw, NROWS * kh + 2 * gap, "keyboard", ZWM_DOCK_BOTTOM, &g);
    if (win < 0) return 1;
    s = zwm_window_surface(c, win, g.w, g.h);
    if (!s) return 1;
    kw = (int)(g.w / UNITS);
    draw();
    zwm_flush(c, win, s);
    for (;;) {
        int timeout = -1;
        if (down_row >= 0 && repeats(layout[layer][down_row][down_col].kind)) timeout = 30;
        struct pollfd pf = { zwm_fd(c), POLLIN, 0 };
        poll(&pf, 1, timeout);
        int dirty = 0, r;
        zwm_event ev;
        while ((r = zwm_next_event(c, &ev, 0)) > 0) {
            if (ev.type == ZWM_S_CLOSE) return 0;
            if (ev.type == ZWM_S_RESIZE) { zwm_surface_resize(s, ev.geom.w, ev.geom.h); kw = (int)(ev.geom.w / UNITS); dirty = 1; }
            if (ev.type != ZWM_S_MOUSE) continue;
            int row, col;
            if (ev.mouse.kind == ZWM_MOUSE_PRESS && (ev.mouse.buttons & ZWM_BTN_LEFT) && key_at(ev.mouse.x, ev.mouse.y, &row, &col)) {
                release();
                down_row = row; down_col = col;
                press(row, col);
                dirty = 1;
            } else if (ev.mouse.kind == ZWM_MOUSE_RELEASE || (ev.mouse.kind == ZWM_MOUSE_MOVE && !(ev.mouse.buttons & ZWM_BTN_LEFT))) {
                if (down_row >= 0) { release(); dirty = 1; }
            }
        }
        if (r < 0) return 0;
        /* held: repeat after 400 ms, every 60 */
        if (down_row >= 0 && down_sym && repeats(layout[layer][down_row][down_col].kind) && ms_since(&down_at) > 400 && ms_since(&repeat_at) >= 60) {
            zwm_type_key(c, down_sym, down_mods, 1);
            clock_gettime(CLOCK_MONOTONIC, &repeat_at);
        }
        if (dirty) { draw(); zwm_flush(c, win, s); }
    }
}
