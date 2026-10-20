/* SPDX-License-Identifier: MIT */
/* Shared Standard-DV transport checks. No hardware or pixel processing. */
#ifndef DRM_DVBRIDGE_H
#define DRM_DVBRIDGE_H
#include <linux/types.h>
#include <linux/errno.h>
#include "drm_dvbridge_edid.h"

static inline bool drm_dvbridge_edid(const u8 *edid, size_t size)
{
	return dvbridge_parse_edid(edid, size).status == DV_EDID_STANDARD;
}

struct drm_dvbridge_link {
	bool requested, lab_enabled, sink_standard, hdmi, native_hdmi;
	bool rgb, limited_range, hdr_blob, cloned, progressive;
	unsigned int requested_bpc, pipe_bpp, colorspace;
	unsigned int width, height, rounded_refresh;
};

/* CTA 1080p/2160p 24,25,30,50,60 Hz (including fractional clocks).
 * DV occupies the vendor slot, so the timing goes in AVI rather than the
 * legacy HDMI-1.4 4K vendor packet. Other modes remain out of this lab's scope.
 */
static inline unsigned int drm_dvbridge_avi_vic(unsigned int cea_vic)
{
	switch (cea_vic) {
        case 16: case 31: case 32: case 33: case 34:
        case 93: case 94: case 95: case 96: case 97:
            return cea_vic;
        default:
            return 0;
        }
}

static inline int drm_dvbridge_validate(const struct drm_dvbridge_link *s)
{
	if (!s->requested)
		return 0;
	if (!s->lab_enabled || !s->sink_standard || !s->native_hdmi)
		return -EOPNOTSUPP;
	if (!s->hdmi || !s->rgb || s->limited_range || s->hdr_blob || s->cloned ||
	    !s->progressive || s->requested_bpc != 8 || s->pipe_bpp != 24 ||
	    s->colorspace ||
        (s->rounded_refresh != 24 && s->rounded_refresh != 25 &&
         s->rounded_refresh != 30 && s->rounded_refresh != 50 &&
         s->rounded_refresh != 60))
		return -EINVAL;
	if (!((s->width == 1920 && s->height == 1080) ||
	      (s->width == 3840 && s->height == 2160)))
		return -EINVAL;
	return 0;
}

#endif
