# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# TinyGL for sic: a software OpenGL 1.x (Fabrice Bellard's, the C-Chads
# fork) rendering into a 32-bit buffer, behind the sic GL context ABI
# (GL/sic_gl.h from zgl) so SDL can use it in place of the hardware libzgl.
#
#   src/     the TinyGL checkout (fetched by `make fetch` if missing)
#   port/    sic_gl.c: the context ABI on ZBuffer
#   build/   libTinyGL.a -> $(SYSROOT)/usr/lib; the headers -> usr/include/TinyGL
#            (GL/gl.h there is TinyGL's; usr/include/GL/gl.h stays libzgl's)

TINYGL_URL := https://github.com/C-Chads/tinygl/archive/refs/heads/main.tar.gz

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

# The OpenMP pragmas are ignored (no runtime here): everything renders on the calling thread.
CFLAGS := --target=$(TARGET) -std=gnu99 -nostdinc -isystem $(LIBC)/include $(ARCH_CFLAGS) \
          -fno-stack-protector -fno-asynchronous-unwind-tables -O2 -g0 -DNDEBUG -I$(SRC)/include -Wno-everything

SRCS := $(wildcard $(SRC)/src/*.c)
OBJS := $(patsubst $(SRC)/src/%.c,$(BUILD)/%.o,$(SRCS)) $(BUILD)/sic_gl.o
LIB  := $(BUILD)/libTinyGL.a

.PHONY: all fetch install clean distclean
all: $(LIB)

fetch: $(SRC)/include/GL/gl.h
$(SRC)/include/GL/gl.h:
	curl -sSL -o tinygl.tar.gz $(TINYGL_URL)
	rm -rf $(SRC) tinygl-main && tar xzf tinygl.tar.gz && mv tinygl-main $(SRC) && rm tinygl.tar.gz

$(BUILD)/%.o: $(SRC)/src/%.c $(SRC)/include/GL/gl.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# The adapter sees TinyGL's gl.h first and zgl's GL/sic_gl.h from the sysroot.
$(BUILD)/sic_gl.o: port/sic_gl.c $(SRC)/include/GL/gl.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	rm -f $@ && $(AR) rcs $@ $^

install: all
	@mkdir -p $(SYSROOT)/usr/include/TinyGL/GL $(SYSROOT)/usr/lib
	cp $(SRC)/include/GL/gl.h $(SYSROOT)/usr/include/TinyGL/GL/
	cp $(SRC)/include/zbuffer.h $(SRC)/include/zfeatures.h $(SYSROOT)/usr/include/TinyGL/
	cp $(LIB) $(SYSROOT)/usr/lib/libTinyGL.a
	@echo "installed into $(SYSROOT)"

clean:
	rm -rf build

distclean: clean
	rm -rf $(SRC)
