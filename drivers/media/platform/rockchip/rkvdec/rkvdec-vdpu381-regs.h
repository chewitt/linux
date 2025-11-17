/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip VDPU381 Video Decoder driver registers description
 *
 * Copyright (C) 2024 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 *
 * Copyright (C) 2025 Venkata Atchuta Bheemeswara Sarma Darbha <vdarbha0473@gmail.com>
 *
 */

#include <linux/types.h>

#ifndef _RKVDEC_REGS_H_
#define _RKVDEC_REGS_H_

#define OFFSET_COMMON_REGS		(8 * sizeof(u32))
#define OFFSET_CODEC_PARAMS_REGS	(64 * sizeof(u32))
#define OFFSET_COMMON_ADDR_REGS		(128 * sizeof(u32))
#define OFFSET_CODEC_ADDR_REGS		(160 * sizeof(u32))
#define OFFSET_POC_HIGHBIT_REGS		(200 * sizeof(u32))

#define VDPU381_MODE_HEVC	0
#define VDPU381_MODE_H264	1
#define VDPU381_MODE_VP9	2
#define VDPU381_MODE_AVS2	3

#define MAX_SLICE_NUMBER	0x3fff

#define RKVDEC_TIMEOUT_1080p		(0xefffff)
#define RKVDEC_TIMEOUT_4K		(0x2cfffff)
#define RKVDEC_TIMEOUT_8K		(0x4ffffff)
#define RKVDEC_TIMEOUT_MAX		(0xffffffff)

#define VDPU381_REG_DEC_E		0x028
#define VDPU381_DEC_E_BIT		1

#define VDPU381_REG_IMPORTANT_EN	0x02c
#define VDPU381_DEC_IRQ_DISABLE	BIT(4)

#define VDPU381_REG_STA_INT		0x380
#define VDPU381_STA_INT_DEC_RDY_STA	BIT(2)
#define VDPU381_STA_INT_ERROR		BIT(4)
#define VDPU381_STA_INT_TIMEOUT		BIT(5)
#define VDPU381_STA_INT_SOFTRESET_RDY	BIT(9)

/* base: OFFSET_COMMON_REGS */
struct rkvdec_vdpu381_regs_common {
	struct {
		u32 in_endian		: 1;
		u32 in_swap32_e		: 1;
		u32 in_swap64_e		: 1;
		u32 str_endian		: 1;
		u32 str_swap32_e	: 1;
		u32 str_swap64_e	: 1;
		u32 out_endian		: 1;
		u32 out_swap32_e	: 1;
		u32 out_cbcr_swap	: 1;
		u32 out_swap64_e	: 1;
		u32 reserved		: 22;
	} reg008_in_out;

	struct {
		u32 dec_mode	: 10;
		u32 reserved	: 22;
	} reg009_dec_mode;

	struct {
		u32 dec_e	: 1;
		u32 reserved	: 31;
	} reg010_dec_e;

	struct {
		u32 reserved0			: 1;
		u32 dec_clkgate_e		: 1;
		u32 dec_e_strmd_clkgate_dis	: 1;
		u32 reserved1			: 1;

		u32 dec_irq_dis			: 1;
		u32 dec_timeout_e		: 1;
		u32 buf_empty_en		: 1;
		u32 reserved2			: 3;

		u32 dec_e_rewrite_valid		: 1;
		u32 reserved3			: 9;
		u32 softrst_en_p		: 1;
		u32 force_softreset_valid	: 1;
		u32 reserved4			: 2;
		u32 pix_range_det_e		: 1;
		u32 reserved5			: 7;
	} reg011_important_en;

	struct {
		u32 reserved0			: 1;
		u32 colmv_compress_en		: 1;
		u32 fbc_e			: 1;
		u32 reserved1			: 1;

		u32 buspr_slot_disable		: 1;
		u32 error_info_en		: 1;
		u32 collect_info_en		: 1;
		u32 error_auto_rst_disable	: 1;

		u32 scanlist_addr_valid_en	: 1;
		u32 scale_down_en		: 1;
		u32 error_cfg_wr_disable	: 1;
		u32 reserved2			: 21;
	} reg012_secondary_en;

	struct {
		u32 reserved0			: 1;
		u32 req_timeout_rst_sel		: 1;
		u32 reserved1			: 1;
		u32 dec_commonirq_mode		: 1;
		u32 reserved2			: 2;
		u32 stmerror_waitdecfifo_empty	: 1;
		u32 reserved3			: 5;
		u32 allow_not_wr_unref_bframe	: 1;
		u32 fbc_output_wr_disable	: 1;
		u32 reserved4			: 4;
		u32 error_mode			: 1;
		u32 reserved5			: 2;
		u32 ycacherd_prior		: 1;
		u32 reserved6			: 2;
		u32 cur_pic_is_idr		: 1;
		u32 reserved7			: 1;
		u32 right_auto_rst_disable	: 1;
		u32 frame_end_err_rst_flag	: 1;
		u32 rd_prior_mode		: 1;
		u32 rd_ctrl_prior_mode		: 1;
		u32 reserved8			: 1;
		u32 filter_outbuf_mode		: 1;
	} reg013_en_mode_set;

	struct {
		u32 fbc_force_uncompress	: 1;

		u32 reserved0			: 2;
		u32 allow_16x8_cp_flag		: 1;
		u32 reserved1			: 2;

		u32 fbc_h264_exten_4or8_flag	: 1;
		u32 reserved2			: 25;
	} reg014_fbc_param_set;

	struct {
		u32 rlc_mode_direct_write	: 1;
		u32 rlc_mode			: 1;
		u32 reserved0			: 3;

		u32 strm_start_bit		: 7;
		u32 reserved1			: 20;
	} reg015_stream_param_set;

	u32 reg016_stream_len;

	struct {
		u32 slice_num	: 25;
		u32 reserved	: 7;
	} reg017_slice_number;

	struct {
		u32 y_hor_virstride	: 16;
		u32 reserved		: 16;
	} reg018_y_hor_stride;

	struct {
		u32 uv_hor_virstride	: 16;
		u32 reserved		: 16;
	} reg019_uv_hor_stride;

	struct {
		u32 y_virstride		: 28;
		u32 reserved		: 4;
	} reg020_y_stride;

	struct {
		u32 inter_error_prc_mode		: 1;
		u32 error_intra_mode			: 1;
		u32 error_deb_en			: 1;
		u32 picidx_replace			: 5;
		u32 error_spread_e			: 1;
		u32 reserved0				: 3;
		u32 error_inter_pred_cross_slice	: 1;
		u32 reserved1				: 11;
		u32 roi_error_ctu_cal_en		: 1;
		u32 reserved2				: 7;
	} reg021_error_ctrl_set;

	struct {
		u32 roi_x_ctu_offset_st	: 12;
		u32 reserved0		: 4;
		u32 roi_y_ctu_offset_st	: 12;
		u32 reserved1		: 4;
	} reg022_err_roi_ctu_offset_start;

	struct {
		u32 roi_x_ctu_offset_end	: 12;
		u32 reserved0			: 4;
		u32 roi_y_ctu_offset_end	: 12;
		u32 reserved1			: 4;
	} reg023_err_roi_ctu_offset_end;

	struct {
		u32 cabac_err_en_lowbits	: 32;
	} reg024_cabac_error_en_lowbits;

	struct {
		u32 cabac_err_en_highbits	: 30;
		u32 reserved			: 2;
	} reg025_cabac_error_en_highbits;

	struct {
		u32 inter_auto_gating_e		: 1;
		u32 filterd_auto_gating_e	: 1;
		u32 strmd_auto_gating_e		: 1;
		u32 mcp_auto_gating_e		: 1;
		u32 busifd_auto_gating_e	: 1;
		u32 reserved0			: 3;
		u32 dec_ctrl_auto_gating_e	: 1;
		u32 intra_auto_gating_e		: 1;
		u32 mc_auto_gating_e		: 1;
		u32 transd_auto_gating_e	: 1;
		u32 reserved1			: 4;
		u32 sram_auto_gating_e		: 1;
		u32 cru_auto_gating_e		: 1;
		u32 reserved2			: 13;
		u32 reg_cfg_gating_en		: 1;
	} reg026_block_gating_en;

	struct {
		u32 core_safe_x_pixels	: 16;
		u32 core_safe_y_pixels	: 16;
	} reg027_core_safe_pixels;

	struct {
		u32 vp9_wr_prob_idx		: 3;
		u32 reserved0			: 1;
		u32 vp9_rd_prob_idx		: 3;
		u32 reserved1			: 1;

		u32 ref_req_advance_flag	: 1;
		u32 colmv_req_advance_flag	: 1;
		u32 poc_only_highbit_flag	: 1;
		u32 poc_arb_flag		: 1;

		u32 reserved2			: 4;
		u32 film_idx			: 10;
		u32 reserved3			: 2;
		u32 pu_req_mismatch_dis		: 1;
		u32 colmv_req_mismatch_dis	: 1;
		u32 reserved4			: 2;
	} reg028_multiply_core_ctrl;

	struct {
		u32 scale_down_hor_ratio	: 2;
		u32 reserved0			: 6;
		u32 scale_down_vrz_ratio	: 2;
		u32 reserved1			: 22;
	} reg029_scale_down_ctrl;

	struct {
		u32 y_scale_down_tile8x8_hor_stride	: 20;
		u32 reserved0				: 12;
	} reg030_y_scale_down_tile8x8_hor_stride;

	struct {
		u32 uv_scale_down8x8_tile_hor_stride	: 20;
		u32 reserved0				: 12;
	} reg031_uv_scale_down_tile8x8_hor_stride;

	u32 reg032_timeout_threshold;
} __packed;

struct rkvdec_vdpu381_vp9_set {
	u32 vp9_cprheader_offset	: 16;
	u32 reserved			: 16;
} __packed;

/* base: OFFSET_COMMON_ADDR_REGS */
struct rkvdec_vdpu381_regs_common_addr {
	u32 rlc_base;
	u32 rlcwrite_base;
	u32 decout_base;
	u32 colmv_cur_base;
	u32 error_ref_base;
	u32 rcb_base[10];
} __packed;

struct rkvdec_vdpu381_h26x_set {
	u32 h26x_frame_orslice		: 1;
	u32 h26x_rps_mode		: 1;
	u32 h26x_stream_mode		: 1;
	u32 h26x_stream_lastpacket	: 1;
	u32 h264_firstslice_flag	: 1;
	u32 reserved			: 27;
} __packed;

/* base: OFFSET_CODEC_PARAMS_REGS */
struct rkvdec_vdpu381_regs_h264_params {
	struct rkvdec_vdpu381_h26x_set reg064_h26x_set;

	u32 reg065_cur_top_poc;
	u32 reg066_cur_bot_poc;
	u32 reg067_098_ref_poc[32];

	struct rkvdec_vdpu381_h264_info {
		struct rkvdec_vdpu381_h264_ref_info {
			u32 ref_field		: 1;
			u32 ref_topfield_used	: 1;
			u32 ref_botfield_used	: 1;
			u32 ref_colmv_use_flag	: 1;
			u32 reserved		: 4;
		} __packed ref_info[4];
	} __packed reg099_102_ref_info_regs[4];

	u32 reserved_103_111[9];

	struct {
		u32 avs2_ref_error_field	: 1;
		u32 avs2_ref_error_topfield	: 1;
		u32 ref_error_topfield_used	: 1;
		u32 ref_error_botfield_used	: 1;
		u32 reserved			: 28;
	} reg112_error_ref_info;
} __packed;

struct rkvdec_vdpu381_regs_hevc_params {
	struct rkvdec_vdpu381_h26x_set reg064_h26x_set;

	u32 reg065_cur_top_poc;
	u32 reg066_cur_bot_poc;
	u32 reg067_082_ref_poc[16];

	u32 reserved_083_098[16];

	struct {
		u32 hevc_ref_valid_0    : 1;
		u32 hevc_ref_valid_1    : 1;
		u32 hevc_ref_valid_2    : 1;
		u32 hevc_ref_valid_3    : 1;
		u32 reserve0            : 4;
		u32 hevc_ref_valid_4    : 1;
		u32 hevc_ref_valid_5    : 1;
		u32 hevc_ref_valid_6    : 1;
		u32 hevc_ref_valid_7    : 1;
		u32 reserve1            : 4;
		u32 hevc_ref_valid_8    : 1;
		u32 hevc_ref_valid_9    : 1;
		u32 hevc_ref_valid_10   : 1;
		u32 hevc_ref_valid_11   : 1;
		u32 reserve2            : 4;
		u32 hevc_ref_valid_12   : 1;
		u32 hevc_ref_valid_13   : 1;
		u32 hevc_ref_valid_14   : 1;
		u32 reserve3            : 5;
	} reg099_hevc_ref_valid;

	u32 reserved_100_102[3];

	struct {
		u32 ref_pic_layer_same_with_cur : 16;
		u32 reserve                     : 16;
	} reg103_hevc_mvc0;

	struct {
		u32 poc_lsb_not_present_flag        : 1;
		u32 num_direct_ref_layers           : 6;
		u32 reserve0                        : 1;

		u32 num_reflayer_pics               : 6;
		u32 default_ref_layers_active_flag  : 1;
		u32 max_one_active_ref_layer_flag   : 1;

		u32 poc_reset_info_present_flag     : 1;
		u32 vps_poc_lsb_aligned_flag        : 1;
		u32 mvc_poc15_valid_flag            : 1;
		u32 reserve1                        : 13;
	} reg104_hevc_mvc1;

	u32 reserved_105_111[7];

	struct {
		u32 avs2_ref_error_field        : 1;
		u32 avs2_ref_error_topfield     : 1;
		u32 ref_error_topfield_used     : 1;
		u32 ref_error_botfield_used     : 1;
		u32 reserve                     : 28;
	} reg112_hevc_ref_info;

} __packed;

/* base: OFFSET_CODEC_PARAMS_REGS */
struct rkvdec_vdpu381_regs_vp9_params {
	struct rkvdec_vdpu381_vp9_set reg064; // 0x0100
	u32 cur_top_poc; // 0x0104
	u32 reserved0 ; // 0x0108

	struct rkvdec_vdpu381_vp9_segid_grp {
		u32 vp9_segid_abs_delta 		: 1;
		u32 vp9_segid_frame_qp_delta_en		: 1;
		u32 vp9_segid_frame_qp_delta		: 9;
		u32 vp9_segid_frame_loopfilter_value_en	: 1;
		u32 vp9_segid_frame_loopfilter_value	: 7;
		u32 vp9_segid_referinfo_en		: 1;
		u32 vp9_segid_referinfo			: 2;
		u32 vp9_segid_frame_skip_en		: 1;
		u32 reserved				: 9;
	} reg67_74[8]; // 0x010c - 0x0128

	struct rkvdec_vdpu381_vp9_info_lastframe {
		u32 vp9_mode_deltas_lastframe		: 14;
		u32 reserved0				: 2;
		u32 segmentation_enable_lstframe	: 1;
		u32 vp9_last_showframe			: 1;
		u32 vp9_last_intra_only			: 1;
		u32 vp9_last_widhheight_eqcur		: 1;
		u32 vp9_color_sapce_lastkeyframe	: 3;
		u32 reserved1				: 9;
	} reg75; // 0x012c

	struct rkvdec_vdpu381_vp9_cprheader_config {
		u32 vp9_tx_mode				: 3;
		u32 vp9_frame_reference_mode		: 2;
		u32 reserved				: 27;
	} reg76; // 0x0130

	struct rkvdec_vdpu381_vp9_intercmd_num {
		u32 vp9_intercmd_num			: 24;
		u32 reserved				: 8;
	} reg77; // 0x0134

	u32 reg78_vp9_stream_size; // 0x0138

	struct rkvdec_vdpu381_vp9_lastf_y_hor_virstride {
		u32 vp9_lastfy_hor_virstride		: 16;
		u32 reserved				: 16;
	} reg79; // 0x013c

	struct rkvdec_vdpu381_vp9_lastf_uv_hor_virstride {
		u32 vp9_lastfuv_hor_virstride		: 16;
		u32 reserved				: 16;
	} reg80 ; // 0x0140

	struct rkvdec_vdpu381_vp9_goldenf_y_hor_virstride {
		u32 vp9_goldenfy_hor_virstride		: 16;
		u32 reserved				: 16;
	} reg81; // 0x0144

	struct rkvdec_vdpu381_vp9_golden_uv_hot_virstride {
		u32 vp9_goldenuv_hor_virstride		: 16;
		u32 reserved				: 16;
	} reg82; // 0x0148

	struct rkvdec_vdpu381_vp9_altreff_y_hor_virstride {
		u32 vp9_altreffy_hor_virstride		: 16;
		u32 reserved				: 16;
	} reg83; // 0x014c

	struct rkvdec_vdpu381_vp9_altreff_uv_hor_virstride {
		u32 vp9_altreff_uv_hor_virstride	: 16;
		u32 reserved				: 16;
	} reg84; // 0x0150

	struct rkvdec_vdpu381_vp9_lastf_y_virstride {
		u32 vp9_lastfy_virstride		: 28;
		u32 reserved				: 4;
	} reg85; // 0x0154

	struct rkvdec_vdpu381_vp9_golden_y_virstride {
		u32 vp9_goldeny_virstride		: 28;
		u32 reserved				: 4;
	} reg86; // 0x0158

	struct rkvdec_vdpu381_vp9_altref_y_virstride {
		u32 vp9_altrefy_virstride		: 28;
		u32 reserved				: 4;
	} reg87; // 0x015c

	struct rkvdec_vdpu381_vp9_lref_hor_scale {
		u32 vp9_lref_hor_scale			: 16;
		u32 reserved				: 16;
	} reg88; // 0x0160

	struct rkvdec_vdpu381_vp9_lref_ver_scale {
		u32 vp9_lref_ver_scale			: 16;
		u32 reserved				: 16;
	} reg89; // 0x0164

	struct rkvdec_vdpu381_vp9_gref_hor_scale {
		u32 vp9_gref_hor_scale			: 16;
		u32 reserved				: 16;
	} reg90; // 0x0168

	struct rkvdec_vdpu381_vp9_gref_ver_scale {
		u32 vp9_gref_ver_scale			: 16;
		u32 reserved				: 16;
	} reg91; // 0x016c

	struct rkvdec_vdpu381_vp9_aref_hor_scale {
		u32 vp9_aref_hor_scale			: 16;
		u32 reserved				: 16;
	} reg92; // 0x0170

	struct rkvdec_vdpu381_vp9_aref_ver_scale {
		u32 vp9_aref_ver_scale			: 16;
		u32 reserved				: 16;
	} reg93; // 0x0174

	struct rkvdec_vdpu381_vp9_ref_deltas_lastframe {
		u32 vp9_ref_deltas_lastframe		: 28;
		u32 reserved				: 4;
	} reg94; // 0x0178

	u32 reg95_vp9_last_poc; // 0x017c

	u32 reg96_vp9_golden_poc; // 0x0180

	u32 reg97_vp9_altref_poc; // 0x0184

	u32 reg98_vp9_col_ref_poc; // 0x0188

	struct rkvdec_vdpu381_vp9_prob_ref_poc {
		u32 vp9_prob_ref_poc			: 16;
		u32 reserved				: 16;
	} reg99; // 0x018c

	struct rkvdec_vdpu381_vp9_segid_ref_poc {
		u32 vp9_segid_ref_poc			: 16;
		u32 reserved				: 16;
	} reg100; // 0x0190

	u32 reserved1[2]; // 0x0194 - 0x0198 [2 bits [101,102]]

	struct rkvdec_vdpu381_vp9_prob_en {
		u32 reserved				: 20;
		u32 vp9_prob_update_en			: 1;
		u32 vp9_refresh_en			: 1;
		u32 vp9_prob_save_en			: 1;
		u32 vp9_intra_only_flag			: 1;
		u32 vp9_txfmmode_rfsh_en		: 1;
		u32 vp9_ref_mode_rfsh_en		: 1;
		u32 vp9_single_ref_rfsh_en		: 1;
		u32 vp9_comp_ref_rfsh_en		: 1;
		u32 vp9_interp_filter_switch_en		: 1;
		u32 vp9_allow_high_precision_mv		: 1;
		u32 vp9_last_key_frame_flag		: 1;
		u32 vp9_inter_coef_rfsh_flag		: 1;
	} reg103; // 0x019c

	u32 reserved2; // 0x01a0 [1 bit [104]]

	struct rkvdec_vdpu381_vp9_cnt_upd_en_avs2_headlen {
		u32 avs2_head_len			: 4;
		u32 vp9count_update_en			: 1;
		u32 reserved				: 27;
	} reg105; // 0x01a4

	struct rkvdec_vdpu381_vp9_frame_width_last {
		u32 vp9_framewidth_last			: 16;
		u32 reserved				: 16;

	} reg106; // 0x01a8

	struct rkvdec_vdpu381_vp9_frame_height_last {
		u32 vp9_frameheight_last		: 16;
		u32 reserved				: 16;
	} reg107; // 0x01ac

	struct rkvdec_vdpu381_vp9_frame_width_golden {
		u32 vp9_framewidth_golden		: 16;
		u32 reserved				: 16;
	} reg108; // 0x01b0

	struct rkvdec_vdpu381_vp9_frame_height_golden {
		u32 vp9_frameheight_golden		: 16;
		u32 reserved				: 16;
	} reg109; // 0x01b4

	struct rkvdec_vdpu381_vp9_frame_width_altref {
		u32 vp9_framewidth_altref		: 16;
		u32 reserved				: 16;
	} reg110; // 0x01b8

	struct rkvdec_vdpu381_vp9_frame_height_altref {
		u32 vp9_frameheight_altref		: 16;
		u32 reserved				: 16;
	} reg111; // 0x01bc

	u32 reserved3; // 0x01c0 [1 bit [reg112]]
} __packed;

struct rkvdec_vdpu381_regs_vp9_addr {
	u32 vp9_delta_prob_base; // 0x0280 [reg160]
	u32 reserved0; // 0x0284 [reg161]
	u32 vp9_last_prob_base; // 0x0288 [reg162]
	u32 reserved1; // 0x028c [reg163]
	u32 vp9_referlast_base; // 0x0290 [reg164]
	u32 vp9_refergolden_base; // 0x0294 [reg165]
	u32 vp9_referalfter_base; // 0x0298 [reg166]
	u32 vp9_count_base; // 0x029c [reg167]
	u32 vp9_segidlast_base; // 0x02a0 [reg168]
	u32 avp9_segidcur_base; // 0x02a4 [reg169]
	u32 vp9_refcolmv_base; // 0x02a8 [reg170]
	u32 vp9_intercmd_base; // 0x02ac [reg171]
	u32 vp9_update_prob_wr_bas; // 0x02b0 [reg172]
	u32 reserved2[7]; // 0x02b4 - 0x02cc [reg173 - reg179]
	// testing, could see pixels but damaged[ changed below lines]
	u32 scanlist_addr; // 0x02d0 [reg180]
	u32 colmv_base[16]; // 0x02d4 - 0x0310 [reg181 - reg196]
	u32 cabactbl_base;
} __packed;

/* base: OFFSET_CODEC_ADDR_REGS */
struct rkvdec_vdpu381_regs_h26x_addr {
	u32 reserved_160;
	u32 reg161_pps_base;
	u32 reserved_162;
	u32 reg163_rps_base;
	u32 reg164_180_ref_base[16];
	u32 reg181_scanlist_addr;
	u32 reg182_198_colmv_base[16];
	u32 reg199_cabactbl_base;
} __packed;

struct rkvdec_vdpu381_regs_h26x_highpoc {
	struct {
		u32 ref0_poc_highbit	: 4;
		u32 ref1_poc_highbit	: 4;
		u32 ref2_poc_highbit	: 4;
		u32 ref3_poc_highbit	: 4;
		u32 ref4_poc_highbit	: 4;
		u32 ref5_poc_highbit	: 4;
		u32 ref6_poc_highbit	: 4;
		u32 ref7_poc_highbit	: 4;
	} reg200_203_ref_poc_highbit[4];
	struct {
		u32 cur_poc_highbit	: 4;
		u32 reserved		: 28;
	} reg204_cur_poc_highbit;
} __packed;

// struct rkvdec_vdpu381_regs_vp9_highpoc {
// 	struct rkvdec_vdpu381_ref_poc_highbit {
// 		u32 ref0_poc_highbit	: 4;
// 		u32 ref1_poc_highbit	: 4;
// 		u32 ref2_poc_highbit	: 4;
// 		u32 ref3_poc_highbit	: 4;
// 		u32 ref4_poc_highbit	: 4;
// 		u32 ref5_poc_highbit	: 4;
// 		u32 ref6_poc_highbit	: 4;
// 		u32 ref7_poc_highbit	: 4;
// 	} reg200[4];
// 	struct rkvdec_vdpu381_cur_poc_highbit {
// 		u32 cur_poc_highbit	: 4;
// 		u32 reserved		: 28;
// 	} reg204;
// } __packed;

struct rkvdec_vdpu381_regs_h264 {
	struct rkvdec_vdpu381_regs_common		common;
	struct rkvdec_vdpu381_regs_h264_params		h264_param;
	struct rkvdec_vdpu381_regs_common_addr		common_addr;
	struct rkvdec_vdpu381_regs_h26x_addr		h264_addr;
	struct rkvdec_vdpu381_regs_h26x_highpoc		h264_highpoc;
} __packed;

struct rkvdec_vdpu381_regs_hevc {
	struct rkvdec_vdpu381_regs_common		common;
	struct rkvdec_vdpu381_regs_hevc_params		hevc_param;
	struct rkvdec_vdpu381_regs_common_addr		common_addr;
	struct rkvdec_vdpu381_regs_h26x_addr		hevc_addr;
	struct rkvdec_vdpu381_regs_h26x_highpoc		hevc_highpoc;
} __packed;

struct rkvdec_vdpu381_regs_vp9 {
	struct rkvdec_vdpu381_regs_common 		common;
	struct rkvdec_vdpu381_regs_vp9_params		vp9_param;
	struct rkvdec_vdpu381_regs_common_addr		common_addr;
	struct rkvdec_vdpu381_regs_vp9_addr		vp9_addr;
	//struct rkvdec_vdpu381_regs_h26x_highpoc	vp9_highpoc;
} __packed;

#endif /* __RKVDEC_REGS_H__ */
