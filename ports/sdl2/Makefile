# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# SDL2 for sic: cross-built with clang against the sic musl, with a video
# driver for zwm (port/video/zwm) and a software OpenGL (TinyGL) behind
# SDL_GL_*. No CMake: SDL builds fine from a source list and a hand-written
# SDL_config.h, which is how its console ports work too.
#
#   src/       SDL2 release tarball (fetched by `make fetch` if missing)
#   patches/   registering the zwm driver
#   port/      SDL_config_sic.h, the zwm video driver, the GL glue
#   build/     libSDL2.a and the headers -> installed into the sysroot

SDL_VERSION := 2.30.11
SDL_URL := https://github.com/libsdl-org/SDL/releases/download/release-$(SDL_VERSION)/SDL2-$(SDL_VERSION).tar.gz

LLVM_PREFIX ?= $(shell brew --prefix llvm 2>/dev/null)
ifeq ($(origin CC),default)
CC := $(LLVM_PREFIX)/bin/clang
endif
ifeq ($(origin AR),default)
AR := $(LLVM_PREFIX)/bin/llvm-ar
endif

SYSROOT ?= $(if $(SIC_SYSROOT),$(SIC_SYSROOT),$(HOME)/.sic/sysroot)
LIBC  := $(SYSROOT)/usr
ARCH  ?= x86_64
SRC   := src
BUILD := build/$(ARCH)
PATCHES := $(wildcard patches/*.patch)

ifeq ($(ARCH),powerpc)
TARGET := powerpc-linux-musl
ARCH_CFLAGS := -mcpu=7450 -maltivec -fno-pic -fno-pie
else ifeq ($(ARCH),aarch64)
TARGET := aarch64-linux-musl
ARCH_CFLAGS := -fPIE
else
TARGET := x86_64-linux-musl
ARCH_CFLAGS := -fPIE
endif

RESOURCE_DIR := $(shell $(CC) -print-resource-dir)
CFLAGS := --target=$(TARGET) -std=gnu11 -nostdinc -isystem $(LIBC)/include -isystem $(RESOURCE_DIR)/include $(ARCH_CFLAGS) \
          -fno-stack-protector -fno-asynchronous-unwind-tables -O2 -g0 -D_GNU_SOURCE -D__sic__ -DSDL_BUILD_SIC -I$(SRC)/include -I$(LIBC)/include -Wno-everything

# What gets compiled: SDL's generic core plus the unix/dummy back ends that
# sic supports, and our own video driver.
SDL_DIRS := src src/atomic src/audio src/audio/dummy src/audio/dsp src/cpuinfo src/dynapi src/events src/file \
            src/filesystem/dummy src/haptic src/haptic/dummy src/hidapi src/joystick src/joystick/dummy \
            src/joystick/virtual src/locale src/locale/dummy src/misc src/misc/dummy src/power \
            src/render src/render/software src/sensor src/sensor/dummy src/stdlib src/thread src/thread/pthread \
            src/timer src/timer/unix src/video src/video/yuv2rgb src/video/dummy src/loadso/dummy
SDL_SRCS := $(foreach d,$(SDL_DIRS),$(wildcard $(SRC)/$(d)/*.c)) $(SRC)/src/core/linux/SDL_threadprio.c
ZWM_SRCS := $(wildcard port/video/zwm/*.c)
OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/obj/%.o,$(SDL_SRCS)) $(patsubst port/%.c,$(BUILD)/obj/port/%.o,$(ZWM_SRCS))

LIB := $(BUILD)/libSDL2.a

.PHONY: all fetch port install clean distclean

all: $(LIB) $(BUILD)/include/SDL2/SDL.h

fetch: $(SRC)/include/SDL.h
$(SRC)/include/SDL.h:
	curl -sSL -o sdl2.tar.gz $(SDL_URL)
	rm -rf $(SRC) SDL2-$(SDL_VERSION) && tar xzf sdl2.tar.gz && mv SDL2-$(SDL_VERSION) $(SRC) && rm sdl2.tar.gz

port: $(SRC)/.sic-port
$(SRC)/.sic-port: $(SRC)/include/SDL.h $(PATCHES) port/SDL_config_sic.h
	@for p in $(PATCHES); do patch -p1 -d $(SRC) -N -r - < $$p || true; done
	cp port/SDL_config_sic.h $(SRC)/include/SDL_config.h
	@touch $@

# SDL includes "SDL_config.h"; ours takes its place in the build's include dir.
$(BUILD)/include/SDL2/SDL.h: $(SRC)/.sic-port port/SDL_config_sic.h
	@mkdir -p $(BUILD)/include/SDL2
	cp $(SRC)/include/*.h $(BUILD)/include/SDL2/
	cp port/SDL_config_sic.h $(BUILD)/include/SDL2/SDL_config.h
	@touch $@

# The zwm driver lives inside SDL's tree while compiling (its includes are relative).
$(SRC)/src/video/zwm/%.c: port/video/zwm/%.c $(SRC)/.sic-port
	@mkdir -p $(dir $@)
	cp $< $@
$(SRC)/src/video/zwm/%.h: port/video/zwm/%.h $(SRC)/.sic-port
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILD)/obj/%.o: $(SRC)/%.c $(BUILD)/include/SDL2/SDL.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ZWM_IN_TREE := $(patsubst port/video/zwm/%,$(SRC)/src/video/zwm/%,$(wildcard port/video/zwm/*))
$(BUILD)/obj/port/video/zwm/%.o: $(SRC)/src/video/zwm/%.c $(ZWM_IN_TREE) $(BUILD)/include/SDL2/SDL.h $(SYSROOT)/usr/include/zwm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	rm -f $@ && $(AR) rcs $@ $^

install: all
	@mkdir -p $(SYSROOT)/usr/include/SDL2 $(SYSROOT)/usr/lib
	cp $(BUILD)/include/SDL2/*.h $(SYSROOT)/usr/include/SDL2/
	cp $(LIB) $(SYSROOT)/usr/lib/libSDL2.a
	@echo "installed into $(SYSROOT)"

clean:
	rm -rf build $(SRC)/src/video/zwm

distclean: clean
	rm -rf $(SRC)
