/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * Per-compatible decoder capabilities and firmware.
 */

#ifndef __MESON_AMVDEC_PLATFORM_H_
#define __MESON_AMVDEC_PLATFORM_H_

#include "amvdec.h"

struct amvdec_format;

enum amvdec_revision {
	AMVDEC_REVISION_GXBB,
	AMVDEC_REVISION_GXL,
	AMVDEC_REVISION_GXLX,
	AMVDEC_REVISION_GXM,
	AMVDEC_REVISION_G12A,
	AMVDEC_REVISION_SM1,
};

struct amvdec_platform {
	const struct amvdec_format *formats;
	const u32 num_formats;
	enum amvdec_revision revision;
};

extern const struct amvdec_platform meson_amvdec_platform_gxbb;
extern const struct amvdec_platform meson_amvdec_platform_gxm;
extern const struct amvdec_platform meson_amvdec_platform_gxl;
extern const struct amvdec_platform meson_amvdec_platform_gxlx;
extern const struct amvdec_platform meson_amvdec_platform_g12a;
extern const struct amvdec_platform meson_amvdec_platform_sm1;

#endif
