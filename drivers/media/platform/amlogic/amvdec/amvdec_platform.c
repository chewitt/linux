// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * Per-compatible decoder capabilities and firmware.
 */

#include "amvdec_platform.h"
#include "amvdec.h"

#include "amvdec_1.h"
#include "amvdec_hevc.h"

/*
 * GXBB: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxbb[] = {
};

/*
 * GXL: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 and VP9
 * profiles 0/2 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxl[] = {
};

/*
 * GXLX: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxlx[] = {
};

/*
 * GXM: H.264 and MPEG-2 at 1920x1088; HEVC Main/Main10 and VP9
 * profiles 0/2 at 3840x2160.
 */
static const struct amvdec_format amvdec_formats_gxm[] = {
};

/*
 * G12A/G12B: H.264, HEVC Main/Main10 and VP9 profiles 0/2 at
 * 3840x2160; MPEG-2 at 1920x1088.
 */
static const struct amvdec_format amvdec_formats_g12a[] = {
};

/*
 * SM1: H.264, HEVC Main/Main10 and VP9 profiles 0/2 at 3840x2160;
 * MPEG-2 at 1920x1088.
 */
static const struct amvdec_format amvdec_formats_sm1[] = {
};

const struct amvdec_platform meson_amvdec_platform_gxbb = {
	.formats = amvdec_formats_gxbb,
	.num_formats = ARRAY_SIZE(amvdec_formats_gxbb),
	.revision = AMVDEC_REVISION_GXBB,
};

const struct amvdec_platform meson_amvdec_platform_gxl = {
	.formats = amvdec_formats_gxl,
	.num_formats = ARRAY_SIZE(amvdec_formats_gxl),
	.revision = AMVDEC_REVISION_GXL,
};

const struct amvdec_platform meson_amvdec_platform_gxlx = {
	.formats = amvdec_formats_gxlx,
	.num_formats = ARRAY_SIZE(amvdec_formats_gxlx),
	.revision = AMVDEC_REVISION_GXLX,
};

const struct amvdec_platform meson_amvdec_platform_gxm = {
	.formats = amvdec_formats_gxm,
	.num_formats = ARRAY_SIZE(amvdec_formats_gxm),
	.revision = AMVDEC_REVISION_GXM,
};

const struct amvdec_platform meson_amvdec_platform_g12a = {
	.formats = amvdec_formats_g12a,
	.num_formats = ARRAY_SIZE(amvdec_formats_g12a),
	.revision = AMVDEC_REVISION_G12A,
};

const struct amvdec_platform meson_amvdec_platform_sm1 = {
	.formats = amvdec_formats_sm1,
	.num_formats = ARRAY_SIZE(amvdec_formats_sm1),
	.revision = AMVDEC_REVISION_SM1,
};

