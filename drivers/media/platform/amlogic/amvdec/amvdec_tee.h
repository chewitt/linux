/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __MESON_AMVDEC_TEE_H_
#define __MESON_AMVDEC_TEE_H_

#include "amvdec.h"

#define AMVDEC_TEE_FW_H264_MULTI	15
#define AMVDEC_TEE_FW_HEVC_MMU		17
#define AMVDEC_TEE_FW_VP9_MMU		19
/* Tagged index of the HEVC MMU microcode with swapped-in sections */
#define AMVDEC_TEE_FW_HEVC_MMU_SWAP	0x32363553
/* Tagged index of the multi-instance MPEG-1/2 microcode */
#define AMVDEC_TEE_FW_MPEG12_MULTI	0x4d31324d

#define AMVDEC_TEE_CORE_VDEC		0
#define AMVDEC_TEE_CORE_HEVC		2

int meson_amvdec_tee_load(struct amvdec_core *core, u32 format, u32 tee_core,
			  bool swap);

#endif
