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
#include "codec_h264.h"

#define AMVDEC_H264_PROFILES (BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) | \
	BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) | \
	BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) | \
	BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH))

#define AMVDEC_FORMAT_GX_H264_SLICE(fw)					\
	{								\
		.pixfmt = V4L2_PIX_FMT_H264_SLICE,			\
		.profile_ctrl = V4L2_CID_MPEG_VIDEO_H264_PROFILE,	\
		.default_profile = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,	\
		.level_ctrl = V4L2_CID_MPEG_VIDEO_H264_LEVEL,	\
		.default_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_1,	\
		.profiles = AMVDEC_H264_PROFILES,			\
		.max_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_2,	\
		.max_bit_depth = 8,				\
		.min_buffers = 16,					\
		.max_buffers = 24,					\
		.max_width = 1920,					\
		/* 1080-line video has a macroblock-aligned coded height of 1088 */ \
		.max_height = 1088,					\
		.vdec_ops = &meson_amvdec_1_ops,				\
		.codec_ops = &meson_amvdec_codec_h264_multi_ops,			\
		.firmware_path = fw,					\
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },		\
		.flags = V4L2_FMT_FLAG_COMPRESSED,			\
	}

/*
 * GXBB: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxbb[] = {
	AMVDEC_FORMAT_GX_H264_SLICE("meson/vdec/gxl_h264_multi.bin"), {
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
	AMVDEC_FORMAT_GX_H264_SLICE("meson/vdec/gxl_h264_multi.bin"), {
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
	AMVDEC_FORMAT_GX_H264_SLICE("meson/vdec/gxl_h264_multi.bin"), {
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
	AMVDEC_FORMAT_GX_H264_SLICE("meson/vdec/gxm_h264_multi.bin"), {
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
		/* Multi-instance firmware, fed straight from the OUTPUT buffer. */
		.pixfmt = V4L2_PIX_FMT_H264_SLICE,
		.profile_ctrl = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		.default_profile = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
		.level_ctrl = V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		.default_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_1,
		.profiles = AMVDEC_H264_PROFILES,
		.max_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
		.max_bit_depth = 8,
		.min_buffers = 16,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &meson_amvdec_1_ops,
		.codec_ops = &meson_amvdec_codec_h264_multi_ops,
		.firmware_path = "meson/vdec/g12a_h264_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED,
	}, {
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
		/* Multi-instance firmware, fed straight from the OUTPUT buffer. */
		.pixfmt = V4L2_PIX_FMT_H264_SLICE,
		.profile_ctrl = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		.default_profile = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
		.level_ctrl = V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		.default_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_1,
		.profiles = AMVDEC_H264_PROFILES,
		.max_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
		.max_bit_depth = 8,
		.min_buffers = 16,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &meson_amvdec_1_ops,
		.codec_ops = &meson_amvdec_codec_h264_multi_ops,
		.firmware_path = "meson/vdec/sm1_h264_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED,
	}, {
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

MODULE_FIRMWARE("meson/vdec/g12a_h264_multi.bin");
MODULE_FIRMWARE("meson/vdec/gxl_h264_multi.bin");
MODULE_FIRMWARE("meson/vdec/gxl_mpeg12_multi.bin");
MODULE_FIRMWARE("meson/vdec/gxm_h264_multi.bin");
MODULE_FIRMWARE("meson/vdec/sm1_h264_multi.bin");
