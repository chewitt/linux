/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip Image Enhancement Processor v2 (IEP2) driver
 *
 * Copyright (C) 2025 Christian Hewitt <christianshewitt@gmail.com>
 *
 */

#ifndef __IEP2_H__
#define __IEP2_H__

#include <linux/platform_device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#define IEP2_NAME "rockchip-iep2"

/* Hardware limits */
#define IEP2_MIN_WIDTH		320U
#define IEP2_MAX_WIDTH		1920U
#define IEP2_MIN_HEIGHT		240U
#define IEP2_MAX_HEIGHT		1088U

/* Hardware defaults */
#define IEP2_DEFAULT_WIDTH	320U
#define IEP2_DEFAULT_HEIGHT	240U

struct iep2_fmt {
	u32 fourcc;
	u8 depth;
	u8 uv_factor;
	u8 color_swap;
	u8 hw_format;
};

struct iep2_frm_fmt {
	struct iep2_fmt *hw_fmt;
	struct v4l2_pix_format pix;
	unsigned int y_stride;
	unsigned int uv_stride;
	unsigned int uv_hw_stride;
};

struct iep2_ctx {
	struct v4l2_fh fh;
	struct rockchip_iep2 *iep2;

	struct iep2_frm_fmt src_fmt;
	struct iep2_frm_fmt dst_fmt;

	struct vb2_v4l2_buffer *prev_src_buf;
	struct vb2_v4l2_buffer *dst0_buf;
	struct vb2_v4l2_buffer *dst1_buf;

	u32 dst_sequence;
	u32 src_sequence;

	/* bff = bottom field first */
	bool field_order_bff;
	bool field_bff;

	unsigned int dst_buffs_done;

	bool fmt_changed;
	bool job_abort;
};

struct rockchip_iep2 {
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev;
	struct video_device vfd;

	struct device *dev;

	void __iomem *regs;

	struct clk *aclk;
	struct clk *hclk;
	struct clk *sclk;

	/* DMA working buffers */
	dma_addr_t mv_dma;
	void *mv_buf;
	dma_addr_t md_dma;
	void *md_buf;

	/* vfd lock */
	struct mutex mutex;
};

static inline void iep2_write(struct rockchip_iep2 *iep2, u32 reg, u32 value)
{
	writel(value, iep2->regs + reg);
}

static inline u32 iep2_read(struct rockchip_iep2 *iep2, u32 reg)
{
	return readl(iep2->regs + reg);
}

#define IEP2_Y_STRIDE(w, h)		((w) * (h))
#define IEP2_UV_STRIDE(w, h, fac)	((w) * (h) + (w) * (h) / (fac))

#endif
