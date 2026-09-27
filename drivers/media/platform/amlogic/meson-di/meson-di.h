/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Christian Hewitt <christianshewitt@gmail.com>
 */

#ifndef __MESON_DI_H
#define __MESON_DI_H

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>

#include "meson-di-regs.h"

#define MESON_DI_MIN_WIDTH	64
#define MESON_DI_MIN_HEIGHT	64
#define MESON_DI_MAX_WIDTH	1920
#define MESON_DI_MAX_HEIGHT	1088
#define MESON_DI_MAX_STRIDE	8192
#define MESON_DI_LOCAL_BUFS	4

enum meson_di_soc {
	MESON_DI_G12A,
	MESON_DI_G12B,
	MESON_DI_SM1,
};

enum meson_di_canvas {
	MESON_DI_CVS_INP_Y,
	MESON_DI_CVS_INP_UV,
	MESON_DI_CVS_MEM,
	MESON_DI_CVS_CHAN2,
	MESON_DI_CVS_NRWR,
	MESON_DI_CVS_MTNWR,
	MESON_DI_CVS_CONTWR,
	MESON_DI_CVS_CONTP2RD,
	MESON_DI_CVS_CONTPRD,
	MESON_DI_CVS_IF0,
	MESON_DI_CVS_IF1,
	MESON_DI_CVS_IF2,
	MESON_DI_CVS_MTNRD,
	MESON_DI_CVS_WR_Y,
	MESON_DI_CVS_WR_UV,
	MESON_DI_CVS_NUM,
};

struct meson_di_data {
	enum meson_di_soc soc;
};

struct meson_di_fmt {
	u32 fourcc;
	unsigned int num_planes;
};

struct meson_di_q {
	struct v4l2_pix_format_mplane pix;
	const struct meson_di_fmt *fmt;
};

struct meson_di_local {
	dma_addr_t nr;
	dma_addr_t mtn;
	dma_addr_t cnt;
	bool bottom;
};

struct meson_di_ctx {
	struct v4l2_fh fh;
	struct meson_di *di;
	struct meson_di_q out;
	struct meson_di_q cap;
	enum v4l2_field field;
	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_xfer_func xfer_func;
	void *loc_virt;
	dma_addr_t loc_dma;
	size_t loc_size;
	struct meson_di_local loc[MESON_DI_LOCAL_BUFS];
	unsigned int nr_stride;
	unsigned int mtn_stride;
	unsigned int seq;
	bool aborting;
};

struct meson_di {
	struct device *dev;
	const struct meson_di_data *data;
	struct v4l2_device v4l2_dev;
	struct video_device vfd;
	struct v4l2_m2m_dev *m2m_dev;
	/* serialises ioctls and queue operations */
	struct mutex mutex;
	void __iomem *base;
	int irq_pre;
	int irq_post;
	struct regmap *hhi;
	struct clk *clkb;
	struct clk *intr;
	struct meson_canvas *canvas;
	u8 cvs[MESON_DI_CVS_NUM];
	/* protects DI_INTR_CTRL, shared by the pre and post IRQs */
	spinlock_t intr_lock;
	struct completion pre_done;
	struct completion post_done;
	struct work_struct work;
	struct meson_di_ctx *cur_ctx;
};

struct meson_di_pre {
	dma_addr_t in_y;
	dma_addr_t in_uv;
	unsigned int in_stride;
	const struct meson_di_local *wr;
	const struct meson_di_local *mem;
	const struct meson_di_local *chan2;
	unsigned int seq;
};

struct meson_di_post {
	dma_addr_t out_y;
	dma_addr_t out_uv;
	unsigned int out_stride;
	const struct meson_di_local *prev;
	const struct meson_di_local *cur;
	const struct meson_di_local *next;
};

static inline u32 di_read(struct meson_di *di, u32 reg)
{
	return readl(di->base + ((reg - DI_REG_BASE) << 2));
}

static inline void di_write(struct meson_di *di, u32 reg, u32 val)
{
	writel(val, di->base + ((reg - DI_REG_BASE) << 2));
}

static inline void di_update(struct meson_di *di, u32 reg, u32 mask, u32 val)
{
	di_write(di, reg, (di_read(di, reg) & ~mask) | (val & mask));
}

void meson_di_hw_init(struct meson_di *di);
void meson_di_hw_size(struct meson_di *di, unsigned int width,
		      unsigned int height);
int meson_di_hw_pre(struct meson_di *di, struct meson_di_ctx *ctx,
		    const struct meson_di_pre *p);
int meson_di_hw_post(struct meson_di *di, struct meson_di_ctx *ctx,
		     const struct meson_di_post *p);
irqreturn_t meson_di_hw_pre_irq(struct meson_di *di);
irqreturn_t meson_di_hw_post_irq(struct meson_di *di);
void meson_di_hw_stop(struct meson_di *di);
void meson_di_hw_recover(struct meson_di *di, bool pre);

#endif
