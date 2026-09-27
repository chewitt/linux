// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Christian Hewitt <christianshewitt@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/soc/amlogic/meson-canvas.h>

#include "meson-di.h"

#define DI_FIFO_SIZE		0x120
#define DI_HOLD_LINES		10
#define DI_ARB_IDLE		0x14500


static unsigned int out_endian = MESON_CANVAS_ENDIAN_SWAP64;
module_param(out_endian, uint, 0644);
MODULE_PARM_DESC(out_endian, "Output canvas endian (default 7)");

struct meson_di_rd_mif {
	u32 gen;
	u32 gen2;
	u32 gen3;
	u32 canvas;
	u32 luma_x;
	u32 luma_y;
	u32 chroma_x;
	u32 chroma_y;
	u32 rpt_loop;
	u32 luma_pat;
	u32 chroma_pat;
	u32 dummy;
	u32 fifo;
	u32 fmt_ctrl;
	u32 fmt_w;
};

struct meson_di_wr_mif {
	u32 x;
	u32 y;
	u32 ctrl;
	u32 canvas;
};

struct meson_di_layout {
	struct meson_di_rd_mif inp;
	struct meson_di_rd_mif mem;
	struct meson_di_rd_mif chan2;
	struct meson_di_rd_mif if0;
	struct meson_di_rd_mif if1;
	struct meson_di_rd_mif if2;
	struct meson_di_wr_mif nrwr;
	struct meson_di_wr_mif diwr;
	u32 pre_gl_ctrl;
	u32 pre_gl_thd;
	u32 post_gl_ctrl;
	u32 post_gl_thd;
};

#define DI_V3_RD_MIF(base) {						\
	.gen = (base),							\
	.gen2 = (base) + 0x19,						\
	.gen3 = (base) + 0x1c,						\
	.canvas = (base) + 0x01,					\
	.luma_x = (base) + 0x03,					\
	.luma_y = (base) + 0x04,					\
	.chroma_x = (base) + 0x05,					\
	.chroma_y = (base) + 0x06,					\
	.rpt_loop = (base) + 0x0b,					\
	.luma_pat = (base) + 0x0c,					\
	.chroma_pat = (base) + 0x0d,					\
	.dummy = (base) + 0x12,						\
	.fifo = (base) + 0x13,						\
	.fmt_ctrl = (base) + 0x1d,					\
	.fmt_w = (base) + 0x1e,						\
}

static const struct meson_di_layout meson_di_g12a_layout = {
	.inp = {
		DI_INP_GEN_REG, DI_INP_GEN_REG2, DI_INP_GEN_REG3,
		DI_INP_CANVAS0, DI_INP_LUMA_X0, DI_INP_LUMA_Y0,
		DI_INP_CHROMA_X0, DI_INP_CHROMA_Y0, DI_INP_RPT_LOOP,
		DI_INP_LUMA0_RPT_PAT, DI_INP_CHROMA0_RPT_PAT,
		DI_INP_DUMMY_PIXEL, DI_INP_LUMA_FIFO_SIZE, DI_INP_FMT_CTRL,
		DI_INP_FMT_W,
	},
	.mem = {
		DI_MEM_GEN_REG, DI_MEM_GEN_REG2, DI_MEM_GEN_REG3,
		DI_MEM_CANVAS0, DI_MEM_LUMA_X0, DI_MEM_LUMA_Y0,
		DI_MEM_CHROMA_X0, DI_MEM_CHROMA_Y0, DI_MEM_RPT_LOOP,
		DI_MEM_LUMA0_RPT_PAT, DI_MEM_CHROMA0_RPT_PAT,
		DI_MEM_DUMMY_PIXEL, DI_MEM_LUMA_FIFO_SIZE, DI_MEM_FMT_CTRL,
		DI_MEM_FMT_W,
	},
	.chan2 = {
		DI_CHAN2_GEN_REG, DI_CHAN2_GEN_REG2, DI_CHAN2_GEN_REG3,
		DI_CHAN2_CANVAS0, DI_CHAN2_LUMA_X0, DI_CHAN2_LUMA_Y0,
		DI_CHAN2_CHROMA_X0, DI_CHAN2_CHROMA_Y0, DI_CHAN2_RPT_LOOP,
		DI_CHAN2_LUMA0_RPT_PAT, DI_CHAN2_CHROMA0_RPT_PAT,
		DI_CHAN2_DUMMY_PIXEL, DI_CHAN2_LUMA_FIFO_SIZE,
		DI_CHAN2_FMT_CTRL, DI_CHAN2_FMT_W,
	},
	.if0 = {
		DI_IF0_GEN_REG, DI_IF0_GEN_REG2, DI_IF0_GEN_REG3,
		DI_IF0_CANVAS0, DI_IF0_LUMA_X0, DI_IF0_LUMA_Y0,
		DI_IF0_CHROMA_X0, DI_IF0_CHROMA_Y0, DI_IF0_RPT_LOOP,
		DI_IF0_LUMA0_RPT_PAT, DI_IF0_CHROMA0_RPT_PAT,
		DI_IF0_DUMMY_PIXEL, DI_IF0_LUMA_FIFO_SIZE, DI_IF0_FMT_CTRL,
		DI_IF0_FMT_W,
	},
	.if1 = {
		DI_IF1_GEN_REG, DI_IF1_GEN_REG2, DI_IF1_GEN_REG3,
		DI_IF1_CANVAS0, DI_IF1_LUMA_X0, DI_IF1_LUMA_Y0,
		DI_IF1_CHROMA_X0, DI_IF1_CHROMA_Y0, DI_IF1_RPT_LOOP,
		DI_IF1_LUMA0_RPT_PAT, DI_IF1_CHROMA0_RPT_PAT,
		DI_IF1_DUMMY_PIXEL, DI_IF1_LUMA_FIFO_SIZE, DI_IF1_FMT_CTRL,
		DI_IF1_FMT_W,
	},
	.if2 = {
		DI_IF2_GEN_REG, DI_IF2_GEN_REG2, DI_IF2_GEN_REG3,
		DI_IF2_CANVAS0, DI_IF2_LUMA_X0, DI_IF2_LUMA_Y0,
		DI_IF2_CHROMA_X0, DI_IF2_CHROMA_Y0, DI_IF2_RPT_LOOP,
		DI_IF2_LUMA0_RPT_PAT, DI_IF2_CHROMA0_RPT_PAT,
		DI_IF2_DUMMY_PIXEL, DI_IF2_LUMA_FIFO_SIZE, DI_IF2_FMT_CTRL,
		DI_IF2_FMT_W,
	},
	.nrwr = { DI_NRWR_X, DI_NRWR_Y, DI_NRWR_CTRL, 0 },
	.diwr = { DI_DIWR_X, DI_DIWR_Y, DI_DIWR_CTRL, 0 },
	.pre_gl_ctrl = DI_PRE_GL_CTRL,
	.pre_gl_thd = DI_PRE_GL_THD,
	.post_gl_ctrl = DI_POST_GL_CTRL,
	.post_gl_thd = DI_POST_GL_THD,
};

static const struct meson_di_layout meson_di_v3_layout = {
	.inp = DI_V3_RD_MIF(DI_S4_INP_MIF),
	.mem = DI_V3_RD_MIF(DI_S4_MEM_MIF),
	.chan2 = DI_V3_RD_MIF(DI_S4_CHAN2_MIF),
	.if0 = DI_V3_RD_MIF(DI_S4_IF0_MIF),
	.if1 = DI_V3_RD_MIF(DI_S4_IF1_MIF),
	.if2 = DI_V3_RD_MIF(DI_S4_IF2_MIF),
	.nrwr = {
		DI_S4_NRWR_X, DI_S4_NRWR_Y, DI_S4_NRWR_CTRL, DI_S4_NRWR_CANVAS,
	},
	.diwr = {
		DI_S4_DIWR_X, DI_S4_DIWR_Y, DI_S4_DIWR_CTRL, DI_S4_DIWR_CANVAS,
	},
	.pre_gl_ctrl = DI_S4_PRE_GL_CTRL,
	.pre_gl_thd = DI_S4_PRE_GL_THD,
	.post_gl_ctrl = DI_S4_POST_GL_CTRL,
	.post_gl_thd = DI_S4_POST_GL_THD,
};

static const struct meson_di_layout *di_layout(struct meson_di *di)
{
	return di->data->v3 ? &meson_di_v3_layout : &meson_di_g12a_layout;
}

static void di_field(struct meson_di *di, u32 reg, unsigned int lsb, u32 val)
{
	di_update(di, reg, GENMASK(lsb + 1, lsb), val << lsb);
}

static u32 di_burst_len(unsigned int stride)
{
	if (!(stride % 64))
		return 2;
	if (!(stride % 32))
		return 1;
	return 0;
}

static void meson_di_hw_gates(struct meson_di *di)
{
	unsigned int i;

	if (di->data->v3) {
		di_update(di, VIUB_GCLK_CTRL0, BIT(8) | BIT(9) | BIT(15),
			  BIT(8) | BIT(9) | BIT(15));
		for (i = 2; i <= 6; i += 2)
			di_field(di, VIUB_GCLK_CTRL2, i, 2);
		di_update(di, VIUB_GCLK_CTRL3, GENMASK(5, 2), 0);
		di_update(di, DI_PRE_CTRL, GENMASK(3, 2), GENMASK(3, 2));
		return;
	}

	di_update(di, VIUB_GCLK_CTRL0, BIT(0) | BIT(10) | BIT(15),
		  BIT(0) | BIT(10) | BIT(15));
	di_field(di, VIUB_GCLK_CTRL1, 0, 2);

	di_update(di, VIUB_GCLK_CTRL0, BIT(9) | BIT(12), BIT(9) | BIT(12));
	di_update(di, VIUB_GCLK_CTRL3, GENMASK(5, 0), 0);
	for (i = 2; i <= 12; i += 2)
		di_field(di, VIUB_GCLK_CTRL1, i, 2);

	di_update(di, VIUB_GCLK_CTRL0, BIT(8) | BIT(11), BIT(8) | BIT(11));
	for (i = 2; i <= 6; i += 2)
		di_field(di, VIUB_GCLK_CTRL2, i, 2);
	for (i = 16; i <= 24; i += 2)
		di_field(di, VIUB_GCLK_CTRL1, i, 2);
	di_update(di, DI_PRE_CTRL, GENMASK(3, 2), GENMASK(3, 2));

	di_field(di, VIUB_GCLK_CTRL2, 0, 0);
	di_field(di, VIUB_GCLK_CTRL2, 8, 0);
	di_field(di, VIUB_GCLK_CTRL2, 10, 0);
	di_update(di, NR4_TOP_CTRL, GENMASK(31, 20), 0);
}

static void meson_di_hw_mtn_init(struct meson_di *di)
{
	di_write(di, DI_MTN_1_CTRL4, 0x01800880);
	di_write(di, DI_MTN_1_CTRL7, 0x0a800480);

	if (di->data->soc == MESON_DI_G12A) {
		di_write(di, DI_MTN_1_CTRL1, 0xa0202015);
	} else if (di->data->v3) {
		di_write(di, DI_MTN_1_CTRL1, 0x00202015);
	} else {
		di_update(di, DI_MTN_CTRL, BIT(0) | BIT(30) | GENMASK(27, 24),
			  BIT(0) | BIT(30) | GENMASK(27, 24));
		di_write(di, DI_MTN_1_CTRL1, 0x00202015);
	}

	di_write(di, DI_MTN_CTRL1, 2);
}

static void meson_di_hw_top_init(struct meson_di *di)
{
	di_write(di, DI_PRE_SEC_IN, 0);
	di_write(di, DI_POST_SEC_IN, 0);
	di_update(di, DI_TOP_PRE_CTRL, DI_TOP_PRE_MASK,
		  DI_TOP_MIF_EN | DI_TOP_PRE_NR_CH0_EN);
	di_update(di, DI_TOP_POST_CTRL, DI_TOP_POST_MASK,
		  DI_TOP_MIF_EN | DI_TOP_POST_FRM_WR);
}

void meson_di_hw_init(struct meson_di *di)
{
	const struct meson_di_layout *l = di_layout(di);

	if (di->data->v3) {
		meson_di_hw_top_init(di);
	} else {
		di_update(di, VPU_CLK_GATE, BIT(18), BIT(18));
		regmap_update_bits(di->hhi, HHI_VPU_MEM_PD_REG0,
				   HHI_VPU_MEM_PD_VD1 | HHI_VPU_MEM_PD_DI_POST,
				   0);
	}

	meson_di_hw_gates(di);

	di_write(di, l->inp.fifo, DI_FIFO_SIZE);
	di_write(di, l->mem.fifo, DI_FIFO_SIZE);
	di_write(di, l->if1.fifo, DI_FIFO_SIZE);
	di_write(di, l->if2.fifo, DI_FIFO_SIZE);
	di_write(di, l->chan2.fifo, DI_FIFO_SIZE);
	di_write(di, l->if0.fifo, DI_FIFO_SIZE);

	di_write(di, DI_ARB_CTRL, 0);
	di_write(di, DI_PRE_HOLD, 0);
	di_write(di, l->pre_gl_ctrl, DI_PRE_GL_STOP);
	di_write(di, l->post_gl_ctrl, DI_POST_GL_STOP);

	meson_di_hw_mtn_init(di);

	di_write(di, DI_EI_CTRL0, 0x00ff0100);
	di_write(di, DI_EI_CTRL1, 0x5a0a0f2d);
	di_write(di, DI_EI_CTRL2, 0x050a0a5d);
	di_write(di, DI_EI_CTRL3, 0x80000013);
	di_update(di, DI_EI_DRT_CTRL, GENMASK(31, 30), GENMASK(31, 30));

	di_write(di, DNR_CTRL, di->data->v3 ? 0xddf00 : 0x1df00);
	di_write(di, NR3_MODE, 3);
	di_write(di, NR3_COOP_PARA, 0x28ff00);
	di_write(di, NR3_CNOOP_GAIN, 0x881900);
	di_write(di, NR3_YMOT_PARA, 0x0c0a1e);
	di_write(di, NR3_CMOT_PARA, 0x08140f);
	di_write(di, NR3_SUREMOT_YGAIN, 0x100c4014);
	di_write(di, NR3_SUREMOT_CGAIN, 0x22264014);

	di_write(di, NR2_CUE_CON_DIF0, 0x1400);
	di_write(di, NR2_CUE_CON_DIF1, 0x80064);
	di_write(di, NR2_CUE_CON_DIF2, 0x80064);
	di_write(di, NR2_CUE_CON_DIF3, 0x80a0a);
	di_write(di, NR2_CUE_PRG_DIF, 0x80a0a);
	if (di->data->v3)
		di_update(di, NR4_TOP_CTRL, BIT(1), 0);
	else
		di_update(di, DI_NR_CTRL0, BIT(26), 0);

	if (!di->data->v3)
		di_update(di, DI_NRWR_Y, BIT(15), BIT(15));
	di_update(di, DI_CANVAS_URGENT0, BIT(8), BIT(8));

	di_update(di, l->if0.gen3, BIT(11), BIT(11));
	di_update(di, l->if1.gen3, BIT(11), BIT(11));
	di_update(di, l->if2.gen3, BIT(11), BIT(11));
	di_update(di, MCDI_MC_CRTL, GENMASK(1, 0), 0);

	di_update(di, VIUB_MISC_CTRL0, BIT(4) | GENMASK(6, 5), 2 << 5);
	di_update(di, NRDSWR_CTRL, BIT(12), 0);
	di_update(di, NR_DS_CTRL, BIT(30), 0);
	di_update(di, NR4_TOP_CTRL, BIT(12), 0);
	di_update(di, NR2_CFR_PARA_CFG0, GENMASK(3, 2), 2 << 2);

	di_write(di, DI_INTR_CTRL, DI_INTR_MODE | DI_INTR_MASKS | 0xffff);
}

static u32 meson_di_hw_lbuf(struct meson_di *di, unsigned int width)
{
	if (!di->data->v3)
		return 1;
	if (width <= 640)
		return 3;
	if (width <= 960)
		return 1;
	if (width <= 1280)
		return 2;
	return 0;
}

void meson_di_hw_size(struct meson_di *di, unsigned int width,
		      unsigned int height)
{
	unsigned int fh = height / 2;

	di_update(di, NR4_TOP_CTRL, BIT(2) | BIT(15) | BIT(17),
		  BIT(2) | BIT(15) | BIT(17));
	di_write(di, DNR_HVSIZE, width << 16 | fh);
	di_write(di, DNR_STAT_X_START_END, 24 << 16 | (width - 25));
	di_write(di, DNR_STAT_Y_START_END, 24 << 16 | (fh - 25));
	di_update(di, DNR_DM_CTRL, BIT(11), BIT(11));
	di_update(di, DNR_CTRL, BIT(16), BIT(16));
	if (di->data->v3) {
		di_write(di, DNR_CTRL, 0xddf00);
		di_update(di, DNR_DM_CTRL, BIT(8) | BIT(9),
			  (width > 1280 ? 0 : BIT(8)) |
			  (width > 1920 ? 0 : BIT(9)));
	} else {
		di_update(di, DNR_DM_CTRL, BIT(8), 0);
		di_write(di, DNR_CTRL, 0x1dd00);
	}

	di_update(di, NR2_CUE_PRG_DIF, BIT(20), 0);
	if (di->data->v3)
		di_update(di, NR4_TOP_CTRL, BIT(1), 0);
	else
		di_update(di, DI_NR_CTRL0, BIT(26), 0);
	di_update(di, NR2_CUE_MODE, GENMASK(3, 0), 5);
	di_update(di, LBUF_TOP_CTRL, GENMASK(25, 20) | GENMASK(17, 16),
		  meson_di_hw_lbuf(di, width) << 16);

	di_write(di, NR4_MCNR_LUMA_STAT_LIMTX, 8 << 16 | (width - 9));
	di_write(di, NR4_MCNR_LUMA_STAT_LIMTY, 8 << 16 | (fh - 9));
	di_write(di, NR4_NM_X_CFG, 8 << 16 | (width - 9));
	di_write(di, NR4_NM_Y_CFG, 8 << 16 | (fh - 9));
	di_update(di, NR4_TOP_CTRL, BIT(3) | BIT(5) | BIT(16) | BIT(18),
		  BIT(3) | BIT(5) | BIT(16) | BIT(18));

	di_write(di, DI_PRE_SIZE, (width - 1) | (fh - 1) << 16);

	if (di->data->soc == MESON_DI_SM1)
		di_write(di, DI_VIU_HSC_CTRL, 0x00400440);
}

static void meson_di_hw_rd_inp(struct meson_di *di,
			       const struct meson_di_rd_mif *m, u32 canvas,
			       unsigned int w, unsigned int h,
			       unsigned int stride, bool bottom, u32 fmt_ctrl)
{
	unsigned int start = bottom ? 1 : 0;

	di_write(di, m->gen, 0x22541742 | BIT(4));
	di_update(di, m->gen2, GENMASK(1, 0), 1);
	di_update(di, m->gen3, BIT(0) | GENMASK(2, 1) | GENMASK(9, 8),
		  di_burst_len(stride) << 1);
	di_write(di, m->canvas, canvas);
	di_write(di, m->luma_x, (w - 1) << 16);
	di_write(di, m->luma_y, (h - 1) << 16 | start);
	di_write(di, m->chroma_x, (w / 2 - 1) << 16);
	di_write(di, m->chroma_y, (h / 2 - 1) << 16 | start);
	di_write(di, m->rpt_loop, 0x1111);
	di_write(di, m->luma_pat, 0x80);
	di_write(di, m->chroma_pat, 0x80);
	di_write(di, m->dummy, 0x00808000);
	di_write(di, m->fmt_ctrl, fmt_ctrl);
	di_write(di, m->fmt_w, w << 16 | w / 2);
}

static void meson_di_hw_rd_local(struct meson_di *di,
				 const struct meson_di_rd_mif *m, u32 canvas,
				 unsigned int w, unsigned int fh)
{
	di_write(di, m->gen, 0x22545700);
	di_update(di, m->gen2, GENMASK(1, 0), 0);
	di_update(di, m->gen3, BIT(0) | GENMASK(2, 1) | GENMASK(9, 8),
		  BIT(0) | 2 << 1);
	di_write(di, m->canvas, canvas);
	di_write(di, m->luma_x, (w - 1) << 16);
	di_write(di, m->luma_y, (fh - 1) << 16);
	di_write(di, m->chroma_x, 0);
	di_write(di, m->chroma_y, 0);
	di_write(di, m->rpt_loop, 0);
	di_write(di, m->luma_pat, 0);
	di_write(di, m->chroma_pat, 0);
	di_write(di, m->dummy, 0x00808000);
	di_write(di, m->fmt_ctrl, 0x00320020);
	di_write(di, m->fmt_w, w << 16 | w / 2);
}

static void meson_di_hw_pre_mifs(struct meson_di *di, bool enable)
{
	const struct meson_di_layout *l = di_layout(di);
	u32 v = enable ? BIT(0) : 0;

	di_update(di, l->chan2.gen, BIT(0), v);
	di_update(di, l->mem.gen, BIT(0), v);
	di_update(di, l->inp.gen, BIT(0), v);
}

static void meson_di_hw_ma_mifs(struct meson_di *di, bool enable)
{
	u32 v = enable ? BIT(12) : 0;

	di_update(di, CONTWR_CTRL, BIT(12), v);
	di_update(di, MTNWR_CTRL, BIT(12), v);
	if (!enable)
		di_update(di, DI_PRE_CTRL, DI_PRE_CTRL_CONT_RD, 0);
}

static int meson_di_hw_canvas(struct meson_di *di, enum meson_di_canvas idx,
			      dma_addr_t addr, unsigned int stride,
			      unsigned int height, unsigned int endian)
{
	return meson_canvas_config(di->canvas, di->cvs[idx], addr, stride,
				   height, MESON_CANVAS_WRAP_NONE,
				   MESON_CANVAS_BLKMODE_LINEAR, endian);
}

static int meson_di_hw_pre_canvases(struct meson_di *di,
				    struct meson_di_ctx *ctx,
				    const struct meson_di_pre *p)
{
	const struct meson_di_local *p2 = p->mem ?: p->wr;
	const struct meson_di_local *pr = p->chan2 ?: p->wr;
	unsigned int h = ctx->out.pix.height, fh = h / 2;
	unsigned int ns = ctx->nr_stride, ms = ctx->mtn_stride;
	int ret;

	ret = meson_di_hw_canvas(di, MESON_DI_CVS_INP_Y, p->in_y, p->in_stride,
				 h, MESON_CANVAS_ENDIAN_SWAP64);
	ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_INP_UV, p->in_uv,
					p->in_stride, h / 2,
					MESON_CANVAS_ENDIAN_SWAP64);
	ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_NRWR, p->wr->nr, ns,
					fh, 0);
	ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_MTNWR, p->wr->mtn, ms,
					fh, 0);
	ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_CONTWR, p->wr->cnt, ms,
					fh, 0);
	ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_CONTP2RD, p2->cnt, ms,
					fh, 0);
	ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_CONTPRD, pr->cnt, ms,
					fh, 0);
	if (!ret && p->mem)
		ret = meson_di_hw_canvas(di, MESON_DI_CVS_MEM, p->mem->nr, ns,
					 fh, 0);
	if (!ret && p->chan2)
		ret = meson_di_hw_canvas(di, MESON_DI_CVS_CHAN2, p->chan2->nr,
					 ns, fh, 0);

	return ret;
}

static void meson_di_hw_ma_pre(struct meson_di *di, unsigned int w,
			       unsigned int fh)
{
	u32 rd = GENMASK(23, 16) | GENMASK(9, 8) | GENMASK(2, 0);

	if (di->data->soc == MESON_DI_G12A || di->data->v3)
		di_update(di, DI_MTN_1_CTRL1, GENMASK(31, 29), 5 << 29);
	else
		di_update(di, DI_MTN_CTRL, BIT(29) | BIT(31), BIT(29) | BIT(31));

	di_write(di, CONTRD_SCOPE_X, (w - 1) << 16);
	di_write(di, CONTRD_SCOPE_Y, (fh - 1) << 16);
	di_update(di, CONTRD_CTRL1, rd,
		  di->cvs[MESON_DI_CVS_CONTP2RD] << 16 | 2 << 8);
	di_write(di, CONT2RD_SCOPE_X, (w - 1) << 16);
	di_write(di, CONT2RD_SCOPE_Y, (fh - 1) << 16);
	di_update(di, CONT2RD_CTRL1, rd,
		  di->cvs[MESON_DI_CVS_CONTPRD] << 16 | 2 << 8);

	di_write(di, MTNWR_X, 2 << 30 | (w - 1));
	di_write(di, MTNWR_Y, fh - 1);
	di_write(di, MTNWR_CTRL, di->cvs[MESON_DI_CVS_MTNWR]);
	di_write(di, MTNWR_CAN_SIZE, (w - 1) << 16 | (fh - 1));
	di_write(di, CONTWR_X, 2 << 30 | (w - 1));
	di_write(di, CONTWR_Y, fh - 1);
	di_write(di, CONTWR_CTRL, di->cvs[MESON_DI_CVS_CONTWR]);
	di_write(di, CONTWR_CAN_SIZE, (w - 1) << 16 | (fh - 1));
}

static void meson_di_hw_pre_reset(struct meson_di *di)
{
	u32 rd2 = BIT(31);

	if (di->data->soc != MESON_DI_G12A) {
		di_update(di, CONTRD_CTRL2, rd2, rd2);
		di_update(di, CONT2RD_CTRL2, rd2, rd2);
		di_update(di, MCINFRD_CTRL2, rd2, rd2);
	}
	di_update(di, DI_PRE_CTRL, DI_PRE_CTRL_CONT_RD, DI_PRE_CTRL_CONT_RD);
	if (di->data->soc != MESON_DI_G12A) {
		di_update(di, CONTRD_CTRL2, rd2, 0);
		di_update(di, CONT2RD_CTRL2, rd2, 0);
		di_update(di, MCINFRD_CTRL2, rd2, 0);
	}

	di_update(di, CONTWR_CAN_SIZE, BIT(14), BIT(14));
	di_update(di, MTNWR_CAN_SIZE, BIT(14), BIT(14));
	di_update(di, MCVECWR_CAN_SIZE, BIT(14), BIT(14));
	di_update(di, MCINFWR_CAN_SIZE, BIT(14), BIT(14));
	di_update(di, CONTWR_CAN_SIZE, BIT(14), 0);
	di_update(di, MTNWR_CAN_SIZE, BIT(14), 0);
	di_update(di, MCVECWR_CAN_SIZE, BIT(14), 0);
	di_update(di, MCINFWR_CAN_SIZE, BIT(14), 0);
}

static void meson_di_hw_nrwr(struct meson_di *di, unsigned int w,
			     unsigned int fh)
{
	const struct meson_di_wr_mif *m = &di_layout(di)->nrwr;
	u8 cvs = di->cvs[MESON_DI_CVS_NRWR];

	if (di->data->v3) {
		di_write(di, m->x, w - 1);
		di_write(di, m->y, fh - 1);
		di_write(di, m->canvas, cvs);
		di_write(di, m->ctrl, DI_S4_WR_EN | DI_S4_WR_WORD_LIMIT(5) |
			 DI_S4_WR_RGB_MODE(0) | DI_S4_WR_BURST(2) |
			 DI_S4_WR_SWAP64);
		return;
	}

	di_write(di, m->x, w - 1);
	di_write(di, m->y, 3U << 30 | BIT(15) | (fh - 1));
	di_write(di, m->ctrl, 2 << 26 | BIT(30) | cvs);
}

int meson_di_hw_pre(struct meson_di *di, struct meson_di_ctx *ctx,
		    const struct meson_di_pre *p)
{
	const struct meson_di_layout *l = di_layout(di);
	unsigned int w = ctx->out.pix.width, h = ctx->out.pix.height;
	unsigned int fh = h / 2;
	u32 inp = di->cvs[MESON_DI_CVS_INP_UV] << 16 |
		  di->cvs[MESON_DI_CVS_INP_UV] << 8 |
		  di->cvs[MESON_DI_CVS_INP_Y];
	bool bottom = p->wr->bottom;
	u32 ctrl;
	int ret;

	ret = meson_di_hw_pre_canvases(di, ctx, p);
	if (ret)
		return ret;

	if (di->data->v3)
		ctrl = DI_S4_PRE_CTRL_BASE | DI_PRE_CTRL_MADI;
	else
		ctrl = DI_PRE_CTRL_BASE | DI_PRE_CTRL_MADI;

	di_update(di, VIUB_MISC_CTRL0, GENMASK(6, 5), 2 << 5);

	meson_di_hw_rd_inp(di, &l->inp, inp, w, h, p->in_stride,
			   bottom, bottom ? 0x00310a11 : 0x00310e11);

	if (p->mem) {
		meson_di_hw_rd_local(di, &l->mem, di->cvs[MESON_DI_CVS_MEM],
				     w, fh);
	} else {
		meson_di_hw_rd_inp(di, &l->mem, inp, w, h, p->in_stride,
				   bottom, 0x00320011);
		ctrl |= DI_PRE_CTRL_MEM_BYPASS;
	}

	if (p->chan2) {
		meson_di_hw_rd_local(di, &l->chan2,
				     di->cvs[MESON_DI_CVS_CHAN2], w, fh);
		ctrl |= di->data->v3 ? DI_S4_PRE_CTRL_CHAN2 :
				       DI_PRE_CTRL_CHAN2;
		if (p->chan2->bottom)
			ctrl |= DI_PRE_CTRL_FIELD;
	} else {
		meson_di_hw_rd_inp(di, &l->chan2, inp, w, h, p->in_stride,
				   bottom, 0x00320011);
		ctrl |= DI_PRE_CTRL_FIELD;
	}

	meson_di_hw_nrwr(di, w, fh);

	meson_di_hw_ma_pre(di, w, fh);

	di_update(di, l->pre_gl_thd, GENMASK(21, 16), DI_HOLD_LINES << 16);
	di_write(di, DI_PRE_CTRL, ctrl);

	if (!p->seq && di->data->soc != MESON_DI_G12A && !di->data->v3)
		di_update(di, DI_MTN_CTRL, BIT(30), BIT(30));
	else if (p->seq >= 4)
		di_update(di, DI_MTN_CTRL, BIT(30), 0);

	reinit_completion(&di->pre_done);
	meson_di_hw_ma_mifs(di, true);
	meson_di_hw_pre_mifs(di, true);
	meson_di_hw_pre_reset(di);
	di_write(di, l->pre_gl_ctrl, DI_PRE_GL_STOP);
	di_write(di, l->pre_gl_ctrl, DI_PRE_GL_START);

	return 0;
}

static bool meson_di_hw_arb_idle(struct meson_di *di)
{
	unsigned int i;

	for (i = 0; i < 100; i++)
		if ((di_read(di, DI_ARB_DBG_STAT_L1C1) & DI_ARB_IDLE) ==
		    DI_ARB_IDLE)
			return true;

	return false;
}

static void meson_di_hw_arb_reset(struct meson_di *di)
{
	bool idle;

	di_write(di, DI_WRARB_REQEN_SLV_L1C1, 0x3e);
	di_write(di, DI_RDARB_REQEN_SLV_L1C1, 0xf1f1);
	di_update(di, DI_PRE_CTRL, BIT(0), 0);

	idle = meson_di_hw_arb_idle(di);
	if (!idle) {
		di_update(di, DI_PRE_CTRL, BIT(31), BIT(31));
		idle = meson_di_hw_arb_idle(di);
	}

	if (idle) {
		di_update(di, VIUB_SW_RESET, BIT(14), BIT(14));
		di_update(di, VIUB_SW_RESET, BIT(14), 0);
	} else {
		dev_warn_ratelimited(di->dev, "arbiter not idle\n");
	}

	di_write(di, DI_WRARB_REQEN_SLV_L1C1, 0x3f);
	di_write(di, DI_RDARB_REQEN_SLV_L1C1, 0xffff);
}

static void meson_di_hw_pre_finish(struct meson_di *di)
{
	const struct meson_di_layout *l = di_layout(di);
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&di->intr_lock, flags);
	v = di_read(di, DI_INTR_CTRL);
	di_write(di, l->pre_gl_ctrl, DI_PRE_GL_STOP);
	di_write(di, DI_INTR_CTRL, (v & 0x3ffffffb) | DI_INTR_MODE);
	spin_unlock_irqrestore(&di->intr_lock, flags);

	meson_di_hw_arb_reset(di);
	meson_di_hw_ma_mifs(di, false);
	di_update(di, DI_PRE_CTRL, BIT(0), 0);
	meson_di_hw_pre_mifs(di, false);
	di_update(di, DI_PRE_CTRL, BIT(30), BIT(30));
}

irqreturn_t meson_di_hw_pre_irq(struct meson_di *di)
{
	if (!(di_read(di, DI_INTR_CTRL) & DI_INTR_NRWR_DONE))
		return IRQ_NONE;

	meson_di_hw_pre_finish(di);
	complete(&di->pre_done);

	return IRQ_HANDLED;
}

static void meson_di_hw_post_rd_mif(struct meson_di *di,
				    const struct meson_di_rd_mif *m,
				    u32 canvas, unsigned int w,
				    unsigned int fh)
{
	di_write(di, m->gen, 0x22545701);
	di_write(di, m->canvas, canvas);
	di_write(di, m->luma_x, (w - 1) << 16);
	di_write(di, m->luma_y, (fh - 1) << 16);
	di_write(di, m->chroma_x, 0);
	di_write(di, m->chroma_y, 0);
	di_write(di, m->rpt_loop, 0);
	di_write(di, m->luma_pat, 0);
	di_write(di, m->chroma_pat, 0);
	di_write(di, m->dummy, 0x00808000);
	di_write(di, m->fmt_ctrl, 0x00320020);
	di_write(di, m->fmt_w, w << 16 | w / 2);

	di_update(di, m->gen2, GENMASK(3, 0), 0);
	di_update(di, m->gen3, BIT(0) | GENMASK(2, 1) | GENMASK(9, 8),
		  BIT(0) | 2 << 1);
}

static void meson_di_hw_diwr(struct meson_di *di, unsigned int w,
			     unsigned int h)
{
	const struct meson_di_wr_mif *m = &di_layout(di)->diwr;
	u8 y = di->cvs[MESON_DI_CVS_WR_Y], uv = di->cvs[MESON_DI_CVS_WR_UV];

	if (di->data->v3) {
		di_write(di, m->x, w - 1);
		di_write(di, m->y, h - 1);
		di_write(di, m->canvas, uv << 8 | y);
		di_write(di, m->ctrl, DI_S4_WR_EN |
			 (out_endian ? DI_S4_WR_L_ENDIAN : DI_S4_WR_SWAP64) |
			 DI_S4_WR_DATA_EXT | DI_S4_WR_WORD_LIMIT(3) |
			 DI_S4_WR_URGENT | DI_S4_WR_RGB_MODE(2) |
			 DI_S4_WR_BURST(2));
		return;
	}

	di_write(di, m->x, (out_endian ? BIT(31) : 0) | (w - 1));
	di_write(di, m->y, 3U << 30 | BIT(15) | (h - 1));
	di_write(di, m->ctrl, (out_endian ? 0 : BIT(30)) | 2 << 26 | 2 << 22 |
		 BIT(16) | uv << 8 | y);
}

int meson_di_hw_post(struct meson_di *di, struct meson_di_ctx *ctx,
		     const struct meson_di_post *p)
{
	const struct meson_di_layout *l = di_layout(di);
	unsigned int w = ctx->out.pix.width, h = ctx->out.pix.height;
	unsigned int fh = h / 2, q = h / 4;
	unsigned int ns = ctx->nr_stride;
	bool normal = p->prev && p->next;
	u8 if0 = di->cvs[MESON_DI_CVS_IF0];
	u8 if1 = normal ? di->cvs[MESON_DI_CVS_IF1] : if0;
	u8 if2 = normal ? di->cvs[MESON_DI_CVS_IF2] : if0;
	u32 ctrl, blend;
	int ret;

	ret = meson_di_hw_canvas(di, MESON_DI_CVS_WR_Y, p->out_y,
				 p->out_stride, h, out_endian);
	ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_WR_UV, p->out_uv,
					p->out_stride, h / 2, out_endian);
	ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_IF0, p->cur->nr, ns,
					fh, 0);
	if (!ret && normal) {
		ret = meson_di_hw_canvas(di, MESON_DI_CVS_IF1, p->prev->nr, ns,
					 fh, 0);
		ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_IF2,
						p->next->nr, ns, fh, 0);
		ret = ret ?: meson_di_hw_canvas(di, MESON_DI_CVS_MTNRD,
						p->next->mtn, ctx->mtn_stride,
						fh, 0);
	}
	if (ret)
		return ret;

	if (!di->data->v3) {
		regmap_update_bits(di->hhi, HHI_VPU_MEM_PD_REG0,
				   HHI_VPU_MEM_PD_DI_POST,
				   HHI_VPU_MEM_PD_DI_POST);
		regmap_update_bits(di->hhi, HHI_VPU_MEM_PD_REG0,
				   HHI_VPU_MEM_PD_DI_POST, 0);
	}

	di_write(di, l->post_gl_ctrl, DI_POST_GL_STOP);
	di_write(di, DI_POST_SIZE, (w - 1) | (h - 1) << 16);
	di_update(di, DI_EI_CTRL3, BIT(31), BIT(31));
	di_write(di, DI_BLEND_REG0_X, w - 1);
	di_write(di, DI_BLEND_REG0_Y, h - 1);
	di_write(di, DI_BLEND_REG1_X, w - 1);
	di_write(di, DI_BLEND_REG1_Y, q << 16 | (2 * q - 1));
	di_write(di, DI_BLEND_REG2_X, w - 1);
	di_write(di, DI_BLEND_REG2_Y, (2 * q) << 16 | (3 * q - 1));
	di_write(di, DI_BLEND_REG3_X, w - 1);
	di_write(di, DI_BLEND_REG3_Y, (3 * q) << 16 | (h - 1));
	di_update(di, VIUB_MISC_CTRL0, BIT(4), 0);
	if (!di->data->v3)
		di_write(di, DI_POST_CTRL, DI_POST_CTRL_WR_BK |
			 DI_HOLD_LINES << 16 | 3U << 30);

	meson_di_hw_post_rd_mif(di, &l->if0, if0, w, fh);
	meson_di_hw_post_rd_mif(di, &l->if1, if1, w, fh);
	meson_di_hw_post_rd_mif(di, &l->if2, if2, w, fh);

	if (normal) {
		di_write(di, MTNRD_SCOPE_X, (w - 1) << 16);
		di_write(di, MTNRD_SCOPE_Y, (fh - 1) << 16);
		di_update(di, MTNRD_CTRL1,
			  GENMASK(23, 16) | GENMASK(5, 4) | GENMASK(2, 0),
			  di->cvs[MESON_DI_CVS_MTNRD] << 16);
	}

	meson_di_hw_diwr(di, w, h);

	blend = normal ? 7 << 22 | 3 << 20 : 7 << 22 | 2 << 20;
	if (normal && !di->data->v3)
		blend |= BIT(31);
	di_write(di, DI_BLEND_CTRL, blend);
	di_update(di, l->post_gl_thd, GENMASK(20, 16), DI_HOLD_LINES << 16);

	if (di->data->v3)
		ctrl = normal ? DI_S4_POST_CTRL_NORMAL : DI_S4_POST_CTRL_EI;
	else
		ctrl = normal ? DI_POST_CTRL_NORMAL : DI_POST_CTRL_EI;
	if (p->cur->bottom)
		ctrl |= DI_POST_CTRL_FIELD;
	di_write(di, DI_POST_CTRL, ctrl);

	reinit_completion(&di->post_done);
	di_update(di, l->if0.gen, BIT(0), BIT(0));
	di_update(di, l->if1.gen, BIT(0), BIT(0));
	di_update(di, l->if2.gen, BIT(0), BIT(0));
	if (!di->data->v3) {
		di_update(di, DI_POST_CTRL, DI_POST_CTRL_WR_BK,
			  DI_POST_CTRL_WR_BK);
		di_update(di, l->diwr.ctrl, BIT(31), BIT(31));
		di_update(di, l->diwr.ctrl, BIT(31), 0);
	}
	di_write(di, l->post_gl_ctrl, DI_POST_GL_START);

	return 0;
}

static void meson_di_hw_post_finish(struct meson_di *di)
{
	const struct meson_di_layout *l = di_layout(di);
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&di->intr_lock, flags);
	v = di_read(di, DI_INTR_CTRL) & 0x3fffffff;
	di_write(di, DI_INTR_CTRL, (v & 0xffff0004) | DI_INTR_MODE);
	di_write(di, l->post_gl_ctrl, DI_POST_GL_DONE);
	di_write(di, DI_POST_CTRL, di->data->v3 ? DI_S4_POST_CTRL_DONE :
						  DI_POST_CTRL_DONE);
	di_write(di, l->post_gl_ctrl, DI_POST_GL_STOP);
	spin_unlock_irqrestore(&di->intr_lock, flags);
}

irqreturn_t meson_di_hw_post_irq(struct meson_di *di)
{
	if (!(di_read(di, DI_INTR_CTRL) & DI_INTR_DIWR_DONE))
		return IRQ_NONE;

	meson_di_hw_post_finish(di);
	complete(&di->post_done);

	return IRQ_HANDLED;
}

void meson_di_hw_recover(struct meson_di *di, bool pre)
{
	if (pre)
		meson_di_hw_pre_finish(di);
	else
		meson_di_hw_post_finish(di);
}

void meson_di_hw_stop(struct meson_di *di)
{
	const struct meson_di_layout *l = di_layout(di);

	di_write(di, l->pre_gl_ctrl, DI_PRE_GL_STOP);
	di_write(di, l->post_gl_ctrl, DI_POST_GL_STOP);
	meson_di_hw_pre_mifs(di, false);
	meson_di_hw_ma_mifs(di, false);
	di_update(di, DI_PRE_CTRL, BIT(0), 0);
	di_update(di, l->if0.gen, BIT(0), 0);
	di_update(di, l->if1.gen, BIT(0), 0);
	di_update(di, l->if2.gen, BIT(0), 0);
	di_update(di, DI_POST_CTRL, di->data->v3 ? DI_S4_POST_CTRL_WR_BK :
						   DI_POST_CTRL_WR_BK, 0);
}
