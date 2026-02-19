/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip Image Enhancement Processor v2 (IEP2) driver
 *
 * Copyright (C) 2025 Christian Hewitt <christianshewitt@gmail.com>
 *
 * Register map derived from the Rockchip vendor userspace library.
 */

#ifndef __IEP2_REGS_H__
#define __IEP2_REGS_H__

/* Configuration registers */
#define IEP2_SRC_FMT			0x000
#define IEP2_SRC_YUV_SWAP		0x004
#define IEP2_DST_FMT			0x008
#define IEP2_DST_YUV_SWAP		0x00c
#define IEP2_TILE_COLS			0x010
#define IEP2_TILE_ROWS			0x014
#define IEP2_SRC_Y_STRIDE		0x018
#define IEP2_SRC_UV_STRIDE		0x01c
#define IEP2_DST_Y_STRIDE		0x020

/* Source buffer addresses: 3 frames x {Y, CBCR, CR} */
#define IEP2_SRC0_Y			0x024
#define IEP2_SRC0_CBCR			0x028
#define IEP2_SRC0_CR			0x02c
#define IEP2_SRC1_Y			0x030
#define IEP2_SRC1_CBCR			0x034
#define IEP2_SRC1_CR			0x038
#define IEP2_SRC2_Y			0x03c
#define IEP2_SRC2_CBCR			0x040
#define IEP2_SRC2_CR			0x044

/* Destination buffer addresses: 2 frames x {Y, CBCR, CR} */
#define IEP2_DST0_Y			0x048
#define IEP2_DST0_CBCR			0x04c
#define IEP2_DST0_CR			0x050
#define IEP2_DST1_Y			0x054
#define IEP2_DST1_CBCR			0x058
#define IEP2_DST1_CR			0x05c

/* Working buffer addresses */
#define IEP2_MV_ADDR			0x060
#define IEP2_MD_ADDR			0x064

/* Deinterlace control */
#define IEP2_DIL_MODE			0x068
#define IEP2_DIL_OUT_MODE		0x06c
#define IEP2_DIL_FIELD_ORDER		0x070

/* Motion detection */
#define IEP2_MD_THETA			0x074
#define IEP2_MD_R			0x078
#define IEP2_MD_LAMBDA			0x07c

/* Detection */
#define IEP2_DECT_RESI_THR		0x080

/* OSD */
#define IEP2_OSD_AREA_NUM		0x084
#define IEP2_OSD_GRADH_THR		0x088
#define IEP2_OSD_GRADV_THR		0x08c
#define IEP2_OSD_POS_LIMIT_EN		0x090
#define IEP2_OSD_POS_LIMIT_NUM		0x094
#define IEP2_OSD_LIMIT_AREA(i)		(0x098 + (i) * 4) /* 2 regs */
#define IEP2_OSD_LINE_NUM		0x0a0
#define IEP2_OSD_PEC_THR		0x0a4
#define IEP2_OSD_X_STA(i)		(0x0a8 + (i) * 4) /* 8 regs */
#define IEP2_OSD_X_END(i)		(0x0c8 + (i) * 4) /* 8 regs */
#define IEP2_OSD_Y_STA(i)		(0x0e8 + (i) * 4) /* 8 regs */
#define IEP2_OSD_Y_END(i)		(0x108 + (i) * 4) /* 8 regs */

/* Motion estimation */
#define IEP2_ME_PENA			0x128
#define IEP2_MV_BONUS			0x12c
#define IEP2_MV_SIMILAR_THR		0x130
#define IEP2_MV_SIMILAR_NUM_THR0	0x134
#define IEP2_ME_THR_OFFSET		0x138
#define IEP2_MV_LEFT_LIMIT		0x13c
#define IEP2_MV_RIGHT_LIMIT		0x140
#define IEP2_MV_TRU_LIST(i)		(0x144 + (i) * 4) /* 2 regs (8 x int8 packed) */
#define IEP2_MV_TRU_VLD(i)		(0x14c + (i) * 4) /* 8 regs */

/* Edge-enhanced deinterlace interpolation */
#define IEP2_EEDI_THR0			0x16c

/* Blend */
#define IEP2_BLE_BACKTOMA_NUM		0x170

/* Comb filter */
#define IEP2_COMB_CNT_THR		0x174
#define IEP2_COMB_FEATURE_THR		0x178
#define IEP2_COMB_T_THR			0x17c
#define IEP2_COMB_OSD_VLD(i)		(0x180 + (i) * 4) /* 8 regs */

/* Motion noise table */
#define IEP2_MTN_EN			0x1a0
#define IEP2_MTN_TAB(i)		(0x1a4 + (i) * 4) /* 16 regs */

/* Pulldown mode */
#define IEP2_PD_MODE			0x1e4

/* ROI */
#define IEP2_ROI_EN			0x1e8
#define IEP2_ROI_LAYER_NUM		0x1ec
#define IEP2_ROI_MODE(i)		(0x1f0 + (i) * 4) /* 8 regs */
#define IEP2_ROI_XSTA(i)		(0x210 + (i) * 4) /* 8 regs */
#define IEP2_ROI_XEND(i)		(0x230 + (i) * 4) /* 8 regs */
#define IEP2_ROI_YSTA(i)		(0x250 + (i) * 4) /* 8 regs */
#define IEP2_ROI_YEND(i)		(0x270 + (i) * 4) /* 8 regs */

/* End of params block at 0x290 */

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
#define IEP2_TILE_H_MAX			480
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
