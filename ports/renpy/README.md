# renpy — Ren'Py 6.99.12.4 on sic

The visual novel engine DDLC was made with, as one static binary:
Python 2.7 (ZAE's python2 port) with pygame_sdl2 and Ren'Py's extension
modules built in, on the SDL2 port, libzgl (OpenGL 2 on the host GPU),
FreeType, FriBidi, libpng/jpeg and FFmpeg's decoders.

```
renpy <game directory>          runs the game there (it has a game/ subdirectory)
renpy /usr/share/renpy/the_question     the sample that ships with Ren'Py
renpy --python ...              the bare interpreter with all of the above importable
```

No `setup.py` is run and no Cython is needed for the modules: both source
tarballs carry their Cython output (`gen/*.c`), which this Makefile
compiles like any C. Static linking needs two things the tarballs did not
plan for: every module's `init<name>` symbol is renamed to be unique, and
each `Py_InitModule4("name")` is rewritten to the dotted name, which is how
the sic python (patched to look packages' extension modules up in the
built-in table) finds them. The three pygame_sdl2 C-API headers
(`*_api.h`) are not in the tarball, so the python2 port's host interpreter
runs Cython 0.23.5 (fetched here) on the three `.pyx` to make them; the C
it produces matches the tarball's byte for byte. GLEW is replaced by a
header in zgl (`GL/glew.h`) that maps the ARB/EXT names to the core
functions — there is one GL here, nothing to look up.

Not built: `pygame_sdl2.font`/`mixer` (SDL2_ttf, SDL2_mixer; Ren'Py does
not use them), the ANGLE renderer, `_renpysteam`. Saves and persistent data
land in `~/.renpy` (`/.renpy`, on the root tmpfs unless you point `HOME`
at a disk).

A game on a disk: `make game GAME=~/Downloads/DDLC-1.1.1-pc` in the top
directory builds a zaefs image from the game's folder (its `game/` with the
`.rpa` archives; the `lib/` of Windows/Mac/Linux engines is left out) and
puts a copy of this binary beside it under the game's name (`DDLC`, from
its `DDLC.sh`; `NAME=` to choose). The engine, run under any name but
`renpy` with no arguments, plays the game next to itself, so the disk is
one thing to start: `make run-gl` attaches it, init mounts it on
`/mnt/nvme2n1`, and double-clicking `DDLC` in Files (or `./DDLC` in a
terminal) runs it. `renpy <directory>` still works for a game without one.

On aarch64 (`make ARCH=aarch64`, own `build-aarch64/`) it is the same
binary with zgl; where libzgl is not installed (PowerPC) the GL modules
are left out and Ren'Py falls back to its software renderer (it says so
in a performance warning at start).

Licence: Ren'Py is MIT (`src/renpy/LICENSE.txt`), pygame_sdl2 LGPL-2.1 /
zlib; the port (Makefile, `port/`, `patches/`) is GPL-2.0-only.
