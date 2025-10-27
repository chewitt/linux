/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#ifndef _AML_VDEC_H_
#define _AML_VDEC_H_

#include "aml_vdec_drv.h"

#define DEFAULT_OUT_IDX  0	/* set default output format to h264 type */
#define DEFAULT_CAP_IDX  2	/* set default capture format to NV21 */

/**
 * struct aml_vdec_v4l2_ctrl - helper type to declare supported ctrls
 * @codec_type: codec id this control belong to (CODEC_TYPE_H264, etc.)
 * @cfg: control configuration
 */
struct aml_vdec_v4l2_ctrl {
	unsigned int codec_type;
	struct v4l2_ctrl_config cfg;
};

extern const struct v4l2_m2m_ops aml_vdec_m2m_ops;
extern const struct v4l2_ioctl_ops aml_vdec_ioctl_ops;

int aml_vdec_ctrls_setup(struct aml_vdec_ctx *ctx);
int aml_vdec_queue_init(void *priv, struct vb2_queue *src_vq,
			struct vb2_queue *dst_vq);
void aml_vdec_set_default_params(struct aml_vdec_ctx *ctx);
#endif
