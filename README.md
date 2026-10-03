# zde — a desktop for sic

What runs on top of [zwm](../zwm): a session, a panel, and the programs the
panel starts. Optional, like zwm; `make install` puts the binaries into the
sysroot's `rootfs/` overlay and ZAE packs them into the initrd.

```
zde       starts zwm, the bar and the panel, waits, tidies up when the server ends
zbar      the bar at the top: the system menu (start an app), the app in
          front (minimize, maximize, close), the volume (a slider and mute;
          kept in /disk/.volume) and the date
zpanel    the dock at the bottom: a tray of icons, the launchpad and the
          launchers (a dot under the ones with a window open) then any other
          window. Click to raise or bring back, click the one in front to put it
          away, hover for its name
zlaunch   the launchpad: a sheet over the work area with every app — the
          programs in /bin that carry an icon, and the executables at the top of
          each mounted volume (games). Click one to start it, Escape closes
zterm     a terminal: /bin/sh over pipes, with its own line editing and echo
zfiles    a file browser: places on the left (the root, /bin, the volumes),
          the directory on the right with an icon per entry — a program's own
          where it has one. Click twice (or Enter) to enter, run (an ELF, in its
          own directory) or view; back and up in the toolbar, Backspace goes up
zview     a text viewer (arrows, PgUp/PgDn, Home/End, q)
zclock    an analogue uptime clock
zabout    what this is
```

Type `zde` at the shell. `Ctrl+Alt+Q` ends the session.

Everything is laid out in multiples of `zwm_scale()`, so on a HiDPI
screen (a Retina Mac with the virgl QEMU, which runs the guest at the
panel's real pixels) the desktop is drawn at 2x; SDL programs keep the
size they ask for.

Icons: `icons/mkicon.py` draws each app's icon (96x96 RGBA, oversampled
shapes) and the build puts it into the program as a `.zicon` ELF section
(`llvm-objcopy --add-section`, flagged allocated so `strip` keeps it),
which `zwm_icon_load` reads back; Files,
the dock and the launchpad all show it. The generic ones (folder, doc,
image, app) go to `/usr/share/icons`. `mkicon.py --game <dir>` takes a
Ren'Py game's `gui/window_icon.png` (plain or inside an `.rpa`), which is
how `make game` gives the game's launcher its real icon. On aarch64 the
desktop runs on the virtio display with virtio keyboard and mouse; with
the Homebrew QEMU (`make run-arm64`) GL is TinyGL in software and Ren'Py
draws in software, on the virgl QEMU (`make run-gl-arm64`) zgl puts both
on the host GPU. `make ARCH=aarch64 desktop userland game GAME=...` first.

```
ports/sdl2     SDL2 2.30 with a zwm video driver (port/video/zwm): windows, the
               window framebuffer, keys, mouse, resize, close; the software
               renderer; threads, timers; audio through SDL's OSS backend on
               the kernel's /dev/dsp. No shared objects, no joysticks.
ports/tinygl   TinyGL (C-Chads fork): OpenGL 1.x in software, behind the same
               context ABI (GL/sic_gl.h) as zgl, for machines without a GPU.
               Its headers go to usr/include/TinyGL so they don't shadow zgl's.
sdl/           programs: sdldemo (software renderer), gldemo (a lit spinning
               cube), blitbench (pixels per second), sdltone (a chord through
               SDL audio).
ports/libpng, jpeg, freetype, fribidi, sdl2_image, ffmpeg
               library ports (ports/port.mk has the shared build): static
               libraries into the sysroot for the ports that link them.
ports/renpy    Ren'Py 6.99.12.4 -- Python 2.7 + pygame_sdl2 + Ren'Py's modules
               in one static /bin/renpy, with `the_question` to try; see its
               README for how a game (DDLC) gets onto a disk.
```

SDL_GL_* on zwm makes one offscreen context per window drawing into the
window's shared buffer; which OpenGL that is gets decided at link time. `make ports`
builds and installs them into the sysroot; SDL programs include
`<SDL2/SDL.h>` and `<GL/gl.h>` and link `libSDL2.a`, then `libzgl.a
libvirgl.a` (the host GPU through [zgl](../zgl)) or `libTinyGL.a`, then
`libzwm.a` (see the Makefile: it picks zgl when it is installed).

There is no pty on sic, so zterm feeds the shell whole lines and skips the
escape sequences it prints; interactive programs won't be happy in it.

## Building

```bash
make            # build/<arch>/*
make install    # -> $SYSROOT/rootfs/bin/
```

Needs libzwm in the sysroot first (`make install` in zwm). `make ports`
fetches SDL2 and TinyGL and installs them into the sysroot; the `sdl/`
programs are built when `libSDL2.a` is there.

## License

Copyright (C) 2026 Rigby Foundation, GPL-2.0-only (`LICENSE`). The ports
keep their own licences: SDL2 is zlib, TinyGL is its own zlib-like notice
(`ports/*/src/LICENSE`); the port glue under `ports/*/port` is zlib too so
it can go upstream.
