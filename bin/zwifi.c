/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zwifi: the Wi-Fi, through the kernel's /dev/wlan (a phone's: sic starts
 * its modem, which runs the WLAN firmware). Reading it says the state
 * ("state off|starting|ready|connected|failed", "detail ...", "note ...")
 * and the networks a scan found ("net <dBm> <secure> <ssid>"); writing
 * "on", "scan", "connect\t<ssid>\t<passphrase>" or "disconnect" asks for
 * them. What the kernel cannot do yet it says, and this shows.
 *   zwifi [device]       (another file in place of /dev/wlan, to try it) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <zwm.h>

static const char *dev = "/dev/wlan";
static zwm *c;
static int win, S;
static zwm_surface *s;

/* ---- the kernel's side --------------------------------------------------------------- */

#define MAX_NETS 32
static struct net { char ssid[33]; int dbm, secure; } nets[MAX_NETS];
static int nnets;
static char state[24] = "off", detail[160], note[160], chip[96], connected_to[33];
static int have_dev;

static void read_status(void)
{
    char buf[4096];
    int fd = open(dev, O_RDONLY);
    have_dev = fd >= 0;
    if (fd < 0) { snprintf(state, sizeof state, "none"); snprintf(detail, sizeof detail, "No Wi-Fi on this machine (%s: %s)", dev, strerror(errno)); return; }
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = 0;
    nnets = 0; chip[0] = 0; connected_to[0] = 0;
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(l, "state ", 6)) snprintf(state, sizeof state, "%s", l + 6);
        else if (!strncmp(l, "detail ", 7)) snprintf(detail, sizeof detail, "%s", l + 7);
        else if (!strncmp(l, "note ", 5)) snprintf(note, sizeof note, "%s", l + 5);
        else if (!strncmp(l, "chip ", 5)) snprintf(chip, sizeof chip, "%s", l);
        else if (!strncmp(l, "connected ", 10)) snprintf(connected_to, sizeof connected_to, "%s", l + 10);
        else if (!strncmp(l, "net ", 4) && nnets < MAX_NETS) {
            struct net *e = &nets[nnets];
            int used = 0;
            if (sscanf(l + 4, "%d %d %n", &e->dbm, &e->secure, &used) >= 2 && used > 0) {
                snprintf(e->ssid, sizeof e->ssid, "%s", l + 4 + used);
                nnets++;
            }
        }
    }
}

static void command(const char *cmd, size_t len)
{
    int fd = open(dev, O_WRONLY);
    if (fd < 0) { snprintf(note, sizeof note, "%s: %s", dev, strerror(errno)); return; }
    ssize_t r = write(fd, cmd, len);
    int err = errno;
    close(fd);
    note[0] = 0;
    read_status();                                  /* the kernel's own note, if it has one */
    if (r < 0 && !note[0]) snprintf(note, sizeof note, "%s", err == EOPNOTSUPP ? "The kernel cannot do this yet" : strerror(err));
}

/* ---- the window ------------------------------------------------------------------------ */

enum { B_NONE, B_MAIN, B_NET, B_FIELD, B_SHOW, B_CONNECT, B_CANCEL };
#define MAX_HITS 48
static struct hit { int x, y, w, h, what, arg; } hits[MAX_HITS];
static int nhits, hover_what = -1, hover_arg = -1;
static int chosen = -1;                             /* the network the password is for */
static char pass[64];
static int show_pass;
static int scroll;

static void add_hit(int x, int y, int w, int h, int what, int arg)
{
    if (nhits < MAX_HITS) hits[nhits++] = (struct hit){ x, y, w, h, what, arg };
}

static int hovered(int what, int arg) { return hover_what == what && hover_arg == arg; }

static void button(int x, int y, int w, int h, const char *label, int primary, int what, int arg)
{
    int px = ZWM_UI_PX;
    uint32_t bg = primary ? ZWM_COL_ACCENT : hovered(what, arg) ? ZWM_COL_ACCENT_DIM : ZWM_COL_SURFACE;
    uint32_t fg = primary ? ZWM_COL_ACCENT_TEXT : ZWM_COL_TEXT;
    zwm_round_rect(s, x, y, w, h, 8 * S, bg);
    if (primary && hovered(what, arg)) zwm_round_rect_border(s, x, y, w, h, 8 * S, ZWM_COL_TEXT_DIM);
    int tw = zwm_ttext_width_w(label, px, ZWM_FONT_MEDIUM);
    zwm_ttext_w(s, x + (w - tw) / 2, y + (h - zwm_ttext_height(px)) / 2, label, fg, px, ZWM_FONT_MEDIUM);
    add_hit(x, y, w, h, what, arg);
}

/* Words onto lines no wider than w (drawn, or only measured); the y after the last. */
static int wrap_at(int x, int y, int w, const char *text, uint32_t col, int px, int paint)
{
    char line[256] = "";
    const char *p = text;
    int lh = zwm_ttext_height(px) + 3 * S;
    while (*p) {
        const char *sp = strchr(p, ' ');
        size_t wl = sp ? (size_t)(sp - p) : strlen(p);
        char cand[256];
        snprintf(cand, sizeof cand, "%s%s%.*s", line, line[0] ? " " : "", (int)wl, p);
        if (line[0] && zwm_ttext_width(cand, px) > w) {
            if (paint) zwm_ttext(s, x, y, line, col, px);
            y += lh;
            snprintf(line, sizeof line, "%.*s", (int)wl, p);
        } else {
            snprintf(line, sizeof line, "%s", cand);
        }
        p += wl;
        while (*p == ' ') p++;
    }
    if (line[0]) { if (paint) zwm_ttext(s, x, y, line, col, px); y += lh; }
    return y;
}
static int wrap(int x, int y, int w, const char *text, uint32_t col, int px) { return wrap_at(x, y, w, text, col, px, 1); }

/* Signal: four bars, as many lit as it is strong. */
static void draw_bars(int x, int y, int h, int dbm)
{
    int lit = dbm >= -55 ? 4 : dbm >= -67 ? 3 : dbm >= -78 ? 2 : 1;
    int bw = 3 * S, gap = 2 * S;
    for (int i = 0; i < 4; i++) {
        int bh = h * (i + 1) / 4;
        zwm_round_rect(s, x + i * (bw + gap), y + h - bh, bw, bh, S, i < lit ? ZWM_COL_TEXT : ZWM_COL_ACCENT_DIM);
    }
}

static void draw_lock(int x, int y, uint32_t col)
{
    zwm_round_rect_border(s, x + 2 * S, y, 6 * S, 7 * S, 3 * S, col);
    zwm_round_rect(s, x, y + 5 * S, 10 * S, 7 * S, 2 * S, col);
}

static void draw(void)
{
    int px = ZWM_UI_PX, pad = 18 * S, W = s->w;
    nhits = 0;
    zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_WINDOW);
    int y = 16 * S;
    zwm_ttext_w(s, pad, y, "Wi-Fi", ZWM_COL_TEXT, 22 * S, ZWM_FONT_MEDIUM);

    /* the state, as a pill on the right */
    const char *label = !strcmp(state, "off") ? "Off" : !strcmp(state, "starting") ? "Starting" : !strcmp(state, "ready") ? "On" :
                        !strcmp(state, "connected") ? "Connected" : !strcmp(state, "failed") ? "Failed" : !strcmp(state, "none") ? "Not here" : state;
    int lw = zwm_ttext_width_w(label, px, ZWM_FONT_MEDIUM) + 20 * S;
    uint32_t pill = !strcmp(state, "failed") ? ZWM_COL_DANGER : !strcmp(state, "ready") || !strcmp(state, "connected") ? ZWM_COL_ACCENT : ZWM_COL_SURFACE;
    uint32_t pilltext = pill == ZWM_COL_ACCENT ? ZWM_COL_ACCENT_TEXT : ZWM_COL_TEXT;
    zwm_round_rect(s, W - pad - lw, y + 2 * S, lw, 22 * S, 11 * S, pill);
    zwm_ttext_w(s, W - pad - lw + 10 * S, y + 2 * S + (22 * S - zwm_ttext_height(px)) / 2, label, pilltext, px, ZWM_FONT_MEDIUM);
    y += 40 * S;

    /* the details, in a card: measured, then drawn */
    char conn[64] = "";
    if (connected_to[0]) snprintf(conn, sizeof conn, "Connected to %s", connected_to);
    int cx = pad + 14 * S, cw = W - 2 * pad - 28 * S;
    for (int paint = 0; paint < 2; paint++) {
        int yy = y + 12 * S;
        yy = wrap_at(cx, yy, cw, detail, ZWM_COL_TEXT_DIM, px, paint);
        if (chip[0]) yy = wrap_at(cx, yy, cw, chip, ZWM_COL_TEXT_DIM, px, paint);
        if (conn[0]) yy = wrap_at(cx, yy, cw, conn, ZWM_COL_TEXT, px, paint);
        if (!paint) zwm_round_rect(s, pad, y, W - 2 * pad, yy - y + 8 * S, 10 * S, ZWM_COL_SURFACE);
        else y = yy + 8 * S + 14 * S;
    }

    /* the one thing to do now */
    if (have_dev) {
        const char *act = !strcmp(state, "ready") || !strcmp(state, "connected") ? "Scan for networks" :
                          !strcmp(state, "starting") ? "Starting..." : "Turn Wi-Fi on";
        button(pad, y, W - 2 * pad, 40 * S, act, strcmp(state, "starting") != 0, B_MAIN, 0);
        y += 54 * S;
    }
    if (note[0]) { y = wrap(pad, y, W - 2 * pad, note, ZWM_COL_DANGER, px) + 6 * S; }

    /* the password for the chosen network */
    if (chosen >= 0 && chosen < nnets) {
        char t[80];
        snprintf(t, sizeof t, "Password for %s", nets[chosen].ssid);
        zwm_ttext_w(s, pad, y, t, ZWM_COL_TEXT, px, ZWM_FONT_MEDIUM);
        y += zwm_ttext_height(px) + 8 * S;
        int fw = W - 2 * pad - 70 * S;
        zwm_round_rect(s, pad, y, fw, 38 * S, 8 * S, ZWM_COL_BG);
        zwm_round_rect_border(s, pad, y, fw, 38 * S, 8 * S, ZWM_COL_TEXT_DIM);
        char shown[80];
        size_t n = strlen(pass);
        if (show_pass) snprintf(shown, sizeof shown, "%s", pass);
        else { for (size_t i = 0; i < n && i < sizeof shown - 1; i++) shown[i] = '*'; shown[n < sizeof shown - 1 ? n : sizeof shown - 1] = 0; }
        int ty = y + (38 * S - zwm_ttext_height(px)) / 2;
        zwm_ttext(s, pad + 10 * S, ty, shown, ZWM_COL_TEXT, px);
        zwm_fill(s, pad + 10 * S + zwm_ttext_width(shown, px) + S, ty, 2 * S, zwm_ttext_height(px), ZWM_COL_TEXT);     /* the caret */
        add_hit(pad, y, fw, 38 * S, B_FIELD, 0);
        button(pad + fw + 8 * S, y, 62 * S, 38 * S, show_pass ? "Hide" : "Show", 0, B_SHOW, 0);
        y += 46 * S;
        int bw = (W - 2 * pad - 8 * S) / 2;
        button(pad, y, bw, 38 * S, "Cancel", 0, B_CANCEL, 0);
        button(pad + bw + 8 * S, y, bw, 38 * S, "Connect", 1, B_CONNECT, 0);
        y += 46 * S;
        y = wrap(pad, y, W - 2 * pad, "Type with a keyboard, or the on-screen one (the keyboard button in the bar).", ZWM_COL_TEXT_DIM, px) + 8 * S;
    }

    /* the networks */
    zwm_ttext_w(s, pad, y, "Networks", ZWM_COL_TEXT_DIM, px, ZWM_FONT_MEDIUM);
    y += zwm_ttext_height(px) + 8 * S;
    if (!nnets) {
        wrap(pad, y, W - 2 * pad, !strcmp(state, "ready") ? "None found yet: scan." : "Turn Wi-Fi on to see networks.", ZWM_COL_TEXT_DIM, px);
        return;
    }
    int row = 44 * S;
    for (int i = scroll; i < nnets && y + row <= s->h; i++) {
        uint32_t bg = i == chosen ? ZWM_COL_ACCENT_DIM : hovered(B_NET, i) ? ZWM_COL_SURFACE : ZWM_COL_WINDOW;
        zwm_round_rect(s, pad, y, W - 2 * pad, row - 4 * S, 8 * S, bg);
        draw_bars(pad + 12 * S, y + 12 * S, 16 * S, nets[i].dbm);
        int ty = y + (row - 4 * S - zwm_ttext_height(px)) / 2;
        zwm_ttext(s, pad + 44 * S, ty, nets[i].ssid[0] ? nets[i].ssid : "(hidden)", ZWM_COL_TEXT, px);
        if (nets[i].secure) draw_lock(W - pad - 24 * S, y + 12 * S, ZWM_COL_TEXT_DIM);
        if (!strcmp(nets[i].ssid, connected_to)) zwm_ttext(s, W - pad - 110 * S, ty, "connected", ZWM_COL_TEXT_DIM, px);
        add_hit(pad, y, W - 2 * pad, row - 4 * S, B_NET, i);
        y += row;
    }
}

static struct hit *hit_at(int x, int y)
{
    for (int i = nhits - 1; i >= 0; i--)
        if (x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y && y < hits[i].y + hits[i].h) return &hits[i];
    return NULL;
}

static void do_connect(void)
{
    if (chosen < 0 || chosen >= nnets) return;
    char cmd[160];
    int n = snprintf(cmd, sizeof cmd, "connect\t%s\t%s\n", nets[chosen].ssid, nets[chosen].secure ? pass : "");
    command(cmd, (size_t)n);
}

static void click(struct hit *h)
{
    if (!h) return;
    switch (h->what) {
    case B_MAIN:
        if (!strcmp(state, "ready") || !strcmp(state, "connected")) command("scan\n", 5);
        else if (strcmp(state, "starting")) command("on\n", 3);
        break;
    case B_NET:
        if (!nets[h->arg].secure) { chosen = h->arg; pass[0] = 0; do_connect(); chosen = -1; break; }
        if (chosen != h->arg) { chosen = h->arg; pass[0] = 0; show_pass = 0; }
        break;
    case B_SHOW:    show_pass = !show_pass; break;
    case B_CANCEL:  chosen = -1; pass[0] = 0; break;
    case B_CONNECT: do_connect(); chosen = -1; memset(pass, 0, sizeof pass); break;
    }
}

static void key(const struct zwm_m_key *k)
{
    if (!k->down) return;
    if (chosen >= 0) {
        size_t n = strlen(pass);
        if (k->sym == '\b' || k->sym == 127) { if (n) pass[n - 1] = 0; }
        else if (k->sym == '\n' || k->sym == '\r') { do_connect(); chosen = -1; memset(pass, 0, sizeof pass); }
        else if (k->sym == 27) { chosen = -1; pass[0] = 0; }
        else if (k->sym >= 32 && k->sym < 127 && n < 63) { pass[n] = (char)k->sym; pass[n + 1] = 0; }
        return;
    }
    if (k->sym == ZWM_KEY_DOWN && scroll + 1 < nnets) scroll++;
    else if (k->sym == ZWM_KEY_UP && scroll > 0) scroll--;
    else if (k->sym == 's') click(&(struct hit){ 0, 0, 0, 0, B_MAIN, 0 });
}

int main(int argc, char **argv)
{
    if (argc > 1) dev = argv[1];
    c = zwm_connect();
    if (!c) { perror("zwifi: zwm"); return 1; }
    S = zwm_scale();
    struct zwm_m_geom g;
    win = zwm_create(c, 400 * S, 560 * S, "Wi-Fi", 0, &g);
    if (win < 0) return 1;
    s = zwm_window_surface(c, win, g.w, g.h);
    if (!s) return 1;
    read_status();
    for (;;) {
        draw();
        zwm_flush(c, win, s);
        struct pollfd pf = { zwm_fd(c), POLLIN, 0 };
        if (poll(&pf, 1, 1000) == 0) { read_status(); continue; }      /* once a second: what the kernel says now */
        zwm_event ev;
        int r;
        while ((r = zwm_next_event(c, &ev, 0)) > 0) {
            switch (ev.type) {
            case ZWM_S_CLOSE: zwm_destroy(c, win); return 0;
            case ZWM_S_RESIZE: zwm_surface_resize(s, ev.geom.w, ev.geom.h); break;
            case ZWM_S_KEY:
                if (ev.key.down && ev.key.sym == 27 && chosen < 0) { zwm_destroy(c, win); return 0; }
                key(&ev.key);
                break;
            case ZWM_S_MOUSE: {
                struct hit *h = hit_at(ev.mouse.x, ev.mouse.y);
                hover_what = h ? h->what : -1; hover_arg = h ? h->arg : -1;
                if (ev.mouse.kind == ZWM_MOUSE_PRESS && (ev.mouse.buttons & ZWM_BTN_LEFT)) click(h);
                break;
            }
            }
        }
        if (r < 0) return 0;
    }
}
