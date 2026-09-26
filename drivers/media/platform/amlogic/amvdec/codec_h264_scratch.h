/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 *
 * H.264 header-parser scratch storage.
 */

#ifndef __MESON_AMVDEC_CODEC_H264_SCRATCH_H_
#define __MESON_AMVDEC_CODEC_H264_SCRATCH_H_

/* Two bounded lists of 32 UE-coded commands can exceed 128 bytes. */
#define H264_PARSE_RBSP_SIZE 1024

struct h264_parse_scratch {
	unsigned char rbsp[H264_PARSE_RBSP_SIZE];
};

#endif
