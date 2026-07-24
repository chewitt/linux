/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Amlogic GX SoC Video Deinterlacer (DI) driver
 *
 * Copyright (C) 2026 Christian Hewitt <christianshewitt@gmail.com>
 */

#ifndef __MESON_DI_H
#define __MESON_DI_H

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>

#define MESON_DI_NAME		"meson-di"

/*
 * The DI register block is a sub-window of the shared VPU register space
 * (VCBUS). The vendor driver addresses registers by their VCBUS word offset;
 * the mapping obtained from the "reg" resource starts at MESON_DI_REG_BASE so
 * register accessors rebase the word offset accordingly.
 */
#define MESON_DI_REG_BASE	0x1700

struct meson_canvas;

/**
 * struct meson_di_fmt - description of a supported pixel format
 * @fourcc:	V4L2 pixel format FOURCC
 * @depth:	bits per pixel including chroma (12 for NV12)
 */
struct meson_di_fmt {
	u32 fourcc;
	u8 depth;
};

/**
 * struct meson_di_match_data - per-SoC quirks
 * @hw_version:	DI hardware generation (matches the vendor "hw-version")
 */
struct meson_di_match_data {
	unsigned int hw_version;
};

/*
 * Canvas indices used by the DI pipeline. The deinterlacer MIFs address
 * buffers through the canvas provider rather than raw DMA addresses. NV12
 * buffers need a canvas each for the luma and chroma planes.
 */
enum {
	MESON_DI_CANVAS_SRC_Y,	/* interlaced source, luma */
	MESON_DI_CANVAS_SRC_C,	/* interlaced source, chroma */
	MESON_DI_CANVAS_NR_Y,	/* noise-reduction working buffer, luma */
	MESON_DI_CANVAS_NR_C,	/* noise-reduction working buffer, chroma */
	MESON_DI_CANVAS_MTN,	/* motion-information buffer */
	MESON_DI_CANVAS_CONT,	/* motion-engine contour feedback buffer */
	MESON_DI_CANVAS_OUT_Y,	/* progressive output, luma */
	MESON_DI_CANVAS_OUT_C,	/* progressive output, chroma */
	MESON_DI_CANVAS_NUM,
};

/*
 * Debug: bound how far the hardware is programmed, to bisect a SoC hang over
 * a serial console. Each higher value enables one more group of register
 * writes; the default (FULL) programs everything. The two writes into
 * display-shared registers land on their own stages: VIU_MISC_CTRL0 at MUX
 * and the VD1_IF0 video-plane MIF at POST_CFG.
 */
enum meson_di_stage {
	MESON_DI_STAGE_NONE,		/* no hardware register writes at all */
	MESON_DI_STAGE_INIT,		/* + clock-gate/arbiter/FIFO (hw_init) */
	MESON_DI_STAGE_SETUP,		/* + size/blend/interrupt setup */
	MESON_DI_STAGE_MUX,		/* + VIU_MISC_CTRL0 display mux */
	MESON_DI_STAGE_PRE_CFG,		/* + pre-stage MIF config */
	MESON_DI_STAGE_PRE,		/* + pre-stage trigger */
	MESON_DI_STAGE_POST_CFG,	/* + post-stage MIF config (VD1_IF0) */
	MESON_DI_STAGE_FULL,		/* + post-stage trigger (normal) */
};

/* State of the hardware pipeline for the currently running job. */
enum meson_di_phase {
	MESON_DI_PHASE_IDLE,
	MESON_DI_PHASE_PRE,	/* pre stage running (motion detection) */
	MESON_DI_PHASE_POST0,	/* post stage running, first output field */
	MESON_DI_PHASE_POST1,	/* post stage running, second output field */
};

/**
 * struct meson_di_ctx - per-open deinterlacer context
 * @fh:		V4L2 file handle
 * @di:		back-pointer to the device
 * @in:		OUTPUT (interlaced source) format
 * @out:	CAPTURE (progressive) format
 * @in_fmt:	OUTPUT pixel format description
 * @out_fmt:	CAPTURE pixel format description
 * @field:	OUTPUT field order (V4L2_FIELD_INTERLACED_TB / _BT)
 * @sequence_out: OUTPUT frame sequence counter
 * @sequence_cap: CAPTURE frame sequence counter
 * @streaming:	both queues are streaming and the hardware has been sized
 */
struct meson_di_ctx {
	struct v4l2_fh fh;
	struct meson_di *di;

	struct v4l2_pix_format_mplane in;
	struct v4l2_pix_format_mplane out;
	const struct meson_di_fmt *in_fmt;
	const struct meson_di_fmt *out_fmt;

	enum v4l2_field field;

	unsigned int sequence_out;
	unsigned int sequence_cap;

	bool streaming;
};

/**
 * struct meson_di - deinterlacer device
 * @v4l2_dev:	V4L2 device
 * @m2m_dev:	mem2mem device
 * @vfd:	video device
 * @dev:	platform device
 * @regs:	mapped DI register window (rebased at MESON_DI_REG_BASE)
 * @vpu_clkb:	DI datapath clock (vpu_clkb)
 * @canvas:	canvas provider
 * @canvas_idx:	allocated canvas indices
 * @irq_de:	pre-stage completion interrupt
 * @irq_post:	post-stage completion interrupt
 * @match_data:	per-SoC quirks
 * @mutex:	serializes device access and vb2 queues
 * @curr:	context of the job currently running on the hardware
 * @phase:	stage of the currently running job
 * @cur_src:	source buffer of the currently running job
 * @cur_dst:	destination buffer of the field currently being written
 * @nr_buf:	noise-reduction working buffer (CPU address)
 * @nr_dma:	noise-reduction working buffer (DMA address)
 * @nr_size:	size of the noise-reduction working buffer
 * @mtn_buf:	motion-information working buffer (CPU address)
 * @mtn_dma:	motion-information working buffer (DMA address)
 * @mtn_size:	size of the motion-information working buffer
 * @cont_buf:	motion-engine contour working buffer (CPU address)
 * @cont_dma:	motion-engine contour working buffer (DMA address)
 * @cont_size:	size of the motion-engine contour working buffer
 */
struct meson_di {
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev;
	struct video_device *vfd;
	struct device *dev;

	void __iomem *regs;
	struct clk *vpu_clkb;
	struct meson_canvas *canvas;
	u8 canvas_idx[MESON_DI_CANVAS_NUM];

	int irq_de;
	int irq_post;

	const struct meson_di_match_data *match_data;

	struct mutex mutex; /* serializes hardware access and vb2 queues */
	struct meson_di_ctx *curr;
	enum meson_di_phase phase;
	struct vb2_v4l2_buffer *cur_src;
	struct vb2_v4l2_buffer *cur_dst;
	struct timer_list job_timer;

	void *nr_buf;
	dma_addr_t nr_dma;
	size_t nr_size;
	void *mtn_buf;
	dma_addr_t mtn_dma;
	size_t mtn_size;
	void *cont_buf;
	dma_addr_t cont_dma;
	size_t cont_size;
};

static inline u32 di_read(struct meson_di *di, u32 reg)
{
	return readl(di->regs + ((reg - MESON_DI_REG_BASE) << 2));
}

static inline void di_write(struct meson_di *di, u32 reg, u32 val)
{
	writel(val, di->regs + ((reg - MESON_DI_REG_BASE) << 2));
}

static inline void di_update_bits(struct meson_di *di, u32 reg, u32 mask,
				  u32 val)
{
	u32 tmp = di_read(di, reg);

	tmp &= ~mask;
	tmp |= val & mask;
	di_write(di, reg, tmp);
}

/* meson-di-hw.c */
void meson_di_hw_init(struct meson_di *di);
void meson_di_hw_disable(struct meson_di *di);
void meson_di_hw_stop(struct meson_di *di);
void meson_di_hw_setup(struct meson_di_ctx *ctx);
void meson_di_hw_pre(struct meson_di_ctx *ctx, struct vb2_v4l2_buffer *src);
void meson_di_hw_post(struct meson_di_ctx *ctx, struct vb2_v4l2_buffer *dst,
		      bool bottom_field);
bool meson_di_hw_pre_done(struct meson_di *di);
bool meson_di_hw_post_done(struct meson_di *di);
void meson_di_hw_dump(struct meson_di *di);

#endif /* __MESON_DI_H */
