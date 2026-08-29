/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __AIC_BSP_EXPORT_H
#define __AIC_BSP_EXPORT_H

#include <linux/types.h>

enum aicbsp_subsys {
	AIC_BLUETOOTH,
	AIC_WIFI,
};

enum aicbsp_pwr_state {
	AIC_PWR_OFF,
	AIC_PWR_ON,
};

enum skb_buff_id {
	AIC_RESV_MEM_TXDATA,
};

struct skb_buff_pool {
	u32 id;
	u32 size;
	const char *name;
	u8 used;
	struct sk_buff *skb;
};

struct aicbsp_feature_t {
	int hwinfo;
	u32 sdio_clock;
	u8 sdio_phase;
	bool fwlog_en;
	u8 irqf;
	u8 cpmode;
	int adapt;
};

#ifdef CONFIG_DPD
struct rf_misc_ram_t {
	u32 bit_mask[3];
	u32 reserved;
	u32 dpd_high[96];
	u32 dpd_11b[96];
	u32 dpd_low[96];
	u32 idac_11b[48];
	u32 idac_high[48];
	u32 idac_low[48];
	u32 loft_res[18];
	u32 rx_iqim_res[16];
};

struct rf_misc_ram_lite_t {
	u32 bit_mask[4];
	u32 dpd_high[96];
	u32 loft_res[18];
};

#define MEMBER_SIZE(type, member) sizeof_field(type, member)
#define DPD_RESULT_SIZE_8800DC    sizeof(struct rf_misc_ram_lite_t)

extern struct rf_misc_ram_lite_t dpd_res;
#endif

int aicbsp_set_subsys(int, int);
int aicbsp_get_feature(struct aicbsp_feature_t *feature);
struct sk_buff *aicbsp_resv_mem_alloc_skb(unsigned int length, uint32_t id);
void aicbsp_resv_mem_kfree_skb(struct sk_buff *skb, uint32_t id);

#endif
