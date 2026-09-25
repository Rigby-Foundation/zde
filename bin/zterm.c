/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zterm: a terminal window running /bin/sh over pipes. There is no pty on
 * sic, so the terminal does the line editing and the echo itself and hands
 * the shell whole lines; the shell's output is drawn into a character
 * grid (ANSI escape sequences are skipped, not interpreted). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <zwm.h>

#define S   zwm_scale()
#define PX  (13 * S)                /* JetBrains Mono; the cell comes from its metrics */
#define PAD (6 * S)
static int CW = 8, CH = 10;

static int cols = 96, rows = 40;
static char *grid;              /* rows * cols */
static int cx, cy;              /* cursor */
static zwm_surface *s;
static zwm *c;
static int win;
static pid_t sh_pid;
static int to_sh = -1, from_sh = -1;
static char line[512];          /* the line being edited */
static int line_len;
static int esc;                 /* inside an escape sequence */
static int dirty_all, focused = 1;

static void grid_alloc(int nc, int nr)
{
    char *g = malloc((size_t)nc * nr);
    memset(g, ' ', (size_t)nc * nr);
    if (grid) {
        for (int y = 0; y < nr && y < rows; y++)
            memcpy(g + (size_t)y * nc, grid + (size_t)y * cols, (size_t)(nc < cols ? nc : cols));
        free(grid);
    }
    grid = g; cols = nc; rows = nr;
    if (cx >= cols) cx = cols - 1;
    if (cy >= rows) cy = rows - 1;
    dirty_all = 1;
}

static void scroll(void)
{
    memmove(grid, grid + cols, (size_t)cols * (rows - 1));
    memset(grid + (size_t)cols * (rows - 1), ' ', (size_t)cols);
    dirty_all = 1;
}

static void putch(char ch)
{
    if (esc) {                                  /* ESC [ ... final byte in @-~ */
        if (ch >= 0x40 && ch <= 0x7E && ch != '[') esc = 0;
        return;
    }
    switch (ch) {
    case 27: esc = 1; return;
    case '\n': cx = 0; if (++cy >= rows) { cy = rows - 1; scroll(); } return;
    case '\r': cx = 0; return;
    case '\b': if (cx > 0) cx--; return;
    case '\t': cx = (cx + 8) & ~7; if (cx >= cols) { cx = 0; if (++cy >= rows) { cy = rows - 1; scroll(); } } return;
    default: break;
    }
    if ((unsigned char)ch < 0x20) return;
    grid[(size_t)cy * cols + cx] = ch;
    if (++cx >= cols) { cx = 0; if (++cy >= rows) { cy = rows - 1; scroll(); } }
}

static void draw(void)
{
    zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_WINDOW);
    char buf[2] = { 0, 0 };
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            char ch = grid[(size_t)y * cols + x];
            if (ch == ' ') continue;
            buf[0] = ch;
            zwm_ttext_w(s, PAD + x * CW, PAD + y * CH, buf, ZWM_COL_TEXT, PX, ZWM_FONT_MONO);
        }
    /* the cursor: a block when focused, an outline otherwise */
    if (focused) zwm_fill(s, PAD + cx * CW, PAD + cy * CH, CW, CH, ZWM_COL_ACCENT);
    else zwm_rect(s, PAD + cx * CW, PAD + cy * CH, CW, CH, ZWM_COL_TEXT_DIM);
    if (focused && grid[(size_t)cy * cols + cx] != ' ') {
        buf[0] = grid[(size_t)cy * cols + cx];
        zwm_ttext_w(s, PAD + cx * CW, PAD + cy * CH, buf, ZWM_COL_ACCENT_TEXT, PX, ZWM_FONT_MONO);
    }
    zwm_flush(c, win, s);
    dirty_all = 0;
}

static void start_shell(void)
{
    int in[2], out[2];
    if (pipe(in) || pipe(out)) { perror("pipe"); exit(1); }
    sh_pid = fork();
    if (sh_pid == 0) {
        dup2(in[0], 0); dup2(out[1], 1); dup2(out[1], 2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        setenv("TERM", "dumb", 1);
        execl("/bin/sh", "sh", (char *)NULL);
        _exit(127);
    }
    close(in[0]); close(out[1]);
    to_sh = in[1]; from_sh = out[0];
    fcntl(from_sh, F_SETFL, fcntl(from_sh, F_GETFL) | O_NONBLOCK);
}

static void key(const struct zwm_m_key *k)
{
    if (!k->down) return;
    uint32_t sym = k->sym;
    if (k->mods & ZWM_MOD_CTRL) {
        if (sym == 'd' || sym == 'D') { if (line_len == 0) { close(to_sh); to_sh = -1; } return; }
        if (sym == 'c' || sym == 'C') { const char *m = "^C\n"; for (const char *q = m; *q; q++) putch(*q); line_len = 0; return; }
        if (sym == 'l' || sym == 'L') { memset(grid, ' ', (size_t)cols * rows); cx = cy = 0; dirty_all = 1; return; }
        return;
    }
    if (sym == '\b') {
        if (line_len) { line_len--; putch('\b'); grid[(size_t)cy * cols + cx] = ' '; }
        return;
    }
    if (sym == '\n') {
        if (to_sh < 0) return;
        line[line_len++] = '\n';
        putch('\n');
        const char *p = line; size_t left = (size_t)line_len;
        while (left) { ssize_t n = write(to_sh, p, left); if (n <= 0) break; p += n; left -= (size_t)n; }
        line_len = 0;
        return;
    }
    if (sym >= 0x20 && sym < 0x7F && line_len < (int)sizeof line - 2) {
        line[line_len++] = (char)sym;
        putch((char)sym);
    }
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    c = zwm_connect();
    if (!c) { perror("zterm: zwm"); return 1; }
    CW = zwm_ttext_width_w("M", PX, ZWM_FONT_MONO);
    CH = zwm_ttext_height_w(PX, ZWM_FONT_MONO) + 1;
    struct zwm_m_geom g;
    win = zwm_create(c, cols * CW + 2 * PAD, rows * CH + 2 * PAD, "Terminal", 0, &g);
    if (win < 0) return 1;
    s = zwm_window_surface(c, win, g.w, g.h);
    grid_alloc((g.w - 2 * PAD) / CW, (g.h - 2 * PAD) / CH);
    start_shell();
    draw();
    for (;;) {
        struct pollfd pf[2] = { { zwm_fd(c), POLLIN, 0 }, { from_sh, POLLIN, 0 } };
        poll(pf, from_sh >= 0 ? 2 : 1, -1);
        int changed = 0;
        if (from_sh >= 0 && (pf[1].revents & (POLLIN | POLLHUP))) {
            char buf[1024];
            ssize_t n;
            while ((n = read(from_sh, buf, sizeof buf)) > 0) { for (ssize_t i = 0; i < n; i++) putch(buf[i]); changed = 1; }
            if (n == 0) {                       /* the shell is gone */
                const char *m = "[shell exited]"; for (const char *q = m; *q; q++) putch(*q);
                close(from_sh); from_sh = -1; changed = 1;
            }
        }
        zwm_event ev; int r;
        while ((r = zwm_next_event(c, &ev, 0)) > 0) {
            switch (ev.type) {
            case ZWM_S_KEY: key(&ev.key); changed = 1; break;
            case ZWM_S_FOCUS: focused = (int)ev.focus.focused; changed = 1; break;
            case ZWM_S_RESIZE:
                zwm_surface_resize(s, ev.geom.w, ev.geom.h);
                grid_alloc((ev.geom.w - 2 * PAD) / CW, (ev.geom.h - 2 * PAD) / CH);
                changed = 1; break;
            case ZWM_S_CLOSE: goto out;
            }
        }
        if (r < 0) break;
        if (changed || dirty_all) draw();
    }
out:
    if (sh_pid > 0) kill(sh_pid, SIGKILL);
    zwm_destroy(c, win);
    return 0;
}
