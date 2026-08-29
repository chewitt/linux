/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AICWF_COMPAT_8800DC_H_
#define _AICWF_COMPAT_8800DC_H_

#include "aic_bsp_export.h"
#include <linux/types.h>

#ifdef CONFIG_DPD
int aicwf_fdrv_dpd_result_apply_8800dc(struct rwnx_hw *rwnx_hw,
				       struct rf_misc_ram_lite_t *dpd_res);
#ifndef CONFIG_FORCE_DPD_CALIB
int aicwf_fdrv_dpd_result_load_8800dc(struct rwnx_hw *rwnx_hw,
				      struct rf_misc_ram_lite_t *dpd_res);
#endif
#endif
int aicwf_fdrv_misc_ram_init_8800dc(struct rwnx_hw *rwnx_hw);
int aicwf_set_rf_config_8800dc(struct rwnx_hw *rwnx_hw,
			       struct mm_set_rf_calib_cfm *cfm);
int rwnx_plat_userconfig_load_8800dc(struct rwnx_hw *rwnx_hw);
int rwnx_plat_userconfig_load_8800dw(struct rwnx_hw *rwnx_hw);

#endif /* _AICWF_COMPAT_8800DC_H_ */
