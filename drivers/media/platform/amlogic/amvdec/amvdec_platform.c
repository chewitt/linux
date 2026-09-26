// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * Per-compatible decoder capabilities and firmware.
 */

#include "amvdec_platform.h"
#include "amvdec.h"

#include "amvdec_1.h"
#include "amvdec_hevc.h"
#include "codec_mpeg12.h"

/*
 * GXBB: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxbb[] = {
	{
		.pixfmt = V4L2_PIX_FMT_MPEG2_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 1920,
		.max_height = 1088,
		.vdec_ops = &meson_amvdec_1_ops,
		.codec_ops = &meson_amvdec_codec_mpeg12_sl_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED,
	}
};

/*
 * GXL: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 and VP9
 * profiles 0/2 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxl[] = {
	{
		.pixfmt = V4L2_PIX_FMT_MPEG2_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 1920,
		.max_height = 1088,
		.vdec_ops = &meson_amvdec_1_ops,
		.codec_ops = &meson_amvdec_codec_mpeg12_sl_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED,
	}
};

/*
 * GXLX: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxlx[] = {
	{
		.pixfmt = V4L2_PIX_FMT_MPEG2_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 1920,
		.max_height = 1088,
		.vdec_ops = &meson_amvdec_1_ops,
		.codec_ops = &meson_amvdec_codec_mpeg12_sl_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED,
	}
};

/*
 * GXM: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 and VP9
 * profiles 0/2 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxm[] = {
	{
		.pixfmt = V4L2_PIX_FMT_MPEG2_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 1920,
		.max_height = 1088,
		.vdec_ops = &meson_amvdec_1_ops,
		.codec_ops = &meson_amvdec_codec_mpeg12_sl_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED,
	}
};

/*
 * G12A/G12B: H.264, HEVC Main/Main10 and VP9 profiles 0/2 at
 * 3840x2160; MPEG-2 at 1920x1088.
 */
static const struct amvdec_format amvdec_formats_g12a[] = {
	{
		/*
		 * The firmware parses sequence and picture headers synthesized from
		 * controls.
		 */
		.pixfmt = V4L2_PIX_FMT_MPEG2_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 1920,
		/* 1080 lines are coded as 1088: whole macroblocks, in field pairs */
		.max_height = 1088,
		.vdec_ops = &meson_amvdec_1_ops,
		.codec_ops = &meson_amvdec_codec_mpeg12_sl_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED,
	}
};

/*
 * SM1: H.264, HEVC Main/Main10 and VP9 profiles 0/2 at 3840x2160;
 * MPEG-2 at 1920x1088.
 */
static const struct amvdec_format amvdec_formats_sm1[] = {
	{
		/*
		 * The firmware parses sequence and picture headers synthesized from
		 * controls.
		 */
		.pixfmt = V4L2_PIX_FMT_MPEG2_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 1920,
		/* 1080 lines are coded as 1088: whole macroblocks, in field pairs */
		.max_height = 1088,
		.vdec_ops = &meson_amvdec_1_ops,
		.codec_ops = &meson_amvdec_codec_mpeg12_sl_ops,
		.firmware_path = "meson/vdec/gxl_mpeg12_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED,
	}
};

const struct amvdec_platform meson_amvdec_platform_gxbb = {
	.formats = amvdec_formats_gxbb,
	.num_formats = ARRAY_SIZE(amvdec_formats_gxbb),
	.revision = AMVDEC_REVISION_GXBB,
};

const struct amvdec_platform meson_amvdec_platform_gxl = {
	.formats = amvdec_formats_gxl,
	.num_formats = ARRAY_SIZE(amvdec_formats_gxl),
	.revision = AMVDEC_REVISION_GXL,
};

const struct amvdec_platform meson_amvdec_platform_gxlx = {
	.formats = amvdec_formats_gxlx,
	.num_formats = ARRAY_SIZE(amvdec_formats_gxlx),
	.revision = AMVDEC_REVISION_GXLX,
};

const struct amvdec_platform meson_amvdec_platform_gxm = {
	.formats = amvdec_formats_gxm,
	.num_formats = ARRAY_SIZE(amvdec_formats_gxm),
	.revision = AMVDEC_REVISION_GXM,
};

const struct amvdec_platform meson_amvdec_platform_g12a = {
	.formats = amvdec_formats_g12a,
	.num_formats = ARRAY_SIZE(amvdec_formats_g12a),
	.revision = AMVDEC_REVISION_G12A,
};

const struct amvdec_platform meson_amvdec_platform_sm1 = {
	.formats = amvdec_formats_sm1,
	.num_formats = ARRAY_SIZE(amvdec_formats_sm1),
	.revision = AMVDEC_REVISION_SM1,
};

MODULE_FIRMWARE("meson/vdec/gxl_mpeg12_multi.bin");
