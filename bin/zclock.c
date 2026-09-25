/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zclock: an analogue clock of the uptime (sic has no real-time clock yet),
 * mostly to have something that animates. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <zwm.h>

static zwm_surface *s;
static zwm *c;
static int win;

/* Bresenham, thick enough to see. */
static void line(int x0, int y0, int x1, int y1, uint32_t col, int thick)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        zwm_fill(s, x0 - thick / 2, y0 - thick / 2, thick, thick, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* sin/cos without libm: a 64-entry quarter table. */
static const int16_t sin_tab[65] = {
    0,25,50,75,100,125,150,175,200,224,249,273,297,321,345,369,392,415,438,460,483,505,526,548,569,590,610,630,650,669,688,706,
    724,742,759,775,792,807,822,837,851,865,878,891,903,915,926,936,946,955,964,972,980,987,993,999,1004,1009,1013,1016,1019,1021,1023,1024
};
static int isin(int deg)                /* degrees clockwise from 12 o'clock, x1024 */
{
    deg = ((deg % 360) + 360) % 360;
    int q = deg / 90, r = deg % 90;
    int idx = r * 64 / 90;
    switch (q) {
    case 0: return sin_tab[idx];
    case 1: return sin_tab[64 - idx];
    case 2: return -sin_tab[idx];
    default: return -sin_tab[64 - idx];
    }
}
static int icos(int deg) { return isin(deg + 90); }

static void draw(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long sec = ts.tv_sec;
    int cxp = s->w / 2, cyp = s->h / 2, r = (s->w < s->h ? s->w : s->h) / 2 - 10;
    zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_WINDOW);
    /* angles measured clockwise from the top: x = sin, y = -cos */
    for (int d = 0; d < 360; d += 30)
        line(cxp + isin(d) * (r - 8) / 1024, cyp - icos(d) * (r - 8) / 1024, cxp + isin(d) * r / 1024, cyp - icos(d) * r / 1024, ZWM_COL_TEXT_DIM, 2 * zwm_scale());
    int ah = (int)((sec / 3600 % 12) * 30 + (sec / 60 % 60) / 2), am = (int)((sec / 60 % 60) * 6), as = (int)((sec % 60) * 6);
    line(cxp, cyp, cxp + isin(ah) * (r / 2) / 1024, cyp - icos(ah) * (r / 2) / 1024, ZWM_COL_TEXT, 4 * zwm_scale());
    line(cxp, cyp, cxp + isin(am) * (r * 3 / 4) / 1024, cyp - icos(am) * (r * 3 / 4) / 1024, ZWM_COL_TEXT, 3 * zwm_scale());
    line(cxp, cyp, cxp + isin(as) * (r - 12) / 1024, cyp - icos(as) * (r - 12) / 1024, ZWM_COL_DANGER, zwm_scale());
    zwm_fill(s, cxp - 3 * zwm_scale(), cyp - 3 * zwm_scale(), 6 * zwm_scale(), 6 * zwm_scale(), ZWM_COL_TEXT);
    char buf[32];
    snprintf(buf, sizeof buf, "up %02ld:%02ld:%02ld", sec / 3600, sec / 60 % 60, sec % 60);
    zwm_ttext(s, cxp - zwm_ttext_width(buf, ZWM_UI_PX) / 2, s->h - 22 * zwm_scale(), buf, ZWM_COL_TEXT_DIM, ZWM_UI_PX);
    zwm_flush(c, win, s);
}

int main(void)
{
    c = zwm_connect();
    if (!c) { perror("zclock: zwm"); return 1; }
    struct zwm_m_geom g;
    win = zwm_create(c, 200 * zwm_scale(), 220 * zwm_scale(), "Clock", 0, &g);
    if (win < 0) return 1;
    s = zwm_window_surface(c, win, g.w, g.h);
    draw();
    for (;;) {
        struct pollfd pf = { zwm_fd(c), POLLIN, 0 };
        poll(&pf, 1, 1000);
        zwm_event ev; int r;
        while ((r = zwm_next_event(c, &ev, 0)) > 0) {
            if (ev.type == ZWM_S_CLOSE) { zwm_destroy(c, win); return 0; }
            if (ev.type == ZWM_S_RESIZE) zwm_surface_resize(s, ev.geom.w, ev.geom.h);
        }
        if (r < 0) break;
        draw();
    }
    return 0;
}
