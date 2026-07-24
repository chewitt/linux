/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Amlogic GX SoC Video Deinterlacer (DI) register definitions
 *
 * Copyright (C) 2026 Christian Hewitt <christianshewitt@gmail.com>
 *
 * Register offsets are VCBUS word offsets, matching the vendor register map.
 * The accessors in meson-di.h rebase them onto the mapped register window.
 */

#ifndef __MESON_DI_REGS_H
#define __MESON_DI_REGS_H

/* Top-level control */
#define DI_PRE_CTRL			0x1700
#define DI_POST_CTRL			0x1701
#define DI_POST_SIZE			0x1702
#define DI_PRE_SIZE			0x1703
#define DI_CANVAS_URGENT0		0x170a
#define DI_BLEND_CTRL			0x170d
#define DI_ARB_CTRL			0x170f
#define DI_CLKG_CTRL			0x1718
#define DI_INTR_CTRL			0x1730
#define DI_MTN_1_CTRL1			0x1740

/* Edge interpolation, motion and noise-reduction block init */
#define DI_EI_CTRL0			0x1704
#define DI_EI_CTRL1			0x1705
#define DI_EI_CTRL2			0x1706
#define DI_EI_CTRL3			0x1719
#define DI_MTN_CTRL1			0x170c
#define DI_PRE_HOLD			0x1733
#define DI_MTN_1_CTRL4			0x1743
#define DI_MTN_1_CTRL7			0x17aa
#define NR2_SW_EN			0x174f
#define NR2_FRM_SIZE			0x1750
#define DNR_CTRL			0x2d00
#define DNR_HVSIZE			0x2d01
#define NR3_MODE			0x2ff0
#define NR3_COOP_PARA			0x2ff1
#define NR3_CNOOP_GAIN			0x2ff2
#define NR3_YMOT_PARA			0x2ff3
#define NR3_CMOT_PARA			0x2ff4
#define NR3_SUREMOT_YGAIN		0x2ff5
#define NR3_SUREMOT_CGAIN		0x2ff6

/* Blend window */
#define DI_BLEND_REG0_X			0x1710
#define DI_BLEND_REG0_Y			0x1711
#define DI_BLEND_REG1_X			0x1712
#define DI_BLEND_REG1_Y			0x1713
#define DI_BLEND_REG2_X			0x1714
#define DI_BLEND_REG2_Y			0x1715
#define DI_BLEND_REG3_X			0x1716
#define DI_BLEND_REG3_Y			0x1717

/* Simple write-back MIFs (canvas index carried in the CTRL register) */
#define DI_NRWR_X			0x17c0
#define DI_NRWR_Y			0x17c1
#define DI_NRWR_CTRL			0x17c2
#define DI_MTNWR_X			0x17c3
#define DI_MTNWR_Y			0x17c4
#define DI_MTNWR_CTRL			0x17c5
#define DI_DIWR_X			0x17c6
#define DI_DIWR_Y			0x17c7
#define DI_DIWR_CTRL			0x17c8
#define DI_MTNPRD_X			0x17cb
#define DI_MTNPRD_Y			0x17cc
#define DI_MTNRD_CTRL			0x17cd

/* Contour write / read-back MIFs (motion engine feedback map) */
#define DI_CONTWR_X			0x17a0
#define DI_CONTWR_Y			0x17a1
#define DI_CONTWR_CTRL			0x17a2
#define DI_CONTPRD_X			0x17a3
#define DI_CONTPRD_Y			0x17a4
#define DI_CONTP2RD_X			0x17a5
#define DI_CONTP2RD_Y			0x17a6
#define DI_CONTRD_CTRL			0x17a7

/* Input (current field) read MIF */
#define DI_INP_GEN_REG			0x17ce
#define DI_INP_CANVAS0			0x17cf
#define DI_INP_LUMA_X0			0x17d0
#define DI_INP_LUMA_Y0			0x17d1
#define DI_INP_CHROMA_X0		0x17d2
#define DI_INP_CHROMA_Y0		0x17d3
#define DI_INP_RPT_LOOP			0x17d4
#define DI_INP_LUMA0_RPT_PAT		0x17d5
#define DI_INP_CHROMA0_RPT_PAT		0x17d6
#define DI_INP_DUMMY_PIXEL		0x17d7
#define DI_INP_LUMA_FIFO_SIZE		0x17d8
#define DI_INP_FMT_CTRL			0x17d9
#define DI_INP_FMT_W			0x17da
#define DI_INP_GEN_REG2			0x1791
#define DI_INP_GEN_REG3			0x20a8

/* Memory (opposite field) read MIF */
#define DI_MEM_GEN_REG			0x17db
#define DI_MEM_CANVAS0			0x17dc
#define DI_MEM_LUMA_X0			0x17dd
#define DI_MEM_LUMA_Y0			0x17de
#define DI_MEM_CHROMA_X0		0x17df
#define DI_MEM_CHROMA_Y0		0x17e0
#define DI_MEM_GEN_REG2			0x1792
#define DI_MEM_RPT_LOOP			0x17e1
#define DI_MEM_LUMA0_RPT_PAT		0x17e2
#define DI_MEM_CHROMA0_RPT_PAT		0x17e3
#define DI_MEM_DUMMY_PIXEL		0x17e4
#define DI_MEM_LUMA_FIFO_SIZE		0x17e5
#define DI_MEM_FMT_CTRL			0x17e6
#define DI_MEM_FMT_W			0x17e7

/* Channel-2 read MIF (same-parity history field, feeds motion detection) */
#define DI_CHAN2_GEN_REG		0x17f5
#define DI_CHAN2_CANVAS0		0x17f6
#define DI_CHAN2_LUMA_X0		0x17f7
#define DI_CHAN2_LUMA_Y0		0x17f8
#define DI_CHAN2_CHROMA_X0		0x17f9
#define DI_CHAN2_CHROMA_Y0		0x17fa
#define DI_CHAN2_RPT_LOOP		0x17fb
#define DI_CHAN2_LUMA0_RPT_PAT		0x17b0
#define DI_CHAN2_CHROMA0_RPT_PAT	0x17b1
#define DI_CHAN2_DUMMY_PIXEL		0x17b2
#define DI_CHAN2_LUMA_FIFO_SIZE		0x17b3
#define DI_CHAN2_GEN_REG2		0x17b7
#define DI_CHAN2_FMT_CTRL		0x17b8
#define DI_CHAN2_FMT_W			0x17b9

/* IF1 (post opposite field) read MIF */
#define DI_IF1_GEN_REG			0x17e8
#define DI_IF1_CANVAS0			0x17e9
#define DI_IF1_LUMA_X0			0x17ea
#define DI_IF1_LUMA_Y0			0x17eb
#define DI_IF1_CHROMA_X0		0x17ec
#define DI_IF1_CHROMA_Y0		0x17ed
#define DI_IF1_RPT_LOOP			0x17ee
#define DI_IF1_LUMA0_RPT_PAT		0x17ef
#define DI_IF1_CHROMA0_RPT_PAT		0x17f0
#define DI_IF1_DUMMY_PIXEL		0x17f1
#define DI_IF1_LUMA_FIFO_SIZE		0x17f2
#define DI_IF1_FMT_CTRL			0x17f3
#define DI_IF1_FMT_W			0x17f4

/*
 * IF0 (post current field) read MIF. On GX this is physically the VD1 video
 * plane input MIF, which is shared with the display pipeline.
 */
#define VD1_IF0_GEN_REG			0x1a50
#define VD1_IF0_CANVAS0			0x1a52
#define VD1_IF0_LUMA_X0			0x1a54
#define VD1_IF0_LUMA_Y0			0x1a55
#define VD1_IF0_CHROMA_X0		0x1a56
#define VD1_IF0_CHROMA_Y0		0x1a57
#define VD1_IF0_RPT_LOOP		0x1a5b
#define VD1_IF0_LUMA0_RPT_PAT		0x1a5c
#define VD1_IF0_CHROMA0_RPT_PAT		0x1a5d
#define VD1_IF0_LUMA_FIFO_SIZE		0x1a63
#define VD1_IF0_GEN_REG2		0x1a6d
#define VD1_IF0_FMT_CTRL		0x1a68
#define VD1_IF0_FMT_W			0x1a69
#define VD1_IF0_LUMA_FIFO_SIZE_DEF	0xc0

/* Global VIU routing (shared with the display pipeline) */
#define VIU_MISC_CTRL0			0x1a06

/*
 * VIU-B misc control. Bit16 selects the DI input source: 0 routes the pre
 * stage's current-field read from memory (DDR), 1 from the live VDIN video
 * path. For memory-to-memory operation it must be 0, otherwise the pre engine
 * waits for VDIN input that never arrives and issues no memory transactions.
 */
#define VIUB_MISC_CTRL0			0x2006
#define VIUB_MISC_CTRL0_INP_VDIN	BIT(16)

/*
 * VPU-level clock gate (VCBUS, shared with the VPU). Separate from the DI's
 * own DI_CLKG_CTRL: this is what lets vpu_clkb reach the DI NR/motion
 * datapath. Without it the register/reset FSM still runs on the APB clock
 * (frame_rst self-clears) but no field is processed.
 */
#define VPU_CLK_GATE			0x2723
#define VPU_CLK_GATE_CLKB		BIT(16)	/* clkb_gate (all GX) */
#define VPU_CLK_GATE_CLKB_GEN		BIT(17)	/* clkb_gen_en (GXL/GXM) */

/* VPU memory-interface arbiter debug status (diagnostic read-back). */
#define VPU_ARB_DBG_STAT_L1C1		0x27b4

/* VPU-global write-arbiter slave request-enable for the DI write MIFs. */
#define VPU_WRARB_REQEN_SLV_L1C1	0x2795

/* Read-only frame-motion accumulator: non-zero once the engine processes. */
#define DET3D_RO_FRM_MOTN		0x178f

/*
 * DI_CLKG_CTRL: the datapath (NR/blend/EI/MTN) clocks must be explicitly
 * ungated for the engine to process a field; writing only bit0 leaves them
 * gated. OFF gates everything.
 */
#define DI_CLKG_CTRL_GXBB		0xfef60001
#define DI_CLKG_CTRL_GXL		0xfef60001
#define DI_CLKG_CTRL_OFF		0x00000002

/* DI_PRE_CTRL bits */
#define DI_PRE_CTRL_NR_EN		BIT(0)
#define DI_PRE_CTRL_MTN_EN		BIT(1)
#define DI_PRE_CTRL_PD32_CHECK		BIT(2)
#define DI_PRE_CTRL_PD22_CHECK		BIT(3)
#define DI_PRE_CTRL_CHECK_AFTER_NR	BIT(4)
#define DI_PRE_CTRL_CHAN2_HIST_EN	BIT(6)
#define DI_PRE_CTRL_CHAN2_EN		BIT(8)
#define DI_PRE_CTRL_LINEBUF2_EN		BIT(9)
#define DI_PRE_CTRL_MTN_AFTER_NR	BIT(22)
#define DI_PRE_CTRL_HOLD_LINE(x)	((x) << 16)
#define DI_PRE_CTRL_FIELD_NUM		BIT(29)
#define DI_PRE_CTRL_FRAME_RST		BIT(30)
#define DI_PRE_CTRL_SOFT_RST		BIT(31)

/* DI_POST_CTRL bits */
#define DI_POST_CTRL_LBUF0_EN		BIT(0)
#define DI_POST_CTRL_EI_EN		BIT(2)
#define DI_POST_CTRL_MTN_LBUF_EN	BIT(3)
#define DI_POST_CTRL_MTNP_RD_EN		BIT(4)
#define DI_POST_CTRL_BLEND_EN		BIT(5)
#define DI_POST_CTRL_MUX_EN		BIT(6)
#define DI_POST_CTRL_DDR_EN		BIT(7)
#define DI_POST_CTRL_VPP_EN		BIT(8)
#define DI_POST_CTRL_HOLD_LINE(x)	((x) << 16)
#define DI_POST_CTRL_FIELD_NUM		BIT(29)
#define DI_POST_CTRL_FRAME_RST		BIT(30)
#define DI_POST_CTRL_SOFT_RST		BIT(31)

/* DI_BLEND_CTRL bits */
#define DI_BLEND_CTRL_MODE(x)		((x) << 20)
#define DI_BLEND_CTRL_FIX(x)		((x) << 22)
#define DI_BLEND_CTRL_EN		BIT(31)

/* DI_INTR_CTRL bits (low half is write-1-to-clear, high half masks) */
#define DI_INTR_NRWR_DONE		BIT(0)
#define DI_INTR_MTNWR_DONE		BIT(1)
#define DI_INTR_DIWR_DONE		BIT(2)
#define DI_INTR_MASK_SHIFT		16

/* MIF GEN_REG common bits */
#define DI_MIF_GEN_REG_EN		BIT(0)

#endif /* __MESON_DI_REGS_H */
