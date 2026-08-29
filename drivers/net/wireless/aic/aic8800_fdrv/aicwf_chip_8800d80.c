// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aicwf_chip_8800d80.c
 * @brief Chip operations for AIC8800D80
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ieee80211.h>
#include "aicwf_chip_ops.h"
#include "rwnx_defs.h"
#include "aicwf_sdio.h"
#include "aicwf_compat_8800dc.h"
#include "aicwf_compat_8800d80.h"
#include "aicwf_txrxif.h"
#include "aic_priv_cmd.h"

/* ========== 8800D80 specific ========== */

static int aic8800d80_init_capa(struct rwnx_hw *rwnx_hw)
{
	rwnx_hw->mod_params->sgi80 = true;
	rwnx_hw->mod_params->use_80 = true;
	return 0;
}

static int aic8800d80_userconfig_load(struct rwnx_hw *rwnx_hw)
{
	return rwnx_plat_userconfig_load_8800d80(rwnx_hw);
}

#ifdef CONFIG_AIC8800_POWER_LIMIT
static int aic8800d80_powerlimit_load(struct rwnx_hw *rwnx_hw)
{
	return rwnx_plat_powerlimit_load_8800d80(rwnx_hw);
}
#endif

static int aic8800d80_set_rf_config(struct rwnx_hw *rwnx_hw,
				    struct mm_set_rf_calib_cfm *cfm)
{
	return aicwf_set_rf_config_8800d80(rwnx_hw, cfm);
}

static void aic8800d80_set_rf_calib_cfg(struct mm_set_rf_calib_req *rf_calib_req)
{
	rf_calib_req->cal_cfg_24g = 0x0f8f;
	rf_calib_req->cal_cfg_5g = 0x0f0f;
}

static u8 aic8800d80_cmd_hdr_checksum(u8 *hdr, int len)
{
	return crc8_ponl_107(hdr, 3);
}

static int aic8800d80_set_stack_start(struct rwnx_hw *rwnx_hw,
				      struct mm_set_stack_start_cfm *cfm)
{
	return rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, CO_BIT(5), 0, cfm);
}

static int aic8800d80_get_userconfig_txpwr_ofst2x(struct txpwr_ofst2x_conf *txpwr_ofst2x)
{
	get_userconfig_txpwr_ofst2x_in_fdrv(txpwr_ofst2x);
	return 0;
}

static int aic8800d80_ic_system_init(struct rwnx_hw *rwnx_hw, struct dbg_mem_read_cfm *cfm)
{
	u32 memdata_temp = 0x00000006;
	int ret;

	ret = rwnx_send_dbg_mem_block_write_req(rwnx_hw, 0x40504084, 4,
						&memdata_temp);
	if (ret) {
		AICWFDBG(LOGERROR, "[0x40504084] write fail: %d\n", ret);
		return -1;
	}

	ret = rwnx_send_dbg_mem_read_req(rwnx_hw, 0x40504084, cfm);
	if (ret) {
		AICWFDBG(LOGERROR, "[0x40504084] rd fail\n");
		return -1;
	}
	AICWFDBG(LOGINFO, "rd [0x40504084] = %x\n", cfm->memdata);
	return ret;
}

static int aic8800d80_priv_cmd_set_power(struct rwnx_hw *rwnx_hw, int argc,
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

static int aic8800d80_priv_cmd_get_freq_cal(struct rwnx_hw *rwnx_hw, int argc,
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

static int aic8800d80_priv_cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
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

static int aic8800d80_priv_cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
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

static int aic8800d80_priv_cmd_set_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
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

static int aic8800d80_priv_cmd_get_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
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

static int aic8800d80_priv_cmd_rdwr_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
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
		if (argc > 4) {
			u8 type = (u8)command_strtoul(argv[2], NULL, 16);
			u8 chgrp = (u8)command_strtoul(argv[3], NULL, 16);
			s8 pwrofst = (u8)command_strtoul(argv[4], NULL, 10);
			u8 buf[4] = {func, type, chgrp, (u8)pwrofst};

			AICWFDBG(LOGINFO, "set pwrofst_%s:[%x][%x]=%d\r\n",
				 (func == 1) ? "2.4g" : "5g", type, chgrp, pwrofst);
			rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFST, sizeof(buf), buf, &cfm);
		} else {
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	res_len = 3 * 3 + 3 * 6;
	memcpy(command, &cfm.rftest_result[0], res_len);
	return res_len;
}

static int aic8800d80_priv_cmd_rdwr_efuse_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
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
		if (argc > 4) {
			u8 type = (u8)command_strtoul(argv[2], NULL, 16);
			u8 chgrp = (u8)command_strtoul(argv[3], NULL, 16);
			s8 pwrofst = (u8)command_strtoul(argv[4], NULL, 10);
			u8 buf[4] = {func, type, chgrp, (u8)pwrofst};

			AICWFDBG(LOGINFO, "set efuse pwrofst_%s:[%x][%x]=%d\r\n",
				 (func == 1) ? "2.4g" : "5g", type, chgrp, pwrofst);
			rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFST, sizeof(buf), buf, &cfm);
		} else {
			AICWFDBG(LOGERROR, "wrong args\n");
			return -EINVAL;
		}
	} else {
		AICWFDBG(LOGERROR, "wrong func: %x\n", func);
		return -EINVAL;
	}
	res_len = (3 * 3 + 3 * 6) * 2; // 3 * 2 (2.4g) + 3 * 6 (5g)
	memcpy(command, &cfm.rftest_result[0], res_len);
	return res_len;
}

/* ========== Ops instances ========== */

const struct aic_chip_ops aic_chip_aic8800d80_ops = {
	.name						= "AIC8800D80",
	.is_old_ic					= false,
	.limit_by_testmode			= true,
	.use_80_bandwidth			= true,
	.support_priv_cmd_rdwr_pwridx	= false,
	.support_priv_cmd_rdwr_pwrlvl	= true,
	.pwrlvl_result_copy_len		= 6,
	.support_priv_cmd_set_txpwr_loss	= true,
	.init_capa					= aic8800d80_init_capa,
	.userconfig_load			= aic8800d80_userconfig_load,
#ifdef CONFIG_AIC8800_POWER_LIMIT
	.powerlimit_load			= aic8800d80_powerlimit_load,
#endif
	.set_rf_calib_cfg			= aic8800d80_set_rf_calib_cfg,
	.set_rf_config				= aic8800d80_set_rf_config,
	.cmd_hdr_checksum			= aic8800d80_cmd_hdr_checksum,
	.set_stack_start			= aic8800d80_set_stack_start,
	.get_userconfig_txpwr_ofst2x	= aic8800d80_get_userconfig_txpwr_ofst2x,
	.ic_system_init				= aic8800d80_ic_system_init,
	.priv_cmd_set_power			= aic8800d80_priv_cmd_set_power,
	.priv_cmd_get_freq_cal		= aic8800d80_priv_cmd_get_freq_cal,
	.priv_cmd_get_mac_addr		= aic8800d80_priv_cmd_get_mac_addr,
	.priv_cmd_get_bt_mac_addr	= aic8800d80_priv_cmd_get_bt_mac_addr,
	.priv_cmd_set_vendor_info	= aic8800d80_priv_cmd_set_vendor_info,
	.priv_cmd_get_vendor_info	= aic8800d80_priv_cmd_get_vendor_info,
	.priv_cmd_rdwr_pwrofst		= aic8800d80_priv_cmd_rdwr_pwrofst,
	.priv_cmd_rdwr_efuse_pwrofst	= aic8800d80_priv_cmd_rdwr_efuse_pwrofst,
};
