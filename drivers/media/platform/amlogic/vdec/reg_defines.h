/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#ifndef _REG_DEFINES_H_
#define _REG_DEFINES_H_

#define VDEC_ASSIST_MMC_CTRL0        0x0001
#define VDEC_ASSIST_MMC_CTRL1        0x0002

#define VDEC_ASSIST_CANVAS_BLK32     0x0005

#define VDEC_ASSIST_MBOX1_CLR_REG    0x0075
#define VDEC_ASSIST_MBOX1_MASK       0x0076

#define MPSR                         0x0301
#define MPC_P                        0x0306
#define MPC_D                        0x0307
#define MPC_E                        0x0308
#define MPC_W                        0x0309
#define CPSR                         0x0321
#define IMEM_DMA_CTRL                0x0340
#define IMEM_DMA_ADR                 0x0341
#define IMEM_DMA_COUNT               0x0342
#define WRRSP_IMEM                   0x0343
#define LMEM_DMA_CTRL                0x0350
#define WRRSP_LMEM                   0x0353

#define PSCALE_CTRL                  0x0911
#define GCLK_EN                      0x0983
#define MDEC_PIC_DC_CTRL             0x098e
#define MDEC_PIC_DC_MUX_CTRL         0x098d
#define ANC0_CANVAS_ADDR             0x0990
#define ANC1_CANVAS_ADDR             0x0991
#define ANC2_CANVAS_ADDR             0x0992
#define ANC3_CANVAS_ADDR             0x0993
#define ANC4_CANVAS_ADDR             0x0994
#define ANC5_CANVAS_ADDR             0x0995
#define ANC6_CANVAS_ADDR             0x0996
#define ANC7_CANVAS_ADDR             0x0997
#define ANC8_CANVAS_ADDR             0x0998
#define ANC9_CANVAS_ADDR             0x0999
#define ANC10_CANVAS_ADDR            0x099a
#define ANC11_CANVAS_ADDR            0x099b
#define ANC12_CANVAS_ADDR            0x099c
#define ANC13_CANVAS_ADDR            0x099d
#define ANC14_CANVAS_ADDR            0x099e
#define ANC15_CANVAS_ADDR            0x099f
#define ANC16_CANVAS_ADDR            0x09a0
#define ANC17_CANVAS_ADDR            0x09a1
#define ANC18_CANVAS_ADDR            0x09a2
#define ANC19_CANVAS_ADDR            0x09a3
#define ANC20_CANVAS_ADDR            0x09a4
#define ANC21_CANVAS_ADDR            0x09a5
#define ANC22_CANVAS_ADDR            0x09a6
#define ANC23_CANVAS_ADDR            0x09a7
#define ANC24_CANVAS_ADDR            0x09a8
#define ANC25_CANVAS_ADDR            0x09a9
#define ANC26_CANVAS_ADDR            0x09aa
#define ANC27_CANVAS_ADDR            0x09ab
#define ANC28_CANVAS_ADDR            0x09ac
#define ANC29_CANVAS_ADDR            0x09ad
#define ANC30_CANVAS_ADDR            0x09ae
#define ANC31_CANVAS_ADDR            0x09af
#define DBKR_CANVAS_ADDR             0x09b0
#define DBKW_CANVAS_ADDR             0x09b1
#define REC_CANVAS_ADDR              0x09b2
#define CURR_CANVAS_CTRL             0x09b3
#define MDEC_PIC_DC_THRESH           0x09b8
#define AV_SCRATCH_0                 0x09c0
#define AV_SCRATCH_1                 0x09c1
#define AV_SCRATCH_2                 0x09c2
#define AV_SCRATCH_3                 0x09c3
#define AV_SCRATCH_4                 0x09c4
#define AV_SCRATCH_5                 0x09c5
#define AV_SCRATCH_6                 0x09c6
#define AV_SCRATCH_7                 0x09c7
#define AV_SCRATCH_8                 0x09c8
#define AV_SCRATCH_9                 0x09c9
#define AV_SCRATCH_A                 0x09ca
#define AV_SCRATCH_B                 0x09cb
#define AV_SCRATCH_C                 0x09cc
#define AV_SCRATCH_D                 0x09cd
#define AV_SCRATCH_E                 0x09ce
#define AV_SCRATCH_F                 0x09cf
#define AV_SCRATCH_G                 0x09d0
#define AV_SCRATCH_H                 0x09d1
#define AV_SCRATCH_I                 0x09d2
#define AV_SCRATCH_J                 0x09d3
#define AV_SCRATCH_K                 0x09d4
#define AV_SCRATCH_L                 0x09d5
#define AV_SCRATCH_M                 0x09d6
#define AV_SCRATCH_N                 0x09d7
#define WRRSP_VLD                    0x09da
#define MDEC_DOUBLEW_CFG0            0x09db
#define MDEC_DOUBLEW_CFG1            0x09dc
#define MDEC_DOUBLEW_CFG2            0x09dd
#define MDEC_DOUBLEW_CFG3            0x09de
#define MDEC_DOUBLEW_CFG4            0x09df
#define MDEC_DOUBLEW_CFG5            0x09e0
#define MDEC_DOUBLEW_CFG6            0x09e1
#define MDEC_DOUBLEW_CFG7            0x09e2
#define MDEC_DOUBLEW_STATUS          0x09e3
#define MDEC_EXTIF_CFG0              0x09e4

#define MDEC_EXTIF_CFG1              0x09e5
#define MDEC_EXTIF_CFG2              0x09e6

#define POWER_CTL_VLD                0x0c08
#define VLD_DECODE_CONTROL           0x0c18

#define PMV1_X                       0x0c20
#define PMV1_Y                       0x0c21
#define PMV2_X                       0x0c22
#define PMV2_Y                       0x0c23
#define PMV3_X                       0x0c24
#define PMV3_Y                       0x0c25
#define PMV4_X                       0x0c26
#define PMV4_Y                       0x0c27
#define M4_TABLE_SELECT              0x0c28
#define M4_CONTROL_REG               0x0c29
#define BLOCK_NUM                    0x0c2a
#define PATTERN_CODE                 0x0c2b
#define MB_INFO                      0x0c2c
#define VLD_DC_PRED                  0x0c2d
#define VLD_ERROR_MASK               0x0c2e
#define VLD_DC_PRED_C                0x0c2f
#define LAST_SLICE_MV_ADDR           0x0c30
#define LAST_MVX                     0x0c31
#define LAST_MVY                     0x0c32

#define MB_MOTION_MODE               0x0c07
#define VIFF_BIT_CNT                 0x0c1a
#define M4_CONTROL_REG               0x0c29
#define VLD_C38                      0x0c38
#define VLD_C39                      0x0c39
#define VLD_SHIFT_STATUS             0x0c3b
#define VLD_C3D                      0x0c3d
#define VLD_MEM_VIFIFO_START_PTR     0x0c40
#define VLD_MEM_VIFIFO_CURR_PTR      0x0c41
#define VLD_MEM_VIFIFO_END_PTR       0x0c42
#define VLD_MEM_VIFIFO_BYTES_AVAIL   0x0c43
#define VLD_MEM_VIFIFO_CONTROL       0x0c44
#define VLD_MEM_VIFIFO_WP            0x0c45
#define VLD_MEM_VIFIFO_RP            0x0c46
#define VLD_MEM_VIFIFO_LEVEL         0x0c47
#define VLD_MEM_VIFIFO_BUF_CNTL      0x0c48

#define VCOP_CTRL_REG                0x0e00
#define RV_AI_MB_COUNT               0x0e0c
#define IQIDCT_CONTROL               0x0e0e
#define DCAC_DDR_BYTE64_CTL          0x0e1d

#define VDEC2_IMEM_DMA_CTRL          0x2340
#define VDEC2_IMEM_DMA_ADR           0x2341
#define VDEC2_IMEM_DMA_COUNT         0x2342

#define DOS_SW_RESET0                0x3f00
#define DOS_GCLK_EN0                 0x3f01
#define DOS_GCLK_EN1                 0x3f09
#define DOS_GCLK_EN3                 0x3f35

#define DOS_MEM_PD_VDEC              0x3f30
#define DOS_MEM_PD_VDEC2             0x3f31
#define DOS_MEM_PD_HCODEC            0x3f32
/*add from M8M2*/
#define DOS_MEM_PD_HEVC              0x3f33

#define DOS_SW_RESET3                0x3f34
#define DOS_GCLK_EN3                 0x3f35
#define DOS_HEVC_INT_EN              0x3f36

#endif

