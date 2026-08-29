/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AIC8800D80_COMPAT_H_
#define _AIC8800D80_COMPAT_H_

#include "aicsdio.h"

int aicbsp_system_config_8800d80(struct aic_sdio_dev *sdiodev);
int aicwifi_sys_config_8800d80(struct aic_sdio_dev *sdiodev);
int aicwifi_patch_config_8800d80(struct aic_sdio_dev *sdiodev);

#endif
