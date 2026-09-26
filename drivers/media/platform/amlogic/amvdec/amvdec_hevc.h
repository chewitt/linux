/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 *
 * VDEC_HEVC hardware control for HEVC and VP9.
 */

#ifndef __MESON_AMVDEC_VDEC_HEVC_H_
#define __MESON_AMVDEC_VDEC_HEVC_H_

#include "amvdec.h"

extern struct amvdec_ops meson_amvdec_hevc_ops;

int meson_amvdec_hevc_restream(struct amvdec_session *sess);

#endif
