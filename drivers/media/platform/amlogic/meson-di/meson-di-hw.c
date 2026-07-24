// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Amlogic GX SoC Video Deinterlacer (DI) hardware programming
 *
 * Copyright (C) 2026 Christian Hewitt <christianshewitt@gmail.com>
 *
 * The deinterlacer runs a two stage pipeline. The "pre" stage reads the two
 * interlaced fields, runs noise reduction and per-pixel motion detection and
 * writes a motion-information buffer to memory. The "post" stage reads the
 * fields together with the motion information and blends them into a
 * progressive frame, run once per output field so that two progressive
 * frames are produced for each interlaced input.
 *
 * Only the core motion-adaptive path is implemented: film-mode/pulldown
 * cadence detection, 3D detection, AFBC compression and the tunable noise
 * reduction coefficients of the vendor driver are left at their defaults.
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/moduleparam.h>

#include <linux/soc/amlogic/meson-canvas.h>

#include <media/videobuf2-dma-contig.h>

#include "meson-di.h"
#include "meson-di-regs.h"

#define DI_HOLD_LINE		10
#define DI_FIFO_SIZE		0xc0

/* Debug knobs for on-hardware bring-up (see enum meson_di_stage). */
static unsigned int stage = MESON_DI_STAGE_FULL;
module_param(stage, uint, 0644);
MODULE_PARM_DESC(stage, "debug: bound hardware programming (0=setup .. 5=full)");

static bool trace;
module_param(trace, bool, 0644);
MODULE_PARM_DESC(trace, "debug: log each hardware programming step");

/*
 * Debug: bound hw_init() to isolate which of its writes hangs (used when
 * stage >= INIT). 1=DI_CLKG_CTRL, 2=+DI_ARB_CTRL, 3=+DI FIFO sizes,
 * 4=+VD1_IF0 FIFO size (the display-shared one).
 */
static unsigned int init_step = 4;
module_param(init_step, uint, 0644);
MODULE_PARM_DESC(init_step, "debug: bound hw_init writes (1=clkg .. 4=all)");

/*
 * The DI pre/post blocks have their own VPU memory power domains, separate
 * from the top-level VPU power domain. They default to powered-down; the
 * engine can be configured but cannot process a field until they are powered
 * on by clearing their power-down bits in HHI_VPU_MEM_PD_REG0 (VPU_DI_PRE
 * [27:26], VPU_DI_POST [29:28], VPU_VIU_VD1 [5:4]). Bring-up workaround.
 */
#define HHI_VPU_MEM_PD_REG0_PHYS	0xc883c104
#define HHI_VPU_MEM_PD_DI		((0x3 << 26) | (0x3 << 28) | (0x3 << 4))

static bool clkb = true;
module_param(clkb, bool, 0644);
MODULE_PARM_DESC(clkb, "debug: enable DI memory power + VPU-level clkb gate");

/*
 * Debug: feed the pre readers from our own dma_alloc_coherent buffer (filled
 * with a constant) instead of the imported source dmabuf. Isolates whether the
 * engine can read/process at all (clock/engine OK) from whether it just cannot
 * reach the source buffer (input path). If set, the NR write-back to nr_dma
 * going non-zero proves the datapath ran.
 */
static bool test_src;
module_param(test_src, bool, 0644);
MODULE_PARM_DESC(test_src, "debug: read from an internal patterned buffer");

/* Traced jobs since the last (runtime-resume) init, to bound serial spam. */
static unsigned int dbg_jobs;

#define di_trace(di, fmt, ...)						\
	do {								\
		if (trace && dbg_jobs <= 2)				\
			dev_info((di)->dev, "di: " fmt "\n", ##__VA_ARGS__); \
	} while (0)

static void meson_di_config_canvas(struct meson_di *di, unsigned int idx,
				   dma_addr_t addr, u32 stride, u32 height)
{
	int ret = meson_canvas_config(di->canvas, di->canvas_idx[idx], addr,
				      stride, height, MESON_CANVAS_WRAP_NONE,
				      MESON_CANVAS_BLKMODE_LINEAR,
				      MESON_CANVAS_ENDIAN_SWAP64);

	if (ret)
		dev_warn(di->dev,
			 "di: canvas[%u] idx=%u cfg failed %d (addr=%pad stride=%u h=%u)\n",
			 idx, di->canvas_idx[idx], ret, &addr, stride, height);
	else if (trace && dbg_jobs <= 2)
		dev_info(di->dev,
			 "di: canvas[%u] idx=%u addr=%pad stride=%u h=%u\n",
			 idx, di->canvas_idx[idx], &addr, stride, height);
}

/*
 * Vendor per-stream NR2 (matnr) / EI / DNR / NR3-matnr / MCDI coefficient
 * configuration, captured verbatim from a live GXBB register-write trace.
 * These are constant (not size- or frame-dependent); the NR2 core will not
 * emit its write-back unless configured. Size-dependent registers
 * (NR2_FRM_SIZE, DNR_HVSIZE) and the NR2 enable are programmed per-format in
 * meson_di_hw_setup.
 */
static const struct meson_di_reg_val {
	u16 reg;
	u32 val;
} meson_di_nr_cfg[] = {
	/* NR2 matnr */
	{ 0x1745, 0x05056410 }, { 0x1746, 0x45056410 }, { 0x1747, 0x45056410 },
	{ 0x1748, 0x00000001 }, { 0x1749, 0x00007c3a }, { 0x174a, 0x00029e77 },
	{ 0x174b, 0x00009f1a }, { 0x174c, 0x0002822c }, { 0x174d, 0x00000077 },
	{ 0x174e, 0x00003030 }, { 0x174f, 0x00020030 }, { 0x1751, 0x0000132f },
	{ 0x1752, 0x0000008d }, { 0x1753, 0x0040ff00 }, { 0x1754, 0x00000004 },
	{ 0x1755, 0x000c2b64 }, { 0x1756, 0x00000000 }, { 0x1757, 0x00003608 },
	{ 0x1758, 0x00000420 }, { 0x1759, 0x00a06664 }, { 0x175a, 0x000e0000 },
	{ 0x175b, 0x00991c00 }, { 0x175c, 0x00991000 }, { 0x175d, 0x000f9f3e },
	{ 0x175e, 0x7292abcd }, { 0x175f, 0x1c23314f }, { 0x1760, 0x0f111317 },
	{ 0x1761, 0x08090a0c }, { 0x1762, 0x80a0e0ff }, { 0x1763, 0x04102050 },
	{ 0x1764, 0x00000002 }, { 0x1765, 0x00000000 }, { 0x1766, 0x20100400 },
	{ 0x1767, 0xc4804030 }, { 0x1768, 0xfffff0e0 }, { 0x1769, 0xffffffff },
	{ 0x176a, 0x00001132 }, { 0x176b, 0x00032020 }, { 0x176c, 0x00003333 },
	{ 0x176d, 0x4b4e4b4d }, { 0x176e, 0x00000111 }, { 0x176f, 0x32181818 },
	{ 0x1770, 0x80644032 }, { 0x1771, 0x9e808080 }, { 0x1772, 0xffffffff },
	{ 0x1773, 0x32181818 }, { 0x1774, 0x80644032 }, { 0x1775, 0xa5808080 },
	{ 0x1776, 0xffffffff }, { 0x1777, 0x00a06663 }, { 0x179c, 0x0000011b },
	{ 0x179d, 0x00202220 },
	/* EI coefficient LUTs */
	{ 0x171a, 0x151b3084 }, { 0x171b, 0x5273204f }, { 0x171c, 0x50232815 },
	{ 0x171d, 0x2fb56650 }, { 0x171e, 0x230019a4 }, { 0x171f, 0x7cb9bb33 },
	{ 0x1793, 0x0842c6a9 }, { 0x179e, 0x486ab07a }, { 0x179f, 0xdb0c2503 },
	{ 0x17a8, 0x0f021414 },
	/* DNR */
	{ 0x2d04, 0x00000002 }, { 0x2d08, 0x00180767 }, { 0x2d09, 0x00180203 },
	{ 0x2d60, 0x00000800 },
	/* NR3 matnr */
	{ 0x2fd0, 0x02400210 }, { 0x2fd1, 0x88080808 }, { 0x2fd2, 0x41041008 },
	{ 0x2fd3, 0x00008053 }, { 0x2fd4, 0x20070002 }, { 0x2fd5, 0x04041020 },
	/* MCDI / motion */
	{ 0x2f0b, 0x004f6124 }, { 0x2f0c, 0x00007455 }, { 0x2f0d, 0x6014d409 },
	{ 0x2f3b, 0x0a010001 }, { 0x2f3c, 0x01010101 },
};

void meson_di_hw_init(struct meson_di *di)
{
	u32 clkg = DI_CLKG_CTRL_GXL;
	unsigned int i;

	if (di->match_data && di->match_data->hw_version == 2)
		clkg = DI_CLKG_CTRL_GXBB;

	dbg_jobs = 0;
	di_trace(di, "init: enter (stage=%u)", stage);

	if (stage < MESON_DI_STAGE_INIT) {
		di_trace(di, "init: skipped (no hw writes)");
		return;
	}

	/*
	 * vpu_clkb (the DI datapath clock) is enabled through the clock
	 * framework in the runtime-resume path. Here we only handle the VPU
	 * memory power domains and the VPU-level clkb gate.
	 */
	if (clkb) {
		void __iomem *p;

		/* Power on the DI pre/post VPU memory domains. */
		p = ioremap(HHI_VPU_MEM_PD_REG0_PHYS, 4);
		if (p) {
			u32 v = readl(p) & ~HHI_VPU_MEM_PD_DI;

			di_trace(di, "init: HHI_VPU_MEM_PD_REG0 <= 0x%08x", v);
			writel(v, p);
			iounmap(p);
		}

		/*
		 * Ungate vpu_clkb at the VPU level so it actually reaches the DI
		 * NR/motion datapath. clkb_gate applies to all GX; GXL/GXM
		 * (hw_version != 2) additionally need clkb_gen_en.
		 */
		di_trace(di, "init: VPU_CLK_GATE clkb");
		di_update_bits(di, VPU_CLK_GATE, VPU_CLK_GATE_CLKB,
			       VPU_CLK_GATE_CLKB);
		if (di->match_data && di->match_data->hw_version != 2)
			di_update_bits(di, VPU_CLK_GATE, VPU_CLK_GATE_CLKB_GEN,
				       VPU_CLK_GATE_CLKB_GEN);
	}

	/* Ungate the DI clocks. */
	di_trace(di, "init: DI_CLKG_CTRL=0x%08x", clkg);
	di_write(di, DI_CLKG_CTRL, clkg);
	if (init_step < 2) {
		di_trace(di, "init: stop after clkg (init_step=%u)", init_step);
		return;
	}

	/* Enable DI MIF bus arbitration: request-enable bits 0,1,8,9. */
	di_trace(di, "init: DI_ARB_CTRL");
	di_update_bits(di, DI_ARB_CTRL, 0xffff, 0x0303);
	if (init_step < 3) {
		di_trace(di, "init: stop after arb (init_step=%u)", init_step);
		return;
	}

	/*
	 * Program the DI read-MIF FIFO sizes. A reader whose luma FIFO size is
	 * left at 0 can never fill, so it issues no DDR reads and stalls the
	 * pre pipeline. chan2 feeds the motion engine and must be sized too.
	 */
	di_trace(di, "init: DI FIFO sizes");
	di_write(di, DI_INP_LUMA_FIFO_SIZE, DI_FIFO_SIZE);
	di_write(di, DI_MEM_LUMA_FIFO_SIZE, DI_FIFO_SIZE);
	di_write(di, DI_CHAN2_LUMA_FIFO_SIZE, DI_FIFO_SIZE);
	di_write(di, DI_IF1_LUMA_FIFO_SIZE, DI_FIFO_SIZE);
	if (init_step < 4) {
		di_trace(di, "init: stop after DI FIFOs (init_step=%u)", init_step);
		return;
	}

	/* VD1_IF0 is the display-shared video-plane MIF. */
	di_trace(di, "init: VD1_IF0 FIFO size");
	di_write(di, VD1_IF0_LUMA_FIFO_SIZE, DI_FIFO_SIZE);

	/* Pre-stage FIFO hold configuration. */
	di_write(di, DI_PRE_HOLD, BIT(31) | (31 << 16) | 31);

	/* Edge-interpolation block. */
	di_write(di, DI_EI_CTRL0, 0x00ff0100);
	di_write(di, DI_EI_CTRL1, 0x5a0a0f2d);
	di_write(di, DI_EI_CTRL2, 0x050a0a5d);
	di_write(di, DI_EI_CTRL3, 0x80000013);

	/* Motion-adaptive block. */
	di_write(di, DI_MTN_1_CTRL4, 0x01800880);
	di_write(di, DI_MTN_1_CTRL7, 0x0a800480);
	di_write(di, DI_MTN_1_CTRL1, 0xa0202015);
	di_write(di, DI_MTN_CTRL1, 0x00020002);

	/* Noise-reduction block (NR3 temporal + DNR). */
	di_trace(di, "init: NR/EI/MA blocks");
	di_write(di, DNR_CTRL, 0x0001df00);
	di_write(di, NR3_MODE, 0x00000003);
	di_write(di, NR3_COOP_PARA, 0x0028ff00);
	di_write(di, NR3_CNOOP_GAIN, 0x00881900);
	di_write(di, NR3_YMOT_PARA, 0x000c0a1e);
	di_write(di, NR3_CMOT_PARA, 0x0008140f);
	di_write(di, NR3_SUREMOT_YGAIN, 0x100c4014);
	di_write(di, NR3_SUREMOT_CGAIN, 0x22264014);

	/*
	 * NR2 (matnr) / DNR / NR3-matnr / MCDI / EI coefficient block. The NR2
	 * core produces the pre write-back, and enabled-but-unconfigured it
	 * emits nothing, so these must be programmed. Values are the vendor's
	 * fixed per-stream configuration captured from a live register trace;
	 * size-dependent registers (NR2_FRM_SIZE, DNR_HVSIZE, NR2_SW_EN enable)
	 * are programmed per-format in meson_di_hw_setup.
	 */
	for (i = 0; i < ARRAY_SIZE(meson_di_nr_cfg); i++)
		di_write(di, meson_di_nr_cfg[i].reg, meson_di_nr_cfg[i].val);

	di_trace(di, "init: done");
}

void meson_di_hw_disable(struct meson_di *di)
{
	di_write(di, DI_PRE_CTRL, 0);
	di_write(di, DI_POST_CTRL, 0);
	di_write(di, DI_CLKG_CTRL, DI_CLKG_CTRL_OFF);
}

/*
 * Halt the pipeline and mask/clear all interrupt sources. Used to fence the
 * hardware when a job is torn down mid-flight.
 */
void meson_di_hw_stop(struct meson_di *di)
{
	di_write(di, DI_PRE_CTRL, 0);
	di_write(di, DI_POST_CTRL, 0);
	di_write(di, DI_INTR_CTRL,
		 (GENMASK(9, 0) << DI_INTR_MASK_SHIFT) | GENMASK(15, 0));
}

/*
 * Program the frame geometry and interrupt routing. Called once per stream
 * when the format is known.
 */
void meson_di_hw_setup(struct meson_di_ctx *ctx)
{
	struct meson_di *di = ctx->di;
	u32 width = ctx->in.width;
	u32 height = ctx->in.height;
	u32 field_height = height / 2;
	u32 mask;

	if (stage < MESON_DI_STAGE_SETUP) {
		di_trace(di, "setup: skipped (stage=%u)", stage);
		return;
	}

	/* Pre operates on a single field, post on the full progressive frame. */
	di_write(di, DI_PRE_SIZE, (width - 1) | ((field_height - 1) << 16));
	di_write(di, DI_POST_SIZE, (width - 1) | ((height - 1) << 16));

	/* Per-field noise-reduction sizing and enable. */
	di_write(di, NR2_FRM_SIZE, (field_height << 16) | width);
	di_update_bits(di, NR2_SW_EN, BIT(4), BIT(4));
	di_write(di, DNR_HVSIZE, (width << 16) | field_height);
	di_write(di, DNR_CTRL, 0x0001df00);

	/* Blend window covers the whole frame. */
	di_write(di, DI_BLEND_REG0_X, (width - 1));
	di_write(di, DI_BLEND_REG0_Y, (height - 1));
	di_write(di, DI_BLEND_REG1_X, (width - 1));
	di_write(di, DI_BLEND_REG1_Y, (height - 1));
	di_write(di, DI_BLEND_REG2_X, (width - 1));
	di_write(di, DI_BLEND_REG2_Y, (height - 1));
	di_write(di, DI_BLEND_REG3_X, (width - 1));
	di_write(di, DI_BLEND_REG3_Y, (height - 1));

	/*
	 * The pre stage completes on the motion write-back (MTNWR) because
	 * MTN-after-NR makes it the last event; NRWR is left masked. Unmask
	 * MTNWR (pre) and DIWR (post), mask everything else. The low half-word
	 * is write-1-to-clear, so clear any pending status at the same time.
	 */
	mask = GENMASK(9, 0) << DI_INTR_MASK_SHIFT;
	mask &= ~((DI_INTR_MTNWR_DONE | DI_INTR_DIWR_DONE)
		  << DI_INTR_MASK_SHIFT);
	di_write(di, DI_INTR_CTRL, mask | GENMASK(15, 0));

	di_trace(di, "setup: size/blend/intr %ux%u", width, height);

	if (stage < MESON_DI_STAGE_MUX) {
		di_trace(di, "setup: skip VIU_MISC_CTRL0 (stage=%u)", stage);
		return;
	}

	/*
	 * Enable the motion-adaptive DI datapath. VIU_MISC_CTRL0 bits[19:16]
	 * gate it: non-zero disables MA and routes IF0 straight to VPP (display
	 * bypass). Clear them so the pre/post stages actually process the field
	 * rather than being bypassed to the display.
	 */
	di_trace(di, "setup: VIU_MISC_CTRL0 mux");
	di_update_bits(di, VIU_MISC_CTRL0, 0xf << 16, 0);
	di_trace(di, "setup: done");
}

/* Register set of a pre-stage read MIF (input / memory). */
struct meson_di_mif_regs {
	u16 gen_reg;
	u16 gen_reg2;
	u16 canvas0;
	u16 luma_x0;
	u16 luma_y0;
	u16 chroma_x0;
	u16 chroma_y0;
	u16 rpt_loop;
	u16 luma_rpt_pat;
	u16 chroma_rpt_pat;
	u16 dummy_pixel;
	u16 fmt_ctrl;
	u16 fmt_w;
};

static const struct meson_di_mif_regs di_inp_mif = {
	.gen_reg = DI_INP_GEN_REG, .gen_reg2 = DI_INP_GEN_REG2,
	.canvas0 = DI_INP_CANVAS0,
	.luma_x0 = DI_INP_LUMA_X0, .luma_y0 = DI_INP_LUMA_Y0,
	.chroma_x0 = DI_INP_CHROMA_X0, .chroma_y0 = DI_INP_CHROMA_Y0,
	.rpt_loop = DI_INP_RPT_LOOP,
	.luma_rpt_pat = DI_INP_LUMA0_RPT_PAT,
	.chroma_rpt_pat = DI_INP_CHROMA0_RPT_PAT,
	.dummy_pixel = DI_INP_DUMMY_PIXEL,
	.fmt_ctrl = DI_INP_FMT_CTRL, .fmt_w = DI_INP_FMT_W,
};

static const struct meson_di_mif_regs di_mem_mif = {
	.gen_reg = DI_MEM_GEN_REG, .gen_reg2 = DI_MEM_GEN_REG2,
	.canvas0 = DI_MEM_CANVAS0,
	.luma_x0 = DI_MEM_LUMA_X0, .luma_y0 = DI_MEM_LUMA_Y0,
	.chroma_x0 = DI_MEM_CHROMA_X0, .chroma_y0 = DI_MEM_CHROMA_Y0,
	.rpt_loop = DI_MEM_RPT_LOOP,
	.luma_rpt_pat = DI_MEM_LUMA0_RPT_PAT,
	.chroma_rpt_pat = DI_MEM_CHROMA0_RPT_PAT,
	.dummy_pixel = DI_MEM_DUMMY_PIXEL,
	.fmt_ctrl = DI_MEM_FMT_CTRL, .fmt_w = DI_MEM_FMT_W,
};

static const struct meson_di_mif_regs di_chan2_mif = {
	.gen_reg = DI_CHAN2_GEN_REG, .gen_reg2 = DI_CHAN2_GEN_REG2,
	.canvas0 = DI_CHAN2_CANVAS0,
	.luma_x0 = DI_CHAN2_LUMA_X0, .luma_y0 = DI_CHAN2_LUMA_Y0,
	.chroma_x0 = DI_CHAN2_CHROMA_X0, .chroma_y0 = DI_CHAN2_CHROMA_Y0,
	.rpt_loop = DI_CHAN2_RPT_LOOP,
	.luma_rpt_pat = DI_CHAN2_LUMA0_RPT_PAT,
	.chroma_rpt_pat = DI_CHAN2_CHROMA0_RPT_PAT,
	.dummy_pixel = DI_CHAN2_DUMMY_PIXEL,
	.fmt_ctrl = DI_CHAN2_FMT_CTRL, .fmt_w = DI_CHAN2_FMT_W,
};

/*
 * Program a pre-stage read MIF for one interlaced field of a 2-plane NV12
 * buffer. @phase selects the top (0) or bottom (1) field. The cntl_enable
 * bit (bit0 of the GEN_REG) is left clear and set when the pipeline is kicked.
 */
static void meson_di_set_pre_mif(struct meson_di *di,
				 const struct meson_di_mif_regs *r,
				 u8 canvas_y, u8 canvas_c, u32 width,
				 u32 field_height, u8 phase)
{
	u32 vt_ini_phase = phase ? 0xa : 0xe;

	/*
	 * no-dummy, hold-line, push-dummy, y/cb/cr bursts of 3/1/1, chroma
	 * repeat-last-line and separate_en (2-plane NV12). reset_on_gofield
	 * (bit29) must be 0 on GXBB and later; setting it makes the MIF reset
	 * its read address every go-field so no pixels are delivered.
	 */
	di_write(di, r->gen_reg,
		 BIT(25) | (DI_HOLD_LINE << 19) | BIT(18) |
		 BIT(12) | BIT(10) | (3 << 8) | BIT(6) | BIT(1));
	/* GEN_REG2 bit0 enables 2-plane NV12 (separate luma/chroma canvas). */
	di_write(di, r->gen_reg2, BIT(0));

	di_write(di, r->canvas0, canvas_y | (canvas_c << 8));
	di_write(di, r->luma_x0, (width - 1) << 16);
	di_write(di, r->luma_y0, (field_height - 1) << 16);
	di_write(di, r->chroma_x0, (width / 2 - 1) << 16);
	di_write(di, r->chroma_y0, (field_height / 2 - 1) << 16);

	/* Repeat first/last line so a single field reads correctly. */
	di_write(di, r->rpt_loop, 0x1010);
	di_write(di, r->luma_rpt_pat, 0x80);
	di_write(di, r->chroma_rpt_pat, 0x80);
	di_write(di, r->dummy_pixel, 0x00808000);

	/* 4:2:0 vertical/horizontal chroma format converter. */
	di_write(di, r->fmt_ctrl,
		 (1 << 21) | BIT(20) | BIT(16) | (vt_ini_phase << 8) |
		 (8 << 1) | 1);
	di_write(di, r->fmt_w, (width << 16) | (width / 2));
}

/*
 * Program a read MIF that consumes one interlaced field of an NV12 buffer.
 * @phase selects the top (0) or bottom (1) field lines.
 */
static void meson_di_set_read_mif(struct meson_di *di, u16 gen, u16 canvas,
				  u16 luma_x, u16 luma_y, u16 chroma_x,
				  u16 chroma_y, u8 canvas_y, u8 canvas_c,
				  u32 width, u32 field_height, u8 phase)
{
	/*
	 * NV12 semi-planar, separate luma/chroma canvases, little-endian,
	 * bursts of 3/1/1 for y/cb/cr, hold-line as configured. Bit0 (enable)
	 * is left clear here and set when the pipeline is kicked.
	 */
	di_write(di, gen, (DI_HOLD_LINE << 19) | (3 << 8) | (1 << 10) |
		 (1 << 12) | (1 << 25) | (phase << 4));
	di_write(di, canvas, canvas_y | (canvas_c << 8));

	di_write(di, luma_x, (width - 1) << 16);
	di_write(di, luma_y, (field_height - 1) << 16);
	di_write(di, chroma_x, (width / 2 - 1) << 16);
	di_write(di, chroma_y, (field_height / 2 - 1) << 16);
}

/*
 * Program a write-back MIF (NR or DI output). This is a single-canvas MIF: for
 * NV12 the canvas describes the luma plane and the hardware writes chroma at the
 * following stride*height offset. burst_lim=2 (bits[27:26]) and the DDR write
 * enable (bit30). Note bit24 is a per-field write-clock gate toggled at kick
 * time, not here.
 */
static void meson_di_set_wr_mif(struct meson_di *di, u16 x, u16 y, u16 ctrl,
				u8 canvas, u32 width, u32 height)
{
	di_write(di, x, width - 1);
	di_write(di, y, (3u << 30) | (height - 1));
	di_write(di, ctrl, canvas | (2 << 26) | BIT(30));
}

void meson_di_hw_pre(struct meson_di_ctx *ctx, struct vb2_v4l2_buffer *src)
{
	struct meson_di *di = ctx->di;
	dma_addr_t src_y = vb2_dma_contig_plane_dma_addr(&src->vb2_buf, 0);
	dma_addr_t src_c = vb2_dma_contig_plane_dma_addr(&src->vb2_buf, 1);
	u32 width = ctx->in.width;
	u32 height = ctx->in.height;
	u32 field_height = height / 2;
	u32 stride = ctx->in.plane_fmt[0].bytesperline;
	bool bottom_first = ctx->field == V4L2_FIELD_INTERLACED_BT;
	u32 pre_ctrl;

	dbg_jobs++;

	if (stage < MESON_DI_STAGE_PRE_CFG) {
		di_trace(di, "pre: skipped (stage=%u)", stage);
		return;
	}

	di_trace(di, "pre: canvas + MIF config");

	/*
	 * Route the pre stage's current-field input read from memory (DDR)
	 * rather than the live VDIN video path. Without this the engine waits
	 * for VDIN input that never arrives and issues no memory transactions.
	 */
	di_update_bits(di, VIUB_MISC_CTRL0, VIUB_MISC_CTRL0_INP_VDIN, 0);

	/*
	 * Zero the start of the NR and motion write-back buffers so the timeout
	 * dump can tell whether the engine actually wrote anything (the buffers
	 * are dma_alloc_coherent, so a device write is visible to the CPU).
	 */
	if (di->nr_buf)
		memset(di->nr_buf, 0, 64);
	if (di->mtn_buf)
		memset(di->mtn_buf, 0, 64);

	/*
	 * Debug: read from our own coherent buffer (cont_dma) filled with a
	 * constant, instead of the imported source dmabuf. If the engine then
	 * writes nr_dma (wrote? nr!=0) the datapath and clock are fine and the
	 * source dmabuf was the problem; if it still writes nothing the fault
	 * is the clock/engine, not the input.
	 */
	if (test_src && di->cont_buf) {
		memset(di->cont_buf, 0x3f, di->cont_size);
		src_y = di->cont_dma;
		src_c = di->cont_dma;
		di_trace(di, "pre: TEST reading from cont_dma=%pad", &di->cont_dma);
	}

	/* Source planes: full-frame canvases, field selection is done by the MIF. */
	meson_di_config_canvas(di, MESON_DI_CANVAS_SRC_Y, src_y, stride,
			       height);
	meson_di_config_canvas(di, MESON_DI_CANVAS_SRC_C, src_c, stride,
			       height / 2);
	/* NR write-back scratch (denoised current field). */
	meson_di_config_canvas(di, MESON_DI_CANVAS_NR_Y, di->nr_dma, stride,
			       field_height);
	meson_di_config_canvas(di, MESON_DI_CANVAS_NR_C,
			       di->nr_dma + stride * field_height, stride,
			       field_height / 2);
	/* Motion information buffer. */
	meson_di_config_canvas(di, MESON_DI_CANVAS_MTN, di->mtn_dma, width,
			       field_height);

	/* Current field read MIF. */
	meson_di_set_pre_mif(di, &di_inp_mif,
			     di->canvas_idx[MESON_DI_CANVAS_SRC_Y],
			     di->canvas_idx[MESON_DI_CANVAS_SRC_C],
			     width, field_height, 0);

	/* Opposite field read MIF (memory). */
	meson_di_set_pre_mif(di, &di_mem_mif,
			     di->canvas_idx[MESON_DI_CANVAS_SRC_Y],
			     di->canvas_idx[MESON_DI_CANVAS_SRC_C],
			     width, field_height, 1);

	/*
	 * Channel-2 read MIF: the same-parity field the motion engine compares
	 * the current field against. With no cross-frame history maintained yet
	 * it reads the current field's source, so the first frames measure
	 * near-zero motion; this still exercises the motion write-back that
	 * signals completion. Matched-parity, so the top field like INP.
	 */
	meson_di_set_pre_mif(di, &di_chan2_mif,
			     di->canvas_idx[MESON_DI_CANVAS_SRC_Y],
			     di->canvas_idx[MESON_DI_CANVAS_SRC_C],
			     width, field_height, 0);

	/* NR and motion write-back MIFs. */
	meson_di_set_wr_mif(di, DI_NRWR_X, DI_NRWR_Y, DI_NRWR_CTRL,
			    di->canvas_idx[MESON_DI_CANVAS_NR_Y],
			    width, field_height);
	di_write(di, DI_MTNWR_X, (width - 1) << 16);
	di_write(di, DI_MTNWR_Y, (field_height - 1) << 16);
	di_write(di, DI_MTNWR_CTRL, di->canvas_idx[MESON_DI_CANVAS_MTN]);

	/*
	 * Contour write-back MIF. The motion engine (enabled by DI_MTN_1_CTRL1
	 * bit31) always emits a contour map, so it must have a valid target; an
	 * unconfigured MIF would default to canvas index 0 and scribble over
	 * whatever buffer that canvas describes. The contour read-back is left
	 * disabled (DI_PRE_CTRL bit25 clear, DI_MTN_1_CTRL1 bit30 mtn_init set),
	 * so no history is consumed. The read MIFs are pointed at the same
	 * buffer as a safe default.
	 */
	meson_di_config_canvas(di, MESON_DI_CANVAS_CONT, di->cont_dma, width,
			       field_height);
	di_write(di, DI_CONTWR_X, width - 1);
	di_write(di, DI_CONTWR_Y, field_height - 1);
	di_write(di, DI_CONTWR_CTRL, di->canvas_idx[MESON_DI_CANVAS_CONT]);
	di_write(di, DI_CONTPRD_X, width - 1);
	di_write(di, DI_CONTPRD_Y, field_height - 1);
	di_write(di, DI_CONTP2RD_X, width - 1);
	di_write(di, DI_CONTP2RD_Y, field_height - 1);
	di_write(di, DI_CONTRD_CTRL,
		 (di->canvas_idx[MESON_DI_CANVAS_CONT] << 8) |
		 di->canvas_idx[MESON_DI_CANVAS_CONT]);

	/*
	 * Motion-adaptive pre pass: noise reduction plus per-pixel motion
	 * detection. The motion engine reads the current NR output and the
	 * chan2 history field and writes the motion map (MTNWR); with
	 * MTN-after-NR set, that write-back is the last event of the pass and
	 * marks it done. This reproduces the vendor's steady-state DI_PRE_CTRL
	 * (0x004a035c before the reset/enable bits are applied). Bit29 selects
	 * which field is read first.
	 */
	pre_ctrl = DI_PRE_CTRL_NR_EN | DI_PRE_CTRL_MTN_EN |
		   DI_PRE_CTRL_PD32_CHECK | DI_PRE_CTRL_PD22_CHECK |
		   DI_PRE_CTRL_CHECK_AFTER_NR | DI_PRE_CTRL_CHAN2_HIST_EN |
		   DI_PRE_CTRL_CHAN2_EN | DI_PRE_CTRL_LINEBUF2_EN |
		   DI_PRE_CTRL_MTN_AFTER_NR |
		   DI_PRE_CTRL_HOLD_LINE(DI_HOLD_LINE) |
		   (bottom_first ? DI_PRE_CTRL_FIELD_NUM : 0);
	if (stage < MESON_DI_STAGE_PRE) {
		di_write(di, DI_PRE_CTRL, pre_ctrl);
		di_trace(di, "pre: skip trigger (stage=%u)", stage);
		return;
	}

	/*
	 * Launch, matching the vendor's exact ordered sequence:
	 *   1. assert soft + frame reset,
	 *   2. write the config holding frame reset asserted,
	 *   3. re-write the config with frame reset released,
	 *   4. enable the read MIFs (GEN bit0) and ungate the NR write-back.
	 * The MIF enable in step 4 is the actual "go" that starts the field, so
	 * it must come last, after the config is fully in place and frame reset
	 * has been released.
	 */
	di_trace(di, "pre: trigger");
	di_update_bits(di, DI_PRE_CTRL,
		       DI_PRE_CTRL_SOFT_RST | DI_PRE_CTRL_FRAME_RST,
		       DI_PRE_CTRL_SOFT_RST | DI_PRE_CTRL_FRAME_RST);
	di_write(di, DI_PRE_CTRL, pre_ctrl | DI_PRE_CTRL_FRAME_RST);
	di_write(di, DI_PRE_CTRL, pre_ctrl);

	di_trace(di, "pre: enable MIFs");
	di_update_bits(di, DI_NRWR_CTRL, BIT(24), 0);
	di_update_bits(di, DI_INP_GEN_REG, DI_MIF_GEN_REG_EN, DI_MIF_GEN_REG_EN);
	di_update_bits(di, DI_MEM_GEN_REG, DI_MIF_GEN_REG_EN, DI_MIF_GEN_REG_EN);
	di_update_bits(di, DI_CHAN2_GEN_REG, DI_MIF_GEN_REG_EN,
		       DI_MIF_GEN_REG_EN);
}

void meson_di_hw_post(struct meson_di_ctx *ctx, struct vb2_v4l2_buffer *dst,
		      bool bottom_field)
{
	struct meson_di *di = ctx->di;
	dma_addr_t dst_y = vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0);
	dma_addr_t dst_c = vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 1);
	u32 width = ctx->in.width;
	u32 height = ctx->in.height;
	u32 field_height = height / 2;
	u32 stride = ctx->out.plane_fmt[0].bytesperline;

	if (stage < MESON_DI_STAGE_POST_CFG) {
		di_trace(di, "post: skipped (stage=%u)", stage);
		return;
	}

	di_trace(di, "post: canvas + MIF config (incl VD1_IF0)");

	/* Output planes. */
	meson_di_config_canvas(di, MESON_DI_CANVAS_OUT_Y, dst_y, stride,
			       height);
	meson_di_config_canvas(di, MESON_DI_CANVAS_OUT_C, dst_c, stride,
			       height / 2);

	/* Current field (IF0, on GX the shared VD1 video plane MIF). */
	meson_di_set_read_mif(di, VD1_IF0_GEN_REG, VD1_IF0_CANVAS0,
			      VD1_IF0_LUMA_X0, VD1_IF0_LUMA_Y0,
			      VD1_IF0_CHROMA_X0, VD1_IF0_CHROMA_Y0,
			      di->canvas_idx[MESON_DI_CANVAS_SRC_Y],
			      di->canvas_idx[MESON_DI_CANVAS_SRC_C],
			      width, field_height, bottom_field ? 1 : 0);
	/* Opposite field (IF1). */
	meson_di_set_read_mif(di, DI_IF1_GEN_REG, DI_IF1_CANVAS0,
			      DI_IF1_LUMA_X0, DI_IF1_LUMA_Y0,
			      DI_IF1_CHROMA_X0, DI_IF1_CHROMA_Y0,
			      di->canvas_idx[MESON_DI_CANVAS_SRC_Y],
			      di->canvas_idx[MESON_DI_CANVAS_SRC_C],
			      width, field_height, bottom_field ? 0 : 1);

	/* Motion read MIF. */
	di_write(di, DI_MTNPRD_X, (width - 1) << 16);
	di_write(di, DI_MTNPRD_Y, (field_height - 1) << 16);
	di_write(di, DI_MTNRD_CTRL, di->canvas_idx[MESON_DI_CANVAS_MTN] << 8);

	/* Progressive output write-back MIF. */
	meson_di_set_wr_mif(di, DI_DIWR_X, DI_DIWR_Y, DI_DIWR_CTRL,
			    di->canvas_idx[MESON_DI_CANVAS_OUT_Y],
			    width, height);

	/* Motion-adaptive blend. */
	di_write(di, DI_BLEND_CTRL, DI_BLEND_CTRL_EN | DI_BLEND_CTRL_FIX(7) |
		 DI_BLEND_CTRL_MODE(1));

	if (stage < MESON_DI_STAGE_FULL) {
		di_trace(di, "post: skip trigger (stage=%u)", stage);
		return;
	}

	/* Enable the read MIFs. */
	di_update_bits(di, VD1_IF0_GEN_REG, DI_MIF_GEN_REG_EN,
		       DI_MIF_GEN_REG_EN);
	di_update_bits(di, DI_IF1_GEN_REG, DI_MIF_GEN_REG_EN,
		       DI_MIF_GEN_REG_EN);

	/*
	 * Program and kick the post stage. Output goes to DDR (bit7), not VPP.
	 * Bit29 selects the output field, bits[31:30] trigger the pass.
	 */
	di_trace(di, "post: trigger");
	di_write(di, DI_POST_CTRL,
		 DI_POST_CTRL_LBUF0_EN | DI_POST_CTRL_EI_EN |
		 DI_POST_CTRL_MTN_LBUF_EN | DI_POST_CTRL_MTNP_RD_EN |
		 DI_POST_CTRL_BLEND_EN | DI_POST_CTRL_MUX_EN |
		 DI_POST_CTRL_DDR_EN |
		 DI_POST_CTRL_HOLD_LINE(DI_HOLD_LINE) |
		 (bottom_field ? DI_POST_CTRL_FIELD_NUM : 0) |
		 DI_POST_CTRL_FRAME_RST | DI_POST_CTRL_SOFT_RST);
}

bool meson_di_hw_pre_done(struct meson_di *di)
{
	u32 val = di_read(di, DI_INTR_CTRL);

	di_trace(di, "de_irq: DI_INTR_CTRL=0x%08x", val);

	/*
	 * The motion write-back (MTNWR) completing marks the pre stage done:
	 * with MTN-after-NR it is written after the NR pass, so it is the last
	 * event of the field.
	 */
	if (!(val & DI_INTR_MTNWR_DONE))
		return false;

	/* Acknowledge the pre sources (write-1-to-clear), keep the post bit. */
	di_write(di, DI_INTR_CTRL, val & ~DI_INTR_DIWR_DONE);

	/* Disable the pre read MIFs. */
	di_update_bits(di, DI_INP_GEN_REG, DI_MIF_GEN_REG_EN, 0);
	di_update_bits(di, DI_MEM_GEN_REG, DI_MIF_GEN_REG_EN, 0);
	di_update_bits(di, DI_CHAN2_GEN_REG, DI_MIF_GEN_REG_EN, 0);

	di_trace(di, "de_irq: pre done");

	return true;
}

bool meson_di_hw_post_done(struct meson_di *di)
{
	u32 val = di_read(di, DI_INTR_CTRL);

	di_trace(di, "post_irq: DI_INTR_CTRL=0x%08x", val);

	if (!(val & DI_INTR_DIWR_DONE))
		return false;

	/* Acknowledge the post source and drop the DDR write enable. */
	di_write(di, DI_INTR_CTRL, (val & GENMASK(31, 16)) | DI_INTR_DIWR_DONE);
	di_update_bits(di, DI_POST_CTRL, DI_POST_CTRL_DDR_EN, 0);

	/* Disable the post read MIFs. */
	di_update_bits(di, VD1_IF0_GEN_REG, DI_MIF_GEN_REG_EN, 0);
	di_update_bits(di, DI_IF1_GEN_REG, DI_MIF_GEN_REG_EN, 0);

	di_trace(di, "post_irq: DIWR done");

	return true;
}

/* Dump the key DI registers, for diagnosing a stalled job. */
void meson_di_hw_dump(struct meson_di *di)
{
	dev_warn(di->dev,
		 "di: PRE_CTRL=%08x POST_CTRL=%08x INTR_CTRL=%08x ARB=%08x CLKG=%08x\n",
		 di_read(di, DI_PRE_CTRL), di_read(di, DI_POST_CTRL),
		 di_read(di, DI_INTR_CTRL), di_read(di, DI_ARB_CTRL),
		 di_read(di, DI_CLKG_CTRL));
	dev_warn(di->dev,
		 "di: INP_GEN=%08x MEM_GEN=%08x NRWR_CTRL=%08x MTNWR_CTRL=%08x\n",
		 di_read(di, DI_INP_GEN_REG), di_read(di, DI_MEM_GEN_REG),
		 di_read(di, DI_NRWR_CTRL), di_read(di, DI_MTNWR_CTRL));
	dev_warn(di->dev,
		 "di: INP_CANVAS=%08x INP_X=%08x INP_Y=%08x GEN2=%08x PRE_SIZE=%08x\n",
		 di_read(di, DI_INP_CANVAS0), di_read(di, DI_INP_LUMA_X0),
		 di_read(di, DI_INP_LUMA_Y0), di_read(di, DI_INP_GEN_REG2),
		 di_read(di, DI_PRE_SIZE));
	dev_warn(di->dev,
		 "di: NR2_SW_EN=%08x NR2_FRM=%08x DNR_CTRL=%08x DNR_HV=%08x\n",
		 di_read(di, NR2_SW_EN), di_read(di, NR2_FRM_SIZE),
		 di_read(di, DNR_CTRL), di_read(di, DNR_HVSIZE));
	dev_warn(di->dev,
		 "di: MTN_1_CTRL1=%08x CHAN2_GEN=%08x CONTWR_CTRL=%08x MTNWR_X=%08x\n",
		 di_read(di, DI_MTN_1_CTRL1), di_read(di, DI_CHAN2_GEN_REG),
		 di_read(di, DI_CONTWR_CTRL), di_read(di, DI_MTNWR_X));
	dev_warn(di->dev, "di: VPU_CLK_GATE=%08x ARB_DBG_STAT=%08x WRARB_REQEN=%08x\n",
		 di_read(di, VPU_CLK_GATE),
		 di_read(di, VPU_ARB_DBG_STAT_L1C1),
		 di_read(di, VPU_WRARB_REQEN_SLV_L1C1));
	dev_warn(di->dev, "di: VIU_MISC_CTRL0=%08x VIUB_MISC_CTRL0=%08x\n",
		 di_read(di, VIU_MISC_CTRL0), di_read(di, VIUB_MISC_CTRL0));
	if (di->nr_buf && di->mtn_buf) {
		const u32 *nr = di->nr_buf;
		const u32 *mtn = di->mtn_buf;

		dev_warn(di->dev,
			 "di: wrote? nr=%08x %08x mtn=%08x %08x (0=engine never wrote)\n",
			 nr[0], nr[1], mtn[0], mtn[1]);
	}
	dev_warn(di->dev, "di: FRM_MOTN=%08x (non-zero=engine processed pixels)\n",
		 di_read(di, DET3D_RO_FRM_MOTN));
}
