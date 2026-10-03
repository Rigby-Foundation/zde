# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# zde: the desktop for sic on top of zwm: session, panel, terminal, files, viewer.
# Optional: builds against the sysroot (with libzwm in it) and installs
# into $(SYSROOT)/rootfs, which ZAE overlays onto the initrd.

LLVM_PREFIX ?= $(shell brew --prefix llvm 2>/dev/null)
LLD_PREFIX  ?= $(firstword $(foreach f,lld lld@21 lld@20 llvm,$(if $(wildcard $(shell brew --prefix $(f) 2>/dev/null)/bin/ld.lld),$(shell brew --prefix $(f)),)))
ifeq ($(origin CC),default)
CC := $(if $(LLVM_PREFIX),$(LLVM_PREFIX)/bin/clang,clang)
endif
ifeq ($(origin LD),default)
LD := $(if $(LLD_PREFIX),$(LLD_PREFIX)/bin/ld.lld,ld.lld)
endif
ifeq ($(origin AR),default)
AR := $(if $(LLVM_PREFIX),$(LLVM_PREFIX)/bin/llvm-ar,ar)
endif

SYSROOT ?= $(if $(SIC_SYSROOT),$(SIC_SYSROOT),$(HOME)/.sic/sysroot)
LIBC    := $(SYSROOT)/usr
ARCH    ?= x86_64
BUILD   := build/$(ARCH)

ifeq ($(ARCH),powerpc)
TARGET := powerpc-linux-musl
ARCH_CFLAGS := -mcpu=7450 -maltivec -fno-pic -fno-pie
IMAGE_BASE := 0x10000000
LD_EMUL := -m elf32ppc
else ifeq ($(ARCH),aarch64)
TARGET := aarch64-linux-musl
ARCH_CFLAGS := -fPIE
IMAGE_BASE := 0x8000000000
LD_EMUL := -m aarch64elf
else
TARGET := x86_64-linux-musl
ARCH_CFLAGS := -fPIE
IMAGE_BASE := 0x8000000000
LD_EMUL :=
endif

CFLAGS  := --target=$(TARGET) -std=c11 -nostdinc -isystem $(LIBC)/include \
           $(ARCH_CFLAGS) -fno-stack-protector -fno-asynchronous-unwind-tables \
           -O2 -g -Wall -Wextra -D_GNU_SOURCE
LDFLAGS := $(LD_EMUL) -static -nostdlib --image-base=$(IMAGE_BASE) -z max-page-size=0x1000 -z noexecstack \
           --defsym=_DYNAMIC=$(IMAGE_BASE)
CRT_BEGIN := $(LIBC)/lib/crt1.o $(LIBC)/lib/crti.o
CRT_END   := $(LIBC)/lib/crtn.o
stamp-osabi = printf '\123' | dd of=$(1) bs=1 seek=7 count=1 conv=notrunc status=none

PROGS := $(patsubst bin/%.c,%,$(wildcard bin/*.c))
ELFS  := $(patsubst %,$(BUILD)/%,$(PROGS))
# SDL programs (sdl/*.c) need the SDL2 port in the sysroot: `make ports` puts it there.
SDL_PROGS := $(if $(wildcard $(LIBC)/lib/libSDL2.a),$(patsubst sdl/%.c,%,$(wildcard sdl/*.c)),)
SDL_ELFS  := $(patsubst %,$(BUILD)/%,$(SDL_PROGS))
# in dependency order: the libraries first, then what links them
ifeq ($(ARCH),powerpc)
PORTS := sdl2 tinygl        # the media libraries and Ren'Py ride on the python2 port, not built for ppc
else
PORTS := sdl2 tinygl libpng jpeg freetype fribidi sdl2_image ffmpeg renpy
endif

.PHONY: icons all install clean ports $(PORTS)
all: $(ELFS) $(SDL_ELFS)

# App icons: icons/mkicon.py draws them; a program named like one gets it
# as a `.zicon` section after linking (zwm_icon_load reads it back), and
# they all go to /usr/share/icons for Files' generic ones (folder, doc, ...).
# The section is marked allocated so `strip` keeps it (outside every
# segment, the loader never maps it); unallocated, a stripped program has
# no icon and the launchpad leaves it out.
OBJCOPY := $(if $(LLVM_PREFIX),$(LLVM_PREFIX)/bin/llvm-objcopy,llvm-objcopy)
ICON_NAMES := $(shell python3 icons/mkicon.py --list)
ICON_FILES := $(patsubst %,$(BUILD)/icons/%.zicon,$(ICON_NAMES))
$(BUILD)/icons/%.zicon: icons/mkicon.py
	@mkdir -p $(dir $@)
	python3 icons/mkicon.py $* $@
icons: $(ICON_FILES)
add-icon = $(if $(filter $(notdir $(1)),$(ICON_NAMES)),$(OBJCOPY) --add-section .zicon=$(BUILD)/icons/$(notdir $(1)).zicon --set-section-flags .zicon=alloc,readonly $(1),true)

ports:
	@for p in $(PORTS); do $(MAKE) -C ports/$$p install || exit 1; done
$(PORTS):
	$(MAKE) -C ports/$@ install

$(BUILD)/%.o: sdl/%.c $(LIBC)/include/zwm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# OpenGL: libzgl (the host GPU through virgl) when it is installed, else TinyGL.
GL_LIBS := $(if $(wildcard $(LIBC)/lib/libzgl.a),$(LIBC)/lib/libzgl.a $(LIBC)/lib/libvirgl.a,$(wildcard $(LIBC)/lib/libTinyGL.a))

$(SDL_ELFS): $(BUILD)/%: $(BUILD)/%.o $(LIBC)/lib/libSDL2.a $(GL_LIBS) $(LIBC)/lib/libzwm.a | icons
	$(LD) $(LDFLAGS) -o $@ $(CRT_BEGIN) $< $(LIBC)/lib/libSDL2.a $(GL_LIBS) $(LIBC)/lib/libzwm.a $(LIBC)/lib/libc.a $(wildcard $(LIBC)/lib/libcompiler_rt.a) $(CRT_END)
	@$(call add-icon,$@)
	@$(call stamp-osabi,$@)

$(BUILD)/%.o: bin/%.c $(LIBC)/include/zwm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%: $(BUILD)/%.o $(LIBC)/lib/libzwm.a | icons
	$(LD) $(LDFLAGS) -o $@ $(CRT_BEGIN) $< $(LIBC)/lib/libzwm.a $(LIBC)/lib/libc.a $(wildcard $(LIBC)/lib/libcompiler_rt.a) $(CRT_END)
	@$(call add-icon,$@)
	@$(call stamp-osabi,$@)

$(LIBC)/include/zwm.h:
	@echo "error: no libzwm in $(SYSROOT) (run 'make install' in zwm)"; exit 1

install: all
	@mkdir -p $(SYSROOT)/rootfs/bin
	cp $(ELFS) $(SDL_ELFS) $(SYSROOT)/rootfs/bin/
	@mkdir -p $(SYSROOT)/rootfs/usr/share/icons
	cp $(ICON_FILES) $(SYSROOT)/rootfs/usr/share/icons/
	@echo "installed into $(SYSROOT)"

clean:
	rm -rf build
	@for p in $(PORTS); do $(MAKE) -C ports/$$p clean; done
