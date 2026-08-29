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
#include "aic_chip_ops.h"
#include "aic_bsp_driver.h"

#include "aic8800dc_compat.h"
#include "aic8800d80_compat.h"

static int aic8801_wifi_init(struct aic_sdio_dev *sdiodev, int testmode)
{
#ifdef CONFIG_M2D_OTA_AUTO_SUPPORT
	if (testmode == FW_M2D_OTA_MODE)
		rwnx_plat_m2d_flash_ota(sdiodev, FW_M2D_OTA_NAME);
	else if (testmode == FW_NORMAL_MODE)
		rwnx_plat_m2d_flash_ota_check(sdiodev, FW_M2D_OTA_NAME);
#endif
	const char *fw_path = aicbsp_firmware_list[aicbsp_info.cpmode].wl_fw;

	if (rwnx_plat_bin_fw_upload(sdiodev, RAM_FMAC_FW_ADDR,
				    fw_path)) {
		pr_err("aicbsp download wifi fw fail\n");
		return -1;
	}
	if (testmode == FW_NORMAL_MODE) {
		if (rwnx_plat_bin_fw_upload(sdiodev, RAM_FMAC_FW_PATCH_ADDR,
					    RAM_FMAC_FW_PATCH_NAME)) {
			pr_err("aicbsp download wifi fw patch fail\n");
			return -1;
		}
	}

	if (aicwifi_patch_config(sdiodev)) {
		pr_err("aicbsp aicwifi_patch_config fail\n");
		return -1;
	}

	if (aicwifi_sys_config(sdiodev)) {
		pr_err("aicbsp aicwifi_sys_config fail\n");
		return -1;
	}

	if (aicwifi_start_from_bootrom(sdiodev)) {
		pr_err("aicbsp wifi start fail\n");
		return -1;
	}
	return 0;
}

static int aic8801_set_patch_info(struct aic_sdio_dev *sdiodev,
				  struct aicbt_patch_info_t *patch_info,
				  struct aicbsp_info_t *aicbsp_info,
				  struct aicbt_patch_table *head)
{
	patch_info->addr_adid = FW_RAM_ADID_BASE_ADDR;
	patch_info->addr_patch = FW_RAM_PATCH_BASE_ADDR;
	return 0;
}

static int aic8801_driver_fw_init(struct aic_sdio_dev *sdiodev, u32 *btenable,
				  struct aicbsp_info_t *aicbsp_info,
				  const struct aicbsp_firmware **aicbsp_firmware_list)
{
	u32 mem_addr;
	struct dbg_mem_read_cfm rd_mem_addr_cfm;

	mem_addr = 0x40500000;

	if (rwnx_send_dbg_mem_read_req(sdiodev, mem_addr, &rd_mem_addr_cfm))
		return -1;

	aicbsp_info->chip_rev = (u8)(rd_mem_addr_cfm.memdata >> 16);
	*btenable = 1;

	if (aicbsp_info->chip_rev != CHIP_REV_U02 &&
	    aicbsp_info->chip_rev != CHIP_REV_U03 &&
		aicbsp_info->chip_rev != CHIP_REV_U04) {
		pr_err("aicbsp: %s, unsupport chip rev: %d\n", __func__,
		       aicbsp_info->chip_rev);
		return -1;
	}

	if (aicbsp_info->chip_rev != CHIP_REV_U02)
		*aicbsp_firmware_list = fw_u03;

	if (aicbsp_system_config(sdiodev))
		return -1;

	return 0;
}

/* ========== Ops instances ========== */

const struct aic_chip_ops aic_chip_aic8801_ops = {
	.name						= "AIC8801",
	.use_func_msg				= false,
	.use_sdiov3_func			= false,
	.need_flowctrl_mask			= true,
	.wakeup_reg_val				= 1,
	.use_flowctrl_msg			= true,
	.use_hdr_checksum			= false,
	.need_fix_hdr_len			= true,
	.need_func0_intr			= false,
	.use_func2					= true,
	.sdio_clock					= FEATURE_SDIO_CLOCK,
	.wifi_init					= aic8801_wifi_init,
	.set_patch_info				= aic8801_set_patch_info,
	.driver_fw_init				= aic8801_driver_fw_init,
};
