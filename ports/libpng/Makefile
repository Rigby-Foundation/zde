# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# libpng for sic (a library port: static libpng.a + headers into the sysroot).
NAME    := png
VERSION := 1.6.43
URL     := https://download.sourceforge.net/libpng/libpng-$(VERSION).tar.gz
TARDIR  := libpng-$(VERSION)
SRCS    := png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c \
           pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c
HEADERS := png.h pngconf.h
GEN_HEADERS = $(SRC)/pnglibconf.h
INCDIR  :=
PORT_CFLAGS := -DPNG_ARM_NEON_OPT=0
PORT_DEPS = $(SRC)/pnglibconf.h
include ../port.mk

# the stock configuration; no configure run
$(SRC)/pnglibconf.h: $(SRC)/.sic-port
	cp $(SRC)/scripts/pnglibconf.h.prebuilt $@
