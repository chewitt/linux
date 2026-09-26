/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Gus Bourg <gus@bourg.net>
 *
 * HEVC short-term reference-picture-set resolution.
 */

#ifndef __MESON_AMVDEC_HEVC_RPS_H
#define __MESON_AMVDEC_HEVC_RPS_H

#define HEVC_RPS_MAX_SETS 64
#define HEVC_RPS_MAX_REFS 16

struct hevc_rps {
	int delta[HEVC_RPS_MAX_REFS];
	u16 used;
	u8 negative;
	u8 positive;
};

/* Insert in syntax order: negative closest first, then positive closest first.
 * The firmware's packed RPS records carry a signed 14-bit delta.
 */
static inline int hevc_rps_insert(struct hevc_rps *r, int delta, bool used)
{
	unsigned int n = r->negative + r->positive, at = 0, i;

	if (!delta || delta < -8192 || delta > 8191 || n >= HEVC_RPS_MAX_REFS)
		return -EINVAL;
	while (at < n) {
		int old = r->delta[at];

		if (delta == old)
			return -EINVAL;
		if ((delta < 0 && (old > 0 || delta > old)) ||
		    (delta > 0 && old > 0 && delta < old))
			break;
		at++;
	}
	for (i = n; i > at; i--) {
		r->delta[i] = r->delta[i - 1];
		if (r->used & (1U << (i - 1)))
			r->used |= 1U << i;
		else
			r->used &= ~(1U << i);
	}
	r->delta[at] = delta;
	r->used = (r->used & ~(1U << at)) | ((unsigned int)used << at);
	if (delta < 0)
		r->negative++;
	else
		r->positive++;
	return 0;
}

/*
 * Resolve SPS reference sets in syntax order. Prediction uses the preceding
 * set; delta_idx_minus1 applies only to slice RPS. Ignore inferred bits
 * outside the signaled candidate count.
 */
static inline int hevc_rps_resolve(const struct v4l2_ctrl_hevc_ext_sps_st_rps *src,
				   unsigned int count, struct hevc_rps *out)
{
	unsigned int i, j;

	if (count > HEVC_RPS_MAX_SETS || (count && (!src || !out)))
		return -EINVAL;
	for (i = 0; i < count; i++) {
		const struct v4l2_ctrl_hevc_ext_sps_st_rps *s = src + i;
		struct hevc_rps *r = out + i;
		int ret, delta;

		memset(r, 0, sizeof(*r));
		if (s->flags & ~V4L2_HEVC_EXT_SPS_ST_RPS_FLAG_INTER_REF_PIC_SET_PRED ||
		    s->num_negative_pics + s->num_positive_pics > HEVC_RPS_MAX_REFS)
			return -EINVAL;
		if (s->flags & V4L2_HEVC_EXT_SPS_ST_RPS_FLAG_INTER_REF_PIC_SET_PRED) {
			const struct hevc_rps *prev;
			unsigned int n;

			if (!i || s->delta_idx_minus1 || s->delta_rps_sign > 1)
				return -EINVAL;
			prev = out + i - 1;
			n = prev->negative + prev->positive;
			delta = (int)s->abs_delta_rps_minus1 + 1;
			if (s->delta_rps_sign)
				delta = -delta;
			for (j = 0; j <= n; j++) {
				if (!((s->used_by_curr_pic | s->use_delta_flag) & (1U << j)))
					continue;
				ret = hevc_rps_insert(r, delta + (j < n ? prev->delta[j] : 0),
						      !!(s->used_by_curr_pic & (1U << j)));
				if (ret)
					return ret;
			}
		} else {
			delta = 0;
			for (j = 0; j < s->num_negative_pics; j++) {
				delta -= (int)s->delta_poc_s0_minus1[j] + 1;
				ret = hevc_rps_insert(r, delta,
						      !!(s->used_by_curr_pic & (1U << j)));
				if (ret)
					return ret;
			}
			delta = 0;
			for (j = 0; j < s->num_positive_pics; j++) {
				delta += (int)s->delta_poc_s1_minus1[j] + 1;
				ret = hevc_rps_insert(r, delta,
						      !!(s->used_by_curr_pic &
						 (1U << (j + s->num_negative_pics))));
				if (ret)
					return ret;
			}
		}
		if (r->negative != s->num_negative_pics || r->positive != s->num_positive_pics)
			return -EINVAL;
	}
	return 0;
}
#endif
