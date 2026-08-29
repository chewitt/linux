// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aic_chip_8801.c
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

#define FW_USERCONFIG_NAME "aic_userconfig.txt"

static int aic8801_init_capa(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->mod_params->he_mcs_map == IEEE80211_HE_MCS_SUPPORT_0_11)
		rwnx_hw->mod_params->he_mcs_map = IEEE80211_HE_MCS_SUPPORT_0_9;
	return 0;
}

static int aic8801_userconfig_load(struct rwnx_hw *rwnx_hw)
{
	return rwnx_plat_userconfig_upload(rwnx_hw, FW_USERCONFIG_NAME);
}

static void aic8801_set_rf_calib_cfg(struct mm_set_rf_calib_req *rf_calib_req)
{
	rf_calib_req->cal_cfg_24g = 0xbf;
	rf_calib_req->cal_cfg_5g = 0x3f;
}

static int aic8801_set_rf_config(struct rwnx_hw *rwnx_hw,
				 struct mm_set_rf_calib_cfm *cfm)
{
	int ret = 0;

	ret = rwnx_send_txpwr_idx_req(rwnx_hw);
	if (ret)
		return -1;
	ret = rwnx_send_txpwr_ofst_req(rwnx_hw);
	if (ret)
		return -1;

	if (rwnx_hw->testmode == 0) {
		ret = rwnx_send_rf_calib_req(rwnx_hw, cfm);
		if (ret)
			return -1;
	}
	return ret;
}

static u8 aic8801_cmd_hdr_checksum(u8 *hdr, int len)
{
	return 0x00;
}

static int aic8801_set_stack_start(struct rwnx_hw *rwnx_hw,
				   struct mm_set_stack_start_cfm *cfm)
{
#ifdef AIC8800_USE_5G
	return rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, CO_BIT(5), 0, cfm);
#else
	return rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, 0, 0, cfm);
#endif
}

static int aic8801_get_userconfig_txpwr_ofst(struct txpwr_ofst_conf *txpwr_ofst)
{
	get_userconfig_txpwr_ofst(txpwr_ofst);
	return 0;
}

static int aic8801_priv_cmd_set_power(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	u8 ana_pwr;
	u8 dig_pwr;
	u8 pwr;

	ana_pwr = command_strtoul(argv[1], NULL, 16);
	dig_pwr = command_strtoul(argv[2], NULL, 16);
	pwr = (ana_pwr << 4 | dig_pwr);
	if (ana_pwr > 0xf || dig_pwr > 0xf)
		return -EINVAL;

	AICWFDBG(LOGINFO, "pwr =%x\r\n", pwr);
	rwnx_send_rftest_req(rwnx_hw, SET_POWER, sizeof(pwr), (u8 *)&pwr, NULL);
	return 0;
}

static int aic8801_priv_cmd_get_freq_cal(struct rwnx_hw *rwnx_hw, int argc,
					 char *argv[], char *command)
{
	u8 val;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	rwnx_send_rftest_req(rwnx_hw, GET_FREQ_CAL, 0, NULL, &cfm);
	memcpy(command, &cfm.rftest_result[0], 4);
	val = cfm.rftest_result[0];

	AICWFDBG(LOGINFO, "cap=0x%x (remain:%x), cap_fine=%x (remain:%x)\n",
		 val & 0xff, (val >> 8) & 0xff, (val >> 16) & 0xff,
				 (val >> 24) & 0xff);

	return 0;
}

static int aic8801_priv_cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
					 char *argv[], char *command)
{
	u32 addr0, addr1;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	rwnx_send_rftest_req(rwnx_hw, GET_MAC_ADDR, 0, NULL, &cfm);
	memcpy(command, &cfm.rftest_result[0], 8);

	addr0 = cfm.rftest_result[0];
	addr1 = cfm.rftest_result[1];
	AICWFDBG(LOGINFO, "0x%x,0x%x\n", addr0, addr1);
	return 0;
}

static int aic8801_priv_cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
					    char *argv[], char *command)
{
	u32 addr0, addr1;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	rwnx_send_rftest_req(rwnx_hw, GET_BT_MAC_ADDR, 0, NULL, &cfm);
	memcpy(command, &cfm.rftest_result[0], 8);

	addr0 = cfm.rftest_result[0];
	addr1 = cfm.rftest_result[1];
	AICWFDBG(LOGINFO, "0x%x,0x%x\n", addr0, addr1);

	return 0;
}

static int aic8801_priv_cmd_set_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
					    char *argv[], char *command)
{
	u8 vendor_info;
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};

	vendor_info = command_strtoul(argv[1], NULL, 16);
	AICWFDBG(LOGINFO, "set vendor info:%x\n", vendor_info);
	rwnx_send_rftest_req(rwnx_hw, SET_VENDOR_INFO, 1, &vendor_info, &cfm);

	memcpy(command, &cfm.rftest_result[0], 1);
	AICWFDBG(LOGINFO, "0x%x\n", cfm.rftest_result[0]);
	return 1;
}

static int aic8801_priv_cmd_get_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
					    char *argv[], char *command)
{
	struct dbg_rftest_cmd_cfm cfm = {{
		0,
	}};
	rwnx_send_rftest_req(rwnx_hw, GET_VENDOR_INFO, 0, NULL, &cfm);

	memcpy(command, &cfm.rftest_result[0], 1);
	AICWFDBG(LOGINFO, "0x%x\n", cfm.rftest_result[0]);
	return 1;
}

static int aic8801_priv_cmd_rdwr_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
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
	res_len = 3 + 4;
	memcpy(command, &cfm.rftest_result[0], res_len);
	return res_len;
}

static int aic8801_priv_cmd_rdwr_efuse_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
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
	res_len = 3 + 4; // 7 = 3(2.4g) + 4(5g)
	memcpy(command, &cfm.rftest_result[0], res_len);
	return res_len;
}

/* ========== Ops instances ========== */

const struct aic_chip_ops aic_chip_aic8801_ops = {
	.name						= "AIC8801",
	.is_old_ic					= true,
	.limit_by_testmode			= false,
	.use_80_bandwidth			= false,
	.support_priv_cmd_rdwr_pwridx	= true,
	.support_priv_cmd_rdwr_pwrlvl	= false,
	.pwrlvl_result_copy_len		= 0,
	.support_priv_cmd_set_txpwr_loss	= false,
	.init_capa					= aic8801_init_capa,
	.userconfig_load			= aic8801_userconfig_load,
	.set_rf_calib_cfg			= aic8801_set_rf_calib_cfg,
	.set_rf_config				= aic8801_set_rf_config,
	.cmd_hdr_checksum			= aic8801_cmd_hdr_checksum,
	.set_stack_start			= aic8801_set_stack_start,
	.get_userconfig_txpwr_ofst	= aic8801_get_userconfig_txpwr_ofst,
	.priv_cmd_set_power			= aic8801_priv_cmd_set_power,
	.priv_cmd_get_freq_cal		= aic8801_priv_cmd_get_freq_cal,
	.priv_cmd_get_mac_addr		= aic8801_priv_cmd_get_mac_addr,
	.priv_cmd_get_bt_mac_addr	= aic8801_priv_cmd_get_bt_mac_addr,
	.priv_cmd_set_vendor_info	= aic8801_priv_cmd_set_vendor_info,
	.priv_cmd_get_vendor_info	= aic8801_priv_cmd_get_vendor_info,
	.priv_cmd_rdwr_pwrofst		= aic8801_priv_cmd_rdwr_pwrofst,
	.priv_cmd_rdwr_efuse_pwrofst	= aic8801_priv_cmd_rdwr_efuse_pwrofst,
};
