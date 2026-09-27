/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Christian Hewitt <christianshewitt@gmail.com>
 */

#ifndef __MESON_DI_REGS_H
#define __MESON_DI_REGS_H

/* VCBUS word addresses; the mapped window starts at DI_REG_BASE */
#define DI_REG_BASE			0x1700

#define DI_PRE_CTRL			0x1700
#define DI_POST_CTRL			0x1701
#define DI_POST_SIZE			0x1702
#define DI_PRE_SIZE			0x1703
#define DI_EI_CTRL0			0x1704
#define DI_EI_CTRL1			0x1705
#define DI_EI_CTRL2			0x1706
#define DI_NR_CTRL0			0x1707
#define DI_CANVAS_URGENT0		0x170a
#define DI_MTN_CTRL			0x170b
#define DI_MTN_CTRL1			0x170c
#define DI_BLEND_CTRL			0x170d
#define DI_ARB_CTRL			0x170f
#define DI_BLEND_REG0_X			0x1710
#define DI_BLEND_REG0_Y			0x1711
#define DI_BLEND_REG1_X			0x1712
#define DI_BLEND_REG1_Y			0x1713
#define DI_BLEND_REG2_X			0x1714
#define DI_BLEND_REG2_Y			0x1715
#define DI_BLEND_REG3_X			0x1716
#define DI_BLEND_REG3_Y			0x1717
#define DI_EI_CTRL3			0x1719
#define DI_INTR_CTRL			0x1730
#define DI_PRE_HOLD			0x1733
#define DI_MTN_1_CTRL1			0x1740
#define DI_MTN_1_CTRL4			0x1743
#define NR2_CUE_MODE			0x1778
#define NR2_CUE_CON_DIF0		0x177a
#define NR2_CUE_CON_DIF1		0x177b
#define NR2_CUE_CON_DIF2		0x177c
#define NR2_CUE_CON_DIF3		0x177d
#define NR2_CUE_PRG_DIF			0x177e
#define DI_IF1_GEN_REG2			0x1790
#define DI_INP_GEN_REG2			0x1791
#define DI_MEM_GEN_REG2			0x1792
#define NR2_CFR_PARA_CFG0		0x179c
#define DI_MTN_1_CTRL7			0x17aa
#define DI_CHAN2_LUMA0_RPT_PAT		0x17b0
#define DI_CHAN2_CHROMA0_RPT_PAT	0x17b1
#define DI_CHAN2_DUMMY_PIXEL		0x17b2
#define DI_CHAN2_LUMA_FIFO_SIZE		0x17b3
#define DI_CHAN2_GEN_REG2		0x17b7
#define DI_CHAN2_FMT_CTRL		0x17b8
#define DI_CHAN2_FMT_W			0x17b9
#define DI_NRWR_X			0x17c0
#define DI_NRWR_Y			0x17c1
#define DI_NRWR_CTRL			0x17c2
#define DI_DIWR_X			0x17c6
#define DI_DIWR_Y			0x17c7
#define DI_DIWR_CTRL			0x17c8
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
#define DI_MEM_GEN_REG			0x17db
#define DI_MEM_CANVAS0			0x17dc
#define DI_MEM_LUMA_X0			0x17dd
#define DI_MEM_LUMA_Y0			0x17de
#define DI_MEM_CHROMA_X0		0x17df
#define DI_MEM_CHROMA_Y0		0x17e0
#define DI_MEM_RPT_LOOP			0x17e1
#define DI_MEM_LUMA0_RPT_PAT		0x17e2
#define DI_MEM_CHROMA0_RPT_PAT		0x17e3
#define DI_MEM_DUMMY_PIXEL		0x17e4
#define DI_MEM_LUMA_FIFO_SIZE		0x17e5
#define DI_MEM_FMT_CTRL			0x17e6
#define DI_MEM_FMT_W			0x17e7
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
#define DI_CHAN2_GEN_REG		0x17f5
#define DI_CHAN2_CANVAS0		0x17f6
#define DI_CHAN2_LUMA_X0		0x17f7
#define DI_CHAN2_LUMA_Y0		0x17f8
#define DI_CHAN2_CHROMA_X0		0x17f9
#define DI_CHAN2_CHROMA_Y0		0x17fa
#define DI_CHAN2_RPT_LOOP		0x17fb

#define VIUB_SW_RESET			0x2001
#define VIUB_MISC_CTRL0			0x2006
#define VIUB_GCLK_CTRL0			0x2007
#define VIUB_GCLK_CTRL1			0x2008
#define VIUB_GCLK_CTRL2			0x2009
#define VIUB_GCLK_CTRL3			0x200a
#define DI_IF2_GEN_REG			0x2010
#define DI_IF2_CANVAS0			0x2011
#define DI_IF2_LUMA_X0			0x2012
#define DI_IF2_LUMA_Y0			0x2013
#define DI_IF2_CHROMA_X0		0x2014
#define DI_IF2_CHROMA_Y0		0x2015
#define DI_IF2_RPT_LOOP			0x2016
#define DI_IF2_LUMA0_RPT_PAT		0x2017
#define DI_IF2_CHROMA0_RPT_PAT		0x2018
#define DI_IF2_DUMMY_PIXEL		0x2019
#define DI_IF2_LUMA_FIFO_SIZE		0x201a
#define DI_IF2_GEN_REG2			0x201e
#define DI_IF2_FMT_CTRL			0x201f
#define DI_IF2_FMT_W			0x2020
#define DI_IF2_GEN_REG3			0x2022
#define DI_EI_DRT_CTRL			0x2028
#define DI_IF0_GEN_REG			0x2030
#define DI_IF0_CANVAS0			0x2031
#define DI_IF0_LUMA_X0			0x2032
#define DI_IF0_LUMA_Y0			0x2033
#define DI_IF0_CHROMA_X0		0x2034
#define DI_IF0_CHROMA_Y0		0x2035
#define DI_IF0_RPT_LOOP			0x2036
#define DI_IF0_LUMA0_RPT_PAT		0x2037
#define DI_IF0_CHROMA0_RPT_PAT		0x2038
#define DI_IF0_DUMMY_PIXEL		0x2039
#define DI_IF0_LUMA_FIFO_SIZE		0x203a
#define DI_IF0_GEN_REG2			0x203e
#define DI_IF0_FMT_CTRL			0x203f
#define DI_IF0_FMT_W			0x2040
#define DI_IF0_GEN_REG3			0x2042
#define DI_RDARB_REQEN_SLV_L1C1		0x2051
#define DI_WRARB_REQEN_SLV_L1C1		0x2055
#define DI_ARB_DBG_STAT_L1C1		0x205a
#define DI_IF1_GEN_REG3			0x20a7
#define DI_INP_GEN_REG3			0x20a8
#define DI_MEM_GEN_REG3			0x20a9
#define DI_CHAN2_GEN_REG3		0x20aa
#define DI_PRE_GL_CTRL			0x20ab
#define DI_PRE_GL_THD			0x20ac
#define DI_POST_GL_CTRL			0x20ad
#define DI_POST_GL_THD			0x20ae

#define VPU_CLK_GATE			0x2723

#define DNR_CTRL			0x2d00
#define DNR_HVSIZE			0x2d01
#define DNR_STAT_X_START_END		0x2d08
#define DNR_STAT_Y_START_END		0x2d09
#define DNR_DM_CTRL			0x2d60
#define NR4_MCNR_LUMA_STAT_LIMTX	0x2db9
#define NR4_MCNR_LUMA_STAT_LIMTY	0x2dba
#define NR4_TOP_CTRL			0x2dff
#define MCDI_MC_CRTL			0x2f70
#define NR3_MODE			0x2ff0
#define NR3_COOP_PARA			0x2ff1
#define NR3_CNOOP_GAIN			0x2ff2
#define NR3_YMOT_PARA			0x2ff3
#define NR3_CMOT_PARA			0x2ff4
#define NR3_SUREMOT_YGAIN		0x2ff5
#define NR3_SUREMOT_CGAIN		0x2ff6
#define LBUF_TOP_CTRL			0x2fff

#define NR4_NM_X_CFG			0x3713
#define NR4_NM_Y_CFG			0x3714
#define NR_DS_CTRL			0x3741
#define DI_VIU_HSC_CTRL			0x37b2
#define CONTRD_CTRL1			0x37d0
#define CONTRD_CTRL2			0x37d1
#define CONTRD_SCOPE_X			0x37d2
#define CONTRD_SCOPE_Y			0x37d3
#define CONT2RD_CTRL1			0x37d5
#define CONT2RD_CTRL2			0x37d6
#define CONT2RD_SCOPE_X			0x37d7
#define CONT2RD_SCOPE_Y			0x37d8
#define MTNRD_CTRL1			0x37da
#define MTNRD_SCOPE_X			0x37dc
#define MTNRD_SCOPE_Y			0x37dd
#define MCINFRD_CTRL2			0x37e5
#define CONTWR_X			0x37e9
#define CONTWR_Y			0x37ea
#define CONTWR_CTRL			0x37eb
#define CONTWR_CAN_SIZE			0x37ec
#define MTNWR_X				0x37ed
#define MTNWR_Y				0x37ee
#define MTNWR_CTRL			0x37ef
#define MTNWR_CAN_SIZE			0x37f0
#define MCVECWR_CAN_SIZE		0x37f4
#define MCINFWR_CAN_SIZE		0x37f8
#define NRDSWR_CTRL			0x37fb

#define DI_REG_END			0x37ff

/* HHI */
#define HHI_VPU_MEM_PD_REG0		(0x41 << 2)
#define HHI_VPU_MEM_PD_VD1		GENMASK(5, 4)
#define HHI_VPU_MEM_PD_DI_POST		GENMASK(29, 28)

/* DI_INTR_CTRL */
#define DI_INTR_NRWR_DONE		BIT(0)
#define DI_INTR_DIWR_DONE		BIT(2)
#define DI_INTR_MODE			(3U << 30)
#define DI_INTR_MASKS			0x03fa0000

/* DI_PRE_CTRL */
#define DI_PRE_CTRL_BASE		0x00600811
#define DI_PRE_CTRL_MADI		0x0000006e
#define DI_PRE_CTRL_CHAN2		GENMASK(9, 8)
#define DI_PRE_CTRL_CONT_RD		BIT(25)
#define DI_PRE_CTRL_MEM_BYPASS		BIT(28)
#define DI_PRE_CTRL_FIELD		BIT(29)

/* DI_POST_CTRL */
#define DI_POST_CTRL_EI			0xc00000c5
#define DI_POST_CTRL_NORMAL		0xc00000fd
#define DI_POST_CTRL_FIELD		BIT(29)

#define DI_PRE_GL_STOP			0xc0000000
#define DI_PRE_GL_START			0x80200005
#define DI_POST_GL_STOP			0xc0000001
#define DI_POST_GL_START		0x80200001
#define DI_POST_GL_DONE			0x00000001
#define DI_POST_CTRL_DONE		0x80000045

#endif
