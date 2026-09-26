# SPDX-License-Identifier: GPL-2.0
# Makefile for the Amlogic meson-amvdec video decoder driver

meson-amvdec-objs = esparser.o amvdec.o amvdec_helpers.o amvdec_platform.o
meson-amvdec-objs += amvdec_1.o amvdec_hevc.o
meson-amvdec-objs += codec_mpeg12.o
meson-amvdec-objs += codec_h264.o codec_hevc_common.o codec_vp9.o codec_hevc.o

obj-$(CONFIG_VIDEO_MESON_AMVDEC) += meson-amvdec.o
