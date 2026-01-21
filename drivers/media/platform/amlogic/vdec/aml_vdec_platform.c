// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#include "aml_vdec_platform.h"
#include "aml_vdec_hw.h"
#include "h264.h"

#define VIDEO_DEC_H264  "s4_h264_multi.bin"

static struct aml_video_fmt aml_s4_video_formats[] = {
	{
		.name = "H.264",
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.type = AML_FMT_DEC,
		.align = 64,
		.is_10_bit_support = 0,
		.codec_type = CODEC_TYPE_H264,
		.num_planes = 1,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},
	},
	{
		.name = "NV21M",
		.fourcc = V4L2_PIX_FMT_NV21M,
		.type = AML_FMT_FRAME,
		.align = 64,
		.codec_type = CODEC_TYPE_FRAME,
		.num_planes = 2,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},
	},
	{
		.name = "NV21",
		.fourcc = V4L2_PIX_FMT_NV21,
		.type = AML_FMT_FRAME,
		.align = 64,
		.codec_type = CODEC_TYPE_FRAME,
		.num_planes = 1,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},
	},
	{
		.name = "NV12M",
		.fourcc = V4L2_PIX_FMT_NV12M,
		.type = AML_FMT_FRAME,
		.align = 64,
		.codec_type = CODEC_TYPE_FRAME,
		.num_planes = 2,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},

	},
	{
		.name = "NV12",
		.fourcc = V4L2_PIX_FMT_NV12,
		.type = AML_FMT_FRAME,
		.align = 64,
		.codec_type = CODEC_TYPE_FRAME,
		.num_planes = 1,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},
	},
};

const struct aml_codec_ops aml_S4_dec_ops[] = {
	[CODEC_TYPE_H264] = {
		.init = aml_h264_init,
		.exit = aml_h264_exit,
		.run = aml_h264_dec_run,
	},
};

const struct aml_dev_platform_data aml_vdec_s4_pdata = {
	.codec_ops = aml_S4_dec_ops,
	.dec_fmt = aml_s4_video_formats,
	.num_fmts = ARRAY_SIZE(aml_s4_video_formats),
	.power_type = AML_PM_PD,
	.req_hw_resource = dev_request_hw_resources,
	.destroy_hw_resource = dev_destroy_hw_resources,
	.fw_path = {
		VIDEO_DEC_H264,
	},
};
