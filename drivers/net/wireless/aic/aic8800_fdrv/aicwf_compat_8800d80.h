/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AICWF_COMPAT_8800D80_H_
#define _AICWF_COMPAT_8800D80_H_
#include "reg_access.h"
#include "rwnx_main.h"
#include "rwnx_msg_tx.h"
#include <linux/types.h>

int aicwf_set_rf_config_8800d80(struct rwnx_hw *rwnx_hw,
				struct mm_set_rf_calib_cfm *cfm);
int rwnx_plat_userconfig_load_8800d80(struct rwnx_hw *rwnx_hw);
#ifdef CONFIG_AIC8800_POWER_LIMIT
int rwnx_plat_powerlimit_load_8800d80(struct rwnx_hw *rwnx_hw);
#endif

#endif
