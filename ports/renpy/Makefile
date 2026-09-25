# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# Ren'Py 6.99.12.4 for sic (the DDLC one): the pygame_sdl2 and Ren'Py
# extension modules -- from the Cython output the source tarballs carry --
# linked into one static Python 2.7 binary, /bin/renpy, plus the Ren'Py
# Python package at /usr/lib/renpy. No setup.py, no Cython: this Makefile
# is the build.
#
#   src/renpy, src/pygame_sdl2   the tarballs (fetched by `make fetch`)
#   port/main.c                  the interpreter with the modules registered
#   patches/                     the port (to the Python side)
#   build/root/                  bin/renpy, usr/lib/renpy/{renpy.py,renpy/}, usr/share/renpy/the_question
#                                -> $(SYSROOT)/rootfs (the image overlay)
#
# Needs in the sysroot: python2 (libpython2.7.a + headers), SDL2, SDL2_image,
# libpng, jpeg, zlib, freetype, fribidi, ffmpeg, zgl, zwm; and the python2
# port's host interpreter, which runs Cython 0.23.5 (fetched here) for the
# three C-API headers the tarballs leave out.

RENPY_VERSION := 6.99.12.4
RENPY_URL := https://www.renpy.org/dl/$(RENPY_VERSION)/renpy-$(RENPY_VERSION)-source.tar.bz2
CYTHON_URL := https://files.pythonhosted.org/packages/source/C/Cython/Cython-0.23.5.tar.gz
PYGAME_URL := https://www.renpy.org/dl/$(RENPY_VERSION)/pygame_sdl2-2.1.0-for-renpy-$(RENPY_VERSION).tar.gz

LLVM_PREFIX ?= $(shell brew --prefix llvm 2>/dev/null)
LLD_PREFIX  ?= $(firstword $(foreach f,lld lld@21 lld@20 llvm,$(if $(wildcard $(shell brew --prefix $(f) 2>/dev/null)/bin/ld.lld),$(shell brew --prefix $(f)),)))
ifeq ($(origin CC),default)
CC := $(LLVM_PREFIX)/bin/clang
endif
ifeq ($(origin LD),default)
LD := $(LLD_PREFIX)/bin/ld.lld
endif
STRIP := $(LLVM_PREFIX)/bin/llvm-strip
OBJCOPY := $(LLVM_PREFIX)/bin/llvm-objcopy

SYSROOT ?= $(if $(SIC_SYSROOT),$(SIC_SYSROOT),$(HOME)/.sic/sysroot)
LIBC  := $(SYSROOT)/usr
SRC   := src
# ARCH: x86_64 (default) or aarch64; the Cython output and the link are per architecture.
ARCH  ?= x86_64
BUILD := $(if $(filter x86_64,$(ARCH)),build,build-$(ARCH))
ROOT  := $(BUILD)/root
RP    := $(SRC)/renpy
PG    := $(SRC)/pygame_sdl2
OSABI_SIC := \123

RESOURCE_DIR := $(shell $(CC) -print-resource-dir)
CFLAGS := --target=$(ARCH)-linux-musl -nostdinc -isystem $(LIBC)/include -isystem $(RESOURCE_DIR)/include -fPIE \
          -fno-stack-protector -fno-asynchronous-unwind-tables -O2 -g0 -D_GNU_SOURCE -Wno-everything \
          -I$(LIBC)/include/python2.7 -I$(LIBC)/include/SDL2 -I$(LIBC)/include/freetype2 -I$(BUILD)/include -I$(PG) -I$(PG)/src -I$(RP)/module
# Ren'Py carries its own IMG_SavePNG (a compression argument) beside SDL2_image's: the first definition, Ren'Py's, wins.
LDFLAGS := -static -nostdlib --image-base=0x8000000000 -z max-page-size=0x1000 -z noexecstack --defsym=_DYNAMIC=0x8000000000 --allow-multiple-definition
CRT_BEGIN := $(LIBC)/lib/crt1.o $(LIBC)/lib/crti.o
CRT_END   := $(LIBC)/lib/crtn.o
# OpenGL: libzgl (the host GPU through virgl) where it is installed, else TinyGL in software.
HAVE_ZGL := $(wildcard $(LIBC)/lib/libzgl.a)
GL_LIBS := $(if $(HAVE_ZGL),$(LIBC)/lib/libzgl.a $(LIBC)/lib/libvirgl.a,$(LIBC)/lib/libTinyGL.a)
LIBS := $(LIBC)/lib/libpython2.7.a $(LIBC)/lib/libSDL2_image.a $(LIBC)/lib/libSDL2.a $(GL_LIBS) \
        $(LIBC)/lib/libzwm.a $(LIBC)/lib/libpng.a $(LIBC)/lib/libjpeg.a $(LIBC)/lib/libfreetype.a $(LIBC)/lib/libfribidi.a \
        $(LIBC)/lib/libavformat.a $(LIBC)/lib/libavcodec.a $(LIBC)/lib/libswresample.a $(LIBC)/lib/libswscale.a $(LIBC)/lib/libavutil.a \
        $(LIBC)/lib/libz.a $(LIBC)/lib/libc.a $(wildcard $(LIBC)/lib/libcompiler_rt.a)

# ---- the modules ------------------------------------------------------------------
# pygame_sdl2: every Cython module but the ones needing SDL2_ttf / SDL2_mixer.
PG_MODS := color controller display draw error event gfxdraw image joystick key locals mouse pygame_time rect render \
           rwobject scrap surface transform
PG_HELPERS := alphablit write_jpeg write_png SDL2_rotozoom SDL_gfxPrimitives
# Ren'Py: the desktop GL modules (not ANGLE; only where libzgl brings OpenGL 2 —
# without them Ren'Py draws in software), sound, text, styles.
RP_GL_MODS := renpy.gl.gl renpy.gl.gl1 renpy.gl.gldraw renpy.gl.glenviron_fixed renpy.gl.glenviron_limited \
              renpy.gl.glenviron_shader renpy.gl.glrtt_copy renpy.gl.glrtt_fbo renpy.gl.gltexture
RP_MODS := _renpy _renpybidi renpy.audio.renpysound renpy.display.accelerator renpy.display.render \
           $(if $(HAVE_ZGL),$(RP_GL_MODS)) \
           renpy.style renpy.styledata.style_activate_functions renpy.styledata.style_functions \
           renpy.styledata.style_hover_functions renpy.styledata.style_idle_functions \
           renpy.styledata.style_insensitive_functions renpy.styledata.style_selected_activate_functions \
           renpy.styledata.style_selected_functions renpy.styledata.style_selected_hover_functions \
           renpy.styledata.style_selected_idle_functions renpy.styledata.style_selected_insensitive_functions \
           renpy.styledata.styleclass renpy.styledata.stylesets renpy.text.ftfont renpy.text.textsupport renpy.text.texwrap
RP_HELPERS := IMG_savepng core subpixel renpybidicore renpysound_core ffmedia ftsupport ttgsubtable $(if $(HAVE_ZGL),egl_none)

# A Cython module "a.b.c" (short name "c") has init function initc and calls
# Py_InitModule4("c"). Static linking needs unique init symbols and the
# built-in table wants the dotted name, so each one is compiled with initc
# renamed and the module name spelt out.
short = $(lastword $(subst ., ,$(1)))
sym   = init_$(subst .,_,$(1))
PG_OBJS := $(patsubst %,$(BUILD)/pg/%.o,$(PG_MODS)) $(patsubst %,$(BUILD)/pg/helper-%.o,$(PG_HELPERS))
RP_OBJS := $(patsubst %,$(BUILD)/rp/%.o,$(RP_MODS)) $(patsubst %,$(BUILD)/rp/helper-%.o,$(RP_HELPERS))

.PHONY: all fetch install clean distclean
all: $(BUILD)/.staged

# into the sysroot's rootfs overlay, which ZAE packs into the image
install: all
	@mkdir -p $(SYSROOT)/rootfs
	cp -R $(ROOT)/. $(SYSROOT)/rootfs/
	@echo "renpy: installed into $(SYSROOT)"

fetch: $(SRC)/.fetched
$(SRC)/.fetched:
	@mkdir -p $(SRC)
	curl -sSL -o renpy.tar.bz2 $(RENPY_URL) && curl -sSL -o pygame_sdl2.tar.gz $(PYGAME_URL)
	rm -rf $(RP) $(PG) && tar xjf renpy.tar.bz2 -C $(SRC) && mv $(SRC)/renpy-$(RENPY_VERSION)-source $(RP)
	tar xzf pygame_sdl2.tar.gz -C $(SRC) && mv $(SRC)/pygame_sdl2-2.1.0-for-renpy-$(RENPY_VERSION) $(PG)
	rm renpy.tar.bz2 pygame_sdl2.tar.gz
	@touch $@

# The pygame_sdl2 C API headers (Cython output for the "api" functions of
# rwobject, surface and display) are not in the tarball: the host python
# runs Cython to make them. The .c it makes match the tarball's exactly.
PY2PORT := $(abspath ../../../ZAE/ports/python2)
HOST_PY := $(PY2PORT)/build/host/python.exe
CYTHON := PYTHONPATH=$(PY2PORT)/src/Lib:$(CURDIR)/$(BUILD)/Cython-0.23.5 $(HOST_PY) -E -S $(CURDIR)/$(BUILD)/Cython-0.23.5/cython.py -2
$(BUILD)/Cython-0.23.5/cython.py:
	@mkdir -p $(BUILD)
	curl -sSL -o $(BUILD)/cython.tgz $(CYTHON_URL) && tar xzf $(BUILD)/cython.tgz -C $(BUILD) && rm $(BUILD)/cython.tgz
API_HEADERS := $(patsubst %,$(BUILD)/include/pygame_sdl2/pygame_sdl2.%_api.h,rwobject surface display)
$(API_HEADERS): $(BUILD)/include/pygame_sdl2/pygame_sdl2.%_api.h: $(PG)/src/pygame_sdl2/%.pyx $(BUILD)/Cython-0.23.5/cython.py $(HOST_PY)
	@mkdir -p $(BUILD)/include/pygame_sdl2 $(BUILD)/capi
	cd $(PG) && $(CYTHON) -I include -o $(CURDIR)/$(BUILD)/capi/pygame_sdl2.$*.c src/pygame_sdl2/$*.pyx
	cp $(BUILD)/capi/pygame_sdl2.$*_api.h $@
	@cp $(PG)/src/pygame_sdl2/pygame_sdl2.h $(BUILD)/include/pygame_sdl2/

# Patched files keep a pristine backup (*.sic-orig); re-porting restores them first.
PATCHES := $(wildcard patches/*.patch)
$(SRC)/.sic-port: $(SRC)/.fetched $(PATCHES)
	@find $(RP) $(PG) -name '*.sic-orig' | while read f; do mv "$$f" "$${f%.sic-orig}"; done
	@for p in $(PATCHES); do patch -p1 -d $(RP) -b -z .sic-orig < $$p || exit 1; done
	@touch $@

$(BUILD)/pg/%.o: $(PG)/gen/pygame_sdl2.%.c $(SRC)/.fetched
	@mkdir -p $(dir $@)
	sed 's/Py_InitModule4("$(call short,$*)"/Py_InitModule4("pygame_sdl2.$*"/' $< > $(BUILD)/pg/$*.c
	$(CC) $(CFLAGS) -Dinit$(call short,$*)=$(call sym,pygame_sdl2.$*) -c $(BUILD)/pg/$*.c -o $@
$(BUILD)/pg/helper-%.o: $(PG)/src/%.c $(SRC)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rp/%.o: $(RP)/module/gen/%.c $(SRC)/.fetched $(API_HEADERS)
	@mkdir -p $(dir $@)
	sed 's/Py_InitModule4("$(call short,$*)"/Py_InitModule4("$*"/' $< > $(BUILD)/rp/$*.c
	$(CC) $(CFLAGS) -DFRIBIDI_ENTRY= -Dinit$(call short,$*)=$(call sym,$*) -c $(BUILD)/rp/$*.c -o $@
$(BUILD)/rp/helper-%.o: $(RP)/module/%.c $(SRC)/.fetched $(API_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DFRIBIDI_ENTRY= -c $< -o $@

# the built-in table, from the module lists
$(BUILD)/modules.h: Makefile $(HAVE_ZGL)        # a libzgl appearing changes the list
	@mkdir -p $(BUILD)
	@{ $(foreach m,$(PG_MODS),echo 'M("pygame_sdl2.$(m)", $(call sym,pygame_sdl2.$(m)))';) \
	   $(foreach m,$(RP_MODS),echo 'M("$(m)", $(call sym,$(m)))';) } > $@

$(LIBC)/lib/libpython2.7.a $(HOST_PY):
	$(MAKE) -C $(PY2PORT) ARCH=$(ARCH)

$(BUILD)/main.o: port/main.c $(BUILD)/modules.h
	$(CC) $(CFLAGS) -I$(BUILD) -c $< -o $@

$(BUILD)/renpy: $(LIBS) $(BUILD)/main.o $(PG_OBJS) $(RP_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(CRT_BEGIN) $(BUILD)/main.o $(PG_OBJS) $(RP_OBJS) --start-group $(LIBS) --end-group $(CRT_END)
	@printf '$(OSABI_SIC)' | dd of=$@ bs=1 seek=7 count=1 conv=notrunc status=none

# ---- the image --------------------------------------------------------------------
$(BUILD)/.staged: $(BUILD)/renpy $(SRC)/.sic-port
	rm -rf $(ROOT)
	@mkdir -p $(ROOT)/bin $(ROOT)/usr/lib/renpy/pygame_sdl2 $(ROOT)/usr/lib/python2.7/site-packages
	cp $(BUILD)/renpy $(ROOT)/bin/renpy && $(STRIP) $(ROOT)/bin/renpy
	python3 ../../icons/mkicon.py renpy $(BUILD)/renpy.zicon && $(OBJCOPY) --add-section .zicon=$(BUILD)/renpy.zicon $(ROOT)/bin/renpy   # its icon (Files, the launchpad)
	cp $(RP)/renpy.py $(ROOT)/usr/lib/renpy/
	cp -R $(RP)/renpy $(ROOT)/usr/lib/renpy/renpy
	cd $(ROOT)/usr/lib/renpy/renpy && find . \( -name '*.pyx' -o -name '*.pxd' -o -name '*.pyo' -o -name '*.pyc' \) -delete
	@mkdir -p $(ROOT)/usr/lib/python2.7/site-packages/pygame_sdl2 $(ROOT)/usr/share/renpy
	cp $(PG)/src/pygame_sdl2/*.py $(ROOT)/usr/lib/python2.7/site-packages/pygame_sdl2/
	cp -R $(RP)/the_question $(ROOT)/usr/share/renpy/the_question      # `renpy /usr/share/renpy/the_question`
	@touch $@
	@echo "renpy: `du -sh $(ROOT) | cut -f1` staged"

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf $(SRC)
