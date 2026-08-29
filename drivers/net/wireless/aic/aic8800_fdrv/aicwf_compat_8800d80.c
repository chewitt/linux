// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief 8800d80 compatibility configuration
 *
 ******************************************************************************
 */

#include "aicwf_compat_8800d80.h"
#include "rwnx_platform.h"

#define FW_USERCONFIG_NAME_8800D80 "aic_userconfig_8800d80.txt"
#define FW_POWERLIMIT_NAME_8800D80 "aic_powerlimit_8800d80.txt"

int aicwf_set_rf_config_8800d80(struct rwnx_hw *rwnx_hw,
				struct mm_set_rf_calib_cfm *cfm)
{
	int ret = 0;

	ret = rwnx_send_txpwr_lvl_v3_req(rwnx_hw, 0);
	if (ret)
		return -1;

	ret = rwnx_send_txpwr_lvl_adj_req(rwnx_hw);
	if (ret)
		return -1;

	ret = rwnx_send_txpwr_ofst2x_req(rwnx_hw);
	if (ret)
		return -1;

	ret = rwnx_send_rf_calib_req(rwnx_hw, cfm);
	if (ret)
		return -1;

	return 0;
}

int rwnx_plat_userconfig_load_8800d80(struct rwnx_hw *rwnx_hw)
{
	int size;
	u32 *dst = NULL;
	char *filename = FW_USERCONFIG_NAME_8800D80;

	AICWFDBG(LOGINFO, "userconfig file path:%s \r\n", filename);

	/* load file */
	size = rwnx_request_firmware_common(rwnx_hw, &dst, filename);
	if (size <= 0) {
		AICWFDBG(LOGERROR, "wrong size of firmware file\n");
		dst = NULL;
		return 0;
	}

	/* Copy the file on the Embedded side */
	AICWFDBG(LOGINFO, "### Load file done: %s, size=%d\n", filename, size);

	rwnx_plat_userconfig_parsing3((char *)dst, size);

	rwnx_release_firmware_common(&dst);

	AICWFDBG(LOGINFO, "userconfig download complete\n\n");
	return 0;
}

#ifdef CONFIG_AIC8800_POWER_LIMIT
int rwnx_plat_powerlimit_load_8800d80(struct rwnx_hw *rwnx_hw)
{
	int size;
	u32 *dst = NULL;
	char *filename = FW_POWERLIMIT_NAME_8800D80;

	AICWFDBG(LOGDEBUG, "powerlimit file path:%s \r\n", filename);

	/* load file */
	size = rwnx_request_firmware_common(rwnx_hw, &dst, filename);
	if (size <= 0) {
		AICWFDBG(LOGERROR, "wrong size of cfg file\n");
		dst = NULL;
		return 0;
	}

	AICWFDBG(LOGDEBUG, "### Load file done: %s, size=%d\n", filename, size);

	/* parsing the file */
	rwnx_plat_powerlimit_parsing((char *)dst, size, rwnx_hw->country_abbr);

	rwnx_release_firmware_common(&dst);

	AICWFDBG(LOGDEBUG, "powerlimit download complete\n\n");
	return 0;
}
#endif
