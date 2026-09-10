// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#include <linux/kernel.h>

#include <drm/drm.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_gem.h"

static const struct drm_framebuffer_funcs rockchip_drm_fb_funcs = {
	.destroy       = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
	.dirty	       = drm_atomic_helper_dirtyfb,
};

static int rockchip_atomic_commit_setup(struct drm_atomic_commit *state)
{
	struct rockchip_drm_private *priv = state->dev->dev_private;
	struct rockchip_drm_commit_hooks *hooks = priv->commit_hooks;

	if (!hooks)
		return 0;

	return hooks->setup(hooks, state);
}

/*
 * drm_atomic_helper_commit_tail_rpm(), with the VOP's commit hooks: tail_begin
 * before any CRTC is touched, tail_end once every CRTC has moved over but
 * before drm_atomic_helper_commit_hw_done().
 */
static void rockchip_atomic_commit_tail(struct drm_atomic_commit *state)
{
	struct drm_device *dev = state->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct rockchip_drm_commit_hooks *hooks = priv->commit_hooks;

	if (hooks)
		hooks->tail_begin(hooks, state);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	drm_atomic_helper_commit_planes(dev, state,
					DRM_PLANE_COMMIT_ACTIVE_ONLY);

	/*
	 * Every CRTC has moved over by now: a disabled one stopped inside its
	 * atomic_disable, an enabled one runs its new mode.  Settle before
	 * commit_hw_done(): after it the state may not be touched, and the
	 * next commit only waits for it.
	 */
	if (hooks)
		hooks->tail_end(hooks, state);

	drm_atomic_helper_fake_vblank(state);

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_wait_for_vblanks(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);
}

static const struct drm_mode_config_helper_funcs rockchip_mode_config_helpers = {
	.atomic_commit_setup = rockchip_atomic_commit_setup,
	.atomic_commit_tail = rockchip_atomic_commit_tail,
};

static struct drm_framebuffer *
rockchip_fb_create(struct drm_device *dev, struct drm_file *file,
		   const struct drm_format_info *info,
		   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_afbc_framebuffer *afbc_fb;
	int ret;

	afbc_fb = kzalloc_obj(*afbc_fb);
	if (!afbc_fb)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_fb_init_with_funcs(dev, &afbc_fb->base,
					 file, info, mode_cmd,
					 &rockchip_drm_fb_funcs);
	if (ret) {
		kfree(afbc_fb);
		return ERR_PTR(ret);
	}

	if (drm_is_afbc(mode_cmd->modifier[0])) {
		ret = drm_gem_fb_afbc_init(dev, info, mode_cmd, afbc_fb);
		if (ret) {
			drm_framebuffer_put(&afbc_fb->base);
			return ERR_PTR(ret);
		}
	}

	return &afbc_fb->base;
}

static const struct drm_mode_config_funcs rockchip_drm_mode_config_funcs = {
	.fb_create = rockchip_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

void rockchip_drm_mode_config_init(struct drm_device *dev)
{
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	/*
	 * set max width and height as default value(4096x4096).
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;

	dev->mode_config.funcs = &rockchip_drm_mode_config_funcs;
	dev->mode_config.helper_private = &rockchip_mode_config_helpers;

	dev->mode_config.normalize_zpos = true;
}
