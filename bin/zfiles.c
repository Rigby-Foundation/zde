/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zfiles: a file browser. Places on the left (the root, /bin, the mounted
 * volumes), the directory on the right with an icon per entry — a
 * program's own icon when it carries one. Click an entry twice (or Enter)
 * to open it: directories are entered, programs run in their directory,
 * everything else goes to zview. Back and up in the toolbar, Backspace
 * goes up, arrows move. Icons and paths come from appicon.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <zwm.h>
#include "appicon.h"

#define S zwm_scale()               /* HiDPI: every size below is in these units */
#define TOOL_H  (38 * S)
#define ROW_H   (30 * S)
#define ICON    (20 * S)
#define SIDE_W  (150 * S)
#define PAD     (8 * S)
#define TX(h) (((h) - zwm_ttext_height(ZWM_UI_PX)) / 2)

struct entry { char name[256]; int is_dir; long size; struct appicon icon; int own; };
static struct entry *entries;
static int nentries, selected = -1, scroll_top, hover_row = -1, hover_tool = -1, hover_place = -1;
static char cwd[512] = "/";
static char history[16][512]; static int nhistory;
static zwm_surface *s;
static zwm *c;
static int win;

struct place { char label[32], path[128]; };
static struct place places[16];
static int nplaces;

static int cmp(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    return strcasecmp(x->name, y->name);
}

static void join(char *out, size_t n, const char *dir, const char *name)
{
    snprintf(out, n, "%s/%s", strcmp(dir, "/") == 0 ? "" : dir, name);
}

static void scan_places(void)
{
    nplaces = 0;
    struct place *p = &places[nplaces++]; strcpy(p->label, "Root"); strcpy(p->path, "/");
    p = &places[nplaces++]; strcpy(p->label, "Programs"); strcpy(p->path, "/bin");
    p = &places[nplaces++]; strcpy(p->label, "Temporary"); strcpy(p->path, "/tmp");
    DIR *d = opendir("/mnt");
    struct dirent *e;
    while (d && (e = readdir(d)) && nplaces < 16) {
        if (e->d_name[0] == '.') continue;
        char path[160];
        snprintf(path, sizeof path, "/mnt/%s", e->d_name);
        DIR *v = opendir(path);
        int nonempty = 0;
        struct dirent *f;
        while (v && (f = readdir(v))) if (f->d_name[0] != '.') { nonempty = 1; break; }
        if (v) closedir(v);
        if (!nonempty) continue;                    /* an unmounted mount point */
        p = &places[nplaces++];
        snprintf(p->label, sizeof p->label, "%s", e->d_name);
        snprintf(p->path, sizeof p->path, "%s", path);
    }
    if (d) closedir(d);
}

static void free_entries(void)
{
    for (int i = 0; i < nentries; i++) if (entries[i].own) free(entries[i].icon.pix);
    free(entries); entries = NULL; nentries = 0;
}

static void load(void)
{
    free_entries();
    selected = -1; scroll_top = 0; hover_row = -1;
    DIR *d = opendir(cwd);
    if (!d) return;
    struct dirent *e;
    int cap = 0;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (nentries == cap) entries = realloc(entries, sizeof *entries * (size_t)(cap = cap ? cap * 2 : 32));
        struct entry *en = &entries[nentries++];
        memset(en, 0, sizeof *en);
        snprintf(en->name, sizeof en->name, "%s", e->d_name);
        char path[768];
        join(path, sizeof path, cwd, e->d_name);
        struct stat st;
        en->is_dir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        en->size = en->is_dir ? 0 : (long)st.st_size;
    }
    closedir(d);
    qsort(entries, (size_t)nentries, sizeof *entries, cmp);
    for (int i = 0; i < nentries; i++) {
        char path[768];
        join(path, sizeof path, cwd, entries[i].name);
        entries[i].icon = icon_for(path, entries[i].is_dir, &entries[i].own);
    }
    char t[80];
    snprintf(t, sizeof t, "Files: %s", strcmp(cwd, "/") == 0 ? "/" : strrchr(cwd, '/') + 1);
    zwm_set_title(c, win, t);
}

static void go(const char *path, int remember)
{
    if (remember && nhistory < 16) snprintf(history[nhistory++], sizeof history[0], "%s", cwd);
    snprintf(cwd, sizeof cwd, "%s", path);
    load();
}

static void go_up(void)
{
    if (strcmp(cwd, "/") == 0) return;
    char up[512];
    snprintf(up, sizeof up, "%s", cwd);
    char *sl = strrchr(up, '/');
    if (sl && sl != up) *sl = 0; else strcpy(up, "/");
    go(up, 1);
}

static void size_text(long n, char *out, size_t cap)
{
    if (n < 1024) snprintf(out, cap, "%ld B", n);
    else if (n < 1024 * 1024) snprintf(out, cap, "%ld KB", n / 1024);
    else snprintf(out, cap, "%ld.%ld MB", n / (1024 * 1024), (n / (1024 * 1024 / 10)) % 10);
}

static int visible_rows(void) { return (s->h - TOOL_H) / ROW_H; }

/* Toolbar buttons: 0 back, 1 up. */
static void tool_button(int i, int x, const char *glyph, int enabled)
{
    int w = 30 * S, y = 5 * S, h = TOOL_H - 10 * S;
    if (enabled && hover_tool == i) zwm_round_rect(s, x, y, w, h, 6 * S, ZWM_COL_SURFACE);
    int gw = zwm_ttext_width_w(glyph, 15 * S, ZWM_FONT_MEDIUM);
    zwm_ttext_w(s, x + (w - gw) / 2, y + (h - zwm_ttext_height(15 * S)) / 2, glyph, enabled ? ZWM_COL_TEXT : ZWM_COL_ACCENT_DIM, 15 * S, ZWM_FONT_MEDIUM);
}

static void draw(void)
{
    zwm_fill(s, 0, 0, s->w, s->h, ZWM_COL_WINDOW);
    /* toolbar */
    zwm_fill(s, 0, 0, s->w, TOOL_H, ZWM_COL_BG);
    zwm_hline(s, 0, TOOL_H - 1, s->w, ZWM_COL_BORDER);
    tool_button(0, PAD, "<", nhistory > 0);
    tool_button(1, PAD + 34 * S, "^", strcmp(cwd, "/") != 0);
    int px = PAD + 76 * S;
    zwm_round_rect(s, px, 6 * S, s->w - px - PAD, TOOL_H - 12 * S, 6 * S, ZWM_COL_WINDOW);
    zwm_round_rect_border(s, px, 6 * S, s->w - px - PAD, TOOL_H - 12 * S, 6 * S, ZWM_COL_BORDER);
    zwm_ttext(s, px + 10 * S, TX(TOOL_H), cwd, ZWM_COL_TEXT, ZWM_UI_PX);
    /* places */
    zwm_fill(s, 0, TOOL_H, SIDE_W, s->h - TOOL_H, ZWM_COL_BG);
    zwm_vline(s, SIDE_W - 1, TOOL_H, s->h - TOOL_H, ZWM_COL_BORDER);
    zwm_ttext_w(s, 14 * S, TOOL_H + 10 * S, "Places", ZWM_COL_TEXT_DIM, 11 * S, ZWM_FONT_MEDIUM);
    for (int i = 0; i < nplaces; i++) {
        int y = TOOL_H + (30 + i * 26) * S;
        int here = strcmp(places[i].path, cwd) == 0;
        if (here) zwm_round_rect(s, 6 * S, y, SIDE_W - 13 * S, 24 * S, 6 * S, ZWM_COL_SURFACE);
        else if (hover_place == i) zwm_round_rect(s, 6 * S, y, SIDE_W - 13 * S, 24 * S, 6 * S, ZWM_COL_ACCENT_DIM);
        struct appicon *ic = generic_icon(ICON_FOLDER);
        icon_draw_or_box(s, 12 * S, y + 4 * S, 16 * S, ic);
        zwm_ttext(s, 34 * S, y + (24 * S - zwm_ttext_height(ZWM_UI_PX)) / 2, places[i].label, here ? ZWM_COL_TEXT : ZWM_COL_TEXT_DIM, ZWM_UI_PX);
    }
    /* the directory */
    int x0 = SIDE_W + PAD, rows = visible_rows();
    for (int i = 0; i < rows && scroll_top + i < nentries; i++) {
        int idx = scroll_top + i;
        struct entry *e = &entries[idx];
        int y = TOOL_H + i * ROW_H;
        if (idx == selected) zwm_round_rect(s, x0 - 4 * S, y + S, s->w - x0 - PAD + 4 * S, ROW_H - 2 * S, 6 * S, ZWM_COL_SURFACE);
        else if (idx == hover_row) zwm_round_rect(s, x0 - 4 * S, y + S, s->w - x0 - PAD + 4 * S, ROW_H - 2 * S, 6 * S, ZWM_COL_BG);
        icon_draw_or_box(s, x0 + 2 * S, y + (ROW_H - ICON) / 2, ICON, &e->icon);
        char label[300];
        snprintf(label, sizeof label, "%s", e->name);
        int maxw = s->w - x0 - 100 * S;
        while (zwm_ttext_width(label, ZWM_UI_PX) > maxw && strlen(label) > 4) { size_t n = strlen(label); strcpy(label + n - 4, "..."); }
        zwm_ttext(s, x0 + ICON + 12 * S, y + TX(ROW_H), label, ZWM_COL_TEXT, ZWM_UI_PX);
        if (!e->is_dir) {
            char sz[32];
            size_text(e->size, sz, sizeof sz);
            zwm_ttext(s, s->w - PAD - 8 * S - zwm_ttext_width(sz, ZWM_UI_PX), y + TX(ROW_H), sz, ZWM_COL_TEXT_DIM, ZWM_UI_PX);
        }
    }
    if (nentries == 0)
        zwm_ttext(s, x0 + 4 * S, TOOL_H + 12 * S, "Nothing here", ZWM_COL_TEXT_DIM, ZWM_UI_PX);
    /* scrollbar */
    if (nentries > rows) {
        int track = s->h - TOOL_H - 8 * S, bar = track * rows / nentries, at = (track - bar) * scroll_top / (nentries - rows);
        zwm_round_rect(s, s->w - 6 * S, TOOL_H + 4 * S + at, 3 * S, bar, S, ZWM_COL_ACCENT_DIM);
    }
    zwm_flush(c, win, s);
}

static void open_entry(int i)
{
    if (i < 0 || i >= nentries) return;
    struct entry *e = &entries[i];
    char path[768];
    join(path, sizeof path, cwd, e->name);
    if (e->is_dir) { go(path, 1); return; }
    int run = is_elf(path);
    pid_t pid = fork();
    if (pid == 0) {
        if (run) { if (chdir(cwd) == 0) execl(path, path, (char *)NULL); }
        else execl("/bin/zview", "zview", path, (char *)NULL);
        _exit(127);
    }
}

static int row_at(int x, int y)
{
    if (x < SIDE_W || y < TOOL_H) return -1;
    int i = scroll_top + (y - TOOL_H) / ROW_H;
    return i < nentries ? i : -1;
}

static int place_at(int x, int y)
{
    if (x >= SIDE_W || y < TOOL_H + 30 * S) return -1;
    int i = (y - TOOL_H - 30 * S) / (26 * S);
    return i < nplaces && (y - TOOL_H - 30 * S) % (26 * S) < 24 * S ? i : -1;
}

static int tool_at(int x, int y)
{
    if (y < 5 * S || y >= TOOL_H - 5 * S) return -1;
    if (x >= PAD && x < PAD + 30 * S) return 0;
    if (x >= PAD + 34 * S && x < PAD + 64 * S) return 1;
    return -1;
}

int main(int argc, char **argv)
{
    signal(SIGCHLD, SIG_IGN);
    if (argc > 1) snprintf(cwd, sizeof cwd, "%s", argv[1]);
    c = zwm_connect();
    if (!c) { perror("zfiles: zwm"); return 1; }
    struct zwm_m_geom g;
    win = zwm_create(c, 620 * S, 420 * S, "Files", 0, &g);
    if (win < 0) return 1;
    s = zwm_window_surface(c, win, g.w, g.h);
    scan_places();
    load();
    draw();
    for (;;) {
        zwm_event ev;
        int r = zwm_next_event(c, &ev, 1);
        if (r < 0) break;
        int rows = visible_rows();
        switch (ev.type) {
        case ZWM_S_MOUSE: {
            int row = row_at(ev.mouse.x, ev.mouse.y), tool = tool_at(ev.mouse.x, ev.mouse.y), place = place_at(ev.mouse.x, ev.mouse.y);
            if (ev.mouse.kind == ZWM_MOUSE_PRESS && (ev.mouse.buttons & ZWM_BTN_LEFT)) {
                if (row >= 0) { if (row == selected) open_entry(row); else selected = row; }
                else if (tool == 0 && nhistory > 0) { go(history[--nhistory], 0); }
                else if (tool == 1) go_up();
                else if (place >= 0) go(places[place].path, 1);
            } else if (ev.mouse.kind == ZWM_MOUSE_PRESS && (ev.mouse.buttons & ZWM_BTN_RIGHT)) {
                go_up();
            }
            if (row != hover_row || tool != hover_tool || place != hover_place) { hover_row = row; hover_tool = tool; hover_place = place; }
            draw();
            break;
        }
        case ZWM_S_KEY:
            if (!ev.key.down) break;
            if (ev.key.sym == ZWM_KEY_DOWN && selected + 1 < nentries) selected++;
            else if (ev.key.sym == ZWM_KEY_UP && selected > 0) selected--;
            else if (ev.key.sym == ZWM_KEY_PGDN) selected = selected + rows < nentries ? selected + rows : nentries - 1;
            else if (ev.key.sym == ZWM_KEY_PGUP) selected = selected - rows > 0 ? selected - rows : 0;
            else if (ev.key.sym == ZWM_KEY_HOME) selected = 0;
            else if (ev.key.sym == ZWM_KEY_END) selected = nentries - 1;
            else if (ev.key.sym == '\n' && selected >= 0) open_entry(selected);
            else if (ev.key.sym == '\b') go_up();
            else if (ev.key.sym == ZWM_KEY_LEFT && nhistory > 0) go(history[--nhistory], 0);
            else break;
            if (selected < scroll_top) scroll_top = selected;
            if (selected >= scroll_top + rows) scroll_top = selected - rows + 1;
            draw();
            break;
        case ZWM_S_RESIZE: zwm_surface_resize(s, ev.geom.w, ev.geom.h); draw(); break;
        case ZWM_S_FOCUS: break;
        case ZWM_S_CLOSE: zwm_destroy(c, win); return 0;
        }
    }
    return 0;
}
