/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* What icon a path gets: a program's own (.zicon in its ELF), else one of
 * the generic ones in /usr/share/icons by kind. Shared by Files, the dock
 * and Launchpad; each keeps its own small cache. */
#pragma once
#include <zwm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <strings.h>

struct appicon { uint32_t *pix; int w, h; };

enum icon_kind { ICON_FOLDER, ICON_DOC, ICON_IMAGE, ICON_APP, ICON_KINDS };
static const char *const icon_kind_file[ICON_KINDS] = {
    "/usr/share/icons/folder.zicon", "/usr/share/icons/doc.zicon", "/usr/share/icons/image.zicon", "/usr/share/icons/app.zicon",
};
static struct appicon generic_icons[ICON_KINDS];

static struct appicon *generic_icon(enum icon_kind k)
{
    if (!generic_icons[k].pix && !generic_icons[k].w) {
        generic_icons[k].pix = zwm_icon_file(icon_kind_file[k], &generic_icons[k].w, &generic_icons[k].h);
        if (!generic_icons[k].pix) generic_icons[k].w = -1;         /* asked once */
    }
    return &generic_icons[k];
}

static int is_elf(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    char magic[4];
    int r = read(fd, magic, 4) == 4 && memcmp(magic, "\x7f" "ELF", 4) == 0;
    close(fd);
    return r;
}

static int has_suffix(const char *name, const char *suf)
{
    size_t n = strlen(name), m = strlen(suf);
    return n >= m && strcasecmp(name + n - m, suf) == 0;
}

/* The icon for `path`; `own` says whether it is the program's own (caller frees those). */
static struct appicon icon_for(const char *path, int is_dir, int *own)
{
    struct appicon ic = { NULL, 0, 0 };
    *own = 0;
    if (is_dir) return *generic_icon(ICON_FOLDER);
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (has_suffix(base, ".png") || has_suffix(base, ".jpg") || has_suffix(base, ".jpeg") || has_suffix(base, ".ppm") || has_suffix(base, ".bmp"))
        return *generic_icon(ICON_IMAGE);
    if (is_elf(path)) {
        ic.pix = zwm_icon_load(path, &ic.w, &ic.h);
        if (ic.pix) { *own = 1; return ic; }
        return *generic_icon(ICON_APP);
    }
    return *generic_icon(ICON_DOC);
}

/* A rounded fallback when even the generic icon files are missing. */
static void icon_draw_or_box(zwm_surface *s, int x, int y, int size, const struct appicon *ic)
{
    if (ic->pix) zwm_icon_draw(s, x, y, size, ic->pix, ic->w, ic->h);
    else zwm_round_rect(s, x + size / 8, y + size / 8, size * 3 / 4, size * 3 / 4, size / 6, ZWM_COL_ACCENT_DIM);
}
