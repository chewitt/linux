// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#include "vdec_platform.h"
#include "vdec.h"

#include "vdec_1.h"
#include "vdec_hevc.h"
#include "codec_mpeg12.h"
#include "codec_h264.h"
#include "codec_mpeg4.h"
#include "codec_hevc.h"
#include "codec_vp9.h"
#include "codec_h264_multi.h"

static const struct amvdec_format vdec_formats_gxbb[] = {
	{
		.pixfmt = V4L2_PIX_FMT_H264_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 1920,
		.max_height = 1080,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_h264_multi_ops,
		.firmware_path = "meson/vdec/gxbb_h264_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED |
			 V4L2_FMT_FLAG_DYN_RESOLUTION,
	},
};

static const struct amvdec_format vdec_formats_gxl[] = {
	{
		.pixfmt = V4L2_PIX_FMT_H264_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_h264_multi_ops,
		.firmware_path = "meson/vdec/gxl_h264_multi.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED |
			 V4L2_FMT_FLAG_DYN_RESOLUTION,
		.direct_input = 1,
	},
	{
		.pixfmt = V4L2_PIX_FMT_VP9,
		.min_buffers = 16,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_hevc_ops,
		.codec_ops = &codec_vp9_ops,
		.firmware_path = "meson/gx/vvp9_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_AM21C, 0 },
	},
};

static const struct amvdec_format vdec_formats_gxm[] = {
	{
		.pixfmt = V4L2_PIX_FMT_H264_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_h264_ops,
		.firmware_path = "meson/vdec/gxm_h264.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED |
			 V4L2_FMT_FLAG_DYN_RESOLUTION,
	},
    {
		.pixfmt = V4L2_PIX_FMT_VP9,
		.min_buffers = 16,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_hevc_ops,
		.codec_ops = &codec_vp9_ops,
		.firmware_path = "meson/gx/vvp9_mc",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_AM21C, 0 },
	},
};

static const struct amvdec_format vdec_formats_g12a[] = {
	{
		.pixfmt = V4L2_PIX_FMT_H264_SLICE,
		.min_buffers = 2,
		.max_buffers = 24,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_1_ops,
		.codec_ops = &codec_h264_ops,
		.firmware_path = "meson/vdec/g12a_h264.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, 0 },
		.flags = V4L2_FMT_FLAG_COMPRESSED |
			 V4L2_FMT_FLAG_DYN_RESOLUTION,
	},
    {
		.pixfmt = V4L2_PIX_FMT_VP9,
		.min_buffers = 4,
		.max_buffers = 16,
		.max_width = 3840,
		.max_height = 2160,
		.vdec_ops = &vdec_hevc_ops,
		.codec_ops = &codec_vp9_ops,
		.firmware_path = "meson/vdec/g12a_vp9.bin",
		.pixfmts_cap = { V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_AM21C, 0 },
	},
};

const struct vdec_platform vdec_platform_gxbb = {
	.formats = vdec_formats_gxbb,
	.num_formats = ARRAY_SIZE(vdec_formats_gxbb),
	.revision = VDEC_REVISION_GXBB,
};

const struct vdec_platform vdec_platform_gxl = {
	.formats = vdec_formats_gxl,
	.num_formats = ARRAY_SIZE(vdec_formats_gxl),
	.revision = VDEC_REVISION_GXL,
};

const struct vdec_platform vdec_platform_gxm = {
	.formats = vdec_formats_gxm,
	.num_formats = ARRAY_SIZE(vdec_formats_gxm),
	.revision = VDEC_REVISION_GXM,
};

const struct vdec_platform vdec_platform_g12a = {
	.formats = vdec_formats_g12a,
	.num_formats = ARRAY_SIZE(vdec_formats_g12a),
	.revision = VDEC_REVISION_G12A,
};
