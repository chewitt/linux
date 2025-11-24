// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "aml_vdec.h"
#include "aml_vdec_hw.h"
#include "aml_vdec_platform.h"

#define VCODEC_DRV_NAME "aml-vdec-drv"

static const struct aml_vdec_v4l2_ctrl controls[] = {
	{
		.codec_type = CODEC_TYPE_H264,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,
		},
	}, {
		.codec_type = CODEC_TYPE_H264,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_SPS,
		},
	}, {
		.codec_type = CODEC_TYPE_H264,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_PPS,
		},
	}, {
		.codec_type = CODEC_TYPE_H264,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_SCALING_MATRIX,
		},
	}, {
		.codec_type = CODEC_TYPE_H264,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_DECODE_MODE,
			.min = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
			.def = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
			.max = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		},
	}, {
		.codec_type = CODEC_TYPE_H264,
		.cfg = {
			.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		},
	}, {
		.codec_type = CODEC_TYPE_H264,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_START_CODE,
			.min = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
			.def = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
			.max = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		},
	}, {
		.codec_type = CODEC_TYPE_H264,
		.cfg = {
			.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
			.max = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
			.def = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
		},
	}
};

static struct aml_video_fmt aml_video_formats[] = {
	{
		.name = "H.264",
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.type = AML_FMT_DEC,
		.codec_type = CODEC_TYPE_H264,
		.num_planes = 1,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},
	},
	{
		.name = "NV21M",
		.fourcc = V4L2_PIX_FMT_NV21M,
		.type = AML_FMT_FRAME,
		.codec_type = CODEC_TYPE_FRAME,
		.num_planes = 2,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},
	},
	{
		.name = "NV21",
		.fourcc = V4L2_PIX_FMT_NV21,
		.type = AML_FMT_FRAME,
		.codec_type = CODEC_TYPE_FRAME,
		.num_planes = 1,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},
	},
	{
		.name = "NV12M",
		.fourcc = V4L2_PIX_FMT_NV12M,
		.type = AML_FMT_FRAME,
		.codec_type = CODEC_TYPE_FRAME,
		.num_planes = 2,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},

	},
	{
		.name = "NV12",
		.fourcc = V4L2_PIX_FMT_NV12,
		.type = AML_FMT_FRAME,
		.codec_type = CODEC_TYPE_FRAME,
		.num_planes = 1,
		.stepwise = {AML_VDEC_MIN_W, AML_VDEC_1080P_MAX_W, 2,
			AML_VDEC_MIN_H, AML_VDEC_1080P_MAX_H, 2},
	},
};

void aml_vdec_set_default_params(struct aml_vdec_ctx *ctx)
{
	struct aml_q_data *q_data = NULL;

	ctx->m2m_ctx->q_lock = &ctx->v4l2_intf_lock;

	ctx->pic_info.colorspace = V4L2_COLORSPACE_DEFAULT;
	ctx->pic_info.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->pic_info.quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->pic_info.xfer_func = V4L2_XFER_FUNC_DEFAULT;

	q_data = &ctx->q_data[AML_Q_DATA_SRC];
	memset(q_data, 0, sizeof(struct aml_q_data));
	q_data->visible_width = AML_VDEC_MIN_W;
	q_data->visible_height = AML_VDEC_MIN_H;
	q_data->coded_width = AML_VDEC_MIN_W;
	q_data->coded_height = AML_VDEC_MIN_H;
	q_data->filed_flag = V4L2_FIELD_NONE;
	q_data->bytesperline[0] = 0;
	q_data->sizeimage[0] = (1024 * 1024);
	q_data->fmt = &aml_video_formats[DEFAULT_OUT_IDX];

	q_data = &ctx->q_data[AML_Q_DATA_DST];
	memset(q_data, 0, sizeof(struct aml_q_data));
	q_data->visible_width = AML_VDEC_MIN_W;
	q_data->visible_height = AML_VDEC_MIN_H;
	q_data->coded_width = AML_VDEC_MIN_W;
	q_data->coded_height = AML_VDEC_MIN_H;
	q_data->filed_flag = V4L2_FIELD_NONE;
	q_data->bytesperline[0] = q_data->coded_width;
	q_data->sizeimage[0] = q_data->coded_width * q_data->coded_height;
	q_data->bytesperline[1] = q_data->coded_width;
	q_data->sizeimage[1] = q_data->sizeimage[0] / 2;
	q_data->fmt = &aml_video_formats[DEFAULT_CAP_IDX];
}

int aml_vdec_ctrls_setup(struct aml_vdec_ctx *ctx)
{
	int i;
	int ctrls_size = sizeof(controls) / sizeof(struct aml_vdec_v4l2_ctrl);

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, ctrls_size);
	for (i = 0; i < ctrls_size; i++) {
		v4l2_ctrl_new_custom(&ctx->ctrl_handler, &controls[i].cfg, NULL);
		if (ctx->ctrl_handler.error) {
			dev_info(&ctx->dev->plat_dev->dev, "add ctrl for (%d) failed%d\n",
				 controls[i].cfg.id, ctx->ctrl_handler.error);
			v4l2_ctrl_handler_free(&ctx->ctrl_handler);
			return ctx->ctrl_handler.error;
		}
	}
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	return v4l2_ctrl_handler_setup(&ctx->ctrl_handler);
}

static void m2mops_vdec_device_run(void *m2m_priv)
{
	struct aml_vdec_ctx *ctx = (struct aml_vdec_ctx *)m2m_priv;
	struct aml_vdec_dev *dev = ctx->dev;
	struct vb2_v4l2_buffer *src, *dst;
	struct media_request *src_req;
	const char *fw_path = dev->pvdec_data->fw_path[ctx->curr_dec_type];

	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	dev_dbg(&dev->plat_dev->dev, "device run : src buf : %d dst buf %d\n",
		src->vb2_buf.index, dst->vb2_buf.index);

	src_req = src->vb2_buf.req_obj.req;
	if (src_req)
		v4l2_ctrl_request_setup(src_req, &ctx->ctrl_handler);

	dos_enable(dev->dec_hw);
	/* incase of bus hang in stop_streaming */
	ctx->dos_clk_en = 1;
	aml_vdec_reset_core(dev->dec_hw);
	load_firmware(dev->dec_hw, fw_path);

	if (ctx->codec_ops->run)
		ctx->codec_ops->run(ctx);

	v4l2_m2m_buf_copy_metadata(src, dst);
	if (src_req)
		v4l2_ctrl_request_complete(src_req, &ctx->ctrl_handler);

	v4l2_m2m_buf_done_and_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx, VB2_BUF_STATE_DONE);
}

const struct v4l2_m2m_ops aml_vdec_m2m_ops = {
	.device_run = m2mops_vdec_device_run,
};

static int vidioc_vdec_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	strscpy(cap->driver, VCODEC_DRV_NAME, sizeof(cap->driver));
	strscpy(cap->card, "platform:" VCODEC_DRV_NAME, sizeof(cap->card));

	return 0;
}

static int vidioc_vdec_enum_fmt(struct v4l2_fmtdesc *f, bool is_output)
{
	struct aml_video_fmt *fmt;
	int fmt_size = sizeof(aml_video_formats) / sizeof(struct aml_video_fmt);
	int i = 0, j = 0;

	for (; i < fmt_size; i++) {
		fmt = &aml_video_formats[i];
		if (is_output && fmt->type != AML_FMT_DEC)
			continue;
		if (!is_output && fmt->type != AML_FMT_FRAME)
			continue;

		if (j == f->index) {
			f->pixelformat = fmt->fourcc;
			strscpy(f->description, fmt->name,
				sizeof(f->description));
			if (strlen(fmt->name) >= sizeof(f->description))
				f->description[sizeof(f->description) - 1] = '\0';
			return 0;
		}
		++j;
	}
	return -EINVAL;
}

static struct aml_q_data *aml_vdec_get_qdata_by_type(struct aml_vdec_ctx *ctx,
						     enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->q_data[AML_Q_DATA_SRC];

	return &ctx->q_data[AML_Q_DATA_DST];
}

static struct aml_video_fmt *aml_vdec_get_video_fmt(u32 format)
{
	struct aml_video_fmt *fmt;
	unsigned int k;

	for (k = 0; k < (sizeof(aml_video_formats) / sizeof(struct aml_video_fmt)); k++) {
		fmt = &aml_video_formats[k];
		if (fmt->fourcc == format)
			return fmt;
	}

	return NULL;
}

static int aml_vdec_init_dec_inst(struct aml_vdec_ctx *ctx,
				  struct aml_video_fmt *fmt_out)
{
	struct aml_vdec_dev *dev = ctx->dev;
	int ret = -1;

	if (!fmt_out)
		return ret;

	if (fmt_out->codec_type == CODEC_TYPE_FRAME) {
		dev_dbg(&dev->plat_dev->dev, "capture type no need to set\n");
		return 0;
	}

	ctx->codec_ops = &dev->pvdec_data->codec_ops[fmt_out->codec_type];
	if (ctx->codec_ops->init) {
		ret = ctx->codec_ops->init(ctx);
		if (ret < 0)
			return ret;
	}
	ctx->curr_dec_type = fmt_out->codec_type;
	dev_info(&dev->plat_dev->dev, "%s set curr_dec_type = %d\n", __func__, ctx->curr_dec_type);

	return ret;
}

static void set_pic_info(struct aml_vdec_ctx *ctx,
			 struct v4l2_pix_format_mplane *pix_mp,
			 enum v4l2_buf_type type)
{
	struct aml_q_data *q_data;

	q_data = aml_vdec_get_qdata_by_type(ctx, type);

	ctx->pic_info.colorspace = pix_mp->colorspace;
	ctx->pic_info.ycbcr_enc = pix_mp->ycbcr_enc;
	ctx->pic_info.quantization = pix_mp->quantization;
	ctx->pic_info.xfer_func = pix_mp->xfer_func;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		q_data->sizeimage[0] = pix_mp->plane_fmt[0].sizeimage;
		ctx->pic_info.output_pix_fmt = pix_mp->pixelformat;
		ctx->pic_info.coded_width = ALIGN(pix_mp->width, 64);
		ctx->pic_info.coded_height = ALIGN(pix_mp->height, 64);
		ctx->pic_info.fb_size[0] =
		    ctx->pic_info.coded_width * ctx->pic_info.coded_height;
		ctx->pic_info.fb_size[1] = ctx->pic_info.fb_size[0] / 2;
		ctx->pic_info.plane_num = 1;
	} else {
		ctx->pic_info.plane_num = q_data->fmt->num_planes;
		ctx->pic_info.cap_pix_fmt = pix_mp->pixelformat;
		q_data->coded_width = ctx->pic_info.coded_width;
		q_data->coded_height = ctx->pic_info.coded_height;
		q_data->sizeimage[0] = ctx->pic_info.fb_size[0];
		q_data->bytesperline[0] = ctx->pic_info.coded_width;
		if (q_data->fmt->num_planes > 1) {
			q_data->sizeimage[1] = ctx->pic_info.fb_size[1];
			q_data->bytesperline[1] = ctx->pic_info.coded_width;
		} else {
			q_data->sizeimage[0] += ctx->pic_info.fb_size[1];
		}
	}
}

static int vidioc_vdec_enum_framesizes(struct file *file, void *priv,
				       struct v4l2_frmsizeenum *fsize)
{
	struct aml_video_fmt *fmt;
	struct aml_vdec_dev *dev = video_drvdata(file);
	u32 max_h, max_w;

	if (fsize->index != 0)
		return -EINVAL;

	max_h = dev->pvdec_data->dec_fmt->max_height;
	max_w = dev->pvdec_data->dec_fmt->max_width;

	fmt = aml_vdec_get_video_fmt(fsize->pixel_format);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = fmt->stepwise;
	fsize->stepwise.max_height = max_h;
	fsize->stepwise.max_width = max_w;

	return 0;
}

static int vdec_try_fmt_mp(struct aml_vdec_ctx *ctx, struct v4l2_format *f,
			   const struct aml_video_fmt *fmt_mp)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct aml_q_data *q_data;
	struct aml_vdec_dev *dev = ctx->dev;
	u32 max_h, max_w;
	int i;

	max_h = dev->pvdec_data->dec_fmt->max_height;
	max_w = dev->pvdec_data->dec_fmt->max_width;

	pix_mp->field = V4L2_FIELD_NONE;
	q_data = aml_vdec_get_qdata_by_type(ctx, f->type);

	pix_mp->height = clamp(pix_mp->height, AML_VDEC_MIN_H, max_h);
	pix_mp->width = clamp(pix_mp->width, AML_VDEC_MIN_H, max_w);

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		pix_mp->num_planes = q_data->fmt->num_planes;
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->plane_fmt[0].bytesperline = q_data->bytesperline[0];
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage[0];
	} else {
		v4l2_fill_pixfmt_mp(pix_mp, fmt_mp->fourcc, pix_mp->width,
				    pix_mp->height);
	}

	for (i = 0; i < pix_mp->num_planes; i++)
		memset(&pix_mp->plane_fmt[i].reserved[0], 0x0,
		       sizeof(pix_mp->plane_fmt[0].reserved));

	memset(pix_mp->reserved, 0x0, sizeof(pix_mp->reserved));
	pix_mp->flags = 0;

	return 0;
}

static int vdec_s_fmt(struct aml_vdec_ctx *ctx, struct v4l2_format *f)
{
	struct aml_q_data *q_data;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct aml_video_fmt *fmt =
	    aml_vdec_get_video_fmt(f->fmt.pix_mp.pixelformat);

	q_data = aml_vdec_get_qdata_by_type(ctx, f->type);

	if (fmt)	/* default fmt was set in fopen */
		q_data->fmt = fmt;

	vdec_try_fmt_mp(ctx, f, q_data->fmt);
	set_pic_info(ctx, pix_mp, f->type);

	return 0;
}

static int vdec_g_fmt(struct aml_vdec_ctx *ctx, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct aml_q_data *q_data;

	q_data = aml_vdec_get_qdata_by_type(ctx, f->type);

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->colorspace = ctx->pic_info.colorspace;
	pix_mp->ycbcr_enc = ctx->pic_info.ycbcr_enc;
	pix_mp->quantization = ctx->pic_info.quantization;
	pix_mp->xfer_func = ctx->pic_info.xfer_func;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		pix_mp->height = q_data->coded_height;
		pix_mp->width = q_data->coded_width;
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->num_planes = q_data->fmt->num_planes;
		pix_mp->plane_fmt[0].bytesperline = q_data->bytesperline[0];
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage[0];
	} else {
		if (ctx->pic_info.coded_width != 0 && ctx->pic_info.coded_height != 0) {
			pix_mp->width = ctx->pic_info.coded_width;
			pix_mp->height = ctx->pic_info.coded_height;
		} else {
			pix_mp->height = q_data->coded_height;
			pix_mp->width = q_data->coded_height;
		}
		v4l2_fill_pixfmt_mp(pix_mp, q_data->fmt->fourcc, pix_mp->width,
				    pix_mp->height);
	}

	return 0;
}

static int vidioc_try_fmt_cap_mplane(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct aml_vdec_ctx *ctx = fh_to_dec_ctx(file);
	const struct aml_video_fmt *fmt_mp;
	struct aml_q_data *q_data;

	q_data = aml_vdec_get_qdata_by_type(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	fmt_mp = aml_vdec_get_video_fmt(f->fmt.pix_mp.pixelformat);
	if (!fmt_mp)
		fmt_mp = q_data->fmt;

	return vdec_try_fmt_mp(ctx, f, fmt_mp);
}

static int vidioc_try_fmt_out_mplane(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct aml_vdec_ctx *ctx = fh_to_dec_ctx(file);
	const struct aml_video_fmt *fmt_mp;
	struct aml_q_data *q_data = aml_vdec_get_qdata_by_type(ctx,
							       V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	fmt_mp = aml_vdec_get_video_fmt(f->fmt.pix_mp.pixelformat);
	if (!fmt_mp)
		fmt_mp = q_data->fmt;

	return vdec_try_fmt_mp(ctx, f, fmt_mp);
}

static int vidioc_vdec_s_fmt_out_mplane(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct aml_vdec_ctx *ctx = fh_to_dec_ctx(file);

	return vdec_s_fmt(ctx, f);
}

static int vidioc_vdec_s_fmt_cap_mplane(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct aml_vdec_ctx *ctx = fh_to_dec_ctx(file);

	return vdec_s_fmt(ctx, f);
}

static int vidioc_vdec_g_fmt_out_mplane(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct aml_vdec_ctx *ctx = fh_to_dec_ctx(file);

	return vdec_g_fmt(ctx, f);
}

static int vidioc_vdec_g_fmt_cap_mplane(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct aml_vdec_ctx *ctx = fh_to_dec_ctx(file);

	return vdec_g_fmt(ctx, f);
}

static int vidioc_vdec_enum_fmt_out_mplane(struct file *file,
					   void *priv, struct v4l2_fmtdesc *f)
{
	return vidioc_vdec_enum_fmt(f, 1);
}

static int vidioc_vdec_enum_fmt_cap_mplane(struct file *file,
					   void *priv, struct v4l2_fmtdesc *f)
{
	return vidioc_vdec_enum_fmt(f, 0);
}

const struct v4l2_ioctl_ops aml_vdec_ioctl_ops = {
	.vidioc_querycap = vidioc_vdec_querycap,
	.vidioc_enum_framesizes = vidioc_vdec_enum_framesizes,

	.vidioc_enum_fmt_vid_cap = vidioc_vdec_enum_fmt_cap_mplane,
	.vidioc_try_fmt_vid_cap_mplane = vidioc_try_fmt_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = vidioc_vdec_s_fmt_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = vidioc_vdec_g_fmt_cap_mplane,

	.vidioc_enum_fmt_vid_out = vidioc_vdec_enum_fmt_out_mplane,
	.vidioc_try_fmt_vid_out_mplane = vidioc_try_fmt_out_mplane,
	.vidioc_s_fmt_vid_out_mplane = vidioc_vdec_s_fmt_out_mplane,
	.vidioc_g_fmt_vid_out_mplane = vidioc_vdec_g_fmt_out_mplane,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,

	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,

	.vidioc_decoder_cmd = v4l2_m2m_ioctl_stateless_decoder_cmd,
	.vidioc_try_decoder_cmd = v4l2_m2m_ioctl_stateless_try_decoder_cmd,

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,

	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
};

static void aml_vdec_release_instance(struct aml_vdec_ctx *ctx)
{
	if (ctx->codec_ops && ctx->codec_ops->exit)
		ctx->codec_ops->exit(ctx);
}

static int vb2ops_vdec_queue_setup(struct vb2_queue *vq,
				   unsigned int *nbuffers,
				   unsigned int *nplanes,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct aml_vdec_ctx *ctx = vb2_get_drv_priv(vq);
	struct aml_q_data *q_data;
	unsigned int i;

	q_data = aml_vdec_get_qdata_by_type(ctx, vq->type);
	if (!q_data) {
		dev_err(&ctx->dev->plat_dev->dev, "not supported vq type\n");
		return -EINVAL;
	}

	if (*nplanes) {
		if (*nplanes != q_data->fmt->num_planes)
			return -EINVAL;

		for (i = 0; i < *nplanes; i++) {
			if (sizes[i] < q_data->sizeimage[i]) {
				dev_err(&ctx->dev->plat_dev->dev, "not supported sizeimage\n");
				return -EINVAL;
			}
			alloc_devs[i] = &ctx->dev->plat_dev->dev;
		}
	} else {
		if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			*nplanes = q_data->fmt->num_planes;
		else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			*nplanes = 1;

		for (i = 0; i < *nplanes; i++) {
			alloc_devs[i] = &ctx->dev->plat_dev->dev;
			sizes[i] = q_data->sizeimage[i];
		}
	}

	if (*nplanes) {
		dev_dbg(&ctx->dev->plat_dev->dev, "type: %d, plane: %d, buf cnt: %d, size: [Y: %u, C: %u]\n",
			vq->type, *nplanes, *nbuffers, sizes[0], sizes[1]);
		return 0;
	}

	return -EINVAL;
}

static int vb2ops_vdec_buf_prepare(struct vb2_buffer *vb)
{
	struct aml_vdec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct aml_q_data *q_data;
	unsigned int sizeimage = 0;
	int i;

	q_data = aml_vdec_get_qdata_by_type(ctx, vb->type);
	if (!q_data)
		return -EINVAL;

	for (i = 0; i < q_data->fmt->num_planes; i++) {
		sizeimage = q_data->sizeimage[i];
		if (vb2_plane_size(vb, i) < sizeimage)
			return -EINVAL;

		if (V4L2_TYPE_IS_CAPTURE(vb->type))
			vb2_set_plane_payload(vb, i, q_data->sizeimage[i]);
	}

	return 0;
}

static int vb2_ops_vdec_buf_init(struct vb2_buffer *vb)
{
	return 0;
}

static void vb2_ops_vdec_buf_queue(struct vb2_buffer *vb)
{
	struct aml_vdec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2 = to_vb2_v4l2_buffer(vb);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vb2_v4l2);
}

static void vb2_ops_vdec_buf_finish(struct vb2_buffer *vb)
{
}

static int vb2ops_vdec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct aml_vdec_ctx *ctx = vb2_get_drv_priv(q);
	struct aml_q_data *q_data;

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		ctx->is_output_streamon = 1;
		q_data = aml_vdec_get_qdata_by_type(ctx, q->type);
		if (aml_vdec_init_dec_inst(ctx, q_data->fmt) < 0)
			return -EINVAL;
	} else {
		ctx->is_cap_streamon = 1;
	}

	return 0;
}

static void vb2ops_vdec_stop_streaming(struct vb2_queue *q)
{
	struct aml_vdec_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *src_buf = NULL, *dst_buf = NULL;

	aml_vdec_release_instance(ctx);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		while ((src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx)))
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		ctx->is_output_streamon = 0;
	} else {
		while ((dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx)))
			v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		ctx->is_cap_streamon = 0;
	}
}

static int vb2ops_vdec_out_buf_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static void vb2ops_vdec_buf_request_complete(struct vb2_buffer *vb)
{
	struct aml_vdec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->ctrl_handler);
}

static const struct vb2_ops aml_vdec_vb2_ops = {
	.queue_setup = vb2ops_vdec_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = vb2ops_vdec_start_streaming,
	.stop_streaming = vb2ops_vdec_stop_streaming,

	.buf_init = vb2_ops_vdec_buf_init,
	.buf_prepare = vb2ops_vdec_buf_prepare,
	.buf_out_validate = vb2ops_vdec_out_buf_validate,
	.buf_queue = vb2_ops_vdec_buf_queue,
	.buf_finish = vb2_ops_vdec_buf_finish,
	.buf_request_complete = vb2ops_vdec_buf_request_complete,
};

int aml_vdec_queue_init(void *priv, struct vb2_queue *src_vq,
			struct vb2_queue *dst_vq)
{
	struct aml_vdec_ctx *ctx = (struct aml_vdec_ctx *)priv;
	struct aml_vdec_dev *dev = ctx->dev;
	int ret = 0;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->drv_priv = ctx;
	src_vq->ops = &aml_vdec_vb2_ops;
	src_vq->lock = &ctx->v4l2_intf_lock;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->supports_requests = true;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	ret = vb2_queue_init(src_vq);
	if (ret) {
		v4l2_info(&dev->v4l2_dev,
			  "Failed to initialize videobuf2 queue(output)");
		return ret;
	}

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->ops = &aml_vdec_vb2_ops;
	dst_vq->lock = &ctx->v4l2_intf_lock;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	ret = vb2_queue_init(dst_vq);
	if (ret) {
		v4l2_info(&dev->v4l2_dev,
			  "Failed to initialize videobuf2 queue(capture)");
		vb2_queue_release(src_vq);
	}

	return ret;
}
