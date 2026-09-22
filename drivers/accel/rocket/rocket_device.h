/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_DEVICE_H__
#define __ROCKET_DEVICE_H__

#include <drm/drm_device.h>
#include <linux/clk.h>
#include <linux/container_of.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>

#include "rocket_core.h"

struct rocket_device {
	struct drm_device ddev;

	struct mutex sched_lock;

	struct rocket_core *cores;
	/* Number of currently bound cores. */
	unsigned int num_cores;
	/* Slot capacity (DT core count); slots with a NULL .dev are free. */
	unsigned int max_cores;

	/*
	 * The cores have no clock of their own: one clock feeds all of them,
	 * so any core's handle refers to the same thing. No handle is kept
	 * here: each one belongs to the devres of the core that asked for it
	 * and dies with that core's unbind, while this structure outlives any
	 * single core. Whoever needs the clock uses the handle of the core it
	 * was called for, which is bound for as long as the call lasts.
	 *
	 * npu_boot_rate is the rate the clock was left at before the driver
	 * touched it, and active_cores counts the cores that are runtime
	 * resumed right now.
	 */
	unsigned long npu_boot_rate;
	atomic_t active_cores;
};

struct rocket_device *rocket_device_init(struct platform_device *pdev,
					 const struct drm_driver *rocket_drm_driver);
void rocket_device_fini(struct rocket_device *rdev);
#define to_rocket_device(drm_dev) \
	((struct rocket_device *)(container_of((drm_dev), struct rocket_device, ddev)))

#endif /* __ROCKET_DEVICE_H__ */
