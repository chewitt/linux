/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_MAIN_H_
#define _RWNX_MAIN_H_

#include "rwnx_defs.h"
#include <linux/delay.h>

int rwnx_cfg80211_init(struct rwnx_plat *rwnx_plat, void **platform_data);
void rwnx_cfg80211_deinit(struct rwnx_hw *rwnx_hw);
int rwnx_ic_rf_init(struct rwnx_hw *rwnx_hw);
int rwnx_ic_system_init(struct rwnx_hw *rwnx_hw);

#ifdef CONFIG_AIC8800_REGION_PW
void aicwf_region_worker(struct work_struct *work);
#endif
#ifdef CONFIG_AIC8800_POWER_LIMIT
void aicwf_pwlt_worker(struct work_struct *work);
#endif

void rwnx_update_mesh_power_mode(struct rwnx_vif *vif);
void aicwf_p2p_alive_timeout(struct timer_list *t);
void apm_staloss_work_process(struct work_struct *work);
void apm_probe_sta_work_process(struct work_struct *work);
int rwnx_cfg80211_probe_client(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *peer, u64 *cookie);

extern u8 chip_sub_id;
extern u8 chip_mcu_id;
extern u8 chip_id;
extern struct ieee80211_sband_iftype_data rwnx_he_capa[1];

#define CHIP_ID_H_MASK 0xC0
#define IS_CHIP_ID_H() ((chip_id & CHIP_ID_H_MASK) == CHIP_ID_H_MASK)

#endif /* _RWNX_MAIN_H_ */
