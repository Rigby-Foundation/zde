# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# FreeType for sic: a library port. The documented one-file-per-module build
# (docs/INSTALL.ANY), with the stock ftoption.h and ftmodule.h.
NAME    := freetype
VERSION := 2.13.2
URL     := https://download.savannah.gnu.org/releases/freetype/freetype-$(VERSION).tar.gz
TARDIR  := freetype-$(VERSION)
SRCS    := src/base/ftsystem.c src/base/ftinit.c src/base/ftdebug.c src/base/ftbase.c src/base/ftbbox.c \
           src/base/ftglyph.c src/base/ftbdf.c src/base/ftbitmap.c src/base/ftcid.c src/base/ftfstype.c \
           src/base/ftgasp.c src/base/ftgxval.c src/base/ftmm.c src/base/ftotval.c src/base/ftpatent.c \
           src/base/ftpfr.c src/base/ftstroke.c src/base/ftsynth.c src/base/fttype1.c src/base/ftwinfnt.c \
           src/bdf/bdf.c src/cff/cff.c src/cid/type1cid.c src/pcf/pcf.c src/pfr/pfr.c src/sfnt/sfnt.c \
           src/truetype/truetype.c src/type1/type1.c src/type42/type42.c src/winfonts/winfnt.c \
           src/raster/raster.c src/smooth/smooth.c src/sdf/sdf.c src/autofit/autofit.c src/cache/ftcache.c \
           src/gzip/ftgzip.c src/lzw/ftlzw.c src/bzip2/ftbzip2.c src/gxvalid/gxvalid.c src/otvalid/otvalid.c \
           src/psaux/psaux.c src/pshinter/pshinter.c src/psnames/psnames.c src/svg/svg.c
INCDIR  := freetype2
PORT_CFLAGS = -DFT2_BUILD_LIBRARY -I$(SRC)/include -DFT_CONFIG_OPTION_SYSTEM_ZLIB
include ../port.mk

# the whole include tree, as FreeType lays it out (ft2build.h + freetype/)
install: $(LIB)
	@mkdir -p build/root $(LIBC)/include/freetype2 $(LIBC)/lib
	@cp -R $(SRC)/include/ft2build.h $(SRC)/include/freetype $(LIBC)/include/freetype2/
	@cmp -s $(LIB) $(LIBC)/lib/lib$(NAME).a 2>/dev/null || cp $(LIB) $(LIBC)/lib/lib$(NAME).a
	@echo "$(NAME): installed into $(SYSROOT)"
