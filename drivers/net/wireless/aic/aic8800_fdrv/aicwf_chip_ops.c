// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aicwf_chip_ops.c
 * @brief Wrapper function implementations for chip-specific operations.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include "aicwf_chip_ops.h"
#include "rwnx_defs.h"
#include "aicwf_sdio.h"
#include "lmac_msg.h"

/* ========== SDIO chip properties ========== */
static const struct aic_sdio_chip_hw aic_sdio_chip_tbl[] = {
	[PRODUCT_ID_AIC8801] = {
		.oob_support = false,
		.use_func2 = true,
		.use_sdiov3_func = false,
		.need_flowctrl_mask = true,
		.use_flowctrl_msg = true,
		.hdr_cksum = false,
		.need_func0_intr = false,
#ifdef CONFIG_AIC8800_AUTO_POWERSAVE
		.auto_ps_support = false,
#endif
		.wakeup_reg_val = 1,
		.use_hdr_checksum = false,
		.need_fix_hdr_len = true,
	},
	[PRODUCT_ID_AIC8800DC] = {
		.oob_support = true,
		.use_func2 = true,
		.use_sdiov3_func = false,
		.need_flowctrl_mask = true,
		.use_flowctrl_msg = false,
		.hdr_cksum = false,
		.need_func0_intr = false,
#ifdef CONFIG_AIC8800_AUTO_POWERSAVE
		.auto_ps_support = false,
#endif
		.wakeup_reg_val = 1,
		.use_hdr_checksum = false,
		.need_fix_hdr_len = true,
	},
	[PRODUCT_ID_AIC8800D80] = {
		.oob_support = true,
		.use_func2 = false,
		.use_sdiov3_func = true,
		.need_flowctrl_mask = false,
		.use_flowctrl_msg = true,
		.hdr_cksum = true,
		.need_func0_intr = true,
#ifdef CONFIG_AIC8800_AUTO_POWERSAVE
		.auto_ps_support = true,
#endif
		.wakeup_reg_val = 0x11,
		.use_hdr_checksum = true,
		.need_fix_hdr_len = false,
	},
};

int aicwf_sdio_chipmatch(u16 vid, u16 did, u16 *chipid)
{
	if (vid != SDIO_VENDOR_ID_AIC8800D80 ||
	    did != SDIO_DEVICE_ID_AIC8800D80)
		return -ENODEV;

	*chipid = PRODUCT_ID_AIC8800D80;
	AICWFDBG(LOGINFO, "%s USE AIC8800D80\r\n", __func__);
	return 0;
}

const struct aic_sdio_chip_hw *aic_sdio_get_props(u16 chipid)
{
	if (chipid < ARRAY_SIZE(aic_sdio_chip_tbl))
		return &aic_sdio_chip_tbl[chipid];
	return NULL;
}

/*========== Chip ops wrappers ========== */
int aic_chip_init_capa(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->init_capa)
		return rwnx_hw->chip_ops->init_capa(rwnx_hw);
	return 0;
}

int aic_chip_userconfig_load(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->userconfig_load)
		return rwnx_hw->chip_ops->userconfig_load(rwnx_hw);
	return -1;
}

#ifdef CONFIG_AIC8800_POWER_LIMIT
int aic_chip_powerlimit_load(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->powerlimit_load)
		return rwnx_hw->chip_ops->powerlimit_load(rwnx_hw);
	return 0;
}
#endif

void aic_chip_set_rf_calib_cfg(struct rwnx_hw *rwnx_hw,
			       struct mm_set_rf_calib_req *rf_calib_req)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->set_rf_calib_cfg)
		rwnx_hw->chip_ops->set_rf_calib_cfg(rf_calib_req);
}

int aic_chip_set_rf_config(struct rwnx_hw *rwnx_hw,
			   struct mm_set_rf_calib_cfm *cfm)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->set_rf_config)
		return rwnx_hw->chip_ops->set_rf_config(rwnx_hw, cfm);
	return -1;
}

u8 aic_chip_cmd_hdr_checksum(struct rwnx_hw *rwnx_hw, u8 *hdr, int len)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->cmd_hdr_checksum)
		return rwnx_hw->chip_ops->cmd_hdr_checksum(hdr, len);
	return 0x00;
}

int aic_chip_misc_ram_init(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->misc_ram_init)
		return rwnx_hw->chip_ops->misc_ram_init(rwnx_hw);
	return 0;
}

int aic_chip_set_stack_start(struct rwnx_hw *rwnx_hw,
			     struct mm_set_stack_start_cfm *cfm)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->set_stack_start)
		return rwnx_hw->chip_ops->set_stack_start(rwnx_hw, cfm);
	return 0;
}

int aic_chip_get_userconfig_txpwr_ofst(struct rwnx_hw *rwnx_hw,
				       struct txpwr_ofst_conf *txpwr_ofst)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->get_userconfig_txpwr_ofst)
		return rwnx_hw->chip_ops->get_userconfig_txpwr_ofst(txpwr_ofst);
	return 0;
}

int aic_chip_get_userconfig_txpwr_ofst2x(struct rwnx_hw *rwnx_hw,
					 struct txpwr_ofst2x_conf *txpwr_ofst2x)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->get_userconfig_txpwr_ofst2x)
		return rwnx_hw->chip_ops->get_userconfig_txpwr_ofst2x(txpwr_ofst2x);
	return 0;
}

int aic_chip_set_rx_gain(struct rwnx_hw *rwnx_hw, u8 rwnx_rx_gain)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->set_rx_gain)
		return rwnx_hw->chip_ops->set_rx_gain(rwnx_hw, rwnx_rx_gain);
	return 0;
}

int aic_chip_ic_system_init(struct rwnx_hw *rwnx_hw, struct dbg_mem_read_cfm *cfm)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->ic_system_init)
		return rwnx_hw->chip_ops->ic_system_init(rwnx_hw, cfm);
	return 0;
}

int aic_chip_get_temp(struct rwnx_hw *rwnx_hw, u32 *param_out)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->get_temp)
		return rwnx_hw->chip_ops->get_temp(rwnx_hw, param_out);
	return 0;
}

int aic_chip_priv_cmd_set_power(struct rwnx_hw *rwnx_hw, int argc,
				char *argv[], char *command)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->priv_cmd_set_power)
		return rwnx_hw->chip_ops->priv_cmd_set_power(rwnx_hw, argc, argv, command);
	return 0;
}

int aic_chip_priv_cmd_get_freq_cal(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->priv_cmd_get_freq_cal)
		return rwnx_hw->chip_ops->priv_cmd_get_freq_cal(rwnx_hw, argc, argv, command);
	return 0;
}

int aic_chip_priv_cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->priv_cmd_get_mac_addr)
		return rwnx_hw->chip_ops->priv_cmd_get_mac_addr(rwnx_hw, argc, argv, command);
	return 0;
}

int aic_chip_priv_cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->priv_cmd_get_bt_mac_addr)
		return rwnx_hw->chip_ops->priv_cmd_get_bt_mac_addr(rwnx_hw, argc, argv, command);
	return 0;
}

int aic_chip_priv_cmd_set_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->priv_cmd_set_vendor_info)
		return rwnx_hw->chip_ops->priv_cmd_set_vendor_info(rwnx_hw, argc, argv, command);
	return 0;
}

int aic_chip_priv_cmd_get_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->priv_cmd_get_vendor_info)
		return rwnx_hw->chip_ops->priv_cmd_get_vendor_info(rwnx_hw, argc, argv, command);
	return 0;
}

int aic_chip_priv_cmd_rdwr_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->priv_cmd_rdwr_pwrofst)
		return rwnx_hw->chip_ops->priv_cmd_rdwr_pwrofst(rwnx_hw, argc, argv, command);
	return 0;
}

int aic_chip_priv_cmd_rdwr_efuse_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
					 char *argv[], char *command)
{
	if (rwnx_hw->chip_ops && rwnx_hw->chip_ops->priv_cmd_rdwr_efuse_pwrofst)
		return rwnx_hw->chip_ops->priv_cmd_rdwr_efuse_pwrofst(rwnx_hw, argc, argv, command);
	return 0;
}

const struct aic_chip_ops *aic_chip_ops_select(u16 chipid)
{
	if (chipid == PRODUCT_ID_AIC8800D80)
		return &aic_chip_aic8800d80_ops;

	return NULL;
}
