// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#include "aml_vdec_platform.h"
#include "aml_vdec_drv.h"
#include "aml_vdec_hw.h"
#include "h264.h"

#define VIDEO_DEC_H264  "s4_h264_multi.bin"

const struct aml_codec_ops aml_S4_dec_ops[] = {
	[CODEC_TYPE_H264] = {
		.init = aml_h264_init,
		.exit = aml_h264_exit,
		.run = aml_h264_dec_run,
	},
};

static const struct aml_video_dec_fmt aml_S4_dec_fmts = {
	.max_height = AML_VDEC_1080P_MAX_H,
	.max_width = AML_VDEC_1080P_MAX_W,
	.align = 32,
	.is_10_bit_support = 0,
};

const struct aml_dev_platform_data aml_vdec_s4_pdata = {
	.dec_fmt = &aml_S4_dec_fmts,
	.codec_ops = aml_S4_dec_ops,
	.power_type = AML_PM_PD,
	.req_hw_resource = dev_request_hw_resources,
	.destroy_hw_resource = dev_destroy_hw_resources,
	.fw_path = {
		VIDEO_DEC_H264,
	},
};
