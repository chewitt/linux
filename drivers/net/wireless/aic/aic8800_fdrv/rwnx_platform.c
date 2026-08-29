// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @file rwnx_platform.c
 *
 * @brief Platform profile
 *
 ******************************************************************************
 */

#include <linux/delay.h>
#include <crypto/md5.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include "hal_desc.h"
#include "reg_access.h"
#include "rwnx_main.h"
#include "rwnx_platform.h"
#include "ipc_host.h"
#include "rwnx_msg_tx.h"

#ifdef AICWF_SDIO_SUPPORT
#include "aicwf_sdio.h"
#endif

#include "aicwf_compat_8800d80.h"
#include "aicwf_compat_8800dc.h"
#include "aicwf_chip_ops.h"

// Parser state
#define INIT      0
#define CMD       1
#define PRINT     2
#define GET_VALUE 3

struct reg_table {
	char ccode[3];
	enum regions_code region;
};

/* If the region conflicts with the kernel, the actual authentication standard prevails */
static struct reg_table reg_tables[] = {
	{.ccode = "CN", .region = REGIONS_SRRC},
	{.ccode = "US", .region = REGIONS_FCC},
	{.ccode = "DE", .region = REGIONS_ETSI},
	{.ccode = "00", .region = REGIONS_DEFAULT},
	{.ccode = "WW", .region = REGIONS_DEFAULT},
	{.ccode = "XX", .region = REGIONS_DEFAULT},
	{.ccode = "JP", .region = REGIONS_JP},
	{.ccode = "AD", .region = REGIONS_ETSI},
	{.ccode = "AE", .region = REGIONS_ETSI},
	{.ccode = "AF", .region = REGIONS_ETSI},
	{.ccode = "AI", .region = REGIONS_ETSI},
	{.ccode = "AL", .region = REGIONS_ETSI},
	{.ccode = "AM", .region = REGIONS_ETSI},
	{.ccode = "AN", .region = REGIONS_ETSI},
	{.ccode = "AR", .region = REGIONS_FCC},
	{.ccode = "AS", .region = REGIONS_FCC},
	{.ccode = "AT", .region = REGIONS_ETSI},
	{.ccode = "AU", .region = REGIONS_ETSI},
	{.ccode = "AW", .region = REGIONS_ETSI},
	{.ccode = "AZ", .region = REGIONS_ETSI},
	{.ccode = "BA", .region = REGIONS_ETSI},
	{.ccode = "BB", .region = REGIONS_FCC},
	{.ccode = "BD", .region = REGIONS_ETSI},
	{.ccode = "BE", .region = REGIONS_ETSI},
	{.ccode = "BF", .region = REGIONS_FCC},
	{.ccode = "BG", .region = REGIONS_ETSI},
	{.ccode = "BH", .region = REGIONS_ETSI},
	{.ccode = "BL", .region = REGIONS_ETSI},
	{.ccode = "BM", .region = REGIONS_FCC},
	{.ccode = "BN", .region = REGIONS_JP},
	{.ccode = "BO", .region = REGIONS_JP},
	{.ccode = "BR", .region = REGIONS_FCC},
	{.ccode = "BS", .region = REGIONS_FCC},
	{.ccode = "BT", .region = REGIONS_ETSI},
	{.ccode = "BW", .region = REGIONS_ETSI},
	{.ccode = "BY", .region = REGIONS_ETSI},
	{.ccode = "BZ", .region = REGIONS_JP},
	{.ccode = "CA", .region = REGIONS_FCC},
	{.ccode = "CF", .region = REGIONS_FCC},
	{.ccode = "CH", .region = REGIONS_ETSI},
	{.ccode = "CI", .region = REGIONS_FCC},
	{.ccode = "CL", .region = REGIONS_ETSI},
	{.ccode = "CO", .region = REGIONS_FCC},
	{.ccode = "CR", .region = REGIONS_FCC},
	{.ccode = "CU", .region = REGIONS_FCC},
	{.ccode = "CX", .region = REGIONS_FCC},
	{.ccode = "CY", .region = REGIONS_ETSI},
	{.ccode = "CZ", .region = REGIONS_ETSI},
	{.ccode = "DK", .region = REGIONS_ETSI},
	{.ccode = "DM", .region = REGIONS_FCC},
	{.ccode = "DO", .region = REGIONS_FCC},
	{.ccode = "DZ", .region = REGIONS_ETSI},
	{.ccode = "EC", .region = REGIONS_FCC},
	{.ccode = "EE", .region = REGIONS_ETSI},
	{.ccode = "EG", .region = REGIONS_ETSI},
	{.ccode = "ES", .region = REGIONS_ETSI},
	{.ccode = "ET", .region = REGIONS_ETSI},
	{.ccode = "FI", .region = REGIONS_ETSI},
	{.ccode = "FK", .region = REGIONS_ETSI},
	{.ccode = "FM", .region = REGIONS_FCC},
	{.ccode = "FO", .region = REGIONS_ETSI},
	{.ccode = "FR", .region = REGIONS_ETSI},
	{.ccode = "GB", .region = REGIONS_ETSI},
	{.ccode = "GD", .region = REGIONS_FCC},
	{.ccode = "GE", .region = REGIONS_ETSI},
	{.ccode = "GF", .region = REGIONS_ETSI},
	{.ccode = "GH", .region = REGIONS_ETSI},
	{.ccode = "GI", .region = REGIONS_ETSI},
	{.ccode = "GL", .region = REGIONS_ETSI},
	{.ccode = "GP", .region = REGIONS_ETSI},
	{.ccode = "GR", .region = REGIONS_ETSI},
	{.ccode = "GT", .region = REGIONS_FCC},
	{.ccode = "GU", .region = REGIONS_FCC},
	{.ccode = "GY", .region = REGIONS_DEFAULT},
	{.ccode = "HK", .region = REGIONS_ETSI},
	{.ccode = "HN", .region = REGIONS_FCC},
	{.ccode = "HR", .region = REGIONS_ETSI},
	{.ccode = "HT", .region = REGIONS_FCC},
	{.ccode = "HU", .region = REGIONS_ETSI},
	{.ccode = "ID", .region = REGIONS_ETSI},
	{.ccode = "IE", .region = REGIONS_ETSI},
	{.ccode = "IL", .region = REGIONS_ETSI},
	{.ccode = "IN", .region = REGIONS_ETSI},
	{.ccode = "IQ", .region = REGIONS_ETSI},
	{.ccode = "IR", .region = REGIONS_JP},
	{.ccode = "IS", .region = REGIONS_ETSI},
	{.ccode = "IT", .region = REGIONS_ETSI},
	{.ccode = "JM", .region = REGIONS_FCC},
	{.ccode = "JO", .region = REGIONS_ETSI},
	{.ccode = "KE", .region = REGIONS_ETSI},
	{.ccode = "KG", .region = REGIONS_ETSI},
	{.ccode = "KH", .region = REGIONS_ETSI},
	{.ccode = "KN", .region = REGIONS_ETSI},
	{.ccode = "KP", .region = REGIONS_JP},
	{.ccode = "KR", .region = REGIONS_ETSI},
	{.ccode = "KW", .region = REGIONS_ETSI},
	{.ccode = "KY", .region = REGIONS_FCC},
	{.ccode = "KZ", .region = REGIONS_ETSI},
	{.ccode = "LB", .region = REGIONS_ETSI},
	{.ccode = "LC", .region = REGIONS_ETSI},
	{.ccode = "LI", .region = REGIONS_ETSI},
	{.ccode = "LK", .region = REGIONS_FCC},
	{.ccode = "LS", .region = REGIONS_ETSI},
	{.ccode = "LT", .region = REGIONS_ETSI},
	{.ccode = "LU", .region = REGIONS_ETSI},
	{.ccode = "LV", .region = REGIONS_ETSI},
	{.ccode = "LY", .region = REGIONS_ETSI},
	{.ccode = "MA", .region = REGIONS_ETSI},
	{.ccode = "MC", .region = REGIONS_ETSI},
	{.ccode = "MD", .region = REGIONS_ETSI},
	{.ccode = "ME", .region = REGIONS_ETSI},
	{.ccode = "MF", .region = REGIONS_ETSI},
	{.ccode = "MH", .region = REGIONS_FCC},
	{.ccode = "MK", .region = REGIONS_ETSI},
	{.ccode = "MN", .region = REGIONS_ETSI},
	{.ccode = "MO", .region = REGIONS_ETSI},
	{.ccode = "MP", .region = REGIONS_FCC},
	{.ccode = "MQ", .region = REGIONS_ETSI},
	{.ccode = "MR", .region = REGIONS_ETSI},
	{.ccode = "MT", .region = REGIONS_ETSI},
	{.ccode = "MU", .region = REGIONS_FCC},
	{.ccode = "MV", .region = REGIONS_ETSI},
	{.ccode = "MW", .region = REGIONS_ETSI},
	{.ccode = "MX", .region = REGIONS_FCC},
	{.ccode = "MY", .region = REGIONS_ETSI},
	{.ccode = "NA", .region = REGIONS_ETSI},
	{.ccode = "NG", .region = REGIONS_ETSI},
	{.ccode = "NI", .region = REGIONS_FCC},
	{.ccode = "NL", .region = REGIONS_ETSI},
	{.ccode = "NO", .region = REGIONS_ETSI},
	{.ccode = "NP", .region = REGIONS_JP},
	{.ccode = "NZ", .region = REGIONS_ETSI},
	{.ccode = "OM", .region = REGIONS_ETSI},
	{.ccode = "PA", .region = REGIONS_FCC},
	{.ccode = "PE", .region = REGIONS_ETSI},
	{.ccode = "PF", .region = REGIONS_ETSI},
	{.ccode = "PG", .region = REGIONS_FCC},
	{.ccode = "PH", .region = REGIONS_ETSI},
	{.ccode = "PK", .region = REGIONS_ETSI},
	{.ccode = "PL", .region = REGIONS_ETSI},
	{.ccode = "PM", .region = REGIONS_ETSI},
	{.ccode = "PR", .region = REGIONS_FCC},
	{.ccode = "PT", .region = REGIONS_ETSI},
	{.ccode = "PW", .region = REGIONS_FCC},
	{.ccode = "PY", .region = REGIONS_FCC},
	{.ccode = "QA", .region = REGIONS_ETSI},
	{.ccode = "RE", .region = REGIONS_ETSI},
	{.ccode = "RO", .region = REGIONS_ETSI},
	{.ccode = "RS", .region = REGIONS_ETSI},
	{.ccode = "RU", .region = REGIONS_ETSI},
	{.ccode = "RW", .region = REGIONS_FCC},
	{.ccode = "SA", .region = REGIONS_ETSI},
	{.ccode = "SE", .region = REGIONS_ETSI},
	{.ccode = "SG", .region = REGIONS_ETSI},
	{.ccode = "SI", .region = REGIONS_ETSI},
	{.ccode = "SK", .region = REGIONS_ETSI},
	{.ccode = "SM", .region = REGIONS_ETSI},
	{.ccode = "SN", .region = REGIONS_FCC},
	{.ccode = "SR", .region = REGIONS_ETSI},
	{.ccode = "SV", .region = REGIONS_FCC},
	{.ccode = "SY", .region = REGIONS_DEFAULT},
	{.ccode = "TC", .region = REGIONS_FCC},
	{.ccode = "TD", .region = REGIONS_ETSI},
	{.ccode = "TG", .region = REGIONS_ETSI},
	{.ccode = "TH", .region = REGIONS_ETSI},
	{.ccode = "TJ", .region = REGIONS_ETSI},
	{.ccode = "TM", .region = REGIONS_ETSI},
	{.ccode = "TN", .region = REGIONS_ETSI},
	{.ccode = "TR", .region = REGIONS_ETSI},
	{.ccode = "TT", .region = REGIONS_FCC},
	{.ccode = "TW", .region = REGIONS_FCC},
	{.ccode = "TZ", .region = REGIONS_DEFAULT},
	{.ccode = "UA", .region = REGIONS_ETSI},
	{.ccode = "UG", .region = REGIONS_FCC},
	{.ccode = "UY", .region = REGIONS_ETSI},
	{.ccode = "UZ", .region = REGIONS_ETSI},
	{.ccode = "VC", .region = REGIONS_ETSI},
	{.ccode = "VE", .region = REGIONS_FCC},
	{.ccode = "VI", .region = REGIONS_FCC},
	{.ccode = "VN", .region = REGIONS_JP},
	{.ccode = "VU", .region = REGIONS_FCC},
	{.ccode = "WF", .region = REGIONS_ETSI},
	{.ccode = "WS", .region = REGIONS_ETSI},
	{.ccode = "YE", .region = REGIONS_DEFAULT},
	{.ccode = "YT", .region = REGIONS_ETSI},
	{.ccode = "ZA", .region = REGIONS_ETSI},
	{.ccode = "ZM", .region = REGIONS_ETSI},
	{.ccode = "ZW", .region = REGIONS_ETSI},
	{.ccode = "XK", .region = REGIONS_ETSI},
};

struct rwnx_plat *g_rwnx_plat;

struct userconfig_info_t {
	struct txpwr_lvl_conf txpwr_lvl;
	struct txpwr_lvl_conf_v2 txpwr_lvl_v2;
	struct txpwr_lvl_conf_v3 txpwr_lvl_v3;
	struct txpwr_lvl_conf_v3 txpwr_lvl_v3_gp[REGION_NUM_MAX];
	struct txpwr_lvl_adj_conf txpwr_lvl_adj;
	struct txpwr_loss_conf txpwr_loss;
	struct txpwr_ofst_conf txpwr_ofst;
	struct txpwr_ofst2x_conf txpwr_ofst2x;
	struct xtal_cap_conf xtal_cap;
};

static struct userconfig_info_t userconfig_info = {
	.txpwr_lvl = { .enable = 1,
				  .dsss = 9,
				  .ofdmlowrate_2g4 = 8,
				  .ofdm64qam_2g4 = 8,
				  .ofdm256qam_2g4 = 8,
				  .ofdm1024qam_2g4 = 8,
				  .ofdmlowrate_5g = 11,
				  .ofdm64qam_5g = 10,
				  .ofdm256qam_5g = 9,
				  .ofdm1024qam_5g = 9},
	.txpwr_lvl_v2 =	{ .enable = 1,
			.pwrlvl_11b_11ag_2g4 =
				// 1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,
				// 48M,  54M
			{20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_2g4 =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 16},
			.pwrlvl_11ax_2g4 =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9,
				// MCS10,MCS11
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15},
		},
	.txpwr_lvl_v3 =	{ .enable = 1,
			.pwrlvl_11b_11ag_2g4 =
				// 1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,
				// 48M,  54M
			{20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_2g4 =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 16},
			.pwrlvl_11ax_2g4 =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9,
				// MCS10,MCS11
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15},
			.pwrlvl_11a_5g =
				// NA,   NA,   NA,   NA,   6M,   9M,   12M,  18M,  24M,  36M,
				// 48M,  54M
			{0x80, 0x80, 0x80, 0x80, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_5g =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 15},
			.pwrlvl_11ax_5g =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9,
				// MCS10,MCS11
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 15, 14, 14},
		},
	.txpwr_lvl_v3_gp[0] = { .enable = 1,
			.pwrlvl_11b_11ag_2g4 =
				// 1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,
				// 48M,  54M
			{20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_2g4 =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 16},
			.pwrlvl_11ax_2g4 =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9,
				// MCS10,MCS11
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15},
			.pwrlvl_11a_5g =
				// NA,   NA,   NA,   NA,   6M,   9M,   12M,  18M,  24M,  36M,
				// 48M,  54M
			{0x80, 0x80, 0x80, 0x80, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_5g =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 15},
			.pwrlvl_11ax_5g =
				// MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9,
				// MCS10,MCS11
			{20, 20, 20, 20, 18, 18, 16, 16, 16, 15, 14, 14},
		},
	.txpwr_lvl_v3_gp[1] = { .enable = 1,
			.pwrlvl_11b_11ag_2g4 = {20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_2g4 = {20, 20, 20, 20, 18, 18, 16, 16, 16, 16},
			.pwrlvl_11ax_2g4 = {20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15},
			.pwrlvl_11a_5g = {0x80, 0x80, 0x80, 0x80, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_5g = {20, 20, 20, 20, 18, 18, 16, 16, 16, 15},
			.pwrlvl_11ax_5g = {20, 20, 20, 20, 18, 18, 16, 16, 16, 15, 14, 14},
		},
	.txpwr_lvl_v3_gp[2] = { .enable = 1,
			.pwrlvl_11b_11ag_2g4 = {20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_2g4 = {20, 20, 20, 20, 18, 18, 16, 16, 16, 16},
			.pwrlvl_11ax_2g4 = {20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15},
			.pwrlvl_11a_5g = {0x80, 0x80, 0x80, 0x80, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_5g = {20, 20, 20, 20, 18, 18, 16, 16, 16, 15},
			.pwrlvl_11ax_5g = {20, 20, 20, 20, 18, 18, 16, 16, 16, 15, 14, 14},
		},
	.txpwr_lvl_v3_gp[3] = { .enable = 1,
			.pwrlvl_11b_11ag_2g4 = {20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_2g4 = {20, 20, 20, 20, 18, 18, 16, 16, 16, 16},
			.pwrlvl_11ax_2g4 = {20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15},
			.pwrlvl_11a_5g = {0x80, 0x80, 0x80, 0x80, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_5g = {20, 20, 20, 20, 18, 18, 16, 16, 16, 15},
			.pwrlvl_11ax_5g = {20, 20, 20, 20, 18, 18, 16, 16, 16, 15, 14, 14},
		},
	.txpwr_lvl_v3_gp[4] = { .enable = 1,
			.pwrlvl_11b_11ag_2g4 = {20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_2g4 = {20, 20, 20, 20, 18, 18, 16, 16, 16, 16},
			.pwrlvl_11ax_2g4 = {20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15},
			.pwrlvl_11a_5g = {0x80, 0x80, 0x80, 0x80, 20, 20, 20, 20, 18, 18, 16, 16},
			.pwrlvl_11n_11ac_5g = {20, 20, 20, 20, 18, 18, 16, 16, 16, 15},
			.pwrlvl_11ax_5g = {20, 20, 20, 20, 18, 18, 16, 16, 16, 15, 14, 14},
		},
	.txpwr_loss = { .loss_enable_2g4 = 1,
			.loss_value_2g4 = 0,
			.loss_enable_5g = 1,
			.loss_value_5g = 0,
		},
	.txpwr_ofst = { .enable = 1,
			.chan_1_4 = 0,
			.chan_5_9 = 0,
			.chan_10_13 = 0,
			.chan_36_64 = 0,
			.chan_100_120 = 0,
			.chan_122_140 = 0,
			.chan_142_165 = 0,
		},
	.txpwr_ofst2x = { .enable = 0,
			.pwrofst2x_tbl_2g4 = {
					// ch1-4, ch5-9, ch10-13
					{0, 0, 0}, // 11b
					{0, 0, 0}, // ofdm_highrate
					{0, 0, 0}, // ofdm_lowrate
				},
			.pwrofst2x_tbl_5g = {
					// ch42,  ch58, ch106,ch122,ch138,ch155
					{0, 0, 0, 0, 0, 0}, // ofdm_lowrate
					{0, 0, 0, 0, 0, 0}, // ofdm_highrate
					{0, 0, 0, 0, 0, 0}, // ofdm_midrate
				},
		},
	.xtal_cap = { .enable = 0,
			.xtal_cap = 24,
			.xtal_cap_fine = 31,
		},
};

#ifdef CONFIG_AIC8800_POWER_LIMIT
#define POWER_LIMIT_INVALID_VAL POWER_LEVEL_INVALID_VAL

#define POWER_LIMIT_CC_MATCHED_BIT (0x1U << 0)

struct txpwr_lmt_info_t {
	u8 ch_cnt_2g4;
	u8 ch_cnt_5g;
	u8 ch_num_2g4[MAC_DOMAINCHANNEL_24G_MAX];
	u8 ch_num_5g[MAC_DOMAINCHANNEL_5G_MAX];
	s8 max_pwr_2g4[MAC_DOMAINCHANNEL_24G_MAX];
	s8 max_pwr_5g[MAC_DOMAINCHANNEL_5G_MAX];
};

struct powerlimit_info_t {
	u32 flags;
	struct txpwr_lmt_info_t txpwr_lmt;
};

static struct powerlimit_info_t powerlimit_info = {
	0,
};
#endif

int aicwf_request_firmware(const struct firmware **fw, const char *name,
			   struct device *dev)
{
	char *fw_name;
	int ret;

	if (!name || !name[0])
		return -EINVAL;

	fw_name = kasprintf(GFP_KERNEL, AIC8800_FW_DIR "%s", name);
	if (!fw_name)
		return -ENOMEM;

	ret = request_firmware(fw, fw_name, dev);
	kfree(fw_name);

	return ret;
}

#define MD5PINRT                                                               \
	"file "                                                                    \
	"md5:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\r\n"

static int rwnx_load_firmware(u32 **fw_buf, const char *name,
			      struct device *device)
{
	const struct firmware *fw = NULL;
	u32 *dst = NULL;
	void *buffer = NULL;
	u8 digest[MD5_DIGEST_SIZE];
	int size = 0;
	int ret = 0;

	AICWFDBG(LOGINFO, "AICWF %s: request firmware = %s\n", __func__, name);

	ret = aicwf_request_firmware(&fw, name, device);

	if (ret < 0) {
		AICWFDBG(LOGDEBUG, "AICWF Load %s fail\n", name);
		return ret;
	}

	size = fw->size;
	dst = (u32 *)fw->data;

	if (size <= 0) {
		AICWFDBG(LOGDEBUG, "AICWF wrong size of firmware file\n");
		release_firmware(fw);
		return -1;
	}

	buffer = vmalloc(size);
	if (!buffer) {
		release_firmware(fw);
		return -ENOMEM;
	}
	memset(buffer, 0, size);
	memcpy(buffer, dst, size);

	*fw_buf = buffer;

	md5(buffer, size, digest);
	AICWFDBG(LOGDEBUG, "AICWF " MD5PINRT, digest[0], digest[1], digest[2], digest[3],
		 digest[4], digest[5], digest[6], digest[7], digest[8], digest[9],
		 digest[10], digest[11], digest[12], digest[13], digest[14], digest[15]);

	release_firmware(fw);

	return size;
}

/* buffer is allocated by kzalloc */
int rwnx_request_firmware_common(struct rwnx_hw *rwnx_hw, u32 **buffer,
				 const char *filename)
{
	int size;

	AICWFDBG(LOGDEBUG, "### Load file %s\n", filename);

	size = rwnx_load_firmware(buffer, filename, rwnx_hw->dev);

	return size;
}

static void rwnx_restore_firmware(u32 **fw_buf)
{
	vfree(*fw_buf);
	*fw_buf = NULL;
}

void rwnx_release_firmware_common(u32 **buffer)
{
	rwnx_restore_firmware(buffer);
}

/**
 * rwnx_plat_bin_fw_upload_2 - Load the requested binary FW into embedded
 * side.
 *
 * @rwnx_hw: Main driver data
 * @fw_addr: Address where the fw must be loaded
 * @filename: Name of the fw.
 *
 * Load a fw, stored as a binary file, into the specified address
 *
 * Return: 0 on success, or a negative error code.
 */
int rwnx_plat_bin_fw_upload_2(struct rwnx_hw *rwnx_hw, u32 fw_addr,
			      char *filename)
{
	int err = 0;
	unsigned int i = 0, size;
	//    u32 *src;
	u32 *dst = NULL;

	/* Copy the file on the Embedded side */
	AICWFDBG(LOGINFO, "### Upload %s firmware, @ = %x\n", filename, fw_addr);

	size = rwnx_request_firmware_common(rwnx_hw, &dst, filename);
	if (!dst) {
		AICWFDBG(LOGERROR, "No such file or directory\n");
		return -1;
	}
	if (size <= 0) {
		AICWFDBG(LOGERROR, "wrong size of firmware file\n");
		dst = NULL;
		err = -1;
	}

	AICWFDBG(LOGINFO, "size=%d, dst[0]=%x\n", size, dst[0]);
	if (size > 512) {
		for (; i < (size - 512); i += 512) {
			// AICWFDBG(LOGDEBUG, "AICWF wr blk 0: %p -> %x\r\n",
			// dst + i / 4, fw_addr + i);
			err = rwnx_send_dbg_mem_block_write_req(rwnx_hw, fw_addr + i, 512,
								dst + i / 4);
			if (err) {
				AICWFDBG(LOGERROR, "bin upload fail: %x, err:%d\r\n",
					 fw_addr + i, err);
				break;
			}
		}
	}
	if (!err && i < size) {
		// AICWFDBG(LOGDEBUG, "AICWF wr blk 1: %p -> %x\r\n", dst + i / 4, fw_addr + i);
		err = rwnx_send_dbg_mem_block_write_req(rwnx_hw, fw_addr + i, size - i,
							dst + i / 4);
		if (err) {
			AICWFDBG(LOGERROR, "bin upload fail: %x, err:%d\r\n", fw_addr + i,
				 err);
		}
	}

	if (dst)
		rwnx_release_firmware_common(&dst);

	return err;
}

struct nvram_info_t {
	struct txpwr_idx_conf txpwr_idx;
	struct txpwr_ofst_conf txpwr_ofst;
	struct xtal_cap_conf xtal_cap;
};

static struct nvram_info_t nvram_info = {
	.txpwr_idx = {.enable = 1,
			.dsss = 9,
			.ofdmlowrate_2g4 = 8,
			.ofdm64qam_2g4 = 8,
			.ofdm256qam_2g4 = 8,
			.ofdm1024qam_2g4 = 8,
			.ofdmlowrate_5g = 11,
			.ofdm64qam_5g = 10,
			.ofdm256qam_5g = 9,
			.ofdm1024qam_5g = 9},
	.txpwr_ofst = { .enable = 1,
			.chan_1_4 = 0,
			.chan_5_9 = 0,
			.chan_10_13 = 0,
			.chan_36_64 = 0,
			.chan_100_120 = 0,
			.chan_122_140 = 0,
			.chan_142_165 = 0,
		},
	.xtal_cap = { .enable = 0,
			.xtal_cap = 24,
			.xtal_cap_fine = 31,
		},
};

void get_userconfig_txpwr_ofst_in_fdrv(struct txpwr_ofst_conf *txpwr_ofst)
{
	txpwr_ofst->enable = userconfig_info.txpwr_ofst.enable;
	txpwr_ofst->chan_1_4 = userconfig_info.txpwr_ofst.chan_1_4;
	txpwr_ofst->chan_5_9 = userconfig_info.txpwr_ofst.chan_5_9;
	txpwr_ofst->chan_10_13 = userconfig_info.txpwr_ofst.chan_10_13;
	txpwr_ofst->chan_36_64 = userconfig_info.txpwr_ofst.chan_36_64;
	txpwr_ofst->chan_100_120 = userconfig_info.txpwr_ofst.chan_100_120;
	txpwr_ofst->chan_122_140 = userconfig_info.txpwr_ofst.chan_122_140;
	txpwr_ofst->chan_142_165 = userconfig_info.txpwr_ofst.chan_142_165;

	AICWFDBG(LOGINFO, "%s:enable      :%d\r\n", __func__, txpwr_ofst->enable);
	AICWFDBG(LOGINFO, "%s:chan_1_4    :%d\r\n", __func__, txpwr_ofst->chan_1_4);
	AICWFDBG(LOGINFO, "%s:chan_5_9    :%d\r\n", __func__, txpwr_ofst->chan_5_9);
	AICWFDBG(LOGINFO, "%s:chan_10_13  :%d\r\n", __func__,
		 txpwr_ofst->chan_10_13);
	AICWFDBG(LOGINFO, "%s:chan_36_64  :%d\r\n", __func__,
		 txpwr_ofst->chan_36_64);
	AICWFDBG(LOGINFO, "%s:chan_100_120:%d\r\n", __func__,
		 txpwr_ofst->chan_100_120);
	AICWFDBG(LOGINFO, "%s:chan_122_140:%d\r\n", __func__,
		 txpwr_ofst->chan_122_140);
	AICWFDBG(LOGINFO, "%s:chan_142_165:%d\r\n", __func__,
		 txpwr_ofst->chan_142_165);
}

void get_userconfig_txpwr_ofst2x_in_fdrv(struct txpwr_ofst2x_conf *txpwr_ofst2x)
{
	int type, ch_grp;
	*txpwr_ofst2x = userconfig_info.txpwr_ofst2x;
	AICWFDBG(LOGINFO, "%s:enable      :%d\r\n", __func__, txpwr_ofst2x->enable);
	AICWFDBG(LOGDEBUG,
		 "pwrofst2x 2.4g: [0]:11b, [1]:ofdm_highrate, [2]:ofdm_lowrate\n  chan=\t1-4\t5-9\t10-13");

	for (type = 0; type < 3; type++) {
		AICWFDBG(LOGDEBUG, "\n  [%d] =", type);
		for (ch_grp = 0; ch_grp < 3; ch_grp++) {
			AICWFDBG(LOGDEBUG, "\t%d",
				 txpwr_ofst2x->pwrofst2x_tbl_2g4[type][ch_grp]);
		}
	}
	AICWFDBG(LOGDEBUG,
		 "\npwrofst2x 5g: [0]:ofdm_lowrate, [1]:ofdm_highrate, [2]:ofdm_midrate\n  chan=\t36-50\t51-64\t98-114\t115-130\t131-146\t147-166");
	for (type = 0; type < 3; type++) {
		AICWFDBG(LOGDEBUG, "\n  [%d] =", type);
		for (ch_grp = 0; ch_grp < 6; ch_grp++) {
			AICWFDBG(LOGDEBUG, "\t%d",
				 txpwr_ofst2x->pwrofst2x_tbl_5g[type][ch_grp]);
		}
	}
	AICWFDBG(LOGDEBUG, "\n");
}

void get_userconfig_txpwr_idx(struct txpwr_idx_conf *txpwr_idx)
{
	memcpy(txpwr_idx, &nvram_info.txpwr_idx, sizeof(struct txpwr_idx_conf));
}

void get_userconfig_txpwr_ofst(struct txpwr_ofst_conf *txpwr_ofst)
{
	memcpy(txpwr_ofst, &nvram_info.txpwr_ofst, sizeof(struct txpwr_ofst_conf));
}

void get_userconfig_xtal_cap(struct xtal_cap_conf *xtal_cap)
{
	if (nvram_info.xtal_cap.enable)
		*xtal_cap = nvram_info.xtal_cap;

	if (userconfig_info.xtal_cap.enable)
		*xtal_cap = userconfig_info.xtal_cap;

	AICWFDBG(LOGINFO, "%s:enable       :%d\r\n", __func__, xtal_cap->enable);
	AICWFDBG(LOGINFO, "%s:xtal_cap     :%d\r\n", __func__, xtal_cap->xtal_cap);
	AICWFDBG(LOGINFO, "%s:xtal_cap_fine:%d\r\n", __func__,
		 xtal_cap->xtal_cap_fine);
}

s8 get_txpwr_max(s8 power)
{
	int i = 0;

	for (i = 0; i <= 11; i++) {
		if (power < userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[i])
			power = userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[i];
	}
	for (i = 0; i <= 9; i++) {
		if (power < userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[i])
			power = userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[i];
	}
	for (i = 0; i <= 11; i++) {
		if (power < userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[i])
			power = userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[i];
	}
	for (i = 4; i <= 11; i++) {
		if (power < userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[i])
			power = userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[i];
	}
	for (i = 0; i <= 9; i++) {
		if (power < userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[i])
			power = userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[i];
	}
	for (i = 0; i <= 11; i++) {
		if (power < userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[i])
			power = userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[i];
	}

	if (userconfig_info.txpwr_loss.loss_enable_2g4 == 1 ||
	    userconfig_info.txpwr_loss.loss_enable_5g == 1) {
		if (userconfig_info.txpwr_loss.loss_value_2g4 <
		    userconfig_info.txpwr_loss.loss_value_5g)
			power += userconfig_info.txpwr_loss.loss_value_5g;
		else
			power += userconfig_info.txpwr_loss.loss_value_2g4;
	}

	AICWFDBG(LOGDEBUG, "AICWF %s:txpwr_max:%d \r\n", __func__, power);
	return power;
}

void set_txpwr_loss_ofst(s8 value)
{
	int i = 0;

	for (i = 0; i <= 11; i++)
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[i] += value;

	for (i = 0; i <= 9; i++)
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[i] += value;

	for (i = 0; i <= 11; i++)
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[i] += value;

	for (i = 4; i <= 11; i++)
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[i] += value;

	for (i = 0; i <= 9; i++)
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[i] += value;

	for (i = 0; i <= 11; i++)
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[i] += value;

	AICWFDBG(LOGDEBUG, "AICWF %s:value:%d\r\n", __func__, value);
}

#define MATCH_NODE(type, node, cfg_key) {cfg_key, offsetof(type, node)}

struct parse_match_t {
	char keyname[64];
	int offset;
};

static const char * const parse_key_prefix[] = {
	[0x01] = "module0_",
	[0x21] = "module1_",
};

static const struct parse_match_t parse_match_tab[] = {
	MATCH_NODE(struct nvram_info_t, txpwr_idx.enable, "enable"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.dsss, "dsss"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.ofdmlowrate_2g4, "ofdmlowrate_2g4"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.ofdm64qam_2g4, "ofdm64qam_2g4"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.ofdm256qam_2g4, "ofdm256qam_2g4"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.ofdm1024qam_2g4, "ofdm1024qam_2g4"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.ofdmlowrate_5g, "ofdmlowrate_5g"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.ofdm64qam_5g, "ofdm64qam_5g"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.ofdm256qam_5g, "ofdm256qam_5g"),
	MATCH_NODE(struct nvram_info_t, txpwr_idx.ofdm1024qam_5g, "ofdm1024qam_5g"),

	MATCH_NODE(struct nvram_info_t, txpwr_ofst.enable, "ofst_enable"),
	MATCH_NODE(struct nvram_info_t, txpwr_ofst.chan_1_4, "ofst_chan_1_4"),
	MATCH_NODE(struct nvram_info_t, txpwr_ofst.chan_5_9, "ofst_chan_5_9"),
	MATCH_NODE(struct nvram_info_t, txpwr_ofst.chan_10_13, "ofst_chan_10_13"),
	MATCH_NODE(struct nvram_info_t, txpwr_ofst.chan_36_64, "ofst_chan_36_64"),
	MATCH_NODE(struct nvram_info_t, txpwr_ofst.chan_100_120, "ofst_chan_100_120"),
	MATCH_NODE(struct nvram_info_t, txpwr_ofst.chan_122_140, "ofst_chan_122_140"),
	MATCH_NODE(struct nvram_info_t, txpwr_ofst.chan_142_165, "ofst_chan_142_165"),

	MATCH_NODE(struct nvram_info_t, xtal_cap.enable, "xtal_enable"),
	MATCH_NODE(struct nvram_info_t, xtal_cap.xtal_cap, "xtal_cap"),
	MATCH_NODE(struct nvram_info_t, xtal_cap.xtal_cap_fine, "xtal_cap_fine"),
};

static int parse_key_val(const char *str, const char *key, char *val)
{
	const char *p = NULL;
	const char *dst = NULL;
	int keysize = 0;
	int bufsize = 0;

	if (!str || !key || !val)
		return -1;

	keysize = strlen(key);
	bufsize = strlen(str);
	if (bufsize <= keysize)
		return -1;

	p = str;
	while (*p != 0 && *p == ' ')
		p++;

	if (*p == '#')
		return -1;

	if (str + bufsize - p <= keysize)
		return -1;

	if (strncmp(p, key, keysize) != 0)
		return -1;

	p += keysize;

	while (*p != 0 && *p == ' ')
		p++;

	if (*p != '=')
		return -1;

	p++;
	while (*p != 0 && *p == ' ')
		p++;

	if (*p == '"')
		p++;

	dst = p;
	while (*p != 0)
		p++;

	p--;
	while (*p == ' ')
		p--;

	if (*p == '"')
		p--;

	while (*p == '\r' || *p == '\n')
		p--;

	p++;
	strscpy(val, dst, p - dst);
	val[p - dst] = 0;
	return 0;
}

int rwnx_atoi(char *value)
{
	int len = 0;
	int i = 0;
	int result = 0;
	int flag = 1;

	if (value[0] == '-') {
		flag = -1;
		value++;
	}
	len = strlen(value);

	for (i = 0; i < len; i++) {
		result = result * 10;
		if (value[i] >= 48 && value[i] <= 57) {
			result += value[i] - 48;
		} else {
			result = 0;
			break;
		}
	}

	return result * flag;
}

static int rwnx_parse_mul_value(char *line, char *val[])
{
	int nargs = 0;

	while (nargs < 10) {
		while ((*line == ' ') || (*line == '\t'))
			++line;

		if (*line == '\0') { /* end of line, no more args    */
			//AICWFDBG(LOGDEBUG, "%s p1,%d\n", __func__, nargs);
			return nargs;
		}

		val[nargs++] = line; /* begin of argument string    */

		/* find end of string */
		while (*line && (*line != ' ') && (*line != '\t'))
			++line;

		if (*line == '\0') { /* end of line, no more args    */
			//AICWFDBG(LOGDEBUG, "%s p2,%d\n", __func__, nargs);
			return nargs;
		}

		*line++ = '\0'; /* terminate current arg     */
	}

	AICWFDBG(LOGDEBUG, "nargs: %d\n", nargs);
	//AICWFDBG(LOGDEBUG, "%s exit,%d\n", __func__, nargs);
	return nargs;
}

int get_ccode_region(char *ccode)
{
	int i, cnt;

	AICWFDBG(LOGDEBUG, "%s ccode:%s\r\n", __func__, ccode);
	cnt = ARRAY_SIZE(reg_tables);

	for (i = 0; i < cnt; i++) {
		if (reg_tables[i].ccode[0] == ccode[0] &&
		    reg_tables[i].ccode[1] == ccode[1]) {
			AICWFDBG(LOGDEBUG, "region: %d\r\n", reg_tables[i].region);
			return reg_tables[i].region;
		}
	}
	AICWFDBG(LOGDEBUG, "use default region\r\n");
	return REGIONS_DEFAULT;
}

void rwnx_plat_nvram_set_value(char *command, char *value)
{
	// TODO send command
	AICWFDBG(LOGDEBUG, "%s:command=%s value=%s\n", __func__, command, value);
	if (!strcmp(command, "enable")) {
		userconfig_info.txpwr_lvl.enable = rwnx_atoi(value);
		userconfig_info.txpwr_lvl_v2.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "dsss")) {
		userconfig_info.txpwr_lvl.dsss = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdmlowrate_2g4")) {
		userconfig_info.txpwr_lvl.ofdmlowrate_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm64qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm64qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm256qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm256qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm1024qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm1024qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdmlowrate_5g")) {
		userconfig_info.txpwr_lvl.ofdmlowrate_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm64qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm64qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm256qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm256qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm1024qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm1024qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_1m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_2m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_5m5_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_11m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_6m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_9m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_12m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_18m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_24m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_36m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_48m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[10] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_54m_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11b_11ag_2g4[11] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs0_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs1_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs2_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs3_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs4_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs5_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs6_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs7_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs8_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs9_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11n_11ac_2g4[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs0_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs1_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs2_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs3_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs4_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs5_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs6_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs7_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs8_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs9_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs10_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[10] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs11_2g4")) {
		userconfig_info.txpwr_lvl_v2.pwrlvl_11ax_2g4[11] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_enable")) {
		userconfig_info.txpwr_ofst.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_1_4")) {
		userconfig_info.txpwr_ofst.chan_1_4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_5_9")) {
		userconfig_info.txpwr_ofst.chan_5_9 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_10_13")) {
		userconfig_info.txpwr_ofst.chan_10_13 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_36_64")) {
		userconfig_info.txpwr_ofst.chan_36_64 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_100_120")) {
		userconfig_info.txpwr_ofst.chan_100_120 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_122_140")) {
		userconfig_info.txpwr_ofst.chan_122_140 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_142_165")) {
		userconfig_info.txpwr_ofst.chan_142_165 = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_enable")) {
		userconfig_info.xtal_cap.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_cap")) {
		userconfig_info.xtal_cap.xtal_cap = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_cap_fine")) {
		userconfig_info.xtal_cap.xtal_cap_fine = rwnx_atoi(value);
	} else {
		AICWFDBG(LOGERROR, "invalid cmd: %s\n", command);
	}
}

void rwnx_plat_nvram_set_value_group(char *command, char *value)
{
	char *mul_val[10];
	int i;

	// TODO send command
	AICWFDBG(LOGDEBUG, "%s:command=%s value=%s\n", __func__, command, value);
	if (!strcmp(command, "enable")) {
		userconfig_info.txpwr_lvl.enable = rwnx_atoi(value);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].enable = rwnx_atoi(value);
	} else if (!strcmp(command, "dsss")) {
		userconfig_info.txpwr_lvl.dsss = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdmlowrate_2g4")) {
		userconfig_info.txpwr_lvl.ofdmlowrate_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm64qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm64qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm256qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm256qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm1024qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm1024qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdmlowrate_5g")) {
		userconfig_info.txpwr_lvl.ofdmlowrate_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm64qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm64qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm256qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm256qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm1024qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm1024qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_1m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[0] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_2m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[1] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_5m5_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[2] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_11m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[3] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_6m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[4] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_9m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[5] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_12m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[6] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_18m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[7] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_24m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[8] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_36m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[9] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_48m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[10] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11b_11ag_54m_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11b_11ag_2g4[11] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs0_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[0] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs1_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[1] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs2_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[2] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs3_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[3] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs4_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[4] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs5_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[5] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs6_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[6] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs7_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[7] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs8_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[8] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs9_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_2g4[9] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs0_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[0] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs1_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[1] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs2_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[2] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs3_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[3] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs4_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[4] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs5_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[5] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs6_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[6] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs7_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[7] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs8_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[8] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs9_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[9] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs10_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[10] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs11_2g4")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_2g4[11] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_1m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[0] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_2m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[1] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_5m5_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[2] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_11m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[3] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_6m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[4] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_9m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[5] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_12m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[6] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_18m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[7] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_24m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[8] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_36m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[9] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_48m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[10] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11a_54m_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11a_5g[11] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs0_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[0] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs1_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[1] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs2_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[2] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs3_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[3] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs4_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[4] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs5_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[5] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs6_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[6] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs7_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[7] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs8_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[8] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs9_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11n_11ac_5g[9] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs0_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[0] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs1_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[1] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs2_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[2] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs3_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[3] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs4_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[4] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs5_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[5] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs6_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[6] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs7_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[7] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs8_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[8] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs9_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[9] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs10_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[10] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_11ax_mcs11_5g")) {
		rwnx_parse_mul_value(value, mul_val);
		for (i = 0; i < REGION_NUM_MAX; i++)
			userconfig_info.txpwr_lvl_v3_gp[i].pwrlvl_11ax_5g[11] =
				rwnx_atoi(mul_val[i]);
	} else if (!strcmp(command, "lvl_adj_enable")) {
		userconfig_info.txpwr_lvl_adj.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_2g4_chan_1_4")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_2g4[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_2g4_chan_5_9")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_2g4[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_2g4_chan_10_13")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_2g4[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_42")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_58")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_106")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_122")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_138")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_155")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "loss_enable_2g4")) {
		userconfig_info.txpwr_loss.loss_enable_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "loss_value_2g4")) {
		userconfig_info.txpwr_loss.loss_value_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "loss_enable_5g")) {
		userconfig_info.txpwr_loss.loss_enable_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "loss_value_5g")) {
		userconfig_info.txpwr_loss.loss_value_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_enable")) {
		userconfig_info.txpwr_ofst.enable = rwnx_atoi(value);
		userconfig_info.txpwr_ofst2x.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_1_4")) {
		userconfig_info.txpwr_ofst.chan_1_4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_5_9")) {
		userconfig_info.txpwr_ofst.chan_5_9 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_10_13")) {
		userconfig_info.txpwr_ofst.chan_10_13 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_36_64")) {
		userconfig_info.txpwr_ofst.chan_36_64 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_100_120")) {
		userconfig_info.txpwr_ofst.chan_100_120 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_122_140")) {
		userconfig_info.txpwr_ofst.chan_122_140 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_142_165")) {
		userconfig_info.txpwr_ofst.chan_142_165 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_11b_chan_1_4")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[0][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_11b_chan_5_9")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[0][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_11b_chan_10_13")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[0][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_highrate_chan_1_4")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[1][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_highrate_chan_5_9")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[1][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_highrate_chan_10_13")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[1][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_lowrate_chan_1_4")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[2][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_lowrate_chan_5_9")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[2][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_lowrate_chan_10_13")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[2][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_42")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_58")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_106")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_122")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][3] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_138")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][4] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_155")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][5] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_42")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_58")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_106")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_122")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][3] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_138")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][4] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_155")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][5] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_42")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_58")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_106")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_122")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][3] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_138")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][4] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_155")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][5] = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_enable")) {
		userconfig_info.xtal_cap.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_cap")) {
		userconfig_info.xtal_cap.xtal_cap = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_cap_fine")) {
		userconfig_info.xtal_cap.xtal_cap_fine = rwnx_atoi(value);
	} else {
		AICWFDBG(LOGERROR, "invalid cmd: %s\n", command);
	}
}

void rwnx_plat_nvram_set_value_v3(char *command, char *value)
{
	// TODO send command
	AICWFDBG(LOGDEBUG, "%s:command=%s value=%s\n", __func__, command, value);
	//AICWFDBG(LOGDEBUG, "AICWF command=%s value=%s\n", command, value);
	if (!strcmp(command, "enable")) {
		userconfig_info.txpwr_lvl.enable = rwnx_atoi(value);
		userconfig_info.txpwr_lvl_v3.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "dsss")) {
		userconfig_info.txpwr_lvl.dsss = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdmlowrate_2g4")) {
		userconfig_info.txpwr_lvl.ofdmlowrate_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm64qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm64qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm256qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm256qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm1024qam_2g4")) {
		userconfig_info.txpwr_lvl.ofdm1024qam_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdmlowrate_5g")) {
		userconfig_info.txpwr_lvl.ofdmlowrate_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm64qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm64qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm256qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm256qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofdm1024qam_5g")) {
		userconfig_info.txpwr_lvl.ofdm1024qam_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_1m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_2m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_5m5_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_11m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_6m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_9m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_12m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_18m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_24m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_36m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_48m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[10] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11b_11ag_54m_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11b_11ag_2g4[11] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs0_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs1_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs2_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs3_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs4_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs5_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs6_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs7_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs8_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs9_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_2g4[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs0_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs1_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs2_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs3_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs4_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs5_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs6_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs7_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs8_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs9_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs10_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[10] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs11_2g4")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[11] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_1m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_2m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_5m5_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_11m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_6m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_9m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_12m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_18m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_24m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_36m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_48m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[10] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11a_54m_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[11] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs0_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs1_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs2_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs3_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs4_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs5_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs6_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs7_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs8_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11n_11ac_mcs9_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11n_11ac_5g[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs0_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs1_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs2_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs3_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs4_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs5_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs6_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[6] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs7_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[7] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs8_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[8] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs9_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[9] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs10_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[10] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_11ax_mcs11_5g")) {
		userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_5g[11] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_enable")) {
		userconfig_info.txpwr_lvl_adj.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_2g4_chan_1_4")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_2g4[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_2g4_chan_5_9")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_2g4[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_2g4_chan_10_13")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_2g4[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_42")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[0] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_58")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[1] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_106")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[2] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_122")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[3] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_138")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[4] = rwnx_atoi(value);
	} else if (!strcmp(command, "lvl_adj_5g_chan_155")) {
		userconfig_info.txpwr_lvl_adj.pwrlvl_adj_tbl_5g[5] = rwnx_atoi(value);
	} else if (!strcmp(command, "loss_enable_2g4")) {
		userconfig_info.txpwr_loss.loss_enable_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "loss_value_2g4")) {
		userconfig_info.txpwr_loss.loss_value_2g4 = rwnx_atoi(value);
	} else if (!strcmp(command, "loss_enable_5g")) {
		userconfig_info.txpwr_loss.loss_enable_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "loss_value_5g")) {
		userconfig_info.txpwr_loss.loss_value_5g = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_enable")) {
		userconfig_info.txpwr_ofst.enable = rwnx_atoi(value);
		userconfig_info.txpwr_ofst2x.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_1_4")) {
		userconfig_info.txpwr_ofst.chan_1_4 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_5_9")) {
		userconfig_info.txpwr_ofst.chan_5_9 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_10_13")) {
		userconfig_info.txpwr_ofst.chan_10_13 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_36_64")) {
		userconfig_info.txpwr_ofst.chan_36_64 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_100_120")) {
		userconfig_info.txpwr_ofst.chan_100_120 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_122_140")) {
		userconfig_info.txpwr_ofst.chan_122_140 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_chan_142_165")) {
		userconfig_info.txpwr_ofst.chan_142_165 = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_11b_chan_1_4")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[0][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_11b_chan_5_9")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[0][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_11b_chan_10_13")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[0][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_highrate_chan_1_4")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[1][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_highrate_chan_5_9")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[1][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_highrate_chan_10_13")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[1][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_lowrate_chan_1_4")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[2][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_lowrate_chan_5_9")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[2][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_2g4_ofdm_lowrate_chan_10_13")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_2g4[2][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_42")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_58")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_106")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_122")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][3] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_138")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][4] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_lowrate_chan_155")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[0][5] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_42")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_58")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_106")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_122")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][3] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_138")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][4] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_highrate_chan_155")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[1][5] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_42")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][0] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_58")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][1] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_106")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][2] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_122")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][3] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_138")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][4] = rwnx_atoi(value);
	} else if (!strcmp(command, "ofst_5g_ofdm_midrate_chan_155")) {
		userconfig_info.txpwr_ofst2x.pwrofst2x_tbl_5g[2][5] = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_enable")) {
		userconfig_info.xtal_cap.enable = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_cap")) {
		userconfig_info.xtal_cap.xtal_cap = rwnx_atoi(value);
	} else if (!strcmp(command, "xtal_cap_fine")) {
		userconfig_info.xtal_cap.xtal_cap_fine = rwnx_atoi(value);
	} else {
		AICWFDBG(LOGERROR, "invalid cmd: %s\n", command);
	}
}

void rwnx_plat_userconfig_parsing2(char *buffer, int size)
{
	int i = 0;
	int parse_state = 0;
	char command[30];
	char value[100];
	int char_counter = 0;

	memset(command, 0, 30);
	memset(value, 0, 100);

	for (i = 0; i < size; i++) {
		// Send command or print nvram log when char is \r or \n
		if (buffer[i] == 0x0a || buffer[i] == 0x0d) {
			if (command[0] != 0 && value[0] != 0) {
				if (parse_state == PRINT)
					AICWFDBG(LOGINFO, "%s:%s\r\n", __func__, value);
				else if (parse_state == GET_VALUE)
					rwnx_plat_nvram_set_value(command, value);
			}
			// Reset command value and char_counter
			memset(command, 0, 30);
			memset(value, 0, 100);
			char_counter = 0;
			parse_state = INIT;
			continue;
		}

		// Switch parser state
		if (parse_state == INIT) {
			if (buffer[i] == '#') {
				parse_state = PRINT;
				continue;
			} else if (buffer[i] == 0x0a || buffer[i] == 0x0d) {
				parse_state = INIT;
				continue;
			} else {
				parse_state = CMD;
			}
		}

		// Fill data to command and value
		if (parse_state == PRINT) {
			command[0] = 0x01;
			value[char_counter] = buffer[i];
			char_counter++;
		} else if (parse_state == CMD) {
			if (command[0] != 0 && buffer[i] == '=') {
				parse_state = GET_VALUE;
				char_counter = 0;
				continue;
			}
			command[char_counter] = buffer[i];
			char_counter++;
		} else if (parse_state == GET_VALUE) {
			value[char_counter] = buffer[i];
			char_counter++;
		}
	}
}

void rwnx_plat_userconfig_parsing3(char *buffer, int size)
{
	int i = 0;
	int parse_state = 0;
	char command[64];
	char value[100];
	int char_counter = 0;
	u8 area_pw = 0;

	memset(command, 0, 64);
	memset(value, 0, 100);

	for (i = 0; i < size; i++) {
		// Send command or print nvram log when char is \r or \n
		if (buffer[i] == 0x0a || buffer[i] == 0x0d) {
			if (command[0] != 0 && value[0] != 0) {
				if (parse_state == PRINT) {
					if (area_pw == 0 &&
					    strnstr(value, "SRRC", strlen(value))) {
						area_pw = 1;
						AICWFDBG(LOGINFO,
							 "use diff_area_power userconfig\n");
					}
					AICWFDBG(LOGINFO, "%s:%s\r\n", __func__, value);
				} else if (parse_state == GET_VALUE) {
					if (area_pw)
						rwnx_plat_nvram_set_value_group(command, value);
					else
						rwnx_plat_nvram_set_value_v3(command, value);
				}
			}
			// Reset command value and char_counter
			memset(command, 0, 64);
			memset(value, 0, 100);
			char_counter = 0;
			parse_state = INIT;
			continue;
		}

		// Switch parser state
		if (parse_state == INIT) {
			if (buffer[i] == '#') {
				parse_state = PRINT;
				continue;
			} else if (buffer[i] == 0x0a || buffer[i] == 0x0d) {
				parse_state = INIT;
				continue;
			} else {
				parse_state = CMD;
			}
		}

		// Fill data to command and value
		if (parse_state == PRINT) {
			command[0] = 0x01;
			value[char_counter] = buffer[i];
			char_counter++;
		} else if (parse_state == CMD) {
			if (command[0] != 0 && buffer[i] == '=') {
				parse_state = GET_VALUE;
				char_counter = 0;
				continue;
			}
			command[char_counter] = buffer[i];
			char_counter++;
		} else if (parse_state == GET_VALUE) {
			if (buffer[i] != 0x2D &&
			    (buffer[i] < 0x30 || buffer[i] > 0x39) &&
			    buffer[i] != 0x20)
				continue;
			value[char_counter] = buffer[i];
			char_counter++;
		}
	}
}

void rwnx_plat_userconfig_parsing(struct rwnx_hw *rwnx_hw, char *buffer,
				  int size)
{
	char conf[100], keyname[64];
	char *line;
	char *data;
	int i = 0, len = 0;
	long val;

	if (size <= 0) {
		pr_err("Config buffer size %d error\n", size);
		return;
	}

	AICWFDBG(LOGDEBUG, "AICWF %s rwnx_hw->vendor_info:0x%02X \r\n", __func__,
		 rwnx_hw->vendor_info);
	if (rwnx_hw->vendor_info == 0x00 ||
	    rwnx_hw->vendor_info > (ARRAY_SIZE(parse_key_prefix) - 1)) {
		AICWFDBG(LOGDEBUG, "AICWF Unsuppor vendor info config\n");
		AICWFDBG(LOGDEBUG, "AICWF Using module0 config\n");
		rwnx_hw->vendor_info = 0x01;
		// return;
	}

	data = vmalloc(size + 1);
	if (!data) {
		AICWFDBG(LOGERROR, "%s vmalloc fail\n", __func__);
		return;
	}

	memcpy(data, buffer, size);
	buffer = data;

	while (1) {
		line = buffer;
		if (*line == 0)
			break;

		while (*buffer != '\r' && *buffer != '\n' &&
		       *buffer != 0 && len++ < size)
			buffer++;

		while ((*buffer == '\r' || *buffer == '\n') && len++ < size)
			*buffer++ = 0;

		if (len >= size)
			*buffer = 0;

		// store value to data struct
		for (i = 0; i < ARRAY_SIZE(parse_match_tab); i++) {
			scnprintf(keyname, sizeof(keyname), "%s%s",
				  parse_key_prefix[rwnx_hw->vendor_info],
				  parse_match_tab[i].keyname);
			if (parse_key_val(line, keyname, conf) == 0) {
				if (kstrtol(conf, 0, &val)) {
					pr_warn("Invalid value for %s\n", keyname);
					continue;
				}
				*(unsigned long *)((unsigned long)&nvram_info +
								parse_match_tab[i].offset) = val;
				AICWFDBG(LOGDEBUG, "AICWF %s, %s = %ld\n", __func__,
					 parse_match_tab[i].keyname, val);
				break;
			}
		}
	}
	vfree(data);
}

int rwnx_plat_userconfig_upload(struct rwnx_hw *rwnx_hw,
				const char *filename)
{
	int size;
	u32 *dst = NULL;

	AICWFDBG(LOGDEBUG, "AICWF userconfig file path:%s \r\n", filename);

	/* load aic firmware */
	size = rwnx_load_firmware(&dst, filename, rwnx_hw->dev);
	if (size <= 0) {
		AICWFDBG(LOGDEBUG, "AICWF wrong size of firmware file\n");
		vfree(dst);
		dst = NULL;
		return 0;
	}

	/* Copy the file on the Embedded side */
	AICWFDBG(LOGDEBUG, "AICWF ### Upload %s userconfig, size=%d\n", filename, size);

	rwnx_plat_userconfig_parsing(rwnx_hw, (char *)dst, size);

	if (dst) {
		vfree(dst);
		dst = NULL;
	}

	AICWFDBG(LOGDEBUG, "AICWF userconfig download complete\n\n");

	return 0;
}

#ifdef CONFIG_AIC8800_POWER_LIMIT
static inline char *get_line_from_buffer(char **buffer)
{
	return strsep(buffer, "\n");
}

int is_all_space_or_tab(u8 *data, u8 size)
{
	u8 cnt = 0;
	u8 num_of_space_and_tab = 0;

	while (size > cnt) {
		if (data[cnt] == ' ' || data[cnt] == '\t' || data[cnt] == '\0')
			++num_of_space_and_tab;
		++cnt;
	}
	return size == num_of_space_and_tab;
}

int is_comment_string(char *sz_str)
{
	if (*sz_str == '#' && *(sz_str + 1) == ' ')
		return 1;
	else
		return 0;
}

int is_enable_limit(char *szs_str)
{
	int i = 0, value = 0;
	char en[15];

	if (*szs_str == '#' && *(szs_str + 1) == '*') {
		i = 2;
		while (szs_str[i] != '=' && (i < 15)) {
			en[i - 2] = szs_str[i];
			i++;
		}
		en[i - 2] = '\0';
		i += 1;
		if (strncmp(en, "enable", 6) == 0) {
			value = rwnx_atoi(szs_str + i);
			if (value == 1)
				return 1;
			else
				return 0;
		}
	}
	return -1;
}

int parse_qualified_string(char *in, u32 *start, char *out, char left_qualifier,
			   char right_qualifier)
{
	u32 i = 0, j = 0;
	char c = in[(*start)++];

	if (c != left_qualifier)
		return 0;
	i = (*start);
	c = in[(*start)++];
	while (c != right_qualifier && c != '\0')
		c = in[(*start)++];
	if (c == '\0')
		return 0;
	j = (*start) - 2;
	strscpy((char *)out, (const char *)(in + i), j - i + 1);
	return 1;
}

int get_u1_byte_integer_from_string_in_decimal(char *str, u8 *p_int)
{
	u16 i = 0;
	*p_int = 0;
	while (str[i] != '\0') {
		if (str[i] >= '0' && str[i] <= '9') {
			*p_int *= 10;
			*p_int += (str[i] - '0');
		} else {
			return 0;
		}
		++i;
	}
	return 1;
}

int get_s1_byte_integer_from_string_in_decimal(char *str, s8 *val)
{
	u8 negative = 0;
	u16 i = 0;
	*val = 0;
	while (str[i] != '\0') {
		if (i == 0 && (str[i] == '+' || str[i] == '-')) {
			if (str[i] == '-')
				negative = 1;
		} else if (str[i] >= '0' && str[i] <= '9') {
			*val *= 10;
			*val += (str[i] - '0');
		} else {
			return 0;
		}
		++i;
	}
	if (negative)
		*val = -*val;
	return 1;
}

static void update_power_limit(u8 band_cc, const char *channel, u8 channel_num,
			       const char *power_limit, u8 power_limit_val,
			       struct powerlimit_info_t *powerlimit_info)
{
	if (band_cc == PHY_BAND_2G4) {
		u8 cur_idx = powerlimit_info->txpwr_lmt.ch_cnt_2g4;

		if (cur_idx == 0)
			AICWFDBG(LOGDEBUG, "[%d]: ch=%s, pwr=%s\n", cur_idx, channel, power_limit);
		if (cur_idx < MAC_DOMAINCHANNEL_24G_MAX) {
			powerlimit_info->txpwr_lmt.ch_num_2g4[cur_idx] = channel_num;
			powerlimit_info->txpwr_lmt.max_pwr_2g4[cur_idx] = power_limit_val;
			powerlimit_info->txpwr_lmt.ch_cnt_2g4++;
		} else {
			AICWFDBG(LOGERROR, "band %d chan_cnt reached %d\n",
				 band_cc, MAC_DOMAINCHANNEL_24G_MAX);
			AICWFDBG(LOGERROR, "channel=%s(%d) powerLimit=%s(%d)\n",
				 channel, channel_num, power_limit, power_limit_val);
		}
	} else if (band_cc == PHY_BAND_5G) {
		u8 cur_idx = powerlimit_info->txpwr_lmt.ch_cnt_5g;

		if (cur_idx == 0)
			AICWFDBG(LOGDEBUG, "[%d]: ch=%s, pwr=%s\n", cur_idx, channel, power_limit);
		if (cur_idx < MAC_DOMAINCHANNEL_5G_MAX) {
			powerlimit_info->txpwr_lmt.ch_num_5g[cur_idx] = channel_num;
			powerlimit_info->txpwr_lmt.max_pwr_5g[cur_idx] = power_limit_val;
			powerlimit_info->txpwr_lmt.ch_cnt_5g++;
		} else {
			AICWFDBG(LOGERROR, "band %d chan_cnt reached %d\n",
				 band_cc, MAC_DOMAINCHANNEL_5G_MAX);
			AICWFDBG(LOGERROR, "channel=%s(%d) powerLimit=%s(%d)\n",
				 channel, channel_num, power_limit, power_limit_val);
		}
	}
}

void rwnx_plat_powerlimit_parsing(char *buffer, int size, char *cc)
{
#define LD_STAGE_EXC_MAPPING   0
#define LD_STAGE_TAB_DEFINE    1
#define LD_STAGE_TAB_START     2
#define LD_STAGE_COLUMN_DEFINE 3
#define LD_STAGE_CH_ROW        4

	u8 loading_stage = LD_STAGE_EXC_MAPPING;
	u32 i = 0, for_cnt = 0;
	u32 i_cc;
	char *sz_line, *ptmp;
	char band[10], col_num_buf[10];
	u8 col_num = 0, col_num_cc = 255, band_cc = 0;
	bool sp_cc = false;
	// clear powerlimit info at first
	memset((void *)&powerlimit_info, 0, sizeof(struct powerlimit_info_t));
	ptmp = buffer;
	for (sz_line = get_line_from_buffer(&ptmp); sz_line;
	     sz_line = get_line_from_buffer(&ptmp)) {
		if (is_all_space_or_tab(sz_line, sizeof(*sz_line)))
			continue;

		if (is_enable_limit(sz_line) == 0)
			return;

		if (is_comment_string(sz_line))
			continue;

		if (loading_stage == LD_STAGE_EXC_MAPPING) {
			if (sz_line[0] == '#' || sz_line[1] == '#')
				loading_stage = LD_STAGE_TAB_DEFINE;
			else
				continue;
		}

		if (loading_stage == LD_STAGE_TAB_DEFINE) {
			/* read "##	2.4G" */
			if (sz_line[0] != '#' || sz_line[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (sz_line[i] == ' ' || sz_line[i] == '\t')
				++i;

			sz_line[--i] =
				' '; /* return the space in front of the regulation info */

			/* Parse the label of the table */
			memset((void *)band, 0, 10);
			if (!parse_qualified_string(sz_line, &i, band, ' ', ',')) {
				AICWFDBG(LOGERROR, "Fail to parse band!\n");
				goto exit;
			}
			if (strncmp(band, "2.4G", 4) == 0)
				band_cc = PHY_BAND_2G4;
			else if (strncmp(band, "5G", 2) == 0)
				band_cc = PHY_BAND_5G;

			memset((void *)col_num_buf, 0, 10);
			if (!parse_qualified_string(sz_line, &i, col_num_buf, '#', '#')) {
				AICWFDBG(LOGERROR, "Fail to parse column number!\n");
				goto exit;
			}
			if (!get_u1_byte_integer_from_string_in_decimal(col_num_buf, &col_num)) {
				AICWFDBG(LOGERROR,
					 "Column number \"%s\" is not unsigned decimal\n",
					 col_num_buf);
				goto exit;
			}
			if (col_num == 0) {
				AICWFDBG(LOGERROR, "Column number is 0\n");
				goto exit;
			}

			AICWFDBG(LOGDEBUG, "band=%s(%d)\n", band, band_cc);
			loading_stage = LD_STAGE_TAB_START;
		} else if (loading_stage == LD_STAGE_TAB_START) {
			/* read "##	START" */
			if (sz_line[0] != '#' || sz_line[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (sz_line[i] == ' ' || sz_line[i] == '\t')
				++i;

			if (strncmp((u8 *)(sz_line + i), "START", 5)) {
				AICWFDBG(LOGERROR, "Missing \"##   START\" label\n");
				goto exit;
			}

			loading_stage = LD_STAGE_COLUMN_DEFINE;
		} else if (loading_stage == LD_STAGE_COLUMN_DEFINE) {
			/* read "##	CN	US" */
			if (sz_line[0] != '#' || sz_line[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (sz_line[i] == ' ' || sz_line[i] == '\t')
				++i;

			for (for_cnt = 0; for_cnt < col_num; for_cnt++) {
				/* skip the space */
				while (sz_line[i] == ' ' || sz_line[i] == '\t')
					i++;
				i_cc = i;

				while (sz_line[i] != ' ' && sz_line[i] != '\t' &&
				       sz_line[i] != '\0')
					i++;

				if ((i - i_cc) != 2) {
					AICWFDBG(LOGERROR, "CC len err\n");
					goto exit;
				} else if ((sz_line[i_cc] == cc[0]) &&
						   (sz_line[i_cc + 1] == cc[1])) {
					AICWFDBG(LOGDEBUG, "CC matched: %s, col=%d\n", cc, for_cnt);
					col_num_cc = for_cnt;
					powerlimit_info.flags |= POWER_LIMIT_CC_MATCHED_BIT;
					sp_cc = true;
					break;
				}
			}
			if (!sp_cc) {
				col_num_cc = col_num - 1;
				AICWFDBG(LOGDEBUG, "use 00: %s, col_num_cc=%d\n", cc, col_num_cc);
				powerlimit_info.flags |= POWER_LIMIT_CC_MATCHED_BIT;
			}

			loading_stage = LD_STAGE_CH_ROW;
		} else if (loading_stage == LD_STAGE_CH_ROW) {
			char channel[10] = {0}, power_limit[10] = {0};
			u8 channel_num, power_limit_val, cnt = 0;

			/* the table ends */
			if (sz_line[0] == '#' && sz_line[1] == '#') {
				i = 2;
				while (sz_line[i] == ' ' || sz_line[i] == '\t')
					++i;

				if (strncmp((u8 *)(sz_line + i), "END", 3) == 0) {
					loading_stage = LD_STAGE_TAB_DEFINE;
					col_num = 0;
					continue;
				} else {
					AICWFDBG(LOGERROR, "Missing \"##   END\" label\n");
					goto exit;
				}
			}

			if ((sz_line[0] != 'c' && sz_line[0] != 'C') ||
			    (sz_line[1] != 'h' && sz_line[1] != 'H')) {
				AICWFDBG(LOGERROR, "Wrong channel prefix: '%c','%c'(%d,%d)\n",
					 sz_line[0], sz_line[1], sz_line[0], sz_line[1]);
				continue;
			}
			i = 2; /* move to the  location behind 'h' */

			/* load the channel number */
			cnt = 0;
			while (sz_line[i] >= '0' && sz_line[i] <= '9') {
				channel[cnt] = sz_line[i];
				++cnt;
				++i;
			}

			for (for_cnt = 0; for_cnt < col_num; ++for_cnt) {
				/* skip the space between channel number and the power limit
				 * value
				 */
				while (sz_line[i] == ' ' || sz_line[i] == '\t')
					++i;

				/* load the power limit value */
				memset((void *)power_limit, 0, 10);

				if (sz_line[i] == 'N' && sz_line[i + 1] == 'A') {
					/*
					 * means channel not available
					 */
					sprintf(power_limit, "%d", POWER_LIMIT_INVALID_VAL);
					i += 2;
				} else if ((sz_line[i] >= '0' && sz_line[i] <= '9') ||
						   sz_line[i] == '+' || sz_line[i] == '-') {
					/* case of dBm value */
					cnt = 0;
					while ((sz_line[i] >= '0' && sz_line[i] <= '9') ||
					       sz_line[i] == '+' || sz_line[i] == '-') {
						power_limit[cnt] = sz_line[i];
						++cnt;
						++i;
					}
				} else {
					AICWFDBG
					(LOGERROR, "Wrong limit expression \"%c%c\"(%d, %d)\n",
					 sz_line[i], sz_line[i + 1], sz_line[i], sz_line[i + 1]);
					goto exit;
				}

				if (for_cnt == col_num_cc) {
					/* store the power limit value */
					if (get_u1_byte_integer_from_string_in_decimal
						((char *)channel, &channel_num) == 0 ||
					    get_s1_byte_integer_from_string_in_decimal
						((char *)power_limit, &power_limit_val) == 0) {
						AICWFDBG(LOGERROR,
							 "Illegal index of power limit table [ch %s][val %s]\n",
							 channel, power_limit);
						goto exit;
					}

					update_power_limit(band_cc, channel, channel_num,
							   power_limit, power_limit_val,
							   &powerlimit_info);
					break;
				}
			}
		}
	}
exit:
	return;
}

/// 5G lower bound freq
#define PHY_FREQ_5G 5000

u16 phy_channel_to_freq(u8 band, int channel)
{
	if (band == PHY_BAND_2G4 && channel >= 1 && channel <= 14) {
		if (channel == 14)
			return 2484;
		else
			return 2407 + channel * 5;
	} else if (band == PHY_BAND_5G && channel >= 1 && channel <= 165) {
		return PHY_FREQ_5G + channel * 5;
	}
	return 0;
}

s8 get_powerlimit_by_freq(u8 band, u16 freq, u8 *enable)
{
	s8 ret = POWER_LIMIT_INVALID_VAL;
	u8 idx;

	if ((powerlimit_info.flags & POWER_LIMIT_CC_MATCHED_BIT) == 0) {
		*enable = 0;
	} else if (powerlimit_info.flags & POWER_LIMIT_CC_MATCHED_BIT) {
		*enable = 1;

		if (band == PHY_BAND_2G4) {
			u8 idx_cnt = powerlimit_info.txpwr_lmt.ch_cnt_2g4;

			for (idx = 0; idx < idx_cnt; idx++) {
				int ch_num = powerlimit_info.txpwr_lmt.ch_num_2g4[idx];
				u16 freq_tmp = phy_channel_to_freq(PHY_BAND_2G4, ch_num);

				if (freq == freq_tmp) {
					ret = powerlimit_info.txpwr_lmt.max_pwr_2g4[idx];
					// AICWFDBG(LOGINFO, "[%d]: ch=%d(freq=%d), pwr=%d\n", idx,
					// ch_num, freq, ret);
					break;
				}
			}
			if (idx == idx_cnt) {
				AICWFDBG(LOGERROR,
					 "powerlimit search failed: band=%d freq=%d\n", band,
					 freq);
			}
		} else if (band == PHY_BAND_5G) {
			u8 idx_cnt = powerlimit_info.txpwr_lmt.ch_cnt_5g;

			for (idx = 0; idx < idx_cnt; idx++) {
				int ch_num = powerlimit_info.txpwr_lmt.ch_num_5g[idx];
				u16 freq_tmp = phy_channel_to_freq(PHY_BAND_5G, ch_num);

				if (freq == freq_tmp) {
					ret = powerlimit_info.txpwr_lmt.max_pwr_5g[idx];
					// AICWFDBG(LOGINFO, "[%d]: ch=%d(freq=%d), pwr=%d\n", idx,
					// ch_num, freq, ret);
					break;
				}
			}
			if (idx == idx_cnt) {
				AICWFDBG(LOGERROR,
					 "powerlimit search failed: band=%d freq=%d\n", band,
					 freq);
			}
		}
	}
	return ret;
}

s8 get_powerlimit_by_chnum(u8 chnum, u8 *enable)
{
	s8 ret = POWER_LIMIT_INVALID_VAL;
	u8 idx;

	if ((powerlimit_info.flags & POWER_LIMIT_CC_MATCHED_BIT) == 0) {
		*enable = 0;
	} else if (powerlimit_info.flags & POWER_LIMIT_CC_MATCHED_BIT) {
		*enable = 1;

		if (chnum <= 14) {
			u8 idx_cnt = powerlimit_info.txpwr_lmt.ch_cnt_2g4;

			for (idx = 0; idx < idx_cnt; idx++) {
				u8 ch_num = powerlimit_info.txpwr_lmt.ch_num_2g4[idx];

				if (chnum == ch_num) {
					ret = powerlimit_info.txpwr_lmt.max_pwr_2g4[idx];
					// AICWFDBG(LOGINFO, "[%d]: ch=%d, pwr=%d\n", idx, ch_num,
					// ret);
					break;
				}
			}
			if (idx == idx_cnt) {
				AICWFDBG(LOGERROR, "powerlimit search failed: chnum=%d\n",
					 chnum);
			}
		} else if (chnum <= 165) {
			u8 idx_cnt = powerlimit_info.txpwr_lmt.ch_cnt_5g;

			for (idx = 0; idx < idx_cnt; idx++) {
				int ch_num = powerlimit_info.txpwr_lmt.ch_num_5g[idx];

				if (chnum == ch_num) {
					ret = powerlimit_info.txpwr_lmt.max_pwr_5g[idx];
					// AICWFDBG(LOGINFO, "[%d]: ch=%d, pwr=%d\n", idx, ch_num,
					// ret);
					break;
				}
			}
			if (idx == idx_cnt) {
				AICWFDBG(LOGERROR, "powerlimit search failed: chnum=%d\n",
					 chnum);
			}
		}
	}
	return ret;
}
#endif

/**
 * rwnx_platform_reset - Reset the platform
 *
 * @rwnx_plat: platform data
 *
 * Return: 0 on success, or %-EIO if the platform did not reset.
 */
static int rwnx_platform_reset(struct rwnx_plat *rwnx_plat)
{
	u32 regval;

#if defined(AICWF_SDIO_SUPPORT)
	return 0;
#endif

	/*
	 * the doc states that SOFT implies FPGA_B_RESET
	 * adding FPGA_B_RESET is clearer
	 */
	RWNX_REG_WRITE(SOFT_RESET | FPGA_B_RESET, rwnx_plat, RWNX_ADDR_SYSTEM,
		       SYSCTRL_MISC_CNTL_ADDR);
	msleep(100);

	regval = RWNX_REG_READ(rwnx_plat, RWNX_ADDR_SYSTEM, SYSCTRL_MISC_CNTL_ADDR);

	if (regval & SOFT_RESET) {
		dev_err(rwnx_platform_get_dev(rwnx_plat), "reset: failed\n");
		return -EIO;
	}

	RWNX_REG_WRITE(regval & ~FPGA_B_RESET, rwnx_plat, RWNX_ADDR_SYSTEM,
		       SYSCTRL_MISC_CNTL_ADDR);
	msleep(100);
	return 0;
}

/*
 * rwmx_platform_save_config() - Save hardware config before reload
 *
 * @rwnx_plat: Pointer to platform data
 *
 * Return configuration registers values.
 */
static void *rwnx_term_save_config(struct rwnx_plat *rwnx_plat)
{
	const u32 *reg_list;
	u32 *reg_value, *res;
	int i, size = 0;

	if (rwnx_plat->get_config_reg)
		size = rwnx_plat->get_config_reg(rwnx_plat, &reg_list);

	if (size <= 0)
		return NULL;

	// res = kmalloc(sizeof(u32) * size, GFP_KERNEL);
	res = kmalloc_array(size, sizeof(u32), GFP_KERNEL);
	if (!res)
		return NULL;

	reg_value = res;
	for (i = 0; i < size; i++)
		*reg_value++ = RWNX_REG_READ(rwnx_plat, RWNX_ADDR_SYSTEM, *reg_list++);

	return res;
}

void get_userconfig_txpwr_lvl_in_fdrv(struct txpwr_lvl_conf *txpwr_lvl)
{
	txpwr_lvl->enable = userconfig_info.txpwr_lvl.enable;
	txpwr_lvl->dsss = userconfig_info.txpwr_lvl.dsss;
	txpwr_lvl->ofdmlowrate_2g4 = userconfig_info.txpwr_lvl.ofdmlowrate_2g4;
	txpwr_lvl->ofdm64qam_2g4 = userconfig_info.txpwr_lvl.ofdm64qam_2g4;
	txpwr_lvl->ofdm256qam_2g4 = userconfig_info.txpwr_lvl.ofdm256qam_2g4;
	txpwr_lvl->ofdm1024qam_2g4 = userconfig_info.txpwr_lvl.ofdm1024qam_2g4;
	txpwr_lvl->ofdmlowrate_5g = userconfig_info.txpwr_lvl.ofdmlowrate_5g;
	txpwr_lvl->ofdm64qam_5g = userconfig_info.txpwr_lvl.ofdm64qam_5g;
	txpwr_lvl->ofdm256qam_5g = userconfig_info.txpwr_lvl.ofdm256qam_5g;
	txpwr_lvl->ofdm1024qam_5g = userconfig_info.txpwr_lvl.ofdm1024qam_5g;

	AICWFDBG(LOGDEBUG, "%s:enable:%d\r\n", __func__, txpwr_lvl->enable);
	AICWFDBG(LOGDEBUG, "%s:dsss:%d\r\n", __func__, txpwr_lvl->dsss);
	AICWFDBG(LOGDEBUG, "%s:ofdmlowrate_2g4:%d\r\n", __func__,
		 txpwr_lvl->ofdmlowrate_2g4);
	AICWFDBG(LOGDEBUG, "%s:ofdm64qam_2g4:%d\r\n", __func__,
		 txpwr_lvl->ofdm64qam_2g4);
	AICWFDBG(LOGDEBUG, "%s:ofdm256qam_2g4:%d\r\n", __func__,
		 txpwr_lvl->ofdm256qam_2g4);
	AICWFDBG(LOGDEBUG, "%s:ofdm1024qam_2g4:%d\r\n", __func__,
		 txpwr_lvl->ofdm1024qam_2g4);
	AICWFDBG(LOGDEBUG, "%s:ofdmlowrate_5g:%d\r\n", __func__,
		 txpwr_lvl->ofdmlowrate_5g);
	AICWFDBG(LOGDEBUG, "%s:ofdm64qam_5g:%d\r\n", __func__,
		 txpwr_lvl->ofdm64qam_5g);
	AICWFDBG(LOGDEBUG, "%s:ofdm256qam_5g:%d\r\n", __func__,
		 txpwr_lvl->ofdm256qam_5g);
	AICWFDBG(LOGDEBUG, "%s:ofdm1024qam_5g:%d\r\n", __func__,
		 txpwr_lvl->ofdm1024qam_5g);
}

void get_userconfig_txpwr_lvl_v2_in_fdrv(struct txpwr_lvl_conf_v2 *txpwr_lvl_v2)
{
	*txpwr_lvl_v2 = userconfig_info.txpwr_lvl_v2;

	AICWFDBG(LOGDEBUG, "%s:enable:%d\r\n", __func__, txpwr_lvl_v2->enable);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_1m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[0]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_2m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[1]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_5m5_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[2]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_11m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[3]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_6m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[4]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_9m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[5]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_12m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[6]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_18m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[7]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_24m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[8]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_36m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[9]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_48m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[10]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11b_11ag_54m_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[11]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs0_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[0]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs1_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[1]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs2_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[2]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs3_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[3]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs4_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[4]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs5_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[5]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs6_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[6]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs7_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[7]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs8_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[8]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11n_11ac_mcs9_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[9]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs0_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[0]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs1_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[1]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs2_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[2]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs3_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[3]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs4_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[4]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs5_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[5]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs6_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[6]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs7_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[7]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs8_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[8]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs9_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[9]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs10_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[10]);
	AICWFDBG(LOGDEBUG, "%s:lvl_11ax_mcs11_2g4:%d\r\n", __func__,
		 txpwr_lvl_v2->pwrlvl_11ax_2g4[11]);
}

void get_userconfig_txpwr_lvl_group_in_fdrv(struct txpwr_lvl_conf_v3 *txpwr_lvl_v3, int index_reg)
{
	*txpwr_lvl_v3 = userconfig_info.txpwr_lvl_v3_gp[index_reg];

	AICWFDBG(LOGDEBUG, "%s:enable:%d\r\n", __func__, txpwr_lvl_v3->enable);
	AICWFDBG(LOGDEBUG, "lvl_11b_11ag_1m_2g4:%d\r\n", txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[0]);
}

void get_userconfig_txpwr_lvl_v3_in_fdrv(struct txpwr_lvl_conf_v3 *txpwr_lvl_v3)
{
	*txpwr_lvl_v3 = userconfig_info.txpwr_lvl_v3;

	AICWFDBG(LOGDEBUG, "%s:enable:%d\r\n", __func__, txpwr_lvl_v3->enable);
	AICWFDBG(LOGDEBUG, "lvl_11b_11ag_1m_2g4:%d\r\n", txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[0]);
}

void get_userconfig_txpwr_lvl_adj_in_fdrv(struct txpwr_lvl_adj_conf *txpwr_lvl_adj)
{
	*txpwr_lvl_adj = userconfig_info.txpwr_lvl_adj;

	AICWFDBG(LOGDEBUG, "%s:enable:%d\r\n", __func__, txpwr_lvl_adj->enable);
	AICWFDBG(LOGDEBUG, "lvl_adj_2g4_chan_1_4:%d\r\n", txpwr_lvl_adj->pwrlvl_adj_tbl_2g4[0]);
}

/*
 * rwnx_plat_userconfig_load  ---Load aic_userconfig.txt
 *@filename name of config
 */
static int rwnx_plat_userconfig_load(struct rwnx_hw *rwnx_hw)
{
	aic_chip_userconfig_load(rwnx_hw);
#ifdef CONFIG_AIC8800_POWER_LIMIT
	aic_chip_powerlimit_load(rwnx_hw);
#endif
	return 0;
}

void get_userconfig_txpwr_loss(struct txpwr_loss_conf *txpwr_loss)
{
	txpwr_loss->loss_enable_2g4 = userconfig_info.txpwr_loss.loss_enable_2g4;
	txpwr_loss->loss_value_2g4 = userconfig_info.txpwr_loss.loss_value_2g4;
	txpwr_loss->loss_enable_5g = userconfig_info.txpwr_loss.loss_enable_5g;
	txpwr_loss->loss_value_5g = userconfig_info.txpwr_loss.loss_value_5g;

	AICWFDBG(LOGDEBUG,
		 "%s:loss_enable_2g4: %d, val_2g4: %d, loss_enable_5g: %d, val_5g: %d\r\n",
		 __func__,
		 txpwr_loss->loss_enable_2g4, txpwr_loss->loss_value_2g4,
		 txpwr_loss->loss_enable_5g, txpwr_loss->loss_value_5g);
}

/**
 * rwnx_platform_on - Start the platform
 *
 * @rwnx_hw: Main driver data
 * @config: Config to restore (NULL if nothing to restore)
 *
 * It starts the platform :
 * - load fw and ucodes
 * - initialize IPC
 * - boot the fw
 * - enable link communication/IRQ
 *
 * Called by 802.11 part
 *
 * Return: 0 on success, or a negative error code.
 */
int rwnx_platform_on(struct rwnx_hw *rwnx_hw, void *config)
{
	int ret;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	(void)ret;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	if (rwnx_plat->enabled)
		return 0;

	rwnx_plat_userconfig_load(rwnx_hw);
	// rwnx_plat->enabled = true;
	return 0;
}

/**
 * rwnx_platform_off() - Stop the platform
 *
 * @rwnx_hw: Main driver data
 * @config: Updated with pointer to config, to be able to restore it with
 * rwnx_platform_on(). It's up to the caller to free the config. Set to NULL
 * if configuration is not needed.
 *
 * Called by 802.11 part
 */
void rwnx_platform_off(struct rwnx_hw *rwnx_hw, void **config)
{
#if defined(AICWF_SDIO_SUPPORT)
	tasklet_kill(&rwnx_hw->task);
	rwnx_hw->plat->enabled = false;
	return;
#endif

	if (!rwnx_hw->plat->enabled) {
		if (config)
			*config = NULL;
		return;
	}

	if (config)
		*config = rwnx_term_save_config(rwnx_hw->plat);

	rwnx_hw->plat->disable(rwnx_hw);

	tasklet_kill(&rwnx_hw->task);
	rwnx_platform_reset(rwnx_hw->plat);

	rwnx_hw->plat->enabled = false;
}

/**
 * rwnx_platform_init() - Initialize the platform
 *
 * @rwnx_plat: platform data (already updated by platform driver)
 * @platform_data: Pointer to store the main driver data pointer (aka rwnx_hw)
 *                That will be set as driver data for the platform driver
 * Return: 0 on success, < 0 otherwise
 *
 * Called by the platform driver after it has been probed
 */
int rwnx_platform_init(struct rwnx_plat *rwnx_plat, void **platform_data)
{
	RWNX_DBG(RWNX_FN_ENTRY_STR);

	rwnx_plat->enabled = false;
	g_rwnx_plat = rwnx_plat;

	return rwnx_cfg80211_init(rwnx_plat, platform_data);
}

/**
 * rwnx_platform_deinit() - Deinitialize the platform
 *
 * @rwnx_hw: main driver data
 *
 * Called by the platform driver after it is removed
 */
void rwnx_platform_deinit(struct rwnx_hw *rwnx_hw)
{
	RWNX_DBG(RWNX_FN_ENTRY_STR);

	rwnx_cfg80211_deinit(rwnx_hw);
}

struct device *rwnx_platform_get_dev(struct rwnx_plat *rwnx_plat)
{
#ifdef AICWF_SDIO_SUPPORT
	return rwnx_plat->sdiodev->dev;
#endif
}

MODULE_FIRMWARE(AIC8800_FW_DIR RWNX_AGC_FW_NAME);
MODULE_FIRMWARE(AIC8800_FW_DIR RWNX_FCU_FW_NAME);
MODULE_FIRMWARE(AIC8800_FW_DIR RWNX_LDPC_RAM_NAME);

MODULE_FIRMWARE(AIC8800_FW_DIR RWNX_MAC_FW_NAME);
MODULE_FIRMWARE(AIC8800_FW_DIR RWNX_MAC_FW_NAME2);
