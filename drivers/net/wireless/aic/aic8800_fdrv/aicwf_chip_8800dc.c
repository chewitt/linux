// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aicwf_chip_8800dc.c
 * @brief Chip operations for AIC8801 / AIC8800DC / AIC8800DW
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ieee80211.h>
#include "aicwf_chip_ops.h"
#include "rwnx_defs.h"
#include "aicwf_sdio.h"
#include "aicwf_compat_8800dc.h"
#include "aicwf_compat_8800d80.h"
#include "rwnx_msg_tx.h"
#include "aicwf_txrxif.h"
#include "aic_priv_cmd.h"

static int aic8800dc_init_capa(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->mod_params->he_mcs_map == IEEE80211_HE_MCS_SUPPORT_0_11)
		rwnx_hw->mod_params->he_mcs_map = IEEE80211_HE_MCS_SUPPORT_0_9;
	return 0;
}

static int aic8800dc_userconfig_load(struct rwnx_hw *rwnx_hw)
{
	return rwnx_plat_userconfig_load_8800dc(rwnx_hw);
}

static void aic8800dc_set_rf_calib_cfg(struct mm_set_rf_calib_req *rf_calib_req)
{
	rf_calib_req->cal_cfg_24g = 0x0f8f;
	rf_calib_req->cal_cfg_5g = 0;
}

static int aic8800dc_set_rf_config(struct rwnx_hw *rwnx_hw,
				   struct mm_set_rf_calib_cfm *cfm)
{
	return aicwf_set_rf_config_8800dc(rwnx_hw, cfm);
}

static int aic8800dc_misc_ram_init(struct rwnx_hw *rwnx_hw)
{
	return aicwf_fdrv_misc_ram_init_8800dc(rwnx_hw);
}

static u8 aic8800_cmd_hdr_checksum(u8 *hdr, int len)
{
	return 0x00;
}

static int aic8800dc_set_stack_start(struct rwnx_hw *rwnx_hw,
				     struct mm_set_stack_start_cfm *cfm)
{
	int ret = rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, 0, 0, cfm);

	cfm->is_5g_support = false;
	return ret;
}

static int aic8800dc_get_userconfig_txpwr_ofst(struct txpwr_ofst_conf *txpwr_ofst)
{
	get_userconfig_txpwr_ofst_in_fdrv(txpwr_ofst);
	return 0;
}

static int aic8800dc_set_rx_gain(struct rwnx_hw *rwnx_hw, u8 rwnx_rx_gain)
{
	return rwnx_send_dbg_mem_mask_write_req(rwnx_hw, 0x4033b300, 0xFF,
									rwnx_rx_gain);
}

static int aic8800dc_get_temp(struct rwnx_hw *rwnx_hw, u32 *param_out)
{
	int error = 0;
	struct mm_set_vendor_hwconfig_cfm cfm = {
		0,
	};

	error = rwnx_send_get_temp_hwconfig_req(rwnx_hw, &cfm);
	if (error) {
		AICWFDBG(LOGDEBUG, "get_chip_temp err=%d\n", error);
		return error;
	}

	if (param_out)
		param_out[0] = (int32_t)cfm.chip_temp_cfm.degree;
	else
		AICWFDBG(LOGDEBUG, "get_chip_temp param_out is NULL\n");

	return error;
}

static int aic8800dc_priv_cmd_set_power(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	u8 ana_pwr;
	u8 pwr;

	ana_pwr = command_strtoul(argv[1], NULL, 10);
	pwr = ana_pwr;
	if (ana_pwr > 0x1e)
		return -EINVAL;

	AICWFDBG(LOGINFO, "pwr =%x\r\n", pwr);
	rwnx_send_rftest_req(rwnx_hw, SET_POWER, sizeof(pwr), (u8 *)&pwr, NULL);
	return 0;
}

static int aic8800dc_priv_cmd_get_freq_cal(struct rwnx_hw *rwnx_hw, int argc,
					   char *argv[], char *command)
{
	u8 val;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	rwnx_send_rftest_req(rwnx_hw, GET_FREQ_CAL, 0, NULL, &cfm);
	memcpy(command, &cfm.rftest_result[0], 4);
	val = cfm.rftest_result[0];

	AICWFDBG(LOGINFO, "cap=0x%x, cap_fine=0x%x\n", val & 0xff,
		 (val >> 8) & 0xff);

	return 0;
}

static int aic8800dc_priv_cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
					   char *argv[], char *command)
{
	u32 addr0, addr1;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	rwnx_send_rftest_req(rwnx_hw, GET_MAC_ADDR, 0, NULL, &cfm);
	memcpy(command, &cfm.rftest_result[0], 8);

	addr0 = cfm.rftest_result[0];
	int rem_cnt = (cfm.rftest_result[1] >> 16) & 0x00FF;

	addr1 = cfm.rftest_result[1] & 0x0000FFFF;
	AICWFDBG(LOGINFO, "0x%x,0x%x (remain:%x)\n", addr0, addr1, rem_cnt);

	return 0;
}

static int aic8800dc_priv_cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
					      char *argv[], char *command)
{
	u32 addr0, addr1;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	rwnx_send_rftest_req(rwnx_hw, GET_BT_MAC_ADDR, 0, NULL, &cfm);
	memcpy(command, &cfm.rftest_result[0], 8);

	addr0 = cfm.rftest_result[0];
	int rem_cnt = (cfm.rftest_result[1] >> 16) & 0x00FF;

	addr1 = cfm.rftest_result[1] & 0x0000FFFF;
	AICWFDBG(LOGINFO, "0x%x,0x%x (remain:%x)\n", addr0, addr1, rem_cnt);

	return 0;
}

static int aic8800dc_priv_cmd_set_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
					      char *argv[], char *command)
{
	u8 vendor_info;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	vendor_info = command_strtoul(argv[1], NULL, 16);
	AICWFDBG(LOGINFO, "set vendor info:%x\n", vendor_info);
	rwnx_send_rftest_req(rwnx_hw, SET_VENDOR_INFO, 1, &vendor_info, &cfm);

	memcpy(command, &cfm.rftest_result[0], 2);
	AICWFDBG(LOGINFO, "0x%x\n", cfm.rftest_result[0]);
	return 2;
}

static int aic8800dc_priv_cmd_get_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
					      char *argv[], char *command)
{
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};
	rwnx_send_rftest_req(rwnx_hw, GET_VENDOR_INFO, 0, NULL, &cfm);

	memcpy(command, &cfm.rftest_result[0], 2);
	AICWFDBG(LOGINFO, "0x%x\n", cfm.rftest_result[0]);
	return 2;
}

static int aic8800dc_priv_cmd_rdwr_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
					   char *argv[], char *command)
{
	u8 func = 0;
	int res_len = 0;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	if (argc > 1)
		func = (u8)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFST, 0, NULL, &cfm);
	} else if (func <= 2) { // write 2.4g/5g pwr ofst
		if (argc > 3) {
			u8 chgrp = (u8)command_strtoul(argv[2], NULL, 16);
			s8 pwrofst = (u8)command_strtoul(argv[3], NULL, 10);
			u8 buf[3] = {func, chgrp, (u8)pwrofst};

			AICWFDBG(LOGINFO, "set pwrofst_%s:[%x]=%d\r\n",
				 (func == 1) ? "2.4g" : "5g", chgrp, pwrofst);
			rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFST, sizeof(buf), buf, &cfm);
		} else {
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	res_len = 3;
	memcpy(command, &cfm.rftest_result[0], res_len);
	return res_len;
}

static int aic8800dc_priv_cmd_rdwr_efuse_pwrofst(struct rwnx_hw *rwnx_hw,
						 int argc, char *argv[],
						 char *command)
{
	u8 func = 0;
	int res_len = 0;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	if (argc > 1)
		func = (u8)command_strtoul(argv[1], NULL, 16);
	if (func == 0) { // read cur
		rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFST, 0, NULL, &cfm);
	} else if (func <= 2) { // write 2.4g/5g pwr ofst
		if (argc > 3) {
			u8 chgrp = (u8)command_strtoul(argv[2], NULL, 16);
			s8 pwrofst = (u8)command_strtoul(argv[3], NULL, 10);
			u8 buf[3] = {func, chgrp, (u8)pwrofst};

			AICWFDBG(LOGINFO, "set efuse pwrofst_%s:[%x]=%d\r\n",
				 (func == 1) ? "2.4g" : "5g", chgrp, pwrofst);
			rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFST, sizeof(buf), buf, &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	res_len = 3 * 2; // 6 = 3 (2.4g) * 2
	memcpy(command, &cfm.rftest_result[0], res_len);
	return res_len;
}

/* ========== Ops instances ========== */

const struct aic_chip_ops aic_chip_aic8800dc_ops = {
	.name						= "AIC8800DC",
	.is_old_ic					= true,
	.limit_by_testmode			= true,
	.use_80_bandwidth			= false,
	.support_priv_cmd_rdwr_pwridx	= false,
	.support_priv_cmd_rdwr_pwrlvl	= true,
	.pwrlvl_result_copy_len		= 3,
	.support_priv_cmd_set_txpwr_loss	= false,
	.init_capa					= aic8800dc_init_capa,
	.userconfig_load			= aic8800dc_userconfig_load,
	.set_rf_calib_cfg			= aic8800dc_set_rf_calib_cfg,
	.set_rf_config				= aic8800dc_set_rf_config,
	.misc_ram_init				= aic8800dc_misc_ram_init,
	.cmd_hdr_checksum			= aic8800_cmd_hdr_checksum,
	.set_stack_start			= aic8800dc_set_stack_start,
	.get_userconfig_txpwr_ofst	= aic8800dc_get_userconfig_txpwr_ofst,
	.set_rx_gain				= aic8800dc_set_rx_gain,
	.get_temp					= aic8800dc_get_temp,
	.priv_cmd_set_power			= aic8800dc_priv_cmd_set_power,
	.priv_cmd_get_freq_cal		= aic8800dc_priv_cmd_get_freq_cal,
	.priv_cmd_get_mac_addr		= aic8800dc_priv_cmd_get_mac_addr,
	.priv_cmd_get_bt_mac_addr	= aic8800dc_priv_cmd_get_bt_mac_addr,
	.priv_cmd_set_vendor_info	= aic8800dc_priv_cmd_set_vendor_info,
	.priv_cmd_get_vendor_info	= aic8800dc_priv_cmd_get_vendor_info,
	.priv_cmd_rdwr_pwrofst		= aic8800dc_priv_cmd_rdwr_pwrofst,
	.priv_cmd_rdwr_efuse_pwrofst	= aic8800dc_priv_cmd_rdwr_efuse_pwrofst,
};
