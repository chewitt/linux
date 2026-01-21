/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */
#ifndef _H264_H_
#define _H264_H_

#define RPM_BEGIN           0x0
#define FRAME_IN_DPB        24
#define RPM_END             0x400
#define DPB_OFFSET          0x100
#define MMCO_OFFSET         0x200
#define SPS_OFFSET          0x100
#define PPS_OFFSET          0x300
#define PARAM_BASE_VAL      0x414d
#define MEM_MMCO_BASE       0x01c3000
#define MEM_SPS_BASE        0x01c3c00
#define MEM_PPS_BASE        0x01cbc00
#define MC_TOTAL_SIZE       ((20 + 16) * SZ_1K)
#define MC_SWAP_SIZE        (4 * SZ_1K)
#define LMEM_DUMP_SIZE      4096
#define V_BUF_ADDR_OFFSET   (0x200000 + 0x8000 + 0x20000 + 0x1000)
#define DCAC_READ_MARGIN    (64 * 1024)
#define MC_OFFSET_HEADER    0x0000
#define MC_OFFSET_DATA      0x1000
#define MC_OFFSET_MMCO      0x2000
#define MC_OFFSET_LIST      0x3000
#define MC_OFFSET_SLICE     0x4000
#define MC_OFFSET_MAIN      0x5000

/* Rename the dos regs */
#define H264_DECODE_INFO    M4_CONTROL_REG
#define INIT_FLAG_REG       AV_SCRATCH_2
#define HEAD_PADDING_REG     AV_SCRATCH_3
#define UCODE_WATCHDOG_REG   AV_SCRATCH_7
#define LMEM_DUMP_ADR       AV_SCRATCH_L
#define DEBUG_REG1          AV_SCRATCH_M
#define DEBUG_REG2          AV_SCRATCH_N
#define FRAME_COUNTER_REG       AV_SCRATCH_I
#define RPM_CMD_REG          AV_SCRATCH_A
#define H264_DECODE_SIZE    AV_SCRATCH_E
#define H264_DECODE_MODE    AV_SCRATCH_4
#define H264_DECODE_SEQINFO    AV_SCRATCH_5
/**
 * NAL_SEARCH_CTL: bit 0, enable itu_t35
 * NAL_SEARCH_CTL: bit 1, enable mmu
 * NAL_SEARCH_CTL: bit 2, detect frame_mbs_only_flag whether switch resolution
 * NAL_SEARCH_CTL: bit 3, recover the correct sps pps
 * NAL_SEARCH_CTL: bit 7-14,level_idc
 * NAL_SEARCH_CTL: bit 15,bitstream_restriction_flag
 */
#define NAL_SEARCH_CTL      AV_SCRATCH_9
#define DPB_STATUS_REG      AV_SCRATCH_J
#define ERROR_STATUS_REG    AV_SCRATCH_9

#define H264_BUFFER_INFO_INDEX      PMV3_X	/* 0xc24 */
#define H264_BUFFER_INFO_DATA       PMV2_X	/* 0xc22 */
#define H264_CURRENT_POC_IDX_RESET  LAST_SLICE_MV_ADDR	/* 0xc30 */
#define H264_CURRENT_POC            LAST_MVY	/* 0xc32 shared with conceal MV */
#define H264_CO_MB_WR_ADDR          VLD_C38
#define H264_CO_MB_RD_ADDR          VLD_C39
#define H264_CO_MB_RW_CTL           VLD_C3D
#define MBY_MBX                     MB_MOTION_MODE

#define H264_ACTION_SEARCH_HEAD     0xf0
#define H264_ACTION_DECODE_SLICE    0xf1
#define H264_ACTION_CONFIG_DONE     0xf2
#define H264_ACTION_DECODE_NEWPIC   0xf3
#define H264_ACTION_DECODE_START    0xff

/* RPM memory definition */
#define FIXED_FRAME_RATE_FLAG						0X21
#define OFFSET_DELIMITER_LO						0x2f
#define OFFSET_DELIMITER_HI						0x30
#define SLICE_IPONLY_BREAK						0X5C
#define PREV_MAX_REFERENCE_FRAME_NUM					0X5D
#define EOS								0X5E
#define FRAME_PACKING_TYPE						0X5F
#define OLD_POC_PAR_1							0X60
#define OLD_POC_PAR_2							0X61
#define PREV_MBX							0X62
#define PREV_MBY							0X63
#define ERROR_SKIP_MB_NUM						0X64
#define ERROR_MB_STATUS							0X65
#define L0_PIC0_STATUS							0X66
#define TIMEOUT_COUNTER							0X67
#define BUFFER_SIZE							0X68
#define BUFFER_SIZE_HI							0X69
#define CROPPING_LEFT_RIGHT						0X6A
#define CROPPING_TOP_BOTTOM						0X6B
/**
 * sps_flags2:
 * bit 3, bitstream_restriction_flag
 * bit 2, pic_struct_present_flag
 * bit 1, vcl_hrd_parameters_present_flag
 * bit 0, nal_hrd_parameters_present_flag
 */
#define SPS_FLAGS2							0x6C
#define NUM_REORDER_FRAMES						0x6D
#define MAX_BUFFER_FRAME						0X6E

#define NON_CONFORMING_STREAM						0X70
#define RECOVERY_POINT							0X71
#define POST_CANVAS							0X72
#define POST_CANVAS_H							0X73
#define SKIP_PIC_COUNT							0X74
#define TARGET_NUM_SCALING_LIST						0X75
#define FF_POST_ONE_FRAME						0X76
#define PREVIOUS_BIT_CNT						0X77
#define MB_NOT_SHIFT_COUNT						0X78
#define PIC_STATUS							0X79
#define FRAME_COUNTER							0X7A
#define NEW_SLICE_TYPE							0X7B
#define NEW_PICTURE_STRUCTURE						0X7C
#define NEW_FRAME_NUM							0X7D
#define NEW_IDR_PIC_ID							0X7E
#define IDR_PIC_ID							0X7F

/* h264 LOCAL */
#define NAL_UNIT_TYPE							0X80
#define NAL_REF_IDC							0X81
#define SLICE_TYPE							0X82
#define LOG2_MAX_FRAME_NUM						0X83
#define FRAME_MBS_ONLY_FLAG						0X84
#define PIC_ORDER_CNT_TYPE						0X85
#define LOG2_MAX_PIC_ORDER_CNT_LSB					0X86
#define PIC_ORDER_PRESENT_FLAG						0X87
#define REDUNDANT_PIC_CNT_PRESENT_FLAG					0X88
#define PIC_INIT_QP_MINUS26						0X89
#define DEBLOCKING_FILTER_CONTROL_PRESENT_FLAG				0X8A
#define NUM_SLICE_GROUPS_MINUS1						0X8B
#define MODE_8X8_FLAGS							0X8C
#define ENTROPY_CODING_MODE_FLAG					0X8D
#define SLICE_QUANT							0X8E
#define TOTAL_MB_HEIGHT							0X8F
#define PICTURE_STRUCTURE						0X90
#define TOP_INTRA_TYPE							0X91
#define RV_AI_STATUS							0X92
#define AI_READ_START							0X93
#define AI_WRITE_START							0X94
#define AI_CUR_BUFFER							0X95
#define AI_DMA_BUFFER							0X96
#define AI_READ_OFFSET							0X97
#define AI_WRITE_OFFSET							0X98
#define AI_WRITE_OFFSET_SAVE						0X99
#define RV_AI_BUFF_START						0X9A
#define I_PIC_MB_COUNT							0X9B
#define AI_WR_DCAC_DMA_CTRL						0X9C
#define SLICE_MB_COUNT							0X9D
#define PICTYPE								0X9E
#define SLICE_GROUP_MAP_TYPE						0X9F
#define MB_TYPE								0XA0
#define MB_AFF_ADDED_DMA						0XA1
#define PREVIOUS_MB_TYPE						0XA2
#define WEIGHTED_PRED_FLAG						0XA3
#define WEIGHTED_BIPRED_IDC						0XA4
/* bit 3:2 - PICTURE_STRUCTURE
 * bit 1 - MB_ADAPTIVE_FRAME_FIELD_FLAG
 * bit 0 - FRAME_MBS_ONLY_FLAG
 */
#define MBFF_INFO							0XA5
#define TOP_INTRA_TYPE_TOP						0XA6
#define RV_AI_BUFF_INC							0xA7
#define DEFAULT_MB_INFO_LO						0xA8
/* 0 -- no need to read
 * 1 -- need to wait Left
 * 2 -- need to read Intra
 * 3 -- need to read back MV
 */
#define NEED_READ_TOP_INFO						0xA9
/* 0 -- idle
 * 1 -- wait Left
 * 2 -- reading top Intra
 * 3 -- reading back MV
 */
#define READ_TOP_INFO_STATE						0xAA
#define DCAC_MBX							0xAB
#define TOP_MB_INFO_OFFSET						0xAC
#define TOP_MB_INFO_RD_IDX						0xAD
#define TOP_MB_INFO_WR_IDX						0xAE

#define VLD_NO_WAIT	 0
#define VLD_WAIT_BUFFER 1
#define VLD_WAIT_HOST   2
#define VLD_WAIT_GAP	3

#define VLD_WAITING							0xAF

#define MB_X_NUM							0xB0
#define MB_HEIGHT							0xB2
#define MBX								0xB3
#define TOTAL_MBY							0xB4
#define INTR_MSK_SAVE							0xB5
#define NEED_DISABLE_PPE						0xB6
#define IS_NEW_PICTURE							0XB7
#define PREV_NAL_REF_IDC						0XB8
#define PREV_NAL_UNIT_TYPE						0XB9
#define FRAME_MB_COUNT							0XBA
#define REF_IDC_OVERRIDE_FLAG					0XBB
#define SLICE_GROUP_CHANGE_RATE						0XBC
#define SLICE_GROUP_CHANGE_CYCLE_LEN					0XBD
#define DELAY_LENGTH							0XBE
#define PICTURE_STRUCT							0XBF
#define DCAC_PREVIOUS_MB_TYPE						0xC1

#define TIME_STAMP							0XC2
#define H_TIME_STAMP							0XC3
#define VPTS_MAP_ADDR							0XC4
#define H_VPTS_MAP_ADDR							0XC5
#define PIC_INSERT_FLAG							0XC7
#define TIME_STAMP_START						0XC8
#define TIME_STAMP_END							0XDF
#define OFFSET_FOR_NON_REF_PIC						0XE0
#define OFFSET_FOR_TOP_TO_BOTTOM_FIELD					0XE2
#define MAX_REFERENCE_FRAME_NUM						0XE4
#define FRAME_NUM_GAP_ALLOWED						0XE5
#define NUM_REF_FRAMES_IN_PIC_ORDER_CNT_CYCLE				0XE6
#define PROFILE_IDC_MMCO						0XE7
#define LEVEL_IDC_MMCO							0XE8
#define FRAME_SIZE_IN_MB						0XE9
#define DELTA_PIC_ORDER_ALWAYS_ZERO_FLAG				0XEA
#define PPS_NUM_REF_IDX_L0_ACTIVE_MINUS1				0XEB
#define PPS_NUM_REF_IDX_L1_ACTIVE_MINUS1				0XEC
#define CURRENT_SPS_ID							0XED
#define CURRENT_PPS_ID							0XEE
/* bit 0 - sequence parameter set may change
 * bit 1 - picture parameter set may change
 * bit 2 - new dpb just inited
 * bit 3 - IDR picture not decoded yet
 * bit 5:4 - 0: mb level code loaded 1: picture
 * level code loaded 2: slice level code loaded
 */
#define DECODE_STATUS							0XEF
#define FIRST_MB_IN_SLICE						0XF0
#define PREV_MB_WIDTH							0XF1
#define PREV_FRAME_SIZE_IN_MB						0XF2
/* bit 0 - aspect_ratio_info_present_flag
 * bit 1 - timing_info_present_flag
 * bit 2 - nal_hrd_parameters_present_flag
 * bit 3 - vcl_hrd_parameters_present_flag
 * bit 4 - pic_struct_present_flag
 * bit 5 - bitstream_restriction_flag
 */
#define VUI_STATUS							0XF4
#define ASPECT_RATIO_IDC						0XF5
#define ASPECT_RATIO_SAR_WIDTH						0XF6
#define ASPECT_RATIO_SAR_HEIGHT						0XF7
#define NUM_UNITS_IN_TICK						0XF8
#define TIME_SCALE							0XFA
#define CURRENT_PIC_INFO						0XFC
#define DPB_BUFFER_INFO							0XFD
#define REFERENCE_POOL_INFO						0XFE
#define REFERENCE_LIST_INFO						0XFF

#define REORDER_CMD_MAX             66

/* config parameters to DDR lmem */
#define GET_SPS_PROFILE_IDC(x)       (((x) & 0xff) << 8)
#define GET_SPS_LEVEL_IDC(x)         ((x) & 0xff)
#define GET_SPS_SEQ_PARAM_SET_ID(x)       (((x) & 0x1f) << 8)
#define GET_SPS_CHROMA_FORMAT_IDC(x)      ((x) << 8)
#define GET_SPS_NUM_REF_FRAMES(x)         ((x) & 0xff)
#define GET_SPS_GAPS_ALLOWED_FLAG(x)      ((x) << 8)
#define GET_SPS_LOG2_MAX_FRAME_NUM(x)     ((x) + 4)
#define GET_SPS_PIC_ORDER_CNT_LSB(x)      ((x) + 4)
#define GET_SPS_PIC_ORDER_TYPE(x)         (x)
#define GET_SPS_OFFSET_FOR_NONREF_PIC_HIGH(x)      (((x) & 0xffff0000) >> 16)
#define GET_SPS_OFFSET_FOR_NONREF_PIC_LOW(x)         ((x) & 0xffff)
#define GET_SPS_OFFSET_FOR_TOP_BOT_FIELD_HIGH(x)      (((x) & 0xffff0000) >> 16)
#define GET_SPS_OFFSET_FOR_TOP_BOT_FIELD_LOW(x)         ((x) & 0xffff)
#define GET_SPS_PIC_WIDTH_IN_MBS(x)       ((x) + 1)
#define GET_SPS_PIC_HEIGHT_IN_MBS(x)      ((x) + 1)
#define GET_SPS_DIRECT_8X8_FLAGS(x)       (((x) & 0x1) << 2)
#define GET_SPS_MB_ADAPTIVE_FRAME_FIELD_FLAGS(x)       (((x) & 0x1) << 1)
#define GET_SPS_FRAME_MBS_ONLY_FLAGS(x)   ((x) & 0x1)

#define GET_PPS_PIC_PARAM_SET_ID(x)       ((x) & 0xff)
#define GET_PPS_SEQ_PARAM_SET_ID(x)       (((x) & 0x1f) << 8)
#define GET_PPS_ENTROPY_CODING_MODE_FLAG(x)  (((x) & 0x1) << 13)
#define GET_PPS_PIC_ORDER_PRESENT_FLAG(x)    (((x) & 0x1) << 14)
#define GET_PPS_NUM_IDX_REF_L0_MINUS1(x)       ((x) & 0x1f)
#define GET_PPS_NUM_IDX_REF_L1_MINUS1(x)       (((x) & 0x1f) << 5)
#define GET_PPS_WEIGHTED_PRED_FLAG(x)         (((x) & 0x1) << 10)
#define GET_PPS_WEIGHTED_BIPRED_IDC(x)        (((x) & 0x3) << 11)
#define GET_PPS_INIT_QS_MINUS26(x)           (((x) & 0xff) << 8)
#define GET_PPS_INIT_QP_MINUS26(x)           ((x) & 0xff)
#define GET_PPS_CHROMA_QP_INDEX_OFFSET(x)   ((x) & 0xff)
#define GET_PPS_DEBLOCK_FILTER_CTRL_PRESENT_FLAG(x)   (((x) & 0x1) << 8)
#define GET_PPS_CONSTRAIN_INTRA_PRED_FLAG(x)          (((x) & 0x1) << 9)
#define GET_PPS_REDUNDANT_PIC_CNT_PRESENT_FLAG(x)     (((x) & 0x1) << 10)
#define GET_PPS_SCALING_MATRIX_PRESENT_FLAG(x)        (((x) & 0x1) << 1)
#define GET_PPS_TRANSFORM_8X8_FLAG(x)                 ((x) & 0x1)
#define GET_PPS_GET_SECOND_CHROMA_QP_OFFSET(x)        (x)

int aml_h264_init(void *priv);
void aml_h264_exit(void *priv);
int aml_h264_dec_run(void *priv);

#endif
