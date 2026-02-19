/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip Image Enhancement Processor v2 (IEP2) driver
 *
 * Copyright (C) 2025 Christian Hewitt <christianshewitt@gmail.com>
 *
 * Register map derived from the Rockchip vendor kernel driver.
 */

#ifndef __IEP2_REGS_H__
#define __IEP2_REGS_H__

/* Control registers */
#define IEP2_FRM_START			0x0000
#define IEP2_CONFIG0			0x0004
#define     IEP2_CONFIG0_DEBUG_DATA_EN		BIT(16)
#define     IEP2_CONFIG0_DST_YUV_SWAP(x)	(((x) & 3) << 12)
#define     IEP2_CONFIG0_DST_FMT(x)		(((x) & 3) << 8)
#define     IEP2_CONFIG0_SRC_YUV_SWAP(x)	(((x) & 3) << 4)
#define     IEP2_CONFIG0_SRC_FMT(x)		((x) & 3)
#define IEP2_WORK_MODE			0x0008
#define     IEP2_WORK_MODE_IEP2		BIT(0)
#define IEP2_STATUS			0x0014

/* Interrupt registers */
#define IEP2_INT_EN			0x0020
#define     IEP2_INT_TIMEOUT_EN		BIT(5)
#define     IEP2_INT_BUS_ERROR_EN	BIT(4)
#define     IEP2_INT_OSD_MAX_EN		BIT(1)
#define     IEP2_INT_FRM_DONE_EN	BIT(0)
#define IEP2_INT_CLR			0x0024
#define IEP2_INT_STS			0x0028
#define     IEP2_INT_STS_TIMEOUT	BIT(5)
#define     IEP2_INT_STS_BUS_ERROR	BIT(4)
#define     IEP2_INT_STS_FRM_DONE	BIT(0)
#define     IEP2_INT_STS_VALID		(BIT(5) | BIT(4) | BIT(0))

/* Stride/size registers */
#define IEP2_VIR_SRC_IMG_WIDTH		0x0030
#define     IEP2_SRC_VIR_UV_STRIDE(x)	(((x) & 0xffff) << 16)
#define     IEP2_SRC_VIR_Y_STRIDE(x)	((x) & 0xffff)
#define IEP2_VIR_DST_IMG_WIDTH		0x0034
#define     IEP2_DST_VIR_STRIDE(x)	((x) & 0xffff)
#define IEP2_SRC_IMG_SIZE		0x0038
#define     IEP2_SRC_PIC_HEIGHT(x)	(((x) & 0x7ff) << 16)
#define     IEP2_SRC_PIC_WIDTH(x)	((x) & 0x7ff)

/* Deinterlace config (packed bitfields) */
#define IEP2_DIL_CONFIG0		0x0040
#define     IEP2_DIL_MV_HIST_EN		BIT(17)
#define     IEP2_DIL_COMB_EN		BIT(15)
#define     IEP2_DIL_BLE_EN		BIT(14)
#define     IEP2_DIL_EEDI_EN		BIT(13)
#define     IEP2_DIL_MEMC_EN		BIT(12)
#define     IEP2_DIL_OSD_EN		BIT(11)
#define     IEP2_DIL_PD_EN		BIT(10)
#define     IEP2_DIL_FF_EN		BIT(9)
#define     IEP2_DIL_MD_PRE_EN		BIT(8)
#define     IEP2_DIL_FIELD_ORDER(x)	(((x) & 1) << 5)
#define     IEP2_DIL_OUT_MODE(x)	(((x) & 1) << 4)
#define     IEP2_DIL_MODE(x)		((x) & 0xf)

/* Timeout config */
#define IEP2_TIMEOUT_CFG		0x0050
#define     IEP2_TIMEOUT_CFG_EN		BIT(31)

/* Source DMA addresses (per-plane, cur/nxt/pre) */
#define IEP2_SRC_ADDR_CURY		0x0060
#define IEP2_SRC_ADDR_NXTY		0x0064
#define IEP2_SRC_ADDR_PREY		0x0068
#define IEP2_SRC_ADDR_CURUV		0x006c
#define IEP2_SRC_ADDR_CURV		0x0070
#define IEP2_SRC_ADDR_NXTUV		0x0074
#define IEP2_SRC_ADDR_NXTV		0x0078
#define IEP2_SRC_ADDR_PREUV		0x007c
#define IEP2_SRC_ADDR_PREV		0x0080
#define IEP2_SRC_ADDR_MD		0x0084
#define IEP2_SRC_ADDR_MV		0x0088

/* Destination DMA addresses */
#define IEP2_DST_ADDR_TOPY		0x00b0
#define IEP2_DST_ADDR_BOTY		0x00b4
#define IEP2_DST_ADDR_TOPC		0x00b8
#define IEP2_DST_ADDR_BOTC		0x00bc
#define IEP2_DST_ADDR_MD		0x00c0
#define IEP2_DST_ADDR_MV		0x00c4

/* Motion detection config (packed) */
#define IEP2_MD_CONFIG0			0x00e0
#define     IEP2_MD_THETA(x)		(((x) & 3) << 8)
#define     IEP2_MD_R(x)		(((x) & 0xf) << 4)
#define     IEP2_MD_LAMBDA(x)		((x) & 0xf)

/* Detection config (packed) */
#define IEP2_DECT_CONFIG0		0x00e4
#define     IEP2_OSD_GRADV_THR(x)	(((x) & 0xff) << 24)
#define     IEP2_OSD_GRADH_THR(x)	(((x) & 0xff) << 16)
#define     IEP2_OSD_AREA_NUM(x)	(((x) & 0xf) << 8)
#define     IEP2_DECT_RESI_THR(x)	((x) & 0xff)

/* OSD config */
#define IEP2_OSD_LIMIT_CONFIG		0x00f0
#define     IEP2_OSD_POS_LIMIT_NUM(x)	(((x) & 7) << 4)
#define     IEP2_OSD_POS_LIMIT_EN	BIT(0)
#define IEP2_OSD_LIMIT_AREA(i)		(0x00f4 + ((i) * 4))
#define IEP2_OSD_CONFIG0		0x00fc
#define     IEP2_OSD_LINE_NUM(x)	(((x) & 0x1ff) << 16)
#define     IEP2_OSD_PEC_THR(x)		((x) & 0x7ff)
#define IEP2_OSD_AREA_CONF(i)		(0x0100 + ((i) * 4))

/* Motion estimation config (packed) */
#define IEP2_ME_CONFIG0			0x0120
#define     IEP2_ME_THR_OFFSET(x)	(((x) & 0xff) << 16)
#define     IEP2_MV_SIMILAR_NUM_THR0(x)	(((x) & 0xf) << 12)
#define     IEP2_MV_SIMILAR_THR(x)	(((x) & 0xf) << 8)
#define     IEP2_MV_BONUS(x)		(((x) & 0xf) << 4)
#define     IEP2_ME_PENA(x)		((x) & 0xf)
#define IEP2_ME_LIMIT_CONFIG		0x0124
#define     IEP2_MV_RIGHT_LIMIT(x)	(((x) & 0x3f) << 8)
#define     IEP2_MV_LEFT_LIMIT(x)	((x) & 0x3f)
#define IEP2_MV_TRU_LIST(i)		(0x0128 + ((i) * 4))

/* EEDI/Blend/Comb config */
#define IEP2_EEDI_CONFIG0		0x0130
#define     IEP2_EEDI_THR0(x)		((x) & 0x1f)
#define IEP2_BLE_CONFIG0		0x0134
#define     IEP2_BLE_BACKTOMA_NUM(x)	((x) & 7)
#define IEP2_COMB_CONFIG0		0x0138
#define     IEP2_COMB_CNT_THR(x)	(((x) & 0xf) << 24)
#define     IEP2_COMB_FEATURE_THR(x)	(((x) & 0x3f) << 16)
#define     IEP2_COMB_T_THR(x)		(((x) & 0xff) << 8)
#define     IEP2_COMB_OSD_VLD(i)	BIT(i)

/* Motion noise table */
#define IEP2_DIL_MTN_TAB(i)		(0x0140 + ((i) * 4))

/* Color format values */
#define IEP2_COLOR_FMT_YUV422		2U
#define IEP2_COLOR_FMT_YUV420		3U

/* YUV swap values */
#define IEP2_YUV_SWP_SP_UV		0U
#define IEP2_YUV_SWP_SP_VU		1U
#define IEP2_YUV_SWP_P			3U

/* Deinterlace modes */
#define IEP2_DIL_MODE_DISABLE		0U
#define IEP2_DIL_MODE_I5O2		1U
#define IEP2_DIL_MODE_I5O1T		2U
#define IEP2_DIL_MODE_I5O1B		3U
#define IEP2_DIL_MODE_I2O2		4U
#define IEP2_DIL_MODE_I1O1T		5U
#define IEP2_DIL_MODE_I1O1B		6U
#define IEP2_DIL_MODE_PD		7U
#define IEP2_DIL_MODE_BYPASS		8U

/* Output modes */
#define IEP2_OUT_MODE_LINE		0U

/* Field order */
#define IEP2_FIELD_ORDER_TFF		0U
#define IEP2_FIELD_ORDER_BFF		1U

/* Working buffer sizes */
#define IEP2_TILE_W			16
#define IEP2_TILE_H			4
#define IEP2_TILE_W_MAX			120
#define IEP2_TILE_H_MAX			272
#define IEP2_MV_BUF_SIZE		(IEP2_TILE_W_MAX * IEP2_TILE_H_MAX)
#define IEP2_MD_BUF_SIZE		(1920 * 1088)

/* Default motion noise table from vendor BSP */
static const u32 iep2_default_mtn_tab[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x01010000, 0x06050302, 0x0f0d0a08, 0x1c191512,
	0x2b282420, 0x3634312e, 0x3d3c3a38, 0x40403f3e,
	0x40404040, 0x40404040, 0x40404040, 0x40404040,
};

#endif
