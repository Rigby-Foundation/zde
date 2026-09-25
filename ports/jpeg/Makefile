# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
# The IJG JPEG library (jpeg-9) for sic: a library port.
NAME    := jpeg
VERSION := 9f
URL     := https://www.ijg.org/files/jpegsrc.v$(VERSION).tar.gz
TARDIR  := jpeg-$(VERSION)
SRCS    := jaricom.c jcapimin.c jcapistd.c jcarith.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c jcinit.c jcmainct.c \
           jcmarker.c jcmaster.c jcomapi.c jcparam.c jcprepct.c jcsample.c jctrans.c jdapimin.c jdapistd.c jdarith.c \
           jdatadst.c jdatasrc.c jdcoefct.c jdcolor.c jddctmgr.c jdhuff.c jdinput.c jdmainct.c jdmarker.c jdmaster.c \
           jdmerge.c jdpostct.c jdsample.c jdtrans.c jerror.c jfdctflt.c jfdctfst.c jfdctint.c jidctflt.c jidctfst.c \
           jidctint.c jquant1.c jquant2.c jutils.c jmemmgr.c jmemnobs.c
HEADERS := jpeglib.h jmorecfg.h jerror.h
GEN_HEADERS = $(SRC)/jconfig.h
INCDIR  :=
PORT_DEPS = $(SRC)/jconfig.h
include ../port.mk

$(SRC)/jconfig.h: $(SRC)/.sic-port
	cp $(SRC)/jconfig.txt $@
