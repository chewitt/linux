/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_DRV_H__
#define __ROCKET_DRV_H__

#include <drm/drm_mm.h>
#include <drm/gpu_scheduler.h>
#include <linux/device-id/of.h>

#include "rocket_device.h"

extern const struct dev_pm_ops rocket_pm_ops;
extern const struct of_device_id rocket_dt_match[];

struct rocket_vm {
	struct iommu_domain *domain;
	struct drm_mm mm;
	/* protects @mm */
	struct mutex lock;
	struct kref kref;
};

struct rocket_file_priv {
	struct rocket_device *rdev;

	struct rocket_vm *vm;

	struct drm_gpu_scheduler **scheds;
	struct drm_sched_entity sched_entity;
};

struct rocket_vm *rocket_vm_get(struct rocket_file_priv *rocket_priv);
void rocket_vm_put(struct rocket_vm *vm);

#endif
