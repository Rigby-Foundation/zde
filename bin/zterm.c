/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zterm: a terminal window running /bin/sh over a pseudo-terminal (PTY). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <pty.h>
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
static int esc_state;           /* 0 = normal, 1 = ESC, 2 = ESC [ */
static int csi_params[4];
static int csi_nparams;
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
    if (esc_state == 1) {
        if (ch == '[') {
            esc_state = 2;
            csi_nparams = 0;
            csi_params[0] = csi_params[1] = csi_params[2] = csi_params[3] = 0;
            return;
        }
        esc_state = 0;
        return;
    }
    if (esc_state == 2) {
        if (ch >= '0' && ch <= '9') {
            csi_params[csi_nparams] = csi_params[csi_nparams] * 10 + (ch - '0');
            return;
        }
        if (ch == ';') {
            if (csi_nparams < 3) csi_nparams++;
            return;
        }
        if (ch == '?') return; /* ignore private mode marker */
        esc_state = 0;
        int p0 = csi_params[0];
        int p1 = csi_params[1];
        if (ch == 'K') { /* erase in line */
            if (p0 == 0) {
                for (int x = cx; x < cols; x++) grid[(size_t)cy * cols + x] = ' ';
            } else if (p0 == 1) {
                for (int x = 0; x <= cx && x < cols; x++) grid[(size_t)cy * cols + x] = ' ';
            } else if (p0 == 2) {
                for (int x = 0; x < cols; x++) grid[(size_t)cy * cols + x] = ' ';
            }
            dirty_all = 1;
            return;
        }
        if (ch == 'J') { /* erase display */
            if (p0 == 2) {
                memset(grid, ' ', (size_t)cols * rows);
                cx = cy = 0;
            } else if (p0 == 0) {
                for (int x = cx; x < cols; x++) grid[(size_t)cy * cols + x] = ' ';
                for (int y = cy + 1; y < rows; y++)
                    memset(grid + (size_t)y * cols, ' ', (size_t)cols);
            }
            dirty_all = 1;
            return;
        }
        if (ch == 'A') { /* cursor up */
            int n = p0 ? p0 : 1;
            cy = (cy >= n) ? cy - n : 0;
            dirty_all = 1;
            return;
        }
        if (ch == 'B') { /* cursor down */
            int n = p0 ? p0 : 1;
            cy = (cy + n < rows) ? cy + n : rows - 1;
            dirty_all = 1;
            return;
        }
        if (ch == 'C') { /* cursor forward */
            int n = p0 ? p0 : 1;
            cx = (cx + n < cols) ? cx + n : cols - 1;
            dirty_all = 1;
            return;
        }
        if (ch == 'D') { /* cursor back */
            int n = p0 ? p0 : 1;
            cx = (cx >= n) ? cx - n : 0;
            dirty_all = 1;
            return;
        }
        if (ch == 'H' || ch == 'f') { /* cursor position: row;col (1-based) */
            int r = p0 ? p0 - 1 : 0;
            int c_pos = p1 ? p1 - 1 : 0;
            if (r < 0) r = 0; if (r >= rows) r = rows - 1;
            if (c_pos < 0) c_pos = 0; if (c_pos >= cols) c_pos = cols - 1;
            cy = r; cx = c_pos;
            dirty_all = 1;
            return;
        }
        return;
    }

    switch (ch) {
    case 27: esc_state = 1; return;
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
    int master, slave;
    struct winsize ws = { .ws_row = (uint16_t)rows, .ws_col = (uint16_t)cols, .ws_xpixel = 0, .ws_ypixel = 0 };
    if (openpty(&master, &slave, NULL, NULL, &ws) < 0) {
        perror("openpty");
        exit(1);
    }
    sh_pid = fork();
    if (sh_pid == 0) {
        dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
        close(master); close(slave);
        setenv("TERM", "vt100", 1);
        execl("/bin/sh", "sh", "-i", (char *)NULL);
        _exit(127);
    }
    close(slave);
    to_sh = from_sh = master;
    fcntl(from_sh, F_SETFL, fcntl(from_sh, F_GETFL) | O_NONBLOCK);
}

static void key(const struct zwm_m_key *k)
{
    if (!k->down || to_sh < 0) return;
    uint32_t sym = k->sym;
    if (k->mods & ZWM_MOD_CTRL) {
        if (sym == 'c' || sym == 'C') { char c_code = 3; write(to_sh, &c_code, 1); return; }
        if (sym == 'd' || sym == 'D') { char c_code = 4; write(to_sh, &c_code, 1); return; }
        if (sym == 'z' || sym == 'Z') { char c_code = 26; write(to_sh, &c_code, 1); return; }
        if (sym == 'l' || sym == 'L') { char c_code = 12; write(to_sh, &c_code, 1); return; }
        if (sym >= 'a' && sym <= 'z') { char c_code = (char)(sym - 'a' + 1); write(to_sh, &c_code, 1); return; }
        if (sym >= 'A' && sym <= 'Z') { char c_code = (char)(sym - 'A' + 1); write(to_sh, &c_code, 1); return; }
        if (sym == '[' || sym == '{') { char c_code = 27; write(to_sh, &c_code, 1); return; }
        if (sym == '\\' || sym == '|') { char c_code = 28; write(to_sh, &c_code, 1); return; }
        if (sym == ']' || sym == '}') { char c_code = 29; write(to_sh, &c_code, 1); return; }
        return;
    }
    if (k->mods & ZWM_MOD_ALT) {
        if (sym == ZWM_KEY_LEFT) { write(to_sh, "\033b", 2); return; }
        if (sym == ZWM_KEY_RIGHT) { write(to_sh, "\033f", 2); return; }
        if (sym >= 0x20 && sym < 0x7F) {
            char buf[2] = { '\033', (char)sym };
            write(to_sh, buf, 2);
            return;
        }
    }
    if (sym == 27) { write(to_sh, "\033", 1); return; }
    if (sym == ZWM_KEY_UP) { write(to_sh, "\033[A", 3); return; }
    if (sym == ZWM_KEY_DOWN) { write(to_sh, "\033[B", 3); return; }
    if (sym == ZWM_KEY_RIGHT) { write(to_sh, "\033[C", 3); return; }
    if (sym == ZWM_KEY_LEFT) { write(to_sh, "\033[D", 3); return; }
    if (sym == ZWM_KEY_HOME) { write(to_sh, "\033[H", 3); return; }
    if (sym == ZWM_KEY_END) { write(to_sh, "\033[F", 3); return; }
    if (sym == ZWM_KEY_PGUP) { write(to_sh, "\033[5~", 4); return; }
    if (sym == ZWM_KEY_PGDN) { write(to_sh, "\033[6~", 4); return; }
    if (sym == ZWM_KEY_INSERT) { write(to_sh, "\033[2~", 4); return; }
    if (sym == ZWM_KEY_DELETE) { write(to_sh, "\033[3~", 4); return; }
    if (sym == '\b' || sym == 127) {
        write(to_sh, "\b", 1);
        return;
    }
    if (sym == '\n' || sym == '\r') {
        write(to_sh, "\n", 1);
        return;
    }
    if (sym == '\t') {
        write(to_sh, "\t", 1);
        return;
    }
    if (sym >= 0x20 && sym < 0x7F) {
        char ch = (char)sym;
        write(to_sh, &ch, 1);
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
            if (n == 0 || (n < 0 && errno == EIO)) {                       /* the shell is gone */
                const char *m = "[shell exited]"; for (const char *q = m; *q; q++) putch(*q);
                close(from_sh); from_sh = to_sh = -1; changed = 1;
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
                if (to_sh >= 0) {
                    struct winsize ws = { .ws_row = (uint16_t)rows, .ws_col = (uint16_t)cols, .ws_xpixel = 0, .ws_ypixel = 0 };
                    ioctl(to_sh, TIOCSWINSZ, &ws);
                }
                changed = 1; break;
            case ZWM_S_CLOSE: goto out;
            }
        }
        if (r < 0) break;
        if (changed || dirty_all) draw();
    }
out:
    if (to_sh >= 0) close(to_sh);
    if (sh_pid > 0) kill(sh_pid, SIGKILL);
    zwm_destroy(c, win);
    return 0;
}
