/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zbar: the bar across the top. On the left the system menu (start an
 * app) and the name of the app in front, whose menu minimizes, maximizes
 * or closes its window; on the right the brightness (a phone's panel), the
 * volume, the battery (/dev/battery) and the date; on a phone, a keyboard
 * button before them starts and stops the one on the screen (zkbd), and
 * a Wi-Fi one (when the kernel has /dev/wlan) opens zwifi. The volume opens a slider (and a mute button) over
 * /dev/mixer, and is kept in /disk/.volume when a disk is mounted, so the
 * next session starts where this one left off; the brightness opens a
 * slider over /dev/panel, kept in /disk/.brightness. Menus are small
 * undecorated windows that go away when they lose the focus. Re-spawned by
 * zde if it dies. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/wait.h>
#include <zwm.h>

#define S zwm_scale()
#define BAR_H    (26 * S)
#define PAD      (10 * S)           /* around each item's text */
#define ROW_H    (26 * S)           /* a menu row */
#define MENU_W   (180 * S)
#define VOL_W    (240 * S)
#define VOL_H    (48 * S)
#define VOLUME_FILE "/disk/.volume"
#define BRIGHTNESS_FILE "/disk/.brightness"
#define BRIGHTNESS_MIN 3            /* percent: lower is black on some panels */

static zwm *c;
static int bar = -1, bar_y;           /* bar_y: below a camera cutout, on a phone */
static zwm_surface *bs;

/* ---- windows: the one in front ----------------------------------------------------- */

static struct zwm_m_window windows[32];
static int nwindows;
static int app_win = -1, app_state;
static char app_name[64] = "Desktop";

/* The app's name: its title up to a ':' ("Terminal: /bin/sh" is Terminal). */
static void pick_app(void)
{
    app_win = -1; app_state = 0;
    snprintf(app_name, sizeof app_name, "Desktop");
    for (int i = 0; i < nwindows; i++)
        if (windows[i].state & ZWM_WIN_FOCUSED) {
            app_win = (int)windows[i].id; app_state = (int)windows[i].state;
            snprintf(app_name, sizeof app_name, "%.*s", (int)strcspn(windows[i].title, ":"), windows[i].title);
            if (!app_name[0]) snprintf(app_name, sizeof app_name, "Untitled");
        }
}

/* ---- the volume ------------------------------------------------------------------- */

static int mixer = -1;
static int volume = -1;             /* 0-100; -1: no sound card */
static int unmuted = 50;            /* what the mute button goes back to */

static void volume_read(void)
{
    int v;
    if (mixer >= 0 && ioctl(mixer, SOUND_MIXER_READ_VOLUME, &v) == 0) volume = v & 0xFF;
}

static void volume_set(int v)
{
    if (mixer < 0) return;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    int lr = v | v << 8;
    if (ioctl(mixer, SOUND_MIXER_WRITE_VOLUME, &lr) == 0) volume = v;
}

static void volume_save(void)
{
    int fd = open(VOLUME_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;                     /* no disk: it lasts until reboot */
    char buf[16];
    int n = snprintf(buf, sizeof buf, "%d\n", volume);
    if (write(fd, buf, (size_t)n) != n) { /* nothing to do about it */ }
    close(fd);
}

static void volume_load(void)
{
    mixer = open("/dev/mixer", O_RDWR);
    if (mixer < 0) return;
    volume_read();
    int fd = open(VOLUME_FILE, O_RDONLY);
    if (fd < 0) return;
    char buf[16] = "";
    if (read(fd, buf, sizeof buf - 1) > 0) volume_set(atoi(buf));
    close(fd);
    if (volume > 0) unmuted = volume;
}

/* A loudspeaker, with as many waves as it is loud (a cross when silent). */
static void draw_speaker(zwm_surface *s, int x, int y, int size, int vol, uint32_t col)
{
    int body_h = size * 3 / 8, body_w = size / 4, cy = y + size / 2;
    zwm_fill(s, x, cy - body_h / 2, body_w, body_h, col);
    for (int i = 0; i < size / 3; i++) {                        /* the cone */
        int h = body_h + i * 2 * (size / 2 - body_h / 2) / (size / 3);
        zwm_vline(s, x + body_w + i, cy - h / 2, h, col);
    }
    int ox = x + body_w + size / 3 + S;
    if (vol == 0) {
        for (int i = 0; i < size / 3; i++) {
            zwm_fill(s, ox + S + i, cy - size / 6 + i, S + S / 2 + 1, S + S / 2 + 1, col);
            zwm_fill(s, ox + S + i, cy + size / 6 - i - S, S + S / 2 + 1, S + S / 2 + 1, col);
        }
        return;
    }
    int waves = vol < 34 ? 1 : vol < 67 ? 2 : 3;
    for (int w = 1; w <= waves; w++) {                          /* arcs, a quarter-circle each side */
        int r = w * size / 7 + S;
        for (int a = -r; a <= r; a++) {
            int dx2 = r * r - a * a, dx = 0;
            while ((dx + 1) * (dx + 1) <= dx2) dx++;
            zwm_fill(s, ox + dx - r / 2, cy + a, S, S, col);
        }
    }
}

/* ---- the brightness ---------------------------------------------------------------- */

static int panel = -1;              /* /dev/panel, for commands */
static int brightness = -1;         /* percent; -1: no panel, or not known yet */
static int panel_max = 2047;        /* its top level, as the kernel says */

/* "brightness N/MAX on|off": the level, if the kernel knows it. */
static void brightness_read(void)
{
    int fd = open("/dev/panel", O_RDONLY);
    if (fd < 0) return;
    char buf[160];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    int level, max;
    if (sscanf(buf, "brightness %d/%d", &level, &max) == 2 && max > 0) { panel_max = max; brightness = (level * 100 + max / 2) / max; }
    else if (sscanf(buf, "brightness ?/%d", &max) == 1 && max > 0) panel_max = max;
}

static void brightness_set(int v)
{
    if (panel < 0) return;
    if (v < BRIGHTNESS_MIN) v = BRIGHTNESS_MIN;
    if (v > 100) v = 100;
    char buf[32];
    int n = snprintf(buf, sizeof buf, "brightness %d\n", (v * panel_max + 50) / 100);
    if (write(panel, buf, (size_t)n) == n) brightness = v;
}

static void brightness_save(void)
{
    int fd = open(BRIGHTNESS_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char buf[16];
    int n = snprintf(buf, sizeof buf, "%d\n", brightness);
    if (write(fd, buf, (size_t)n) != n) { /* nothing to do */ }
    close(fd);
}

static void brightness_load(void)
{
    panel = open("/dev/panel", O_WRONLY);
    if (panel < 0) return;
    brightness_read();
    int fd = open(BRIGHTNESS_FILE, O_RDONLY);
    if (fd < 0) return;
    char buf[16] = { 0 };
    if (read(fd, buf, sizeof buf - 1) > 0 && atoi(buf) > 0) brightness_set(atoi(buf));
    close(fd);
}

/* A sun: a disc and eight rays, longer the brighter. */
static void draw_sun(zwm_surface *s, int x, int y, int size, int pct, uint32_t col)
{
    int cx = x + size / 2, cy = y + size / 2, r = size / 5 + 1;
    zwm_disc(s, cx, cy, r, col);
    static const int dir[8][2] = { { 2, 0 }, { 1, 1 }, { 0, 2 }, { -1, 1 }, { -2, 0 }, { -1, -1 }, { 0, -2 }, { 1, -1 } };
    int len = pct < 0 ? size / 4 : size / 8 + (size / 4 - size / 8) * pct / 100;
    for (int i = 0; i < 8; i++)
        for (int t = r + S + 1; t < r + S + 1 + len; t++) {
            int px = cx + dir[i][0] * t / 2, py = cy + dir[i][1] * t / 2;
            if (dir[i][0] && dir[i][1]) { px = cx + dir[i][0] * t * 7 / 10; py = cy + dir[i][1] * t * 7 / 10; }
            zwm_fill(s, px - S / 2, py - S / 2, S, S, col);
        }
}

/* ---- the battery ------------------------------------------------------------------- */

static int battery = -1, charging;  /* percent; -1: no battery */
static time_t battery_at;

static void battery_read(void)
{
    time_t now = time(NULL);
    if (battery_at && now - battery_at < 10 && now >= battery_at) return;     /* the gauge changes slowly */
    battery_at = now;
    int fd = open("/dev/battery", O_RDONLY);
    if (fd < 0) { battery = -1; return; }
    char buf[512];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    battery = -1;
    if (n <= 0) return;
    buf[n] = 0;
    int v;
    char *p = strstr(buf, "capacity ");
    if (p && sscanf(p, "capacity %d", &v) == 1 && v >= 0 && v <= 100) battery = v;
    charging = strstr(buf, "status charging") || strstr(buf, "status full");
}

/* A battery on its side, filled as far as it is charged, with a bolt while charging. */
static void draw_battery(zwm_surface *s, int x, int y, int h, int pct, int bolt, uint32_t col)
{
    int w = h * 2 - 2 * S;
    zwm_round_rect_border(s, x, y, w, h, 3 * S, col);
    zwm_fill(s, x + w, y + h / 3, 2 * S, h - 2 * (h / 3), col);
    int in = 2 * S, fw = (w - 2 * in) * pct / 100;
    uint32_t fill = pct <= 15 && !bolt ? ZWM_COL_DANGER : col;
    if (fw > 0) zwm_round_rect(s, x + in, y + in, fw, h - 2 * in, S, fill);
    if (bolt) {                                         /* a zig-zag, dark over the fill */
        int cx = x + w / 2, top = y + 2 * S, bot = y + h - 2 * S, mid = (top + bot) / 2;
        for (int yy = top; yy <= bot; yy++) {
            int off = yy < mid ? (mid - yy) * 3 / 4 : -(yy - mid) * 3 / 4;
            zwm_fill(s, cx + off - S, yy, 2 * S, 1, ZWM_COL_PANEL);
        }
    }
}

/* ---- the keyboard on the screen --------------------------------------------------- */

static int touch_device;            /* a phone: a panel, or a portrait screen */
static pid_t kbd_pid;

static void toggle_keyboard(void)
{
    if (kbd_pid > 0 && kill(kbd_pid, 0) == 0) { kill(kbd_pid, SIGTERM); kbd_pid = 0; return; }
    kbd_pid = fork();
    if (kbd_pid == 0) { execl("/bin/zkbd", "zkbd", (char *)NULL); _exit(127); }
}

/* A keyboard: an outline, two rows of keys and a space bar. */
static void draw_keyboard(zwm_surface *s, int x, int y, int h, uint32_t col)
{
    int w = h * 3 / 2, k = S + S / 2 > 1 ? S + S / 2 : 1;
    zwm_round_rect_border(s, x, y, w, h, 2 * S, col);
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 4; i++) zwm_fill(s, x + 3 * S + i * (w - 6 * S) / 4, y + 3 * S + row * (h / 4), k, k, col);
    zwm_fill(s, x + w / 4, y + h - 4 * S, w / 2, k, col);
}

/* ---- the Wi-Fi ---------------------------------------------------------------------- */

#define COL_MAC_BLUE  ZWM_RGB(0x00, 0x7a, 0xff)
#define COL_MAC_GRAY  ZWM_RGB(0x3a, 0x3a, 0x3c)
#define COL_MAC_HOVER ZWM_RGB(0x2c, 0x2c, 0x30)
#define WIFI_POP_W    (270 * S)
#define WIFI_FILE     "/disk/.wifi"
#define WIFI_TMP_FILE "/tmp/.wifi"

static int wifi = -1;               /* -1: no /dev/wlan; 0 off; 1 starting; 2 on; 3 failed */

static void draw_lock(zwm_surface *s, int x, int y, uint32_t col)
{
    zwm_round_rect_border(s, x + 2 * S, y, 6 * S, 7 * S, 3 * S, col);
    zwm_round_rect(s, x, y + 5 * S, 10 * S, 7 * S, 2 * S, col);
}

#define MAX_WIFI_NETS 32
struct wifi_net {
    char ssid[33];
    int dbm;
    int secure;
};

static struct wifi_net wifi_nets[MAX_WIFI_NETS];
static int wifi_nnets;
static char wifi_state[24] = "off";
static char wifi_detail[160];
static char wifi_connected[33];
static int wifi_chosen = -1;
static char wifi_pass[64];
static int wifi_show_pass = 0;

struct known_wifi {
    char ssid[33];
    char pass[64];
};
static struct known_wifi known_wifis[16];
static int nknown_wifis;

enum {
    WIFI_HIT_NONE,
    WIFI_HIT_TOGGLE,
    WIFI_HIT_NET,
    WIFI_HIT_PASS_FIELD,
    WIFI_HIT_CONNECT,
    WIFI_HIT_CANCEL,
    WIFI_HIT_SHOW_PASS,
    WIFI_HIT_SETTINGS,
};

struct wifi_hit {
    int x, y, w, h;
    int what;
    int arg;
};
static struct wifi_hit wifi_hits[32];
static int nwifi_hits;
static int wifi_hover_what = -1, wifi_hover_arg = -1;

static void add_wifi_hit(int x, int y, int w, int h, int what, int arg)
{
    if (nwifi_hits < 32) wifi_hits[nwifi_hits++] = (struct wifi_hit){ x, y, w, h, what, arg };
}

static int wifi_is_known(const char *ssid)
{
    for (int i = 0; i < nknown_wifis; i++)
        if (!strcmp(known_wifis[i].ssid, ssid)) return 1;
    return 0;
}

static const char *wifi_get_saved_pass(const char *ssid)
{
    for (int i = 0; i < nknown_wifis; i++)
        if (!strcmp(known_wifis[i].ssid, ssid)) return known_wifis[i].pass;
    return NULL;
}

static void wifi_load_known(void)
{
    nknown_wifis = 0;
    int fd = open(WIFI_FILE, O_RDONLY);
    if (fd < 0) fd = open(WIFI_TMP_FILE, O_RDONLY);
    if (fd >= 0) {
        char buf[1024];
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n > 0) {
            buf[n] = 0;
            char *save = NULL;
            for (char *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
                char *tab = strchr(l, '\t');
                if (tab && nknown_wifis < 16) {
                    *tab = 0;
                    snprintf(known_wifis[nknown_wifis].ssid, sizeof known_wifis[0].ssid, "%s", l);
                    snprintf(known_wifis[nknown_wifis].pass, sizeof known_wifis[0].pass, "%s", tab + 1);
                    nknown_wifis++;
                }
            }
        }
    }
    if (!wifi_is_known("ByteNode5G") && nknown_wifis < 16) {
        snprintf(known_wifis[nknown_wifis].ssid, sizeof known_wifis[0].ssid, "ByteNode5G");
        snprintf(known_wifis[nknown_wifis].pass, sizeof known_wifis[0].pass, "03081977");
        nknown_wifis++;
    }
}

static void wifi_save_known(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return;
    int found = -1;
    for (int i = 0; i < nknown_wifis; i++) {
        if (!strcmp(known_wifis[i].ssid, ssid)) { found = i; break; }
    }
    if (found >= 0) {
        snprintf(known_wifis[found].pass, sizeof known_wifis[0].pass, "%s", pass ? pass : "");
    } else if (nknown_wifis < 16) {
        snprintf(known_wifis[nknown_wifis].ssid, sizeof known_wifis[0].ssid, "%s", ssid);
        snprintf(known_wifis[nknown_wifis].pass, sizeof known_wifis[0].pass, "%s", pass ? pass : "");
        nknown_wifis++;
    }
    int fd = open(WIFI_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) fd = open(WIFI_TMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        for (int i = 0; i < nknown_wifis; i++) {
            char line[128];
            int n = snprintf(line, sizeof line, "%s\t%s\n", known_wifis[i].ssid, known_wifis[i].pass);
            write(fd, line, (size_t)n);
        }
        close(fd);
    }
}

static void spawn_dhcp(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        usleep(400000);
        execl("/bin/dhcp", "dhcp", "-t", "15", "wlan0", (char *)NULL);
        _exit(127);
    }
}

static void spawn_dhcp_waiter(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        for (int i = 0; i < 40; i++) {
            usleep(500000);
            int fd = open("/dev/wlan", O_RDONLY);
            if (fd >= 0) {
                char buf[512];
                ssize_t n = read(fd, buf, sizeof buf - 1);
                close(fd);
                if (n > 0) {
                    buf[n] = 0;
                    if (strstr(buf, ": connected")) {
                        usleep(300000);
                        execl("/bin/dhcp", "dhcp", "-t", "15", "wlan0", (char *)NULL);
                        _exit(0);
                    }
                    if (strstr(buf, "failed")) break;
                }
            }
        }
        _exit(1);
    }
}

static char dhcp_done_for[33];

static void wifi_read_full(void)
{
    char buf[4096];
    int fd = open("/dev/wlan", O_RDONLY);
    if (fd < 0) { wifi = -1; wifi_nnets = 0; return; }
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = 0;
    wifi_nnets = 0;
    wifi_connected[0] = 0;
    wifi_state[0] = 0;
    wifi_detail[0] = 0;
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(l, "state ", 6)) {
            snprintf(wifi_state, sizeof wifi_state, "%s", l + 6);
        } else if (!strncmp(l, "connected ", 10)) {
            snprintf(wifi_connected, sizeof wifi_connected, "%s", l + 10);
        } else if (!strncmp(l, "detail ", 7)) {
            snprintf(wifi_detail, sizeof wifi_detail, "%s", l + 7);
            if (!strncmp(wifi_detail, "join ", 5)) {
                char *colon = strchr(wifi_detail + 5, ':');
                if (colon && strstr(colon, "connected")) {
                    int sl = (int)(colon - (wifi_detail + 5));
                    if (sl > 0 && sl < (int)sizeof wifi_connected) {
                        memcpy(wifi_connected, wifi_detail + 5, sl);
                        wifi_connected[sl] = 0;
                    }
                }
            }
        } else if (!strncmp(l, "net ", 4) && wifi_nnets < MAX_WIFI_NETS) {
            struct wifi_net *e = &wifi_nets[wifi_nnets];
            int used = 0;
            if (sscanf(l + 4, "%d %d %n", &e->dbm, &e->secure, &used) >= 2 && used > 0) {
                snprintf(e->ssid, sizeof e->ssid, "%s", l + 4 + used);
                if (e->ssid[0]) {
                    int dup = 0;
                    for (int j = 0; j < wifi_nnets; j++) {
                        if (!strcmp(wifi_nets[j].ssid, e->ssid)) {
                            if (e->dbm > wifi_nets[j].dbm) wifi_nets[j].dbm = e->dbm;
                            dup = 1; break;
                        }
                    }
                    if (!dup) wifi_nnets++;
                }
            }
        }
    }
    wifi = (!strcmp(wifi_state, "ready") || !strcmp(wifi_state, "connected")) ? 2 :
           !strcmp(wifi_state, "starting") ? 1 :
           !strcmp(wifi_state, "failed") ? 3 : 0;
    if (wifi_connected[0]) wifi = 2;
    if (wifi_connected[0]) {
        if (strcmp(wifi_connected, dhcp_done_for) != 0) {
            snprintf(dhcp_done_for, sizeof dhcp_done_for, "%s", wifi_connected);
            spawn_dhcp();
        }
    } else {
        dhcp_done_for[0] = 0;
    }
}

static void wifi_read(void)
{
    wifi_read_full();
}


/* Three arcs over a dot, a quarter circle wide; dim when the Wi-Fi is off. */
static void draw_wifi(zwm_surface *s, int x, int y, int size, int st, uint32_t col)
{
    uint32_t on = st == 2 ? col : st == 3 ? ZWM_COL_DANGER : ZWM_COL_TEXT_DIM;
    int cx = x + size / 2, cy = y + size - S;
    int t = size / 9 > 1 ? size / 9 : 1;
    for (int py = y; py < y + size; py++)
        for (int px = x; px < x + size; px++) {
            int dx = px - cx, dy = cy - py;
            if (dy <= 0 || dx > dy || -dx > dy) continue;      /* within 45 degrees of straight up */
            int d2 = dx * dx + dy * dy;
            for (int k = 1; k <= 3; k++) {
                int r = size * k / 3 - t, r0 = r - t, r1 = r + t / 2;
                if (k == 3) r1 = r;
                if (d2 >= r0 * r0 && d2 <= r1 * r1) {
                    uint32_t c = st == 1 && k == 3 ? ZWM_COL_TEXT_DIM : on;
                    zwm_blend(s, px, py, c, 255);
                }
            }
        }
    zwm_disc(s, cx, cy - t, t, on);
}

/* ---- the bar ---------------------------------------------------------------------- */

enum { ITEM_SYS, ITEM_APP, ITEM_KBD, ITEM_WIFI, ITEM_BRIGHT, ITEM_VOL, ITEM_BATT, ITEM_CLOCK, NITEMS };
static int item_x[NITEMS], item_w[NITEMS];
static int hover = -1, open_item = -1;
static int screen_h = 2400;
static char clock_text[40];

static void clock_update(void)
{
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);                      /* the firmware clock, as it is: no time zones yet */
    if (t > 1000000000) strftime(clock_text, sizeof clock_text, "%a %e %b  %H:%M", &tm);
    else strftime(clock_text, sizeof clock_text, "%H:%M", &tm);
}

static void layout(void)
{
    int px = ZWM_UI_PX;
    item_x[ITEM_SYS] = 4 * S;
    item_w[ITEM_SYS] = zwm_ttext_width_w("sic", px, ZWM_FONT_MEDIUM) + 2 * PAD;
    item_x[ITEM_APP] = item_x[ITEM_SYS] + item_w[ITEM_SYS];
    item_w[ITEM_APP] = zwm_ttext_width_w(app_name, px, ZWM_FONT_MEDIUM) + 2 * PAD;
    item_w[ITEM_CLOCK] = zwm_ttext_width(clock_text, px) + 2 * PAD;
    item_x[ITEM_CLOCK] = bs->w - 4 * S - item_w[ITEM_CLOCK];
    item_w[ITEM_BATT] = battery < 0 ? 0 : 26 * S + zwm_ttext_width("100%", px) + 2 * PAD;
    item_x[ITEM_BATT] = item_x[ITEM_CLOCK] - item_w[ITEM_BATT];
    item_w[ITEM_VOL] = volume < 0 ? 0 : 16 * S + zwm_ttext_width("100%", px) + 2 * PAD;
    item_x[ITEM_VOL] = item_x[ITEM_BATT] - item_w[ITEM_VOL];
    item_w[ITEM_BRIGHT] = panel < 0 ? 0 : 16 * S + zwm_ttext_width("100%", px) + 2 * PAD;
    item_x[ITEM_BRIGHT] = item_x[ITEM_VOL] - item_w[ITEM_BRIGHT];
    item_w[ITEM_WIFI] = wifi < 0 ? 0 : 20 * S + 2 * PAD;
    item_x[ITEM_WIFI] = item_x[ITEM_BRIGHT] - item_w[ITEM_WIFI];
    item_w[ITEM_KBD] = touch_device ? 18 * S + 2 * PAD : 0;
    item_x[ITEM_KBD] = item_x[ITEM_WIFI] - item_w[ITEM_KBD];
}

static void draw_bar(void)
{
    layout();
    int px = ZWM_UI_PX, ty = (BAR_H - zwm_ttext_height(px)) / 2;
    zwm_fill(bs, 0, 0, bs->w, bs->h, ZWM_COL_PANEL);
    zwm_hline(bs, 0, bs->h - 1, bs->w, ZWM_COL_BORDER);
    for (int i = 0; i < NITEMS; i++) {
        if (!item_w[i]) continue;
        if (i == open_item || i == hover)
            zwm_round_rect(bs, item_x[i], 3 * S, item_w[i], BAR_H - 6 * S, 6 * S, i == open_item ? ZWM_COL_ACCENT_DIM : ZWM_COL_SURFACE);
        int x = item_x[i] + PAD;
        switch (i) {
        case ITEM_SYS:   zwm_ttext_w(bs, x, ty, "sic", ZWM_COL_TEXT, px, ZWM_FONT_MEDIUM); break;
        case ITEM_APP:   zwm_ttext_w(bs, x, ty, app_name, ZWM_COL_TEXT, px, ZWM_FONT_MEDIUM); break;
        case ITEM_CLOCK: zwm_ttext(bs, x, ty, clock_text, ZWM_COL_TEXT, px); break;
        case ITEM_KBD:
            draw_keyboard(bs, x, (BAR_H - 12 * S) / 2, 12 * S, ZWM_COL_TEXT);
            break;
        case ITEM_WIFI:
            draw_wifi(bs, x + 2 * S, (BAR_H - 16 * S) / 2, 16 * S, wifi, ZWM_COL_TEXT);
            break;
        case ITEM_BRIGHT: {
            draw_sun(bs, x, (BAR_H - 14 * S) / 2, 14 * S, brightness, ZWM_COL_TEXT);
            char pct[8];
            if (brightness >= 0) snprintf(pct, sizeof pct, "%d%%", brightness);
            else snprintf(pct, sizeof pct, "-");
            zwm_ttext(bs, x + 18 * S, ty, pct, ZWM_COL_TEXT_DIM, px);
            break;
        }
        case ITEM_BATT: {
            draw_battery(bs, x, (BAR_H - 11 * S) / 2, 11 * S, battery, charging, ZWM_COL_TEXT);
            char pct[8];
            snprintf(pct, sizeof pct, "%d%%", battery);
            zwm_ttext(bs, x + 26 * S, ty, pct, ZWM_COL_TEXT_DIM, px);
            break;
        }
        case ITEM_VOL: {
            draw_speaker(bs, x, (BAR_H - 14 * S) / 2, 14 * S, volume, ZWM_COL_TEXT);
            char pct[8];
            snprintf(pct, sizeof pct, "%d%%", volume);
            zwm_ttext(bs, x + 18 * S, ty, pct, ZWM_COL_TEXT_DIM, px);
            break;
        }
        }
    }
}

static int bar_item_at(int x, int y)
{
    (void)y;
    for (int i = 0; i < NITEMS; i++)
        if (item_w[i] && x >= item_x[i] && x < item_x[i] + item_w[i]) return i;
    return -1;
}

/* ---- menus ------------------------------------------------------------------------- */

struct entry { const char *label; int action; int enabled; };
enum { A_LAUNCH_ABOUT, A_LAUNCH_PAD, A_LAUNCH_TERM, A_LAUNCH_FILES, A_MINIMIZE, A_MAXIMIZE, A_CLOSE };
static const char *const launch_path[] = { "/bin/zabout", "/bin/zlaunch", "/bin/zterm", "/bin/zfiles" };

static int pop = -1;                    /* the open menu's window */
static zwm_surface *ps;
static struct entry entries[8];
static int nentries, pop_hover = -1, pop_target = -1;
static int dragging;                    /* the volume or brightness slider */

static int slider_x0(void) { return 40 * S; }
static int slider_x1(void) { return VOL_W - 50 * S; }
static void open_pop(int item);
static void close_pop(void);
static void launch(const char *path);

static void draw_wifi_footer(int y)
{
    int px = ZWM_UI_PX;
    int rx = 6 * S, rw = ps->w - 12 * S, rh = 28 * S;
    if (wifi_hover_what == WIFI_HIT_SETTINGS) {
        zwm_round_rect(ps, rx, y, rw, rh, 6 * S, COL_MAC_HOVER);
    }
    int fty = y + (rh - zwm_ttext_height(px)) / 2;
    zwm_ttext(ps, 14 * S, fty, "Wi-Fi Settings...", ZWM_COL_TEXT, px);
    add_wifi_hit(rx, y, rw, rh, WIFI_HIT_SETTINGS, 0);
}

static void draw_wifi_row(int idx, int y, int is_conn)
{
    int px = ZWM_UI_PX;
    int rx = 6 * S, rw = ps->w - 12 * S, rh = 30 * S;
    if (wifi_hover_what == WIFI_HIT_NET && wifi_hover_arg == idx) {
        zwm_round_rect(ps, rx, y, rw, rh, 6 * S, COL_MAC_HOVER);
    }
    add_wifi_hit(rx, y, rw, rh, WIFI_HIT_NET, idx);

    int cx = 20 * S, cy = y + rh / 2;
    uint32_t badge_col = is_conn ? COL_MAC_BLUE : COL_MAC_GRAY;
    zwm_disc(ps, cx, cy, 10 * S, badge_col);
    draw_wifi(ps, cx - 6 * S, cy - 7 * S, 12 * S, 2, is_conn ? 0xffffff : ZWM_COL_TEXT);

    int rty = y + (rh - zwm_ttext_height(px)) / 2;
    zwm_ttext(ps, 36 * S, rty, wifi_nets[idx].ssid, ZWM_COL_TEXT, px);

    if (wifi_nets[idx].secure) {
        draw_lock(ps, ps->w - 24 * S, y + (rh - 12 * S) / 2, ZWM_COL_TEXT_DIM);
    }
}

static int wifi_pop_height(void)
{
    if (wifi <= 0) return 110 * S;
    if (wifi == 1) return 110 * S;
    int h = 42 * S;
    if (wifi_chosen >= 0 && wifi_chosen < wifi_nnets) h += 82 * S;
    int n_known = 0, n_other = 0;
    for (int i = 0; i < wifi_nnets; i++) {
        if (!strcmp(wifi_nets[i].ssid, wifi_connected) || wifi_is_known(wifi_nets[i].ssid)) n_known++;
        else n_other++;
    }
    if (n_known > 0) h += 18 * S + n_known * (32 * S);
    if (n_other > 0) {
        int show_other = n_other > 6 ? 6 : n_other;
        h += 18 * S + show_other * (32 * S);
    }
    if (wifi_nnets == 0) h += 30 * S;
    h += 6 * S + 28 * S + 8 * S;
    int sh = screen_h > 0 ? screen_h : 2400;
    int max_h = sh - (bar_y + BAR_H) - 16 * S;
    if (max_h > 100 * S && h > max_h) h = max_h;
    if (h < 60 * S) h = 60 * S;
    return h;
}

static void draw_wifi_pop(void)
{
    int px = ZWM_UI_PX;
    nwifi_hits = 0;
    zwm_fill(ps, 0, 0, ps->w, ps->h, ZWM_COL_WINDOW);
    zwm_rect(ps, 0, 0, ps->w, ps->h, ZWM_COL_BORDER);

    int ty = 8 * S + (20 * S - zwm_ttext_height(px)) / 2;
    zwm_ttext_w(ps, 14 * S, ty, "Wi-Fi", ZWM_COL_TEXT, px, ZWM_FONT_MEDIUM);

    int sw_w = 34 * S, sw_h = 20 * S, sw_r = 10 * S;
    int sw_x = ps->w - 14 * S - sw_w;
    int sw_y = 8 * S;
    uint32_t sw_col = (wifi == 2 || wifi == 1) ? COL_MAC_BLUE : COL_MAC_GRAY;
    zwm_round_rect(ps, sw_x, sw_y, sw_w, sw_h, sw_r, sw_col);
    int thumb_x = (wifi == 2 || wifi == 1) ? (sw_x + sw_w - 10 * S) : (sw_x + 10 * S);
    zwm_disc(ps, thumb_x, sw_y + 10 * S, 8 * S, 0xffffff);
    add_wifi_hit(sw_x - 4 * S, sw_y - 2 * S, sw_w + 8 * S, sw_h + 4 * S, WIFI_HIT_TOGGLE, 0);

    int y = 34 * S;
    zwm_hline(ps, 10 * S, y, ps->w - 20 * S, ZWM_RGB(0x32, 0x32, 0x35));
    y += 8 * S;

    if (wifi <= 0) {
        zwm_ttext(ps, 14 * S, y + 4 * S, "Wi-Fi is turned off", ZWM_COL_TEXT_DIM, px);
        y += 28 * S;
        zwm_hline(ps, 10 * S, y, ps->w - 20 * S, ZWM_RGB(0x32, 0x32, 0x35));
        y += 6 * S;
        draw_wifi_footer(y);
        return;
    }

    if (wifi == 1) {
        zwm_ttext(ps, 14 * S, y + 4 * S, "Turning Wi-Fi on...", ZWM_COL_TEXT_DIM, px);
        y += 28 * S;
        zwm_hline(ps, 10 * S, y, ps->w - 20 * S, ZWM_RGB(0x32, 0x32, 0x35));
        y += 6 * S;
        draw_wifi_footer(y);
        return;
    }

    if (wifi_chosen >= 0 && wifi_chosen < wifi_nnets) {
        char plabel[64];
        snprintf(plabel, sizeof plabel, "Password for %s", wifi_nets[wifi_chosen].ssid);
        zwm_ttext_w(ps, 14 * S, y, plabel, ZWM_COL_TEXT, px, ZWM_FONT_MEDIUM);
        y += zwm_ttext_height(px) + 6 * S;

        int fw = ps->w - 28 * S - 52 * S;
        zwm_round_rect(ps, 14 * S, y, fw, 28 * S, 6 * S, ZWM_COL_BG);
        zwm_round_rect_border(ps, 14 * S, y, fw, 28 * S, 6 * S, ZWM_COL_BORDER);

        char shown[64];
        size_t n = strlen(wifi_pass);
        if (wifi_show_pass) snprintf(shown, sizeof shown, "%s", wifi_pass);
        else { for (size_t i = 0; i < n && i < sizeof shown - 1; i++) shown[i] = '*'; shown[n < sizeof shown - 1 ? n : sizeof shown - 1] = 0; }
        int pty = y + (28 * S - zwm_ttext_height(px)) / 2;
        zwm_ttext(ps, 20 * S, pty, shown, ZWM_COL_TEXT, px);
        zwm_fill(ps, 20 * S + zwm_ttext_width(shown, px) + S, pty, 2 * S, zwm_ttext_height(px), ZWM_COL_TEXT);
        add_wifi_hit(14 * S, y, fw, 28 * S, WIFI_HIT_PASS_FIELD, 0);

        int sb_col = (wifi_hover_what == WIFI_HIT_SHOW_PASS) ? ZWM_COL_SURFACE : ZWM_COL_WINDOW;
        zwm_round_rect(ps, 14 * S + fw + 6 * S, y, 46 * S, 28 * S, 6 * S, sb_col);
        zwm_round_rect_border(ps, 14 * S + fw + 6 * S, y, 46 * S, 28 * S, 6 * S, ZWM_COL_BORDER);
        const char *sh_txt = wifi_show_pass ? "Hide" : "Show";
        int stw = zwm_ttext_width(sh_txt, px);
        zwm_ttext(ps, 14 * S + fw + 6 * S + (46 * S - stw) / 2, pty, sh_txt, ZWM_COL_TEXT, px);
        add_wifi_hit(14 * S + fw + 6 * S, y, 46 * S, 28 * S, WIFI_HIT_SHOW_PASS, 0);
        y += 32 * S;

        int bw = (ps->w - 28 * S - 8 * S) / 2;
        uint32_t c_bg = (wifi_hover_what == WIFI_HIT_CANCEL) ? ZWM_COL_ACCENT_DIM : ZWM_COL_SURFACE;
        zwm_round_rect(ps, 14 * S, y, bw, 24 * S, 6 * S, c_bg);
        int ctw = zwm_ttext_width("Cancel", px);
        zwm_ttext(ps, 14 * S + (bw - ctw) / 2, y + (24 * S - zwm_ttext_height(px)) / 2, "Cancel", ZWM_COL_TEXT, px);
        add_wifi_hit(14 * S, y, bw, 24 * S, WIFI_HIT_CANCEL, 0);

        zwm_round_rect(ps, 14 * S + bw + 8 * S, y, bw, 24 * S, 6 * S, COL_MAC_BLUE);
        int contw = zwm_ttext_width_w("Connect", px, ZWM_FONT_MEDIUM);
        zwm_ttext_w(ps, 14 * S + bw + 8 * S + (bw - contw) / 2, y + (24 * S - zwm_ttext_height(px)) / 2, "Connect", 0xffffff, px, ZWM_FONT_MEDIUM);
        add_wifi_hit(14 * S + bw + 8 * S, y, bw, 24 * S, WIFI_HIT_CONNECT, 0);
        y += 32 * S;

        zwm_hline(ps, 10 * S, y, ps->w - 20 * S, ZWM_RGB(0x32, 0x32, 0x35));
        y += 8 * S;
    }

    if (wifi_nnets == 0) {
        zwm_ttext(ps, 14 * S, y + 4 * S, "Scanning for networks...", ZWM_COL_TEXT_DIM, px);
        y += 28 * S;
    } else {
        int has_known_header = 0;
        for (int i = 0; i < wifi_nnets; i++) {
            int is_conn = !strcmp(wifi_nets[i].ssid, wifi_connected);
            int is_known = is_conn || wifi_is_known(wifi_nets[i].ssid);
            if (!is_known) continue;

            if (!has_known_header) {
                zwm_ttext(ps, 14 * S, y, "Known Networks", ZWM_COL_TEXT_DIM, px - 2 * S);
                y += 18 * S;
                has_known_header = 1;
            }
            draw_wifi_row(i, y, is_conn);
            y += 32 * S;
        }

        int has_other_header = 0;
        int shown_other = 0;
        for (int i = 0; i < wifi_nnets; i++) {
            int is_conn = !strcmp(wifi_nets[i].ssid, wifi_connected);
            int is_known = is_conn || wifi_is_known(wifi_nets[i].ssid);
            if (is_known) continue;
            if (shown_other >= 6) break;

            if (!has_other_header) {
                zwm_ttext(ps, 14 * S, y, "Other Networks", ZWM_COL_TEXT_DIM, px - 2 * S);
                y += 18 * S;
                has_other_header = 1;
            }
            draw_wifi_row(i, y, 0);
            y += 32 * S;
            shown_other++;
        }
    }

    zwm_hline(ps, 10 * S, y, ps->w - 20 * S, ZWM_RGB(0x32, 0x32, 0x35));
    y += 6 * S;
    draw_wifi_footer(y);
}

static void wifi_pop_mouse(const struct zwm_m_mouse *m)
{
    int what = -1, arg = -1;
    for (int i = 0; i < nwifi_hits; i++) {
        if (m->x >= wifi_hits[i].x && m->x < wifi_hits[i].x + wifi_hits[i].w &&
            m->y >= wifi_hits[i].y && m->y < wifi_hits[i].y + wifi_hits[i].h) {
            what = wifi_hits[i].what;
            arg = wifi_hits[i].arg;
            break;
        }
    }
    wifi_hover_what = what;
    wifi_hover_arg = arg;

    if (m->kind == ZWM_MOUSE_PRESS && (m->buttons & ZWM_BTN_LEFT)) {
        if (what == WIFI_HIT_TOGGLE) {
            int fd = open("/dev/wlan", O_WRONLY);
            if (fd >= 0) {
                if (wifi > 0) write(fd, "off\n", 4);
                else write(fd, "on\n", 3);
                close(fd);
            }
            wifi_read_full();
            open_pop(ITEM_WIFI);
        } else if (what == WIFI_HIT_NET && arg >= 0 && arg < wifi_nnets) {
            if (!strcmp(wifi_nets[arg].ssid, wifi_connected)) {
                /* already connected */
            } else if (!wifi_nets[arg].secure) {
                int fd = open("/dev/wlan", O_WRONLY);
                if (fd >= 0) {
                    char cmd[128];
                    int len = snprintf(cmd, sizeof cmd, "connect\t%s\t\n", wifi_nets[arg].ssid);
                    write(fd, cmd, (size_t)len);
                    close(fd);
                }
                spawn_dhcp_waiter();
                wifi_chosen = -1;
                open_pop(ITEM_WIFI);
            } else {
                const char *saved = wifi_get_saved_pass(wifi_nets[arg].ssid);
                if (saved && saved[0]) {
                    int fd = open("/dev/wlan", O_WRONLY);
                    if (fd >= 0) {
                        char cmd[160];
                        int len = snprintf(cmd, sizeof cmd, "connect\t%s\t%s\n", wifi_nets[arg].ssid, saved);
                        write(fd, cmd, (size_t)len);
                        close(fd);
                    }
                    spawn_dhcp_waiter();
                    wifi_chosen = -1;
                    open_pop(ITEM_WIFI);
                } else {
                    wifi_chosen = arg;
                    wifi_pass[0] = 0;
                    wifi_show_pass = 0;
                    open_pop(ITEM_WIFI);
                }
            }
        } else if (what == WIFI_HIT_CONNECT) {
            if (wifi_chosen >= 0 && wifi_chosen < wifi_nnets) {
                int fd = open("/dev/wlan", O_WRONLY);
                if (fd >= 0) {
                    char cmd[160];
                    int len = snprintf(cmd, sizeof cmd, "connect\t%s\t%s\n", wifi_nets[wifi_chosen].ssid, wifi_pass);
                    write(fd, cmd, (size_t)len);
                    close(fd);
                }
                wifi_save_known(wifi_nets[wifi_chosen].ssid, wifi_pass);
                spawn_dhcp_waiter();
                wifi_chosen = -1;
                memset(wifi_pass, 0, sizeof wifi_pass);
                open_pop(ITEM_WIFI);
            }
        } else if (what == WIFI_HIT_CANCEL) {
            wifi_chosen = -1;
            memset(wifi_pass, 0, sizeof wifi_pass);
            open_pop(ITEM_WIFI);
        } else if (what == WIFI_HIT_SHOW_PASS) {
            wifi_show_pass = !wifi_show_pass;
        } else if (what == WIFI_HIT_SETTINGS) {
            close_pop();
            launch("/bin/zwifi");
        }
    }
}

static void wifi_pop_key(const struct zwm_m_key *k)
{
    if (!k->down) return;
    if (wifi_chosen >= 0 && wifi_chosen < wifi_nnets) {
        size_t n = strlen(wifi_pass);
        if (k->sym == '\b' || k->sym == 127) {
            if (n) wifi_pass[n - 1] = 0;
        } else if (k->sym == '\n' || k->sym == '\r') {
            int fd = open("/dev/wlan", O_WRONLY);
            if (fd >= 0) {
                char cmd[160];
                int len = snprintf(cmd, sizeof cmd, "connect\t%s\t%s\n", wifi_nets[wifi_chosen].ssid, wifi_pass);
                write(fd, cmd, (size_t)len);
                close(fd);
            }
            wifi_save_known(wifi_nets[wifi_chosen].ssid, wifi_pass);
            spawn_dhcp_waiter();
            wifi_chosen = -1;
            memset(wifi_pass, 0, sizeof wifi_pass);
            open_pop(ITEM_WIFI);
        } else if (k->sym == 27) {
            wifi_chosen = -1;
            memset(wifi_pass, 0, sizeof wifi_pass);
            open_pop(ITEM_WIFI);
        } else if (k->sym >= 32 && k->sym < 127 && n < sizeof wifi_pass - 1) {
            wifi_pass[n] = (char)k->sym;
            wifi_pass[n + 1] = 0;
        }
    }
}

static void draw_pop(void)
{
    zwm_fill(ps, 0, 0, ps->w, ps->h, ZWM_COL_WINDOW);
    zwm_rect(ps, 0, 0, ps->w, ps->h, ZWM_COL_BORDER);
    int px = ZWM_UI_PX;
    if (open_item == ITEM_WIFI) {
        draw_wifi_pop();
        return;
    }
    if (open_item == ITEM_VOL || open_item == ITEM_BRIGHT) {
        int cy = ps->h / 2, value = open_item == ITEM_VOL ? volume : brightness < 0 ? 50 : brightness;
        if (open_item == ITEM_VOL) {
            if (pop_hover == 0) zwm_round_rect(ps, 6 * S, cy - 14 * S, 28 * S, 28 * S, 6 * S, ZWM_COL_SURFACE);
            draw_speaker(ps, 10 * S, cy - 9 * S, 18 * S, volume, ZWM_COL_TEXT);
        } else draw_sun(ps, 10 * S, cy - 9 * S, 18 * S, value, ZWM_COL_TEXT);
        int x0 = slider_x0(), x1 = slider_x1(), at = x0 + (x1 - x0) * value / 100;
        zwm_round_rect(ps, x0, cy - 2 * S, x1 - x0, 4 * S, 2 * S, ZWM_COL_ACCENT_DIM);
        zwm_round_rect(ps, x0, cy - 2 * S, at - x0 + 2 * S, 4 * S, 2 * S, ZWM_COL_ACCENT);
        zwm_disc(ps, at, cy, 7 * S, ZWM_COL_ACCENT);
        char pct[8];
        if (open_item == ITEM_BRIGHT && brightness < 0) snprintf(pct, sizeof pct, "-");
        else snprintf(pct, sizeof pct, "%d%%", value);
        zwm_ttext(ps, x1 + 14 * S, cy - zwm_ttext_height(px) / 2, pct, ZWM_COL_TEXT, px);
        return;
    }
    for (int i = 0; i < nentries; i++) {
        int y = 4 * S + i * ROW_H;
        if (i == pop_hover && entries[i].enabled) zwm_round_rect(ps, 4 * S, y, ps->w - 8 * S, ROW_H, 5 * S, ZWM_COL_SURFACE);
        zwm_ttext(ps, 14 * S, y + (ROW_H - zwm_ttext_height(px)) / 2, entries[i].label,
                  entries[i].enabled ? ZWM_COL_TEXT : ZWM_COL_TEXT_DIM, px);
    }
}

static void close_pop(void)
{
    if (pop >= 0) {
        zwm_destroy(c, pop);
        zwm_surface_free(ps);
    }
    pop = -1; ps = NULL; open_item = -1; pop_hover = -1; dragging = 0;
}

static void open_pop(int item)
{
    close_pop();
    int w = MENU_W, h;
    nentries = 0;
    if (item == ITEM_SYS) {
        entries[nentries++] = (struct entry){ "About sic", A_LAUNCH_ABOUT, 1 };
        entries[nentries++] = (struct entry){ "Launchpad", A_LAUNCH_PAD, 1 };
        entries[nentries++] = (struct entry){ "Terminal", A_LAUNCH_TERM, 1 };
        entries[nentries++] = (struct entry){ "Files", A_LAUNCH_FILES, 1 };
    } else if (item == ITEM_APP) {
        int have = app_win >= 0;
        pop_target = app_win;
        entries[nentries++] = (struct entry){ "Minimize", A_MINIMIZE, have };
        entries[nentries++] = (struct entry){ app_state & ZWM_WIN_MAXIMIZED ? "Restore" : "Maximize", A_MAXIMIZE, have };
        entries[nentries++] = (struct entry){ "Close", A_CLOSE, have };
    } else if (item == ITEM_VOL) {
        volume_read();
        w = VOL_W;
    } else if (item == ITEM_BRIGHT) {
        brightness_read();
        w = VOL_W;
    } else if (item == ITEM_WIFI) {
        wifi_load_known();
        wifi_read_full();
        w = WIFI_POP_W;
        if (wifi == 2) {
            int fd = open("/dev/wlan", O_WRONLY);
            if (fd >= 0) { write(fd, "scan\n", 5); close(fd); }
        }
    } else {
        return;
    }
    h = item == ITEM_WIFI ? wifi_pop_height() :
        (item == ITEM_VOL || item == ITEM_BRIGHT) ? VOL_H :
        nentries * ROW_H + 8 * S;
    struct zwm_m_geom g;
    pop = zwm_create(c, w, h, "menu", ZWM_UNDECORATED, &g);
    if (pop < 0) { pop = -1; return; }
    int x = item_x[item];
    if (item == ITEM_WIFI) x = item_x[item] + item_w[item] - w;
    if (x + w > bs->w - 4 * S) x = bs->w - 4 * S - w;
    if (x < 4 * S) x = 4 * S;
    zwm_move(c, pop, x, bar_y + BAR_H);
    ps = zwm_window_surface(c, pop, g.w, g.h);
    if (!ps) { zwm_destroy(c, pop); pop = -1; return; }
    open_item = item;
    draw_pop();
    zwm_flush(c, pop, ps);
}

static void launch(const char *path)
{
    pid_t pid = fork();
    if (pid == 0) { execl(path, path, (char *)NULL); _exit(127); }
}

static void run(int action)
{
    switch (action) {
    case A_LAUNCH_ABOUT: case A_LAUNCH_PAD: case A_LAUNCH_TERM: case A_LAUNCH_FILES:
        launch(launch_path[action - A_LAUNCH_ABOUT]);
        break;
    case A_MINIMIZE: zwm_minimize(c, pop_target); break;
    case A_MAXIMIZE: zwm_activate(c, pop_target); zwm_maximize(c, pop_target); break;
    case A_CLOSE:    zwm_close(c, pop_target); break;
    }
}

static void slider_to(int x)
{
    int x0 = slider_x0(), x1 = slider_x1();
    int v = (x - x0) * 100 / (x1 - x0);
    if (open_item == ITEM_BRIGHT) { brightness_set(v); return; }
    volume_set(v);
    if (volume > 0) unmuted = volume;
}

/* The slider's value saved where the next session finds it. */
static void slider_save(void) { if (open_item == ITEM_BRIGHT) brightness_save(); else volume_save(); }

static void pop_mouse(const struct zwm_m_mouse *m)
{
    if (open_item == ITEM_WIFI) {
        wifi_pop_mouse(m);
        return;
    }
    if (open_item == ITEM_VOL || open_item == ITEM_BRIGHT) {
        int cy = ps->h / 2;
        int on_button = open_item == ITEM_VOL && m->x >= 6 * S && m->x < 34 * S && m->y >= cy - 14 * S && m->y < cy + 14 * S;
        pop_hover = on_button ? 0 : -1;
        if (m->kind == ZWM_MOUSE_PRESS && (m->buttons & ZWM_BTN_LEFT)) {
            if (on_button) { volume_set(volume > 0 ? 0 : unmuted); volume_save(); }
            else if (m->x >= slider_x0() - 10 * S && m->x <= slider_x1() + 10 * S) { dragging = 1; slider_to(m->x); }
        } else if (m->kind == ZWM_MOUSE_MOVE && dragging && (m->buttons & ZWM_BTN_LEFT)) {
            slider_to(m->x);
        } else if (m->kind == ZWM_MOUSE_RELEASE && dragging) {
            dragging = 0;
            slider_save();
        }
        return;
    }
    int row = m->y >= 4 * S ? (m->y - 4 * S) / ROW_H : -1;
    pop_hover = m->x >= 0 && row >= 0 && row < nentries ? row : -1;
    if (m->kind == ZWM_MOUSE_RELEASE && pop_hover >= 0 && entries[pop_hover].enabled) {
        int a = entries[pop_hover].action;
        close_pop();
        run(a);
    }
}

/* ---- main ---------------------------------------------------------------------------- */

int main(void)
{
    signal(SIGCHLD, SIG_IGN);
    c = zwm_connect();
    if (!c) { perror("zbar: zwm"); return 1; }
    struct zwm_m_geom g;
    bar = zwm_create(c, 100, BAR_H, "bar", ZWM_DOCK_TOP | ZWM_TASKBAR, &g);
    if (bar < 0) return 1;
    bar_y = g.y;                    /* below a camera cutout, on a phone */
    if (g.screen_h > 0) screen_h = g.screen_h;
    touch_device = g.screen_h > g.screen_w || access("/dev/panel", F_OK) == 0;
    bs = zwm_window_surface(c, bar, g.w, g.h);
    volume_load();
    brightness_load();
    battery_read();
    wifi_read();
    clock_update();
    draw_bar();
    zwm_flush(c, bar, bs);
    for (;;) {
        struct pollfd pf = { zwm_fd(c), POLLIN, 0 };
        poll(&pf, 1, 1000);
        int st;
        while (waitpid(-1, &st, WNOHANG) > 0) ;
        zwm_event ev;
        int r, pop_dirty = 0;
        while ((r = zwm_next_event(c, &ev, 0)) > 0) {
            if (pop >= 0 && (int)ev.win == pop) {
                if (ev.type == ZWM_S_MOUSE) { pop_mouse(&ev.mouse); pop_dirty = 1; }
                else if (ev.type == ZWM_S_KEY && ev.key.down && ev.key.sym == 27) {
                    if (open_item == ITEM_WIFI && wifi_chosen >= 0) {
                        wifi_chosen = -1; memset(wifi_pass, 0, sizeof wifi_pass);
                        open_pop(ITEM_WIFI);
                    } else {
                        close_pop();
                    }
                }
                else if (ev.type == ZWM_S_FOCUS && !ev.focus.focused) close_pop();
                else if (ev.type == ZWM_S_KEY && ev.key.down && open_item == ITEM_WIFI) {
                    wifi_pop_key(&ev.key);
                    pop_dirty = 1;
                }
                else if (ev.type == ZWM_S_KEY && ev.key.down && (open_item == ITEM_VOL || open_item == ITEM_BRIGHT) &&
                         (ev.key.sym == ZWM_KEY_LEFT || ev.key.sym == ZWM_KEY_RIGHT)) {
                    int step = ev.key.sym == ZWM_KEY_RIGHT ? 5 : -5;
                    if (open_item == ITEM_VOL) volume_set(volume + step);
                    else brightness_set((brightness < 0 ? 50 : brightness) + step);
                    slider_save();
                    pop_dirty = 1;
                }
                continue;
            }
            if (ev.type == ZWM_S_MOUSE) {
                int b = bar_item_at(ev.mouse.x, ev.mouse.y);
                if (ev.mouse.kind == ZWM_MOUSE_PRESS && (ev.mouse.buttons & ZWM_BTN_LEFT) && b == ITEM_KBD) {
                    close_pop();
                    toggle_keyboard();
                } else if (ev.mouse.kind == ZWM_MOUSE_PRESS && (ev.mouse.buttons & ZWM_BTN_LEFT) && b != ITEM_CLOCK && b != ITEM_BATT) {
                    if (b == open_item) close_pop();
                    else if (b >= 0) open_pop(b);
                }
                hover = b == ITEM_CLOCK || b == ITEM_BATT ? -1 : b;
            } else if (ev.type == ZWM_S_RESIZE && (int)ev.win == bar) {
                zwm_surface_resize(bs, ev.geom.w, ev.geom.h);
            } else if (ev.type == ZWM_S_WINDOWS) {
                nwindows = ev.windows.count;
                memcpy(windows, ev.windows.w, sizeof windows[0] * (size_t)nwindows);
                if (pop < 0) pick_app();        /* a menu has the focus: keep the app it is about */
            }
        }
        if (r < 0) break;
        if (pop >= 0 && open_item == ITEM_WIFI) {
            int old_h = ps ? ps->h : 0;
            wifi_read_full();
            int new_h = wifi_pop_height();
            if (new_h != old_h) {
                open_pop(ITEM_WIFI);
            } else {
                pop_dirty = 1;
            }
        }
        if (pop >= 0 && pop_dirty) { draw_pop(); zwm_flush(c, pop, ps); }
        if (pop < 0) volume_read();             /* someone else may have changed it */
        battery_read();
        wifi_read();
        clock_update();
        draw_bar();
        zwm_flush(c, bar, bs);
    }
    return 0;
}
