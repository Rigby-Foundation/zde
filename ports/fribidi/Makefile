# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# GNU FriBidi for sic: a library port (the release tarball carries the
# generated Unicode tables).
NAME    := fribidi
VERSION := 1.0.15
URL     := https://github.com/fribidi/fribidi/releases/download/v$(VERSION)/fribidi-$(VERSION).tar.xz
TARDIR  := fribidi-$(VERSION)
SRCS    := lib/fribidi.c lib/fribidi-arabic.c lib/fribidi-bidi.c lib/fribidi-bidi-types.c lib/fribidi-char-sets.c \
           lib/fribidi-char-sets-cap-rtl.c lib/fribidi-char-sets-cp1255.c lib/fribidi-char-sets-cp1256.c \
           lib/fribidi-char-sets-iso8859-6.c lib/fribidi-char-sets-iso8859-8.c lib/fribidi-char-sets-utf8.c \
           lib/fribidi-deprecated.c lib/fribidi-joining.c lib/fribidi-joining-types.c lib/fribidi-mirroring.c \
           lib/fribidi-brackets.c lib/fribidi-run.c lib/fribidi-shape.c
HEADERS := $(wildcard $(SRC)/lib/fribidi*.h)
INCDIR  := fribidi
PORT_CFLAGS = -DFRIBIDI_LIB_STATIC -I$(SRC) -I$(SRC)/lib -DHAVE_STRINGIZE=1 -DHAVE_STRING_H=1 -DHAVE_STDLIB_H=1 -DHAVE_MEMSET=1 -DHAVE_MEMMOVE=1 -DHAVE_STRDUP=1 -DSTDC_HEADERS=1
PORT_DEPS = $(SRC)/lib/fribidi-config.h
include ../port.mk

# what configure would have written
$(SRC)/lib/fribidi-config.h: $(SRC)/.sic-port
	sed -e 's/@PACKAGE@/fribidi/; s/@PACKAGE_NAME@/GNU FriBidi/; s/@PACKAGE_BUGREPORT@/https:\/\/github.com\/fribidi\/fribidi\/issues/' \
	    -e 's/@FRIBIDI_VERSION@/$(VERSION)/; s/@FRIBIDI_MAJOR_VERSION@/1/; s/@FRIBIDI_MINOR_VERSION@/0/; s/@FRIBIDI_MICRO_VERSION@/15/' \
	    -e 's/@FRIBIDI_INTERFACE_VERSION@/4/; s/@SIZEOF_INT@/4/; s/@FRIBIDI_MSVC_BUILD_PLACEHOLDER@//' $(SRC)/lib/fribidi-config.h.in > $@

install: $(LIB)
	@mkdir -p build/root $(LIBC)/include/fribidi $(LIBC)/lib
	@for h in $(SRC)/lib/fribidi*.h; do cmp -s $$h $(LIBC)/include/fribidi/`basename $$h` 2>/dev/null || cp $$h $(LIBC)/include/fribidi/; done
	@cmp -s $(LIB) $(LIBC)/lib/lib$(NAME).a 2>/dev/null || cp $(LIB) $(LIBC)/lib/lib$(NAME).a
	@echo "$(NAME): installed into $(SYSROOT)"
