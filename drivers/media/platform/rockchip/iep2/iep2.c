// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Image Enhancement Processor v2 (IEP2) driver
 *
 * Copyright (C) 2025 Christian Hewitt <christianshewitt@gmail.com>
 *
 * Based on the Rockchip IEP driver by Alex Bee and the Rockchip vendor
 * kernel IEP2 driver.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/videodev2.h>

#include "iep2-regs.h"
#include "iep2.h"

static struct iep2_fmt formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.color_swap = IEP2_YUV_SWP_SP_UV,
		.hw_format = IEP2_COLOR_FMT_YUV420,
		.depth = 12,
		.uv_factor = 4,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.color_swap = IEP2_YUV_SWP_SP_VU,
		.hw_format = IEP2_COLOR_FMT_YUV420,
		.depth = 12,
		.uv_factor = 4,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.color_swap = IEP2_YUV_SWP_SP_UV,
		.hw_format = IEP2_COLOR_FMT_YUV422,
		.depth = 16,
		.uv_factor = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.color_swap = IEP2_YUV_SWP_SP_VU,
		.hw_format = IEP2_COLOR_FMT_YUV422,
		.depth = 16,
		.uv_factor = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV420,
		.color_swap = IEP2_YUV_SWP_P,
		.hw_format = IEP2_COLOR_FMT_YUV420,
		.depth = 12,
		.uv_factor = 4,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.color_swap = IEP2_YUV_SWP_P,
		.hw_format = IEP2_COLOR_FMT_YUV422,
		.depth = 16,
		.uv_factor = 2,
	},
};

static struct iep2_fmt *iep2_fmt_find(struct v4l2_pix_format *pix_fmt)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].fourcc == pix_fmt->pixelformat)
			return &formats[i];
	}

	return NULL;
}

static bool iep2_check_pix_format(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == pixelformat)
			return true;

	return false;
}

static struct vb2_v4l2_buffer *iep2_m2m_next_dst_buf(struct iep2_ctx *ctx)
{
	struct vb2_v4l2_buffer *dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	if (ctx->dst_sequence == 0 && dst_buf->sequence > 0)
		ctx->dst_sequence = dst_buf->sequence;

	dst_buf->sequence = ctx->dst_sequence++;

	return dst_buf;
}

static void iep2_m2m_dst_bufs_done(struct iep2_ctx *ctx,
				    enum vb2_buffer_state state)
{
	if (ctx->dst0_buf) {
		v4l2_m2m_buf_done(ctx->dst0_buf, state);
		ctx->dst_buffs_done++;
		ctx->dst0_buf = NULL;
	}

	if (ctx->dst1_buf) {
		v4l2_m2m_buf_done(ctx->dst1_buf, state);
		ctx->dst_buffs_done++;
		ctx->dst1_buf = NULL;
	}
}

static void iep2_setup_formats(struct iep2_ctx *ctx)
{
	struct rockchip_iep2 *iep2 = ctx->iep2;
	struct iep2_frm_fmt *src = &ctx->src_fmt;
	struct iep2_frm_fmt *dst = &ctx->dst_fmt;
	unsigned int width = src->pix.width;
	unsigned int height = src->pix.height;

	/* IEP_CONFIG0: packed format fields */
	iep2_write(iep2, IEP2_CONFIG0,
		   IEP2_CONFIG0_DEBUG_DATA_EN |
		   IEP2_CONFIG0_DST_YUV_SWAP(dst->hw_fmt->color_swap) |
		   IEP2_CONFIG0_DST_FMT(dst->hw_fmt->hw_format) |
		   IEP2_CONFIG0_SRC_YUV_SWAP(src->hw_fmt->color_swap) |
		   IEP2_CONFIG0_SRC_FMT(src->hw_fmt->hw_format));

	/* Work mode: must be set to IEP2 mode */
	iep2_write(iep2, IEP2_WORK_MODE, IEP2_WORK_MODE_IEP2);

	/* Image size: width-1 and height-1 */
	iep2_write(iep2, IEP2_SRC_IMG_SIZE,
		   IEP2_SRC_PIC_WIDTH(width - 1) |
		   IEP2_SRC_PIC_HEIGHT(height - 1));

	/* Strides: packed Y and UV stride in one register */
	iep2_write(iep2, IEP2_VIR_SRC_IMG_WIDTH,
		   IEP2_SRC_VIR_Y_STRIDE(src->pix.bytesperline / 4) |
		   IEP2_SRC_VIR_UV_STRIDE(src->uv_hw_stride));
	iep2_write(iep2, IEP2_VIR_DST_IMG_WIDTH,
		   IEP2_DST_VIR_STRIDE(dst->pix.bytesperline / 4));

	ctx->fmt_changed = false;
}

static void iep2_setup_defaults(struct rockchip_iep2 *iep2, u32 md_lambda)
{
	unsigned int i;
	u32 reg;

	/* Timeout config */
	iep2_write(iep2, IEP2_TIMEOUT_CFG, IEP2_TIMEOUT_CFG_EN | 0x3ffffff);

	/* Motion detection: theta, r, lambda packed */
	iep2_write(iep2, IEP2_MD_CONFIG0,
		   IEP2_MD_THETA(1) | IEP2_MD_R(6) | IEP2_MD_LAMBDA(md_lambda));

	/* Detection + OSD params packed */
	iep2_write(iep2, IEP2_DECT_CONFIG0,
		   IEP2_DECT_RESI_THR(30) |
		   IEP2_OSD_AREA_NUM(0) |
		   IEP2_OSD_GRADH_THR(60) |
		   IEP2_OSD_GRADV_THR(60));

	/* OSD limit config */
	iep2_write(iep2, IEP2_OSD_LIMIT_CONFIG, 0);
	iep2_write(iep2, IEP2_OSD_LIMIT_AREA(0), 0);
	iep2_write(iep2, IEP2_OSD_LIMIT_AREA(1), 0);

	/* OSD config: line_num + pec_thr packed */
	iep2_write(iep2, IEP2_OSD_CONFIG0,
		   IEP2_OSD_LINE_NUM(2) | IEP2_OSD_PEC_THR(20));

	/* OSD area config: all zero (disabled) */
	for (i = 0; i < 8; i++)
		iep2_write(iep2, IEP2_OSD_AREA_CONF(i), 0);

	/* Motion estimation: all packed into one register */
	iep2_write(iep2, IEP2_ME_CONFIG0,
		   IEP2_ME_PENA(5) | IEP2_MV_BONUS(10) |
		   IEP2_MV_SIMILAR_THR(4) | IEP2_MV_SIMILAR_NUM_THR0(4) |
		   IEP2_ME_THR_OFFSET(20));

	/* MV limits: negate left limit per vendor kernel */
	iep2_write(iep2, IEP2_ME_LIMIT_CONFIG,
		   IEP2_MV_LEFT_LIMIT((~28U + 1) & 0x3f) |
		   IEP2_MV_RIGHT_LIMIT(27));

	/* MV trust list: zero (disabled) */
	iep2_write(iep2, IEP2_MV_TRU_LIST(0), 0);
	iep2_write(iep2, IEP2_MV_TRU_LIST(1), 0);

	/* EEDI threshold */
	iep2_write(iep2, IEP2_EEDI_CONFIG0, IEP2_EEDI_THR0(12));

	/* Blend config */
	iep2_write(iep2, IEP2_BLE_CONFIG0, IEP2_BLE_BACKTOMA_NUM(1));

	/* Comb filter: packed into one register with OSD valid bits */
	reg = IEP2_COMB_CNT_THR(0) | IEP2_COMB_FEATURE_THR(16) |
	      IEP2_COMB_T_THR(4);
	for (i = 0; i < 8; i++)
		reg |= IEP2_COMB_OSD_VLD(i);
	iep2_write(iep2, IEP2_COMB_CONFIG0, reg);

	/* Motion noise table */
	for (i = 0; i < ARRAY_SIZE(iep2_default_mtn_tab); i++)
		iep2_write(iep2, IEP2_DIL_MTN_TAB(i), iep2_default_mtn_tab[i]);
}

static void iep2_device_run(void *priv)
{
	struct iep2_ctx *ctx = priv;
	struct rockchip_iep2 *iep2 = ctx->iep2;
	struct vb2_v4l2_buffer *src, *dst;
	unsigned int dil_mode;
	dma_addr_t addr;
	u32 dil_reg;
	u32 md_lambda = 4;

	if (ctx->fmt_changed)
		iep2_setup_formats(ctx);

	/* Select deinterlace mode */
	if (ctx->prev_src_buf)
		dil_mode = IEP2_DIL_MODE_I5O2;
	else
		dil_mode = ctx->field_bff ? IEP2_DIL_MODE_I1O1B
					  : IEP2_DIL_MODE_I1O1T;

	/* DIL_CONFIG0: mode, field order, output mode, and feature enables */
	dil_reg = IEP2_DIL_MV_HIST_EN |
		  IEP2_DIL_COMB_EN |
		  IEP2_DIL_BLE_EN |
		  IEP2_DIL_EEDI_EN |
		  IEP2_DIL_MEMC_EN |
		  IEP2_DIL_OSD_EN |
		  IEP2_DIL_PD_EN |
		  IEP2_DIL_FF_EN |
		  IEP2_DIL_FIELD_ORDER(ctx->field_bff ?
			IEP2_FIELD_ORDER_BFF : IEP2_FIELD_ORDER_TFF) |
		  IEP2_DIL_OUT_MODE(IEP2_OUT_MODE_LINE) |
		  IEP2_DIL_MODE(dil_mode);
	if (md_lambda < 8)
		dil_reg |= IEP2_DIL_MD_PRE_EN;
	iep2_write(iep2, IEP2_DIL_CONFIG0, dil_reg);

	/* Setup source buffer DMA addresses */
	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	addr = vb2_dma_contig_plane_dma_addr(&src->vb2_buf, 0);

	if (dil_mode == IEP2_DIL_MODE_I5O2) {
		dma_addr_t prev_addr;

		prev_addr = vb2_dma_contig_plane_dma_addr(
				&ctx->prev_src_buf->vb2_buf, 0);

		/* cur = previous frame */
		iep2_write(iep2, IEP2_SRC_ADDR_CURY, prev_addr);
		iep2_write(iep2, IEP2_SRC_ADDR_CURUV,
			   prev_addr + ctx->src_fmt.y_stride);
		iep2_write(iep2, IEP2_SRC_ADDR_CURV,
			   prev_addr + ctx->src_fmt.uv_stride);

		/* nxt = current frame */
		iep2_write(iep2, IEP2_SRC_ADDR_NXTY, addr);
		iep2_write(iep2, IEP2_SRC_ADDR_NXTUV,
			   addr + ctx->src_fmt.y_stride);
		iep2_write(iep2, IEP2_SRC_ADDR_NXTV,
			   addr + ctx->src_fmt.uv_stride);

		/* pre = previous frame */
		iep2_write(iep2, IEP2_SRC_ADDR_PREY, prev_addr);
		iep2_write(iep2, IEP2_SRC_ADDR_PREUV,
			   prev_addr + ctx->src_fmt.y_stride);
		iep2_write(iep2, IEP2_SRC_ADDR_PREV,
			   prev_addr + ctx->src_fmt.uv_stride);
	} else {
		/* I1O1T/I1O1B: current frame for both cur and nxt */
		iep2_write(iep2, IEP2_SRC_ADDR_CURY, addr);
		iep2_write(iep2, IEP2_SRC_ADDR_CURUV,
			   addr + ctx->src_fmt.y_stride);
		iep2_write(iep2, IEP2_SRC_ADDR_CURV,
			   addr + ctx->src_fmt.uv_stride);

		iep2_write(iep2, IEP2_SRC_ADDR_NXTY, addr);
		iep2_write(iep2, IEP2_SRC_ADDR_NXTUV,
			   addr + ctx->src_fmt.y_stride);
		iep2_write(iep2, IEP2_SRC_ADDR_NXTV,
			   addr + ctx->src_fmt.uv_stride);

		iep2_write(iep2, IEP2_SRC_ADDR_PREY, addr);
		iep2_write(iep2, IEP2_SRC_ADDR_PREUV,
			   addr + ctx->src_fmt.y_stride);
		iep2_write(iep2, IEP2_SRC_ADDR_PREV,
			   addr + ctx->src_fmt.uv_stride);
	}

	/* Working buffer DMA addresses (both src and dst sides) */
	iep2_write(iep2, IEP2_SRC_ADDR_MD, iep2->md_dma);
	iep2_write(iep2, IEP2_SRC_ADDR_MV, iep2->mv_dma);
	iep2_write(iep2, IEP2_DST_ADDR_MD, iep2->md_dma);
	iep2_write(iep2, IEP2_DST_ADDR_MV, iep2->mv_dma);

	/* Setup destination buffer DMA addresses */
	if (dil_mode == IEP2_DIL_MODE_I5O2) {
		/* dst0 = top field (from prev source) */
		dst = iep2_m2m_next_dst_buf(ctx);
		v4l2_m2m_buf_copy_metadata(ctx->prev_src_buf, dst);
		addr = vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0);

		iep2_write(iep2, IEP2_DST_ADDR_TOPY, addr);
		iep2_write(iep2, IEP2_DST_ADDR_TOPC,
			   addr + ctx->dst_fmt.y_stride);

		ctx->dst0_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		/* dst1 = bottom field (from current source) */
		dst = iep2_m2m_next_dst_buf(ctx);
		v4l2_m2m_buf_copy_metadata(src, dst);
		addr = vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0);

		iep2_write(iep2, IEP2_DST_ADDR_BOTY, addr);
		iep2_write(iep2, IEP2_DST_ADDR_BOTC,
			   addr + ctx->dst_fmt.y_stride);

		ctx->dst1_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	} else {
		/* I1O1: single output to top destination */
		dst = iep2_m2m_next_dst_buf(ctx);
		v4l2_m2m_buf_copy_metadata(src, dst);
		addr = vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0);

		iep2_write(iep2, IEP2_DST_ADDR_TOPY, addr);
		iep2_write(iep2, IEP2_DST_ADDR_TOPC,
			   addr + ctx->dst_fmt.y_stride);

		/* Write same addr to bot to avoid IOMMU faults */
		iep2_write(iep2, IEP2_DST_ADDR_BOTY, addr);
		iep2_write(iep2, IEP2_DST_ADDR_BOTC,
			   addr + ctx->dst_fmt.y_stride);

		ctx->dst0_buf = NULL;
		ctx->dst1_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	}

	/* Write default processing parameters */
	iep2_setup_defaults(iep2, md_lambda);

	/* Enable interrupts */
	iep2_write(iep2, IEP2_INT_EN,
		   IEP2_INT_FRM_DONE_EN |
		   IEP2_INT_OSD_MAX_EN |
		   IEP2_INT_BUS_ERROR_EN |
		   IEP2_INT_TIMEOUT_EN);

	/* Ensure all register writes are flushed */
	wmb();

	/* Start hardware processing */
	iep2_write(iep2, IEP2_FRM_START, 1);
}

static int iep2_job_ready(void *priv)
{
	struct iep2_ctx *ctx = priv;

	return v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx) >= 2 &&
	       v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) >= 1;
}

static void iep2_job_abort(void *priv)
{
	struct iep2_ctx *ctx = priv;

	ctx->job_abort = true;
}

static const struct v4l2_m2m_ops iep2_m2m_ops = {
	.device_run	= iep2_device_run,
	.job_ready	= iep2_job_ready,
	.job_abort	= iep2_job_abort,
};

static int iep2_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			    unsigned int *nplanes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct iep2_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt.pix;
	else
		pix_fmt = &ctx->dst_fmt.pix;

	if (*nplanes) {
		if (sizes[0] < pix_fmt->sizeimage)
			return -EINVAL;
	} else {
		sizes[0] = pix_fmt->sizeimage;
		*nplanes = 1;
	}

	return 0;
}

static int iep2_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct iep2_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt.pix;
	else
		pix_fmt = &ctx->dst_fmt.pix;

	if (vb2_plane_size(vb, 0) < pix_fmt->sizeimage)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, pix_fmt->sizeimage);

	return 0;
}

static void iep2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct iep2_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void iep2_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct iep2_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	do {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (vbuf)
			v4l2_m2m_buf_done(vbuf, state);
	} while (vbuf);

	if (V4L2_TYPE_IS_OUTPUT(vq->type) && ctx->prev_src_buf)
		v4l2_m2m_buf_done(ctx->prev_src_buf, state);
	else
		iep2_m2m_dst_bufs_done(ctx, state);
}

static int iep2_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct iep2_ctx *ctx = vb2_get_drv_priv(vq);
	struct device *dev = ctx->iep2->dev;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			dev_err(dev, "Failed to enable module\n");
			goto err_runtime_get;
		}

		ctx->field_order_bff =
			ctx->src_fmt.pix.field == V4L2_FIELD_INTERLACED_BT;
		ctx->field_bff = ctx->field_order_bff;

		ctx->src_sequence = 0;
		ctx->dst_sequence = 0;

		ctx->prev_src_buf = NULL;

		ctx->dst0_buf = NULL;
		ctx->dst1_buf = NULL;
		ctx->dst_buffs_done = 0;

		ctx->job_abort = false;
	}

	return 0;

err_runtime_get:
	iep2_queue_cleanup(vq, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void iep2_stop_streaming(struct vb2_queue *vq)
{
	struct iep2_ctx *ctx = vb2_get_drv_priv(vq);

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		pm_runtime_mark_last_busy(ctx->iep2->dev);
		pm_runtime_put_autosuspend(ctx->iep2->dev);
	}

	iep2_queue_cleanup(vq, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops iep2_qops = {
	.queue_setup		= iep2_queue_setup,
	.buf_prepare		= iep2_buf_prepare,
	.buf_queue		= iep2_buf_queue,
	.start_streaming	= iep2_start_streaming,
	.stop_streaming		= iep2_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int iep2_queue_init(void *priv, struct vb2_queue *src_vq,
			   struct vb2_queue *dst_vq)
{
	struct iep2_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->min_queued_buffers = 1;
	src_vq->ops = &iep2_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->iep2->mutex;
	src_vq->dev = ctx->iep2->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->min_queued_buffers = 2;
	dst_vq->ops = &iep2_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->iep2->mutex;
	dst_vq->dev = ctx->iep2->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static void iep2_prepare_format(struct v4l2_pix_format *pix_fmt)
{
	unsigned int width, height;
	struct iep2_fmt *hw_fmt;

	hw_fmt = iep2_fmt_find(pix_fmt);
	if (!hw_fmt) {
		hw_fmt = &formats[0];
		pix_fmt->pixelformat = hw_fmt->fourcc;
	}

	width = ALIGN(clamp(pix_fmt->width, IEP2_MIN_WIDTH,
			    IEP2_MAX_WIDTH), 16);
	height = ALIGN(clamp(pix_fmt->height, IEP2_MIN_HEIGHT,
			     IEP2_MAX_HEIGHT), 16);

	pix_fmt->width = width;
	pix_fmt->height = height;
	pix_fmt->bytesperline = width;
	pix_fmt->sizeimage = height * (width * hw_fmt->depth) >> 3;
}

static int iep2_open(struct file *file)
{
	struct rockchip_iep2 *iep2 = video_drvdata(file);
	struct iep2_ctx *ctx;
	int ret;

	if (mutex_lock_interruptible(&iep2->mutex))
		return -ERESTARTSYS;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		mutex_unlock(&iep2->mutex);
		return -ENOMEM;
	}

	/* default output format */
	ctx->src_fmt.pix.pixelformat = formats[0].fourcc;
	ctx->src_fmt.pix.field = V4L2_FIELD_INTERLACED;
	ctx->src_fmt.pix.width = IEP2_DEFAULT_WIDTH;
	ctx->src_fmt.pix.height = IEP2_DEFAULT_HEIGHT;
	iep2_prepare_format(&ctx->src_fmt.pix);
	ctx->src_fmt.hw_fmt = &formats[0];
	ctx->src_fmt.y_stride = IEP2_Y_STRIDE(ctx->src_fmt.pix.width,
					       ctx->src_fmt.pix.height);
	ctx->src_fmt.uv_stride = IEP2_UV_STRIDE(ctx->src_fmt.pix.width,
						 ctx->src_fmt.pix.height,
						 ctx->src_fmt.hw_fmt->uv_factor);
	ctx->src_fmt.uv_hw_stride =
		(ctx->src_fmt.hw_fmt->color_swap == IEP2_YUV_SWP_P) ?
		((ctx->src_fmt.pix.bytesperline / 2 + 15) / 16 * 16) / 4 :
		ctx->src_fmt.pix.bytesperline / 4;

	/* default capture format */
	ctx->dst_fmt.pix.pixelformat = formats[0].fourcc;
	ctx->dst_fmt.pix.field = V4L2_FIELD_NONE;
	ctx->dst_fmt.pix.width = IEP2_DEFAULT_WIDTH;
	ctx->dst_fmt.pix.height = IEP2_DEFAULT_HEIGHT;
	iep2_prepare_format(&ctx->dst_fmt.pix);
	ctx->dst_fmt.hw_fmt = &formats[0];
	ctx->dst_fmt.y_stride = IEP2_Y_STRIDE(ctx->dst_fmt.pix.width,
					       ctx->dst_fmt.pix.height);
	ctx->dst_fmt.uv_stride = IEP2_UV_STRIDE(ctx->dst_fmt.pix.width,
						 ctx->dst_fmt.pix.height,
						 ctx->dst_fmt.hw_fmt->uv_factor);
	ctx->dst_fmt.uv_hw_stride = ctx->dst_fmt.pix.bytesperline / 4;

	ctx->fmt_changed = true;

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->iep2 = iep2;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(iep2->m2m_dev, ctx,
					     &iep2_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_free;
	}

	v4l2_fh_add(&ctx->fh, file);

	mutex_unlock(&iep2->mutex);

	return 0;

err_free:
	kfree(ctx);
	mutex_unlock(&iep2->mutex);

	return ret;
}

static int iep2_release(struct file *file)
{
	struct rockchip_iep2 *iep2 = video_drvdata(file);
	struct iep2_ctx *ctx = container_of(file->private_data,
					    struct iep2_ctx, fh);

	mutex_lock(&iep2->mutex);

	v4l2_fh_del(&ctx->fh, file);
	v4l2_fh_exit(&ctx->fh);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	kfree(ctx);

	mutex_unlock(&iep2->mutex);
	return 0;
}

static const struct v4l2_file_operations iep2_fops = {
	.owner		= THIS_MODULE,
	.open		= iep2_open,
	.release	= iep2_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static int iep2_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	strscpy(cap->driver, IEP2_NAME, sizeof(cap->driver));
	strscpy(cap->card, IEP2_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", IEP2_NAME);

	return 0;
}

static int iep2_enum_fmt(struct file *file, void *priv,
			 struct v4l2_fmtdesc *f)
{
	if (f->index < ARRAY_SIZE(formats)) {
		f->pixelformat = formats[f->index].fourcc;
		return 0;
	}

	return -EINVAL;
}

static int iep2_enum_framesizes(struct file *file, void *priv,
				struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index != 0)
		return -EINVAL;

	if (!iep2_check_pix_format(fsize->pixel_format))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	fsize->stepwise.min_width = IEP2_MIN_WIDTH;
	fsize->stepwise.max_width = IEP2_MAX_WIDTH;
	fsize->stepwise.step_width = 16;

	fsize->stepwise.min_height = IEP2_MIN_HEIGHT;
	fsize->stepwise.max_height = IEP2_MAX_HEIGHT;
	fsize->stepwise.step_height = 16;

	return 0;
}

static inline struct iep2_ctx *iep2_file2ctx(struct file *file)
{
	return container_of(file->private_data, struct iep2_ctx, fh);
}

static int iep2_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct iep2_ctx *ctx = iep2_file2ctx(file);

	f->fmt.pix = ctx->dst_fmt.pix;
	return 0;
}

static int iep2_g_fmt_vid_out(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct iep2_ctx *ctx = iep2_file2ctx(file);

	f->fmt.pix = ctx->src_fmt.pix;
	return 0;
}

static int iep2_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	f->fmt.pix.field = V4L2_FIELD_NONE;
	iep2_prepare_format(&f->fmt.pix);
	return 0;
}

static int iep2_try_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	if (f->fmt.pix.field != V4L2_FIELD_INTERLACED_TB &&
	    f->fmt.pix.field != V4L2_FIELD_INTERLACED_BT &&
	    f->fmt.pix.field != V4L2_FIELD_INTERLACED)
		f->fmt.pix.field = V4L2_FIELD_INTERLACED;

	iep2_prepare_format(&f->fmt.pix);
	return 0;
}

static int iep2_s_fmt_vid_out(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct iep2_ctx *ctx = iep2_file2ctx(file);
	struct vb2_queue *vq;
	int ret;

	ret = iep2_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	ctx->src_fmt.pix = f->fmt.pix;
	ctx->src_fmt.hw_fmt = iep2_fmt_find(&f->fmt.pix);
	ctx->src_fmt.y_stride = IEP2_Y_STRIDE(f->fmt.pix.width,
					       f->fmt.pix.height);
	ctx->src_fmt.uv_stride = IEP2_UV_STRIDE(f->fmt.pix.width,
						 f->fmt.pix.height,
						 ctx->src_fmt.hw_fmt->uv_factor);
	ctx->src_fmt.uv_hw_stride =
		(ctx->src_fmt.hw_fmt->color_swap == IEP2_YUV_SWP_P) ?
		((f->fmt.pix.bytesperline / 2 + 15) / 16 * 16) / 4 :
		f->fmt.pix.bytesperline / 4;

	/* Propagate colorspace information to capture */
	ctx->dst_fmt.pix.colorspace = f->fmt.pix.colorspace;
	ctx->dst_fmt.pix.xfer_func = f->fmt.pix.xfer_func;
	ctx->dst_fmt.pix.ycbcr_enc = f->fmt.pix.ycbcr_enc;
	ctx->dst_fmt.pix.quantization = f->fmt.pix.quantization;

	/* scaling is not supported */
	ctx->dst_fmt.pix.width = f->fmt.pix.width;
	ctx->dst_fmt.pix.height = f->fmt.pix.height;
	ctx->dst_fmt.y_stride = IEP2_Y_STRIDE(f->fmt.pix.width,
					       f->fmt.pix.height);
	ctx->dst_fmt.uv_stride = IEP2_UV_STRIDE(f->fmt.pix.width,
						 f->fmt.pix.height,
						 ctx->dst_fmt.hw_fmt->uv_factor);
	ctx->dst_fmt.uv_hw_stride = f->fmt.pix.bytesperline / 4;
	ctx->fmt_changed = true;

	return 0;
}

static int iep2_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct iep2_ctx *ctx = iep2_file2ctx(file);
	struct vb2_queue *vq;
	int ret;

	ret = iep2_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	/* scaling is not supported */
	f->fmt.pix.width = ctx->src_fmt.pix.width;
	f->fmt.pix.height = ctx->src_fmt.pix.height;

	ctx->dst_fmt.pix = f->fmt.pix;
	ctx->dst_fmt.hw_fmt = iep2_fmt_find(&f->fmt.pix);

	ctx->dst_fmt.y_stride = IEP2_Y_STRIDE(f->fmt.pix.width,
					       f->fmt.pix.height);
	ctx->dst_fmt.uv_stride = IEP2_UV_STRIDE(f->fmt.pix.width,
						 f->fmt.pix.height,
						 ctx->dst_fmt.hw_fmt->uv_factor);
	ctx->dst_fmt.uv_hw_stride = f->fmt.pix.bytesperline / 4;
	ctx->fmt_changed = true;

	return 0;
}

static const struct v4l2_ioctl_ops iep2_ioctl_ops = {
	.vidioc_querycap		= iep2_querycap,

	.vidioc_enum_framesizes		= iep2_enum_framesizes,

	.vidioc_enum_fmt_vid_cap	= iep2_enum_fmt,
	.vidioc_g_fmt_vid_cap		= iep2_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= iep2_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= iep2_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out	= iep2_enum_fmt,
	.vidioc_g_fmt_vid_out		= iep2_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= iep2_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= iep2_s_fmt_vid_out,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,
};

static const struct video_device iep2_video_device = {
	.name		= IEP2_NAME,
	.vfl_dir	= VFL_DIR_M2M,
	.fops		= &iep2_fops,
	.ioctl_ops	= &iep2_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
	.device_caps	= V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
};

static irqreturn_t iep2_isr(int irq, void *prv)
{
	struct rockchip_iep2 *iep2 = prv;
	struct iep2_ctx *ctx;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	u32 irq_status;

	/* Read and clear interrupt status */
	irq_status = iep2_read(iep2, IEP2_INT_STS);
	if (!(irq_status & IEP2_INT_STS_VALID))
		return IRQ_NONE;

	/* Clear all interrupts */
	iep2_write(iep2, IEP2_INT_CLR, 0xffffffff);

	ctx = v4l2_m2m_get_curr_priv(iep2->m2m_dev);
	if (!ctx) {
		v4l2_err(&iep2->v4l2_dev,
			 "Instance released before the end of transaction\n");
		return IRQ_HANDLED;
	}

	/* Check for errors */
	if (irq_status & (IEP2_INT_STS_BUS_ERROR | IEP2_INT_STS_TIMEOUT)) {
		state = VB2_BUF_STATE_ERROR;
		ctx->job_abort = true;
	}

	/* Return completed destination buffers */
	iep2_m2m_dst_bufs_done(ctx, state);

	/* Toggle field for next pass */
	ctx->field_bff = (ctx->dst_buffs_done % 2 == 0)
		     ? ctx->field_order_bff : !ctx->field_order_bff;

	if (ctx->dst_buffs_done == 2 || ctx->job_abort) {
		/* Both fields done: promote src buf and finish job */
		if (ctx->prev_src_buf)
			v4l2_m2m_buf_done(ctx->prev_src_buf, state);

		ctx->prev_src_buf =
			v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);

		v4l2_m2m_job_finish(iep2->m2m_dev, ctx->fh.m2m_ctx);
		ctx->dst_buffs_done = 0;
	} else {
		/* First field done, re-run for second field */
		iep2_device_run(ctx);
	}

	return IRQ_HANDLED;
}

static int iep2_probe(struct platform_device *pdev)
{
	struct rockchip_iep2 *iep2;
	struct video_device *vfd;
	int ret, irq;

	if (!pdev->dev.of_node)
		return -ENODEV;

	iep2 = devm_kzalloc(&pdev->dev, sizeof(*iep2), GFP_KERNEL);
	if (!iep2)
		return -ENOMEM;

	platform_set_drvdata(pdev, iep2);
	iep2->dev = &pdev->dev;
	iep2->vfd = iep2_video_device;

	iep2->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(iep2->regs))
		return PTR_ERR(iep2->regs);

	iep2->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(iep2->aclk)) {
		dev_err(&pdev->dev, "failed to get aclk\n");
		return PTR_ERR(iep2->aclk);
	}

	iep2->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(iep2->hclk)) {
		dev_err(&pdev->dev, "failed to get hclk\n");
		return PTR_ERR(iep2->hclk);
	}

	iep2->sclk = devm_clk_get(&pdev->dev, "sclk");
	if (IS_ERR(iep2->sclk)) {
		dev_err(&pdev->dev, "failed to get sclk\n");
		return PTR_ERR(iep2->sclk);
	}

	/* Allocate DMA working buffers */
	iep2->mv_buf = dma_alloc_coherent(&pdev->dev, IEP2_MV_BUF_SIZE,
					   &iep2->mv_dma, GFP_KERNEL);
	if (!iep2->mv_buf) {
		dev_err(&pdev->dev, "failed to allocate MV buffer\n");
		return -ENOMEM;
	}

	iep2->md_buf = dma_alloc_coherent(&pdev->dev, IEP2_MD_BUF_SIZE,
					   &iep2->md_dma, GFP_KERNEL);
	if (!iep2->md_buf) {
		dev_err(&pdev->dev, "failed to allocate MD buffer\n");
		ret = -ENOMEM;
		goto err_free_mv;
	}

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Could not set DMA coherent mask\n");
		goto err_free_md;
	}

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_free_md;
	}

	ret = devm_request_irq(iep2->dev, irq, iep2_isr, IRQF_SHARED,
			       dev_name(iep2->dev), iep2);
	if (ret < 0) {
		dev_err(iep2->dev, "failed to request irq\n");
		goto err_free_md;
	}

	mutex_init(&iep2->mutex);

	ret = v4l2_device_register(&pdev->dev, &iep2->v4l2_dev);
	if (ret) {
		dev_err(iep2->dev, "Failed to register V4L2 device\n");
		goto err_free_md;
	}

	vfd = &iep2->vfd;
	vfd->lock = &iep2->mutex;
	vfd->v4l2_dev = &iep2->v4l2_dev;

	snprintf(vfd->name, sizeof(vfd->name), "%s",
		 iep2_video_device.name);

	video_set_drvdata(vfd, iep2);

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 0);
	if (ret) {
		v4l2_err(&iep2->v4l2_dev,
			 "Failed to register video device\n");
		goto err_v4l2;
	}

	v4l2_info(&iep2->v4l2_dev,
		  "Device %s registered as /dev/video%d\n",
		  vfd->name, vfd->num);

	iep2->m2m_dev = v4l2_m2m_init(&iep2_m2m_ops);
	if (IS_ERR(iep2->m2m_dev)) {
		v4l2_err(&iep2->v4l2_dev,
			 "Failed to initialize V4L2 M2M device\n");
		ret = PTR_ERR(iep2->m2m_dev);
		goto err_video;
	}

	pm_runtime_set_autosuspend_delay(iep2->dev, 100);
	pm_runtime_use_autosuspend(iep2->dev);
	pm_runtime_enable(iep2->dev);

	return 0;

err_video:
	video_unregister_device(&iep2->vfd);
err_v4l2:
	v4l2_device_unregister(&iep2->v4l2_dev);
err_free_md:
	dma_free_coherent(&pdev->dev, IEP2_MD_BUF_SIZE,
			  iep2->md_buf, iep2->md_dma);
err_free_mv:
	dma_free_coherent(&pdev->dev, IEP2_MV_BUF_SIZE,
			  iep2->mv_buf, iep2->mv_dma);

	return ret;
}

static void iep2_remove(struct platform_device *pdev)
{
	struct rockchip_iep2 *iep2 = platform_get_drvdata(pdev);

	pm_runtime_dont_use_autosuspend(iep2->dev);
	pm_runtime_disable(iep2->dev);

	v4l2_m2m_release(iep2->m2m_dev);
	video_unregister_device(&iep2->vfd);
	v4l2_device_unregister(&iep2->v4l2_dev);

	dma_free_coherent(&pdev->dev, IEP2_MD_BUF_SIZE,
			  iep2->md_buf, iep2->md_dma);
	dma_free_coherent(&pdev->dev, IEP2_MV_BUF_SIZE,
			  iep2->mv_buf, iep2->mv_dma);
}

static int __maybe_unused iep2_runtime_suspend(struct device *dev)
{
	struct rockchip_iep2 *iep2 = dev_get_drvdata(dev);

	clk_disable_unprepare(iep2->sclk);
	clk_disable_unprepare(iep2->hclk);
	clk_disable_unprepare(iep2->aclk);

	return 0;
}

static int __maybe_unused iep2_runtime_resume(struct device *dev)
{
	struct rockchip_iep2 *iep2 = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(iep2->aclk);
	if (ret) {
		dev_err(iep2->dev, "Cannot enable aclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(iep2->hclk);
	if (ret) {
		dev_err(iep2->dev, "Cannot enable hclk: %d\n", ret);
		goto err_disable_aclk;
	}

	ret = clk_prepare_enable(iep2->sclk);
	if (ret) {
		dev_err(iep2->dev, "Cannot enable sclk: %d\n", ret);
		goto err_disable_hclk;
	}

	return 0;

err_disable_hclk:
	clk_disable_unprepare(iep2->hclk);
err_disable_aclk:
	clk_disable_unprepare(iep2->aclk);
	return ret;
}

static const struct dev_pm_ops iep2_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(iep2_runtime_suspend,
			   iep2_runtime_resume, NULL)
};

static const struct of_device_id rockchip_iep2_match[] = {
	{
		.compatible = "rockchip,rk3588-iep2",
	},
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_iep2_match);

static struct platform_driver iep2_pdrv = {
	.probe = iep2_probe,
	.remove = iep2_remove,
	.driver = {
		.name = IEP2_NAME,
		.pm = &iep2_pm_ops,
		.of_match_table = rockchip_iep2_match,
	},
};

module_platform_driver(iep2_pdrv);

MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com>");
MODULE_DESCRIPTION("Rockchip Image Enhancement Processor v2");
MODULE_LICENSE("GPL v2");
