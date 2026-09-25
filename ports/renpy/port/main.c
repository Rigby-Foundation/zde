/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* renpy: Python 2.7 with pygame_sdl2 and Ren'Py's extension modules built
 * in (there are no shared objects on sic), running /usr/lib/renpy/renpy.py.
 *   renpy <game directory>       the game there (its game/ subdirectory)
 *   renpy                        the launcher, if one is installed
 * A copy of this binary under any other name (DDLC, say, next to a game/
 * folder: what `make game` builds) runs the game it sits beside, so a game
 * disk is one thing to start.
 * The modules register under their dotted names; the sic python imports
 * package extension modules from the built-in table. */
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The directory this executable is in, absolute. */
static char *own_dir(const char *argv0)
{
    static char path[1024];
    if (argv0[0] == '/') snprintf(path, sizeof path, "%s", argv0);
    else { char cwd[768]; if (!getcwd(cwd, sizeof cwd)) return NULL; snprintf(path, sizeof path, "%s/%s", strcmp(cwd, "/") == 0 ? "" : cwd, argv0); }
    char *sl = strrchr(path, '/');
    if (sl == path) sl[1] = 0; else *sl = 0;
    return path;
}

#define M(name, fn) PyMODINIT_FUNC fn(void);
#include "modules.h"
#undef M

static struct _inittab modules[] = {
#define M(name, fn) { name, fn },
#include "modules.h"
#undef M
    { NULL, NULL }
};

int main(int argc, char **argv)
{
    if (PyImport_ExtendInittab(modules) < 0) { fprintf(stderr, "renpy: no room for the modules\n"); return 1; }
    char **args = malloc((size_t)(argc + 2) * sizeof *args);
    int n = 0;
    args[n++] = argv[0];
    if (argc > 1 && strcmp(argv[1], "--python") == 0) {         /* renpy --python ...: the bare interpreter */
        for (int i = 2; i < argc; i++) args[n++] = argv[i];
    } else {
        args[n++] = "/usr/lib/renpy/renpy.py";
        const char *base = strrchr(argv[0], '/');
        base = base ? base + 1 : argv[0];
        if (argc == 1 && strcmp(base, "renpy") != 0) {          /* a game's own copy: the game is next to it */
            char *dir = own_dir(argv[0]);
            if (dir) args[n++] = dir;
        } else
            for (int i = 1; i < argc; i++) args[n++] = argv[i];
    }
    args[n] = NULL;
    Py_SetProgramName(argv[0]);
    return Py_Main(n, args);
}
