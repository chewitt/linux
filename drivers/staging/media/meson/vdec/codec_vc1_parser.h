/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __MESON_VDEC_CODEC_VC1_PARSER_H_
#define __MESON_VDEC_CODEC_VC1_PARSER_H_

#include <linux/types.h>

struct vc1_stream {
	bool sequence_valid;
	bool entry_valid;
	u8 postproc, broadcast, interlace, tfcntr, finterp;
	u8 panscan, extended_mv, dquant, vstransform, quantizer;
};

int vc1_prepare_stream(struct vc1_stream *stream, u8 *data, u32 *length,
		       u32 capacity, u32 width, u32 height);

int vc1_sequence_header(const u8 *data, u32 length, u32 *width, u32 *height);

int vc1_flush_picture(const struct vc1_stream *stream, u8 *data, u32 height);

#endif
