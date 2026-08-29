// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aic_chip_8800dc.c
 * @brief Chip operations for AIC8801 / AIC8800DC / AIC8800DW
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ieee80211.h>
#include "aic_chip_ops.h"

#include "aic8800dc_compat.h"
#include "aic_bsp_driver.h"

static int aic8800dc_wifi_init(struct aic_sdio_dev *sdiodev, int testmode)
{
	int ret;

	pr_info("aicbsp ############ %s begin\n", __func__);

	system_config_8800dc(sdiodev);
	pr_info("aicbsp ############ system_config_8800dc done\n");

	ret = rwnx_plat_patch_load_8800dc(sdiodev);
	if (ret) {
		pr_err("aicbsp patch load return %d\n", ret);
		return ret;
	}
	pr_info("aicbsp ############ rwnx_plat_patch_load done\n");

	// rwnx_plat_userconfig_load(sdiodev);

	aicwf_patch_config_8800dc(sdiodev);
	pr_info("aicbsp ############ aicwf_patch_config_8800dc done\n");

	start_from_bootrom_8800DC(sdiodev);

	return 0;
}

static int aic8800dc_set_patch_info(struct aic_sdio_dev *sdiodev,
				    struct aicbt_patch_info_t *patch_info,
				    struct aicbsp_info_t *aicbsp_info,
				    struct aicbt_patch_table *head)
{
	if (aicbsp_info->chip_rev == CHIP_REV_U01)
		patch_info->addr_adid = RAM_8800DC_U01_ADID_ADDR;
	else if (aicbsp_info->chip_rev == CHIP_REV_U02)
		patch_info->addr_adid = RAM_8800DC_U02_ADID_ADDR;
	patch_info->addr_patch = RAM_8800DC_FW_PATCH_ADDR;
	aicbt_patch_info_unpack(patch_info, head);
	if (patch_info->reset_addr == 0) {
		patch_info->reset_addr = FW_RESET_START_ADDR;
		patch_info->reset_val = FW_RESET_START_VAL;
		patch_info->adid_flag_addr = FW_ADID_FLAG_ADDR;
		patch_info->adid_flag = FW_ADID_FLAG_VAL;
		if (rwnx_send_dbg_mem_write_req(sdiodev, patch_info->reset_addr,
						patch_info->reset_val))
			return -1;
		if (rwnx_send_dbg_mem_write_req(sdiodev, patch_info->adid_flag_addr,
						patch_info->adid_flag))
			return -1;
	}

	return 0;
}

static int aic8800dc_driver_fw_init(struct aic_sdio_dev *sdiodev, u32 *btenable,
				    struct aicbsp_info_t *aicbsp_info,
				    const struct aicbsp_firmware **aicbsp_firmware_list)
{
	u32 mem_addr;
	struct dbg_mem_read_cfm rd_mem_addr_cfm;
	u8 is_chip_id_h = 0;

	mem_addr = 0x40500000;

	if (rwnx_send_dbg_mem_read_req(sdiodev, mem_addr, &rd_mem_addr_cfm))
		return -1;

	aicbsp_info->chip_rev = (u8)((rd_mem_addr_cfm.memdata >> 16) & 0x3F);
	is_chip_id_h = (u8)(((rd_mem_addr_cfm.memdata >> 16) & 0xC0) == 0xC0);

	*btenable = ((rd_mem_addr_cfm.memdata >> 26) & 0x1);
	AICWFDBG(LOGINFO, "btenable = %d\n", *btenable);

	if (*btenable == 0) {
		sdiodev->chipid = PRODUCT_ID_AIC8800DW;
		AICWFDBG(LOGINFO, "AIC8800DC change to AIC8800DW\n");
	}

	if (aicbsp_info->chip_rev != CHIP_REV_U01 &&
	    aicbsp_info->chip_rev != CHIP_REV_U02 &&
		aicbsp_info->chip_rev != CHIP_REV_U03 &&
		aicbsp_info->chip_rev != CHIP_REV_U04) {
		pr_err("aicbsp: %s, unsupport chip rev: %d\n", __func__,
		       aicbsp_info->chip_rev);
		return -1;
	}
	if (is_chip_id_h) {
		AICWFDBG(LOGINFO, "IS_CHIP_ID_H\n");
		*aicbsp_firmware_list = fw_8800dc_h_u02;
	} else {
		if (aicbsp_info->chip_rev == CHIP_REV_U01)
			*aicbsp_firmware_list = fw_8800dc_u01;
		else
			*aicbsp_firmware_list = fw_8800dc_u02;
	}

	return 0;
}

/* ========== Ops instances ========== */

const struct aic_chip_ops aic_chip_aic8800dc_ops = {
	.name						= "AIC8800DC",
	.use_func_msg				= true,
	.use_sdiov3_func			= false,
	.need_flowctrl_mask			= true,
	.wakeup_reg_val				= 1,
	.use_flowctrl_msg			= false,
	.use_hdr_checksum			= false,
	.need_fix_hdr_len			= true,
	.need_func0_intr			= false,
	.use_func2					= true,
	.sdio_clock					= FEATURE_SDIO_CLOCK,
	.wifi_init					= aic8800dc_wifi_init,
	.set_patch_info				= aic8800dc_set_patch_info,
	.driver_fw_init				= aic8800dc_driver_fw_init,

};
