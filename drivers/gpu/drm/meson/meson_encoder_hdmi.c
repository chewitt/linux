// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include <media/cec-notifier.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <linux/media-bus-format.h>
#include <linux/videodev2.h>

#include "meson_drv.h"
#include "meson_registers.h"
#include "meson_vclk.h"
#include "meson_venc.h"
#include "meson_encoder_hdmi.h"

struct meson_encoder_hdmi {
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_connector *connector;
	struct meson_drm *priv;
	unsigned long output_bus_fmt;
	struct cec_notifier *cec_notifier;
};

#define bridge_to_meson_encoder_hdmi(x) \
	container_of(x, struct meson_encoder_hdmi, bridge)

static int meson_encoder_hdmi_attach(struct drm_bridge *bridge,
				     struct drm_encoder *encoder,
				     enum drm_bridge_attach_flags flags)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);

	return drm_bridge_attach(encoder, encoder_hdmi->bridge.next_bridge,
				 &encoder_hdmi->bridge, flags);
}

static void meson_encoder_hdmi_detach(struct drm_bridge *bridge)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);

	cec_notifier_conn_unregister(encoder_hdmi->cec_notifier);
	encoder_hdmi->cec_notifier = NULL;
}

/*
 * Bus-format classification.  The VENC->HDMI-TX bus is 3x10-bit
 * natively; what varies per format is the chroma layout and the wire
 * (TMDS) depth dw-hdmi will serialise at.
 */
static bool meson_encoder_hdmi_fmt_is_420(u32 fmt)
{
	return fmt == MEDIA_BUS_FMT_UYYVYY8_0_5X24 ||
	       fmt == MEDIA_BUS_FMT_UYYVYY10_0_5X30;
}

static bool meson_encoder_hdmi_fmt_is_422(u32 fmt)
{
	return fmt == MEDIA_BUS_FMT_UYVY8_1X16 ||
	       fmt == MEDIA_BUS_FMT_UYVY10_1X20;
}

static unsigned int meson_encoder_hdmi_fmt_depth(u32 fmt)
{
	switch (fmt) {
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		return 10;
	case MEDIA_BUS_FMT_YUV12_1X36:
		return 12;
	default:
		return 8;
	}
}

/*
 * TMDS (PHY bit) clock from the pixel clock and the negotiated bus
 * format, mirroring dw-hdmi's hdmi_av_composer(): YUV422 carries deep
 * color in the 8-bit-per-component stream (no clock increase); other
 * formats scale by depth/8.
 */
static unsigned long long
meson_encoder_hdmi_phy_freq(unsigned long long vclk_freq, u32 fmt)
{
	unsigned int depth = meson_encoder_hdmi_fmt_depth(fmt);

	if (meson_encoder_hdmi_fmt_is_422(fmt) || depth == 8)
		return vclk_freq * 10;

	return DIV_ROUND_CLOSEST_ULL(vclk_freq * 10 * depth, 8);
}

static void meson_encoder_hdmi_set_vclk(struct meson_encoder_hdmi *encoder_hdmi,
					const struct drm_display_mode *mode)
{
	struct meson_drm *priv = encoder_hdmi->priv;
	int vic = drm_match_cea_mode(mode);
	unsigned long long phy_freq;
	unsigned long long vclk_freq;
	unsigned long long venc_freq;
	unsigned long long hdmi_freq;

	vclk_freq = mode->clock * 1000ULL;

	/* For 420, pixel clock is half unlike venc clock */
	if (meson_encoder_hdmi_fmt_is_420(encoder_hdmi->output_bus_fmt))
		vclk_freq /= 2;

	/* TMDS clock is pixel_clock * 10 (* depth/8 for deep color) */
	phy_freq = meson_encoder_hdmi_phy_freq(vclk_freq,
					       encoder_hdmi->output_bus_fmt);

	if (!vic) {
		meson_vclk_setup(priv, MESON_VCLK_TARGET_DMT, phy_freq,
				 vclk_freq, vclk_freq, vclk_freq, false);
		return;
	}

	/* 480i/576i needs global pixel doubling */
	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		vclk_freq *= 2;

	venc_freq = vclk_freq;
	hdmi_freq = vclk_freq;

	/* VENC double pixels for 1080i, 720p and YUV420 modes */
	if (meson_venc_hdmi_venc_repeat(vic) ||
	    meson_encoder_hdmi_fmt_is_420(encoder_hdmi->output_bus_fmt))
		venc_freq *= 2;

	vclk_freq = max(venc_freq, hdmi_freq);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		venc_freq /= 2;

	dev_dbg(priv->dev,
		"phy:%lluHz vclk=%lluHz venc=%lluHz hdmi=%lluHz enci=%d\n",
		phy_freq, vclk_freq, venc_freq, hdmi_freq,
		priv->venc.hdmi_use_enci);

	meson_vclk_setup(priv, MESON_VCLK_TARGET_HDMI, phy_freq, vclk_freq,
			 venc_freq, hdmi_freq, priv->venc.hdmi_use_enci);
}

static enum drm_mode_status meson_encoder_hdmi_mode_valid(struct drm_bridge *bridge,
					const struct drm_display_info *display_info,
					const struct drm_display_mode *mode)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	struct meson_drm *priv = encoder_hdmi->priv;
	bool is_hdmi2_sink = display_info->hdmi.scdc.supported;
	unsigned long long clock = mode->clock * 1000ULL;
	unsigned long long phy_freq;
	unsigned long long vclk_freq;
	unsigned long long venc_freq;
	unsigned long long hdmi_freq;
	int vic = drm_match_cea_mode(mode);
	enum drm_mode_status status;

	dev_dbg(priv->dev, "Modeline " DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));

	/* If sink does not support 540MHz, reject the non-420 HDMI2 modes */
	if (display_info->max_tmds_clock &&
	    mode->clock > display_info->max_tmds_clock &&
	    !drm_mode_is_420_only(display_info, mode) &&
	    !drm_mode_is_420_also(display_info, mode))
		return MODE_BAD;

	/* Check against non-VIC supported modes */
	if (!vic) {
		status = meson_venc_hdmi_supported_mode(mode);
		if (status != MODE_OK)
			return status;

		return meson_vclk_dmt_supported_freq(priv, clock);
	/* Check against supported VIC modes */
	} else if (!meson_venc_hdmi_supported_vic(vic))
		return MODE_BAD;

	vclk_freq = clock;

	/* For 420, pixel clock is half unlike venc clock */
	if (drm_mode_is_420_only(display_info, mode) ||
	    (!is_hdmi2_sink &&
	     drm_mode_is_420_also(display_info, mode)))
		vclk_freq /= 2;

	/* TMDS clock is pixel_clock * 10 */
	phy_freq = vclk_freq * 10;

	/* 480i/576i needs global pixel doubling */
	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		vclk_freq *= 2;

	venc_freq = vclk_freq;
	hdmi_freq = vclk_freq;

	/* VENC double pixels for 1080i, 720p and YUV420 modes */
	if (meson_venc_hdmi_venc_repeat(vic) ||
	    drm_mode_is_420_only(display_info, mode) ||
	    (!is_hdmi2_sink &&
	     drm_mode_is_420_also(display_info, mode)))
		venc_freq *= 2;

	vclk_freq = max(venc_freq, hdmi_freq);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		venc_freq /= 2;

	dev_dbg(priv->dev,
		"%s: vclk:%lluHz phy=%lluHz venc=%lluHz hdmi=%lluHz\n",
		__func__, phy_freq, vclk_freq, venc_freq, hdmi_freq);

	return meson_vclk_vic_supported_freq(priv, phy_freq, vclk_freq);
}

static void meson_encoder_hdmi_atomic_enable(struct drm_bridge *bridge,
					     struct drm_atomic_commit *state)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	unsigned int ycrcb_map = VPU_HDMI_OUTPUT_CBYCR;
	struct meson_drm *priv = encoder_hdmi->priv;
	struct drm_connector_state *conn_state;
	const struct drm_display_mode *mode;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	bool yuv420_mode = false;
	int vic;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	mode = &crtc_state->adjusted_mode;

	vic = drm_match_cea_mode(mode);

	dev_dbg(priv->dev, "\"%s\" vic %d\n", mode->name, vic);

	if (meson_encoder_hdmi_fmt_is_420(encoder_hdmi->output_bus_fmt)) {
		ycrcb_map = VPU_HDMI_OUTPUT_CRYCB;
		yuv420_mode = true;
	} else if (meson_encoder_hdmi_fmt_is_422(encoder_hdmi->output_bus_fmt))
		ycrcb_map = VPU_HDMI_OUTPUT_CRYCB;

	/* VENC + VENC-DVI Mode setup */
	meson_venc_hdmi_mode_set(priv, vic, ycrcb_map, yuv420_mode, mode);

	/* VCLK Set clock */
	meson_encoder_hdmi_set_vclk(encoder_hdmi, mode);

	/*
	 * VPU_HDMI_FMT_CTRL (per the vendor hdmitx driver):
	 *   [1:0] chroma format (0=444, 1=422, 2=420)
	 *   [3:2] chroma_dnsmp (2 = average)
	 *   [4]   12->10 dither enable
	 *   [10]  12->10 rounding enable
	 * VPU_HDMI_DITH_CNTL:
	 *   [4]   10->8 dither enable
	 *   [3:2] hsync/vsync flags (moved here from VPU_HDMI_SETTING
	 *         [3:2] when the dither block is in the data path)
	 *
	 * 8-bit wire: round 12->10 and DITHER 10->8 (the vendor default;
	 * mainline used to truncate, which is where 8-bit banding came
	 * from).  Deep color wire: everything off, pass 10/12 bits
	 * through untouched.
	 */
	{
		unsigned int depth =
			meson_encoder_hdmi_fmt_depth(encoder_hdmi->output_bus_fmt);
		u32 fmt;

		if (meson_encoder_hdmi_fmt_is_420(encoder_hdmi->output_bus_fmt))
			fmt = 2;
		else if (meson_encoder_hdmi_fmt_is_422(encoder_hdmi->output_bus_fmt))
			fmt = 1;
		else
			fmt = 0;

		if (depth > 8) {
			u32 hs_flag;

			writel_relaxed(fmt | (2 << 2),
				       priv->io_base + _REG(VPU_HDMI_FMT_CTRL));
			/* sync flags ride the dither block in this mode */
			hs_flag = (readl_relaxed(priv->io_base +
					_REG(VPU_HDMI_SETTING)) >> 2) & 0x3;
			writel_bits_relaxed(0x3 << 2, 0,
					priv->io_base + _REG(VPU_HDMI_SETTING));
			writel_bits_relaxed(BIT(4) | (0x3 << 2), hs_flag << 2,
					priv->io_base + _REG(VPU_HDMI_DITH_CNTL));
		} else {
			writel_relaxed(fmt | (2 << 2) | BIT(10),
				       priv->io_base + _REG(VPU_HDMI_FMT_CTRL));
			writel_bits_relaxed(BIT(4) | (0x3 << 2), BIT(4),
					priv->io_base + _REG(VPU_HDMI_DITH_CNTL));
		}
	}

	dev_dbg(priv->dev, "%s\n", priv->venc.hdmi_use_enci ? "VENCI" : "VENCP");

	if (priv->venc.hdmi_use_enci)
		writel_relaxed(1, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(1, priv->io_base + _REG(ENCP_VIDEO_EN));
}

static void meson_encoder_hdmi_atomic_disable(struct drm_bridge *bridge,
					      struct drm_atomic_commit *state)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	struct meson_drm *priv = encoder_hdmi->priv;

	writel_bits_relaxed(0x3, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));

	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));
}

static const u32 meson_encoder_hdmi_out_bus_fmts[] = {
	MEDIA_BUS_FMT_YUV8_1X24,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_UYYVYY8_0_5X24,
	MEDIA_BUS_FMT_YUV10_1X30,
	MEDIA_BUS_FMT_UYVY10_1X20,
	MEDIA_BUS_FMT_UYYVYY10_0_5X30,
	MEDIA_BUS_FMT_YUV12_1X36,
};

static u32 *
meson_encoder_hdmi_get_inp_bus_fmts(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					u32 output_fmt,
					unsigned int *num_input_fmts)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	u32 *input_fmts = NULL;
	int i;

	*num_input_fmts = 0;

	/*
	 * Deep color is implemented (PLL m/frac cases, analog band
	 * parameters, VPU dither block) for G12A-family only; on older
	 * SoCs the clock code would program a dead PLL.
	 */
	if (meson_encoder_hdmi_fmt_depth(output_fmt) > 8 &&
	    !meson_vpu_is_compatible(encoder_hdmi->priv, VPU_COMPATIBLE_G12A))
		return NULL;

	/*
	 * Deep-color wire formats are offered only when:
	 * - the mode is a CEA/VIC mode (the CEA frame phase and the
	 *   clock tree entries are what we validated; PC/DMT sinks
	 *   frequently accept RGB/YCbCr-8 only at their native modes);
	 * - the deeper TMDS character rate stays at or below BOTH the
	 *   340 MHz no-scramble ceiling and the sink's declared TMDS
	 *   limit.  Scrambled deep-color combinations (e.g. 4K60
	 *   YUV420 @ 10-bit = 371.25 MHz) negotiate cleanly but were
	 *   never validated against real silicon+sink; a sink that
	 *   accepts the mode at 8-bit then shows NO SIGNAL at depth.
	 * - (non-422) this exact mode has a clock-tree entry at the
	 *   deeper TMDS rate; otherwise the bridge chain would
	 *   negotiate a depth the modeset cannot deliver.  422 carries
	 *   deep color at the 8-bit TMDS rate and needs no clock gate.
	 */
	if (meson_encoder_hdmi_fmt_depth(output_fmt) > 8) {
		const struct drm_display_mode *mode = &crtc_state->adjusted_mode;
		const struct drm_display_info *info =
			&conn_state->connector->display_info;
		unsigned long long vclk_freq = mode->clock * 1000ULL;
		unsigned long long phy_freq;
		unsigned long long tmds_khz;
		unsigned int max_tmds_khz = 340000;
		int vic = drm_match_cea_mode(mode);

		if (!vic)
			return NULL;

		if (meson_encoder_hdmi_fmt_is_420(output_fmt)) {
			/* pixel clock halves; the vclk table entry keeps
			 * the full venc rate (see mode_valid's math)
			 */
			phy_freq = meson_encoder_hdmi_phy_freq(vclk_freq / 2,
							       output_fmt);
		} else {
			phy_freq = meson_encoder_hdmi_phy_freq(vclk_freq,
							       output_fmt);
		}

		/* phy_freq is the TMDS bit rate (character rate x10) */
		tmds_khz = div_u64(phy_freq, 10000);
		if (info->max_tmds_clock &&
		    info->max_tmds_clock < max_tmds_khz)
			max_tmds_khz = info->max_tmds_clock;
		if (tmds_khz > max_tmds_khz)
			return NULL;

		if (!meson_encoder_hdmi_fmt_is_422(output_fmt) &&
		    meson_vclk_vic_supported_freq(encoder_hdmi->priv,
						  phy_freq,
						  vclk_freq) != MODE_OK)
			return NULL;
	}

	for (i = 0 ; i < ARRAY_SIZE(meson_encoder_hdmi_out_bus_fmts) ; ++i) {
		if (output_fmt == meson_encoder_hdmi_out_bus_fmts[i]) {
			*num_input_fmts = 1;
			input_fmts = kcalloc(*num_input_fmts,
					     sizeof(*input_fmts),
					     GFP_KERNEL);
			if (!input_fmts)
				return NULL;

			input_fmts[0] = output_fmt;

			break;
		}
	}

	return input_fmts;
}

static int meson_encoder_hdmi_atomic_check(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(conn_state->state, conn_state->connector);
	struct meson_drm *priv = encoder_hdmi->priv;

	encoder_hdmi->output_bus_fmt = bridge_state->output_bus_cfg.format;

	dev_dbg(priv->dev, "output_bus_fmt %lx\n", encoder_hdmi->output_bus_fmt);

	if (!drm_connector_atomic_hdr_metadata_equal(old_conn_state, conn_state) ||
	    old_conn_state->colorspace != conn_state->colorspace)
		crtc_state->mode_changed = true;

	return 0;
}

static int meson_encoder_hdmi_acpi_compat_match(struct device *dev,
						const void *data)
{
	return fwnode_device_is_compatible(dev_fwnode(dev), data);
}

static void meson_encoder_hdmi_hpd_notify(struct drm_bridge *bridge,
					  struct drm_connector *connector,
					  enum drm_connector_status status)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);

	if (!encoder_hdmi->cec_notifier)
		return;

	if (status == connector_status_connected) {
		const struct drm_edid *drm_edid;
		const struct edid *edid;

		drm_edid = drm_bridge_edid_read(encoder_hdmi->bridge.next_bridge,
						encoder_hdmi->connector);
		if (!drm_edid)
			return;

		/*
		 * FIXME: The CEC physical address should be set using
		 * cec_notifier_set_phys_addr(encoder_hdmi->cec_notifier,
		 * connector->display_info.source_physical_address) from a path
		 * that has read the EDID and called
		 * drm_edid_connector_update().
		 */
		edid = drm_edid_raw(drm_edid);

		cec_notifier_set_phys_addr_from_edid(encoder_hdmi->cec_notifier, edid);

		drm_edid_free(drm_edid);
	} else
		cec_notifier_phys_addr_invalidate(encoder_hdmi->cec_notifier);
}

static const struct drm_bridge_funcs meson_encoder_hdmi_bridge_funcs = {
	.attach = meson_encoder_hdmi_attach,
	.detach = meson_encoder_hdmi_detach,
	.mode_valid = meson_encoder_hdmi_mode_valid,
	.hpd_notify = meson_encoder_hdmi_hpd_notify,
	.atomic_enable = meson_encoder_hdmi_atomic_enable,
	.atomic_disable = meson_encoder_hdmi_atomic_disable,
	.atomic_get_input_bus_fmts = meson_encoder_hdmi_get_inp_bus_fmts,
	.atomic_check = meson_encoder_hdmi_atomic_check,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

int meson_encoder_hdmi_probe(struct meson_drm *priv)
{
	struct meson_encoder_hdmi *meson_encoder_hdmi;
	struct platform_device *pdev;
	struct device_node *remote;
	int ret;

	meson_encoder_hdmi = devm_drm_bridge_alloc(priv->dev,
						   struct meson_encoder_hdmi,
						   bridge,
						   &meson_encoder_hdmi_bridge_funcs);
	if (IS_ERR(meson_encoder_hdmi))
		return PTR_ERR(meson_encoder_hdmi);

	/* HDMI Transceiver Bridge */
	if (!priv->dev->of_node) {
		/*
		 * ACPI mode: no OF graph and of_drm_find_bridge() cannot
		 * work.  meson_dw_hdmi_bind (a component bound before this
		 * runs) published its bridge in priv->hdmi_bridge.
		 */
		remote = NULL;
		if (!priv->hdmi_bridge)
			return dev_err_probe(priv->dev, -EPROBE_DEFER,
					     "HDMI transceiver bridge not published\n");

		meson_encoder_hdmi->bridge.next_bridge =
			drm_bridge_get(priv->hdmi_bridge);
	} else {
		remote = of_graph_get_remote_node(priv->dev->of_node, 1, 0);
		if (!remote) {
			dev_err(priv->dev, "HDMI transceiver device is disabled");
			return 0;
		}

		meson_encoder_hdmi->bridge.next_bridge = of_drm_find_and_get_bridge(remote);
		if (!meson_encoder_hdmi->bridge.next_bridge) {
			ret = dev_err_probe(priv->dev, -EPROBE_DEFER,
					    "Failed to find HDMI transceiver bridge\n");
			goto err_put_node;
		}
	}

	/* HDMI Encoder Bridge */
	meson_encoder_hdmi->bridge.of_node = priv->dev->of_node;
	meson_encoder_hdmi->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	meson_encoder_hdmi->bridge.interlace_allowed = true;

	drm_bridge_add(&meson_encoder_hdmi->bridge);

	meson_encoder_hdmi->priv = priv;

	/* Encoder */
	ret = drm_simple_encoder_init(priv->drm, &meson_encoder_hdmi->encoder,
				      DRM_MODE_ENCODER_TMDS);
	if (ret) {
		dev_err_probe(priv->dev, ret, "Failed to init HDMI encoder\n");
		goto err_put_node;
	}

	meson_encoder_hdmi->encoder.possible_crtcs = BIT(0);

	/* Attach HDMI Encoder Bridge to Encoder */
	ret = drm_bridge_attach(&meson_encoder_hdmi->encoder, &meson_encoder_hdmi->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err_probe(priv->dev, ret, "Failed to attach bridge\n");
		goto err_put_node;
	}

	/* Initialize & attach Bridge Connector */
	meson_encoder_hdmi->connector = drm_bridge_connector_init(priv->drm,
							&meson_encoder_hdmi->encoder);
	if (IS_ERR(meson_encoder_hdmi->connector)) {
		ret = dev_err_probe(priv->dev,
				    PTR_ERR(meson_encoder_hdmi->connector),
				    "Unable to create HDMI bridge connector\n");
		goto err_put_node;
	}

	/*
	 * We should have now in place:
	 * encoder->[hdmi encoder bridge]->[dw-hdmi bridge]->[display connector bridge]->[display connector]
	 */

	/*
	 * drm_connector_attach_max_bpc_property() requires the
	 * connector to have a state.
	 */
	drm_atomic_helper_connector_reset(meson_encoder_hdmi->connector);

	if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXL) ||
	    meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM) ||
	    meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A)) {
		drm_connector_attach_hdr_output_metadata_property(meson_encoder_hdmi->connector);

		/*
		 * HDR10 output needs BT.2020 colorimetry signalled in the
		 * AVI infoframe alongside the PQ EOTF in the DRM infoframe;
		 * userspace (Kodi) selects it through the Colorspace
		 * property.  dw-hdmi builds the AVI frame from the
		 * connector state.
		 */
		if (!drm_mode_create_hdmi_colorspace_property(meson_encoder_hdmi->connector,
							      BIT(DRM_MODE_COLORIMETRY_BT2020_RGB) |
							      BIT(DRM_MODE_COLORIMETRY_BT2020_YCC)))
			drm_connector_attach_colorspace_property(meson_encoder_hdmi->connector);
	}

	drm_connector_attach_max_bpc_property(meson_encoder_hdmi->connector, 8, 12);

	/* Handle this here until handled by drm_bridge_connector_init() */
	meson_encoder_hdmi->connector->ycbcr_420_allowed = true;

	/*
	 * The CEC notifier is keyed on the dw-hdmi glue platform device
	 * by POINTER IDENTITY (cec-notifier.c matches struct device *).
	 * DT resolves it through the OF graph; under ACPI the same
	 * device is found by its _DSD compatible on the platform bus -
	 * the exact device the ao-cec consumer resolves through its
	 * "hdmi-phandle" _DSD reference to \_SB.HDMI, so both sides
	 * converge on one pointer.  of_find_device_by_node(NULL) must
	 * never run: it could match an arbitrary of_node-less device.
	 */
	if (remote) {
		pdev = of_find_device_by_node(remote);
	} else {
		struct device *hdmi_dev;

		hdmi_dev = bus_find_device(&platform_bus_type, NULL,
					   "amlogic,meson-g12a-dw-hdmi",
					   meson_encoder_hdmi_acpi_compat_match);
		pdev = hdmi_dev ? to_platform_device(hdmi_dev) : NULL;
	}
	of_node_put(remote);
	if (pdev) {
		struct cec_connector_info conn_info;
		struct cec_notifier *notifier;

		cec_fill_conn_info_from_drm(&conn_info, meson_encoder_hdmi->connector);

		notifier = cec_notifier_conn_register(&pdev->dev, NULL, &conn_info);
		if (!notifier) {
			put_device(&pdev->dev);
			return -ENOMEM;
		}

		meson_encoder_hdmi->cec_notifier = notifier;
	}

	priv->encoders[MESON_ENC_HDMI] = meson_encoder_hdmi;

	dev_dbg(priv->dev, "HDMI encoder initialized\n");

	return 0;

err_put_node:
	of_node_put(remote);
	return ret;
}

void meson_encoder_hdmi_remove(struct meson_drm *priv)
{
	struct meson_encoder_hdmi *meson_encoder_hdmi;

	if (priv->encoders[MESON_ENC_HDMI]) {
		meson_encoder_hdmi = priv->encoders[MESON_ENC_HDMI];
		drm_bridge_remove(&meson_encoder_hdmi->bridge);
	}
}
