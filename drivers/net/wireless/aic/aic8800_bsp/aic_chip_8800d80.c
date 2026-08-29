// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aic_chip_8800d80.c
 * @brief Chip operations for AIC8800D80
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ieee80211.h>
#include "aic_chip_ops.h"

#include "aic8800d80_compat.h"
#include "aic_bsp_driver.h"

static int aic8800d80_wifi_init(struct aic_sdio_dev *sdiodev, int testmode)
{
	const char *fw_path = fw_8800d80_u02[AICBSP_CPMODE_WORK].wl_fw;

	(void)testmode;
	aicbsp_firmware_list = fw_8800d80_u02;
	aicbsp_info.cpmode = AICBSP_CPMODE_WORK;
	if (!fw_path)
		return -ENOENT;

	pr_info("aicbsp 8800d80 u02 wifi firmware: %s\n", fw_path);

	if (rwnx_plat_bin_fw_upload(sdiodev, RAM_FMAC_FW_ADDR,
				    fw_path)) {
		pr_err("aicbsp 8800d80 download wifi fw fail\n");
		return -1;
	}

	if (aicwifi_patch_config_8800d80(sdiodev)) {
		pr_err("aicbsp aicwifi_patch_config_8800d80 fail\n");
		return -1;
	}

	if (aicwifi_sys_config_8800d80(sdiodev)) {
		pr_err("aicbsp aicwifi_patch_config_8800d80 fail\n");
		return -1;
	}

	if (aicwifi_start_from_bootrom(sdiodev)) {
		pr_err("aicbsp 8800d80 wifi start fail\n");
		return -1;
	}
	return 0;
}

static int aic8800d80_driver_fw_init(struct aic_sdio_dev *sdiodev, u32 *btenable,
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
	if (is_chip_id_h || aicbsp_info->chip_rev != CHIP_REV_U02) {
		pr_err("aicbsp: AIC8800D80 U02 is required (revision %u)\n",
		       aicbsp_info->chip_rev);
		return -ENODEV;
	}

	*btenable = 0;
	*aicbsp_firmware_list = fw_8800d80_u02;
	if (aicbsp_system_config_8800d80(sdiodev))
		return -1;

	return 0;
}

/* ========== Ops instances ========== */

const struct aic_chip_ops aic_chip_aic8800d80_ops = {
	.name						= "AIC8800D80",
	.use_func_msg				= false,
	.use_sdiov3_func			= true,
	.need_flowctrl_mask			= false,
	.wakeup_reg_val				= 0x11,
	.use_flowctrl_msg			= true,
	.use_hdr_checksum			= true,
	.need_fix_hdr_len			= false,
	.need_func0_intr			= true,
	.use_func2					= false,
	.sdio_clock					= FEATURE_SDIO_CLOCK_V3,
	.wifi_init					= aic8800d80_wifi_init,
	.driver_fw_init				= aic8800d80_driver_fw_init,

};
