/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 AIC semiconductor.
 *
 * @file aic_chip_ops.h
 * @brief Chip-specific operation interfaces for AIC8800 series.
 */
#ifndef _AIC_BSP_CHIP_OPS_H_
#define _AIC_BSP_CHIP_OPS_H_

#include <linux/types.h>

struct aic_sdio_dev;
struct aicbsp_info_t;
struct aicbt_patch_info_t;
struct aicbt_patch_table;
struct aicbsp_firmware;

/* Match SDIO vendor/device IDs to a chip product ID */
int aicwf_sdio_chipmatch(u16 vendor, u16 device, u16 *chipid);

/*
 * struct aic_chip_ops - Chip-specific operations
 *
 * Each chip variant provides an instance of this struct.
 * Selected at probe time based on chipid, then used to
 * eliminate chipid-based if-else branching throughout the driver.
 */
struct aic_chip_ops {
	const char *name;
	const bool use_func_msg;
	const bool use_sdiov3_func;
	const bool need_flowctrl_mask;
	const u8 wakeup_reg_val;
	const bool use_flowctrl_msg;
	const bool use_hdr_checksum;
	const bool need_fix_hdr_len;
	const bool need_func0_intr;
	const bool use_func2;
	const u32  sdio_clock;

	int (*wifi_init)(struct aic_sdio_dev *sdiodev, int testmode);

	int (*set_patch_info)(struct aic_sdio_dev *sdiodev,
			      struct aicbt_patch_info_t *patch_info,
			      struct aicbsp_info_t *aicbsp_info,
			      struct aicbt_patch_table *head);

	int (*driver_fw_init)(struct aic_sdio_dev *sdiodev, u32 *btenable,
			      struct aicbsp_info_t *aicbsp_info,
			      const struct aicbsp_firmware **aicbsp_firmware_list);
};

extern const struct aic_chip_ops aic_chip_aic8800d80_ops;

/* Select the appropriate chip ops based on chip product ID */
const struct aic_chip_ops *aic_chip_ops_select(u16 chipid);

/* Wrapper function declarations (implemented in aic_chip_ops.c) */
int aic_chip_wifi_init(struct aic_sdio_dev *sdiodev, int testmode);
int aic_chip_set_patch_info(struct aic_sdio_dev *sdiodev,
			    struct aicbt_patch_info_t *patch_info,
			    struct aicbsp_info_t *aicbsp_info,
			    struct aicbt_patch_table *head);
int aic_chip_driver_fw_init(struct aic_sdio_dev *sdiodev, u32 *btenable,
			    struct aicbsp_info_t *aicbsp_info,
			    const struct aicbsp_firmware **aicbsp_firmware_list);

#endif /* _AIC_BSP_CHIP_OPS_H_ */
