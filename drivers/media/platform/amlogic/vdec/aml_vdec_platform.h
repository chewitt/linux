/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 */

#ifndef AML_VDEC_PLATFORM_H_
#define AML_VDEC_PLATFORM_H_

#include <linux/videodev2.h>
#include "aml_vdec_drv.h"

#define MAX_DEC_FORMAT 3

/**
 * struct aml_codec_ops - codec mode specific operations
 * @init: Used for decoder initialization.
 * @exit: If needed, can be used to undo the .init phase.
 * @run: Start a single decoding job. Called from atomic context.
 * Caller should ensure that a pair of buffers is ready and the
 * hardware is powered on and clk is enabled. Returns zero if OK,
 * a negative value in error cases.
 */
struct aml_codec_ops {
	int (*init)(void *priv);
	void (*exit)(void *priv);
	int (*run)(void *priv);
};

/**
 * struct aml_dev_platform_data - compatible data for each chip.
 * @dec_fmt: Support dec format.
 * @codec_ops: Codec operation function.
 * @req_hw_resource: Operation function to request the hardware resource.
 * @destroy_hw_resource: Operation function to release the hardware resource.
 * @power_type: Type of power that the current chip need. See aml_power_type_e.
 * @fw_path: Path of the firmware.bin.
 */
struct aml_dev_platform_data {
	const struct aml_codec_ops *codec_ops;
	const struct aml_video_fmt *dec_fmt;
	int num_fmts;
	int (*req_hw_resource)(void *priv);
	void (*destroy_hw_resource)(void *priv);
	int power_type;
	const char *fw_path[MAX_DEC_FORMAT];
};

extern const struct aml_dev_platform_data aml_vdec_s4_pdata;

#endif
