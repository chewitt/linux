/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __MESON_ENCODER_HDMI_H
#define __MESON_ENCODER_HDMI_H

struct drm_atomic_commit;

int meson_encoder_hdmi_probe(struct meson_drm *priv);
void meson_encoder_hdmi_update_hdr(struct meson_drm *priv,
				   struct drm_atomic_commit *state);
void meson_encoder_hdmi_remove(struct meson_drm *priv);

#endif /* __MESON_ENCODER_HDMI_H */
