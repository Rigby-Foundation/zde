# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# FFmpeg 4.4 for sic: what Ren'Py's renpysound needs -- libavformat,
# libavcodec, libavutil, libswresample, libswscale, with the decoders a
# visual novel plays (Vorbis, Opus, MP3, FLAC, PCM; Theora/VP8/VP9 video),
# static, no assembly (no nasm on the build host), no programs. Its own
# configure does the rest; a library port into the sysroot.
NAME    := avcodec
VERSION := 4.4.5
URL     := https://ffmpeg.org/releases/ffmpeg-$(VERSION).tar.xz
TARDIR  := ffmpeg-$(VERSION)
SRCS    :=
include ../port.mk

FF_CC := $(CC) $(BASE_CFLAGS)
FF_LDFLAGS := -static -no-pie -nostdlib -fuse-ld=$(LLD) -L$(LIBC)/lib -Wl,--image-base=0x8000000000 -Wl,-z,max-page-size=0x1000 -Wl,--defsym=_DYNAMIC=0x8000000000 \
              $(LIBC)/lib/crt1.o $(LIBC)/lib/crti.o
FF_LIBS := $(LIBC)/lib/libc.a $(wildcard $(LIBC)/lib/libcompiler_rt.a) $(LIBC)/lib/crtn.o
DECODERS := vorbis opus mp3 mp3float flac pcm_s16le pcm_s16be pcm_u8 pcm_f32le aac theora vp8 vp9 mpeg4 h264
DEMUXERS := ogg mp3 flac wav matroska mov aac
PARSERS  := vorbis opus mpegaudio flac aac vp8 vp9 h264 mpeg4video
comma := ,
list = $(subst $(eval) ,$(comma),$(strip $(1)))

$(BUILD)/config.h: $(SRC)/.sic-port
	@mkdir -p $(BUILD)
	cd $(BUILD) && $(CURDIR)/$(SRC)/configure --enable-cross-compile --target-os=linux --arch=$(ARCH) $(if $(filter x86_64,$(ARCH)),,--disable-asm) \
	    --cc="$(FF_CC)" --ar=$(AR) --ranlib=$(LLVM_PREFIX)/bin/llvm-ranlib --nm=$(LLVM_PREFIX)/bin/llvm-nm --strip=$(LLVM_PREFIX)/bin/llvm-strip \
	    --extra-ldflags="$(FF_LDFLAGS)" --extra-libs="$(FF_LIBS)" --pkg-config=false \
	    --prefix=/usr --enable-static --disable-shared --disable-x86asm --disable-programs --disable-doc \
	    --disable-avdevice --disable-avfilter --disable-network --disable-debug \
	    --disable-everything --enable-decoder=$(call list,$(DECODERS)) --enable-demuxer=$(call list,$(DEMUXERS)) \
	    --enable-parser=$(call list,$(PARSERS)) --enable-protocol=file \
	    > configure.log 2>&1 || { tail -30 configure.log; exit 1; }

$(BUILD)/libavcodec/libavcodec.a: $(BUILD)/config.h
	$(MAKE) -C $(BUILD) -j8 > $(BUILD)/make.log 2>&1 || { grep -B2 -A6 "rror" $(BUILD)/make.log | head -40; exit 1; }

install: $(BUILD)/libavcodec/libavcodec.a
	@mkdir -p build/root $(LIBC)/lib
	$(MAKE) -C $(BUILD) install DESTDIR=$(CURDIR)/$(BUILD)/dest > /dev/null
	@cp -R $(BUILD)/dest/usr/include/. $(LIBC)/include/
	@for l in avcodec avformat avutil swresample swscale; do cmp -s $(BUILD)/dest/usr/lib/lib$$l.a $(LIBC)/lib/lib$$l.a 2>/dev/null || cp $(BUILD)/dest/usr/lib/lib$$l.a $(LIBC)/lib/; done
	@echo "ffmpeg: installed into $(SYSROOT)"
