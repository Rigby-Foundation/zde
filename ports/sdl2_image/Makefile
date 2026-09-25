# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# SDL2_image for sic: a library port. PNG and JPEG through the libpng and
# jpeg ports, the simple formats built in; no webp/tiff/avif/jxl.
NAME    := SDL2_image
VERSION := 2.6.3
URL     := https://github.com/libsdl-org/SDL_image/releases/download/release-$(VERSION)/SDL2_image-$(VERSION).tar.gz
TARDIR  := SDL2_image-$(VERSION)
SRCS    := IMG.c IMG_bmp.c IMG_gif.c IMG_jpg.c IMG_lbm.c IMG_pcx.c IMG_png.c IMG_pnm.c IMG_svg.c IMG_tga.c \
           IMG_xcf.c IMG_xpm.c IMG_xv.c IMG_qoi.c IMG_tif.c IMG_webp.c IMG_avif.c IMG_jxl.c IMG_WIC.c
HEADERS := SDL_image.h
INCDIR  := SDL2
PORT_CFLAGS = -I$(LIBC)/include/SDL2 -DLOAD_BMP -DLOAD_GIF -DLOAD_JPG -DLOAD_LBM -DLOAD_PCX -DLOAD_PNG -DLOAD_PNM \
              -DLOAD_SVG -DLOAD_TGA -DLOAD_XCF -DLOAD_XPM -DLOAD_XV -DLOAD_QOI -DSDL_IMAGE_SAVE_PNG=1 -DSDL_IMAGE_SAVE_JPG=1
include ../port.mk
