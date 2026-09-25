# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# Shared by the library ports: the cross toolchain against the sic sysroot,
# and the fetch/install boilerplate. A port sets NAME, VERSION, URL, TARDIR
# (the directory the tarball unpacks to), SRCS (relative to src/), CFLAGS
# additions in PORT_CFLAGS, and HEADERS (relative to src/) to install under
# usr/include/$(INCDIR); anything mentioning $(SRC) or $(BUILD) with "=" (they are
# defined below). Library ports put nothing into the image: they
# install a static library into the sysroot for the ports that link it.

LLVM_PREFIX ?= $(shell brew --prefix llvm 2>/dev/null)
LLD_PREFIX  ?= $(firstword $(foreach f,lld lld@21 lld@20 llvm,$(if $(wildcard $(shell brew --prefix $(f) 2>/dev/null)/bin/ld.lld),$(shell brew --prefix $(f)),)))
ifeq ($(origin CC),default)
CC := $(LLVM_PREFIX)/bin/clang
endif
ifeq ($(origin AR),default)
AR := $(LLVM_PREFIX)/bin/llvm-ar
endif
LLD := $(LLD_PREFIX)/bin/ld.lld

SYSROOT ?= $(if $(SIC_SYSROOT),$(SIC_SYSROOT),$(HOME)/.sic/sysroot)
LIBC  := $(SYSROOT)/usr
ARCH  ?= x86_64
SRC   := src
BUILD := build/$(ARCH)

ifeq ($(ARCH),powerpc)
TARGET := powerpc-linux-musl
ARCH_CFLAGS := -mcpu=7450 -fno-pic -fno-pie
else ifeq ($(ARCH),aarch64)
TARGET := aarch64-linux-musl
ARCH_CFLAGS := -fPIE
else
TARGET := x86_64-linux-musl
ARCH_CFLAGS := -fPIE
endif
RESOURCE_DIR := $(shell $(CC) -print-resource-dir)
BASE_CFLAGS := --target=$(TARGET) -nostdinc -isystem $(LIBC)/include -isystem $(RESOURCE_DIR)/include $(ARCH_CFLAGS) \
               -fno-stack-protector -fno-asynchronous-unwind-tables -O2 -g0 -D_GNU_SOURCE -Wno-everything
CFLAGS := $(BASE_CFLAGS) $(PORT_CFLAGS)

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
LIB  := $(BUILD)/lib$(NAME).a

.PHONY: all fetch install clean distclean
all: install

fetch: $(SRC)/.fetched
$(SRC)/.fetched:
	curl -sSL -o $(NAME).tar $(URL)
	rm -rf $(SRC) $(TARDIR) && tar xf $(NAME).tar && mv $(TARDIR) $(SRC) && rm $(NAME).tar
	@touch $@

# Patched files keep a pristine backup (*.sic-orig); re-porting restores them first.
PATCHES := $(wildcard patches/*.patch)
$(SRC)/.sic-port: $(SRC)/.fetched $(PATCHES)
	@find $(SRC) -name '*.sic-orig' | while read f; do mv "$$f" "$${f%.sic-orig}"; done
	@for p in $(PATCHES); do patch -p1 -d $(SRC) -b -z .sic-orig < $$p || exit 1; done
	@touch $@

$(BUILD)/%.o: $(SRC)/%.c $(SRC)/.sic-port $(PORT_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	rm -f $@ && $(AR) rcs $@ $^

# Copies only when changed: dependants rebuild on the sysroot copy's timestamp.
INSTALL_FILES := $(foreach h,$(HEADERS),$(SRC)/$(h):$(LIBC)/include/$(INCDIR)/$(notdir $(h))) \
                 $(foreach h,$(GEN_HEADERS),$(h):$(LIBC)/include/$(INCDIR)/$(notdir $(h))) \
                 $(LIB):$(LIBC)/lib/lib$(NAME).a
install: $(LIB) $(PORT_INSTALL_DEPS)
	@mkdir -p build/root $(LIBC)/include/$(INCDIR) $(LIBC)/lib
	@for pair in $(INSTALL_FILES); do src=$${pair%%:*}; dst=$${pair##*:}; cmp -s $$src $$dst 2>/dev/null || cp $$src $$dst; done
	@echo "$(NAME): installed into $(SYSROOT)"

clean:
	rm -rf build

distclean: clean
	rm -rf $(SRC)
