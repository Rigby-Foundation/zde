/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zabout: what this is. */
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <zwm.h>

int main(void)
{
    zwm *c = zwm_connect();
    if (!c) { perror("zabout: zwm"); return 1; }
    struct zwm_m_geom g;
    int S = zwm_scale();
    int win = zwm_create(c, 420 * S, 240 * S, "About", 0, &g);
    if (win < 0) return 1;
    zwm_surface *s = zwm_window_surface(c, win, g.w, g.h);
    struct utsname u;
    uname(&u);
    char l1[128], l2[128];
    snprintf(l1, sizeof l1, "%s %s on %s", u.sysname, u.release, u.machine);
    snprintf(l2, sizeof l2, "screen %dx%d", g.screen_w, g.screen_h);
    const char *lines[] = {
        "zde on sic", "",
        l1, l2, "",
        "zwm: window server over AF_UNIX",
        "zde: panel, terminal, files, viewer, clock", "",
        "Copyright (C) 2026 Rigby Foundation, GPL-2.0-only",
    };
    for (;;) {
        zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_WINDOW);
        zwm_ttext_w(s, 16 * S, 14 * S, lines[0], ZWM_COL_TEXT, 22 * S, ZWM_FONT_MEDIUM);
        for (size_t i = 2; i < sizeof lines / sizeof lines[0]; i++)
            zwm_ttext(s, 16 * S, (52 + (int)(i - 2) * 20) * S, lines[i], i == 2 || i == 3 ? ZWM_COL_TEXT : ZWM_COL_TEXT_DIM, ZWM_UI_PX);
        zwm_flush(c, win, s);
        zwm_event ev;
        int r;
        do {
            r = zwm_next_event(c, &ev, 1);
            if (r < 0) return 0;
            if (ev.type == ZWM_S_CLOSE || (ev.type == ZWM_S_KEY && ev.key.down && (ev.key.sym == 27 || ev.key.sym == 'q'))) { zwm_destroy(c, win); return 0; }
        } while (ev.type != ZWM_S_RESIZE);
        zwm_surface_resize(s, ev.geom.w, ev.geom.h);
    }
}
