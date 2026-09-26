/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * Elementary-stream input and request scheduling.
 */

#ifndef __MESON_AMVDEC_ESPARSER_H_
#define __MESON_AMVDEC_ESPARSER_H_

#include <linux/platform_device.h>

#include "amvdec.h"

int meson_amvdec_esparser_init(struct platform_device *pdev, struct amvdec_core *core);
void meson_amvdec_esparser_quiesce(struct amvdec_core *core);
int meson_amvdec_esparser_power_up(struct amvdec_session *sess);

/* Feed the next scheduled request from worker context. */
void meson_amvdec_esparser_queue_all_src(struct work_struct *work);

#define ESPARSER_SEARCH_PATTERN_SIZE SZ_512

#endif
