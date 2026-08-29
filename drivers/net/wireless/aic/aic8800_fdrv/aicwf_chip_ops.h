/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aicwf_chip_ops.h
 * @brief Chip-specific operation interfaces for AIC8800 series.
 */
#ifndef _AICWF_CHIP_OPS_H_
#define _AICWF_CHIP_OPS_H_

#include <linux/types.h>
#include "lmac_types.h"
#include "lmac_msg.h"

struct rwnx_hw;
struct mm_set_rf_calib_req;
struct mm_set_rf_calib_cfm;
struct mm_set_stack_start_cfm;
struct aic_sdio_dev;

/* SDIO chip hardware properties */
struct aic_sdio_chip_hw {
	bool oob_support;
	bool use_func2;
	bool use_sdiov3_func;
	bool need_flowctrl_mask;
	bool use_flowctrl_msg;
	bool hdr_cksum;
	bool need_func0_intr;
#ifdef CONFIG_AIC8800_AUTO_POWERSAVE
	bool auto_ps_support;
#endif
	u8 wakeup_reg_val;
	bool use_hdr_checksum;
	bool need_fix_hdr_len;
};

/* Match SDIO vendor/device IDs to a chip product ID */
int aicwf_sdio_chipmatch(u16 vendor, u16 device, u16 *chipid);

/* Get SDIO hardware properties for a given chip product ID */
const struct aic_sdio_chip_hw *aic_sdio_get_props(u16 chipid);

/*
 * struct aic_chip_ops - Chip-specific operations
 *
 * Each chip variant provides an instance of this struct.
 * Selected at probe time based on chipid, then used to
 * eliminate chipid-based if-else branching throughout the driver.
 */
struct aic_chip_ops {
	const char *name;

	const bool is_old_ic;

	const bool limit_by_testmode;

	const bool use_80_bandwidth;

	const bool support_priv_cmd_rdwr_pwridx;

	const bool support_priv_cmd_rdwr_pwrlvl;

	const int  pwrlvl_result_copy_len;

	const bool support_priv_cmd_set_txpwr_loss;

	/* Initialize chip-specific capabilities in mod_params */
	int (*init_capa)(struct rwnx_hw *rwnx_hw);

	/* Load chip-specific user configuration */
	int (*userconfig_load)(struct rwnx_hw *rwnx_hw);

#ifdef CONFIG_AIC8800_POWER_LIMIT
	/* Load power limit table */
	int (*powerlimit_load)(struct rwnx_hw *rwnx_hw);
#endif

	/* Set RF calibration config values (cal_cfg_24g / cal_cfg_5g) */
	void (*set_rf_calib_cfg)(struct mm_set_rf_calib_req *rf_calib_req);

	/* Chip-specific RF init sequence (txpwr, RF config, etc.) */
	int (*set_rf_config)(struct rwnx_hw *rwnx_hw,
			     struct mm_set_rf_calib_cfm *cfm);

	/* Misc RAM initialization (DPD, etc.) */
	int (*misc_ram_init)(struct rwnx_hw *rwnx_hw);

	/* Compute checksum for firmware command header */
	u8 (*cmd_hdr_checksum)(u8 *hdr, int len);

	/* Initialize the FW stack start (params differ per chip) */
	int (*set_stack_start)(struct rwnx_hw *rwnx_hw,
			       struct mm_set_stack_start_cfm *cfm);

	/* Get userconfig TX power offset values */
	int (*get_userconfig_txpwr_ofst)(struct txpwr_ofst_conf *txpwr_ofst);

	int (*get_userconfig_txpwr_ofst2x)(struct txpwr_ofst2x_conf *txpwr_ofst2x);

	/* Set RX gain */
	int (*set_rx_gain)(struct rwnx_hw *rwnx_hw, u8 rwnx_rx_gain);

	int (*ic_system_init)(struct rwnx_hw *rwnx_hw, struct dbg_mem_read_cfm *cfm);

	/* Get chip temperature */
	int (*get_temp)(struct rwnx_hw *rwnx_hw, u32 *param_out);

	/* Handle private command to set power */
	int (*priv_cmd_set_power)(struct rwnx_hw *rwnx_hw, int argc,
				  char *argv[], char *command);

	/* Handle private command to get frequency calibration */
	int (*priv_cmd_get_freq_cal)(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command);

	/* Handle private command to get MAC address */
	int (*priv_cmd_get_mac_addr)(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command);

	/* Handle private command to set BT MAC address */
	int (*priv_cmd_get_bt_mac_addr)(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command);

	/* Handle private command to set vendor info */
	int (*priv_cmd_set_vendor_info)(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command);

	/* Handle private command to get vendor info */
	int (*priv_cmd_get_vendor_info)(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command);

	/* Handle private command to read/write power offset */
	int (*priv_cmd_rdwr_pwrofst)(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command);

	/* Handle private command to read/write efuse power offset */
	int (*priv_cmd_rdwr_efuse_pwrofst)(struct rwnx_hw *rwnx_hw, int argc,
					   char *argv[], char *command);

};

extern const struct aic_chip_ops aic_chip_aic8800d80_ops;

/* Select the appropriate chip ops based on chip product ID */
const struct aic_chip_ops *aic_chip_ops_select(u16 chipid);

/* Wrapper function declarations (implemented in aic_chip_ops.c) */
int aic_chip_init_capa(struct rwnx_hw *rwnx_hw);
int aic_chip_userconfig_load(struct rwnx_hw *rwnx_hw);
int aic_chip_powerlimit_load(struct rwnx_hw *rwnx_hw);
void aic_chip_set_rf_calib_cfg(struct rwnx_hw *rwnx_hw,
			       struct mm_set_rf_calib_req *rf_calib_req);
int aic_chip_set_rf_config(struct rwnx_hw *rwnx_hw,
			   struct mm_set_rf_calib_cfm *cfm);
u8 aic_chip_cmd_hdr_checksum(struct rwnx_hw *rwnx_hw, u8 *hdr, int len);
int aic_chip_misc_ram_init(struct rwnx_hw *rwnx_hw);
int aic_chip_set_stack_start(struct rwnx_hw *rwnx_hw,
			     struct mm_set_stack_start_cfm *cfm);
int aic_chip_get_userconfig_txpwr_ofst(struct rwnx_hw *rwnx_hw,
				       struct txpwr_ofst_conf *txpwr_ofst);
int aic_chip_get_userconfig_txpwr_ofst2x(struct rwnx_hw *rwnx_hw,
					 struct txpwr_ofst2x_conf *txpwr_ofst2x);
int aic_chip_set_rx_gain(struct rwnx_hw *rwnx_hw, u8 rwnx_rx_gain);
int aic_chip_ic_system_init(struct rwnx_hw *rwnx_hw,
			    struct dbg_mem_read_cfm *cfm);
int aic_chip_get_temp(struct rwnx_hw *rwnx_hw, u32 *param_out);
int aic_chip_priv_cmd_set_power(struct rwnx_hw *rwnx_hw, int argc,
				char *argv[], char *command);
int aic_chip_priv_cmd_get_freq_cal(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command);
int aic_chip_priv_cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command);
int aic_chip_priv_cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command);
int aic_chip_priv_cmd_set_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command);
int aic_chip_priv_cmd_get_vendor_info(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command);
int aic_chip_priv_cmd_rdwr_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command);
int aic_chip_priv_cmd_rdwr_efuse_pwrofst(struct rwnx_hw *rwnx_hw, int argc,
					 char *argv[], char *command);
#endif /* _AICWF_CHIP_OPS_H_ */
