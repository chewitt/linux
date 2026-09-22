/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Igor Paunovic <royalnet026@gmail.com> */

#ifndef __ROCKET_DEVFREQ_H__
#define __ROCKET_DEVFREQ_H__

#include <linux/devfreq.h>
#include <linux/mutex_types.h>
#include <linux/spinlock_types.h>

struct rocket_core;
struct rocket_device;
struct thermal_cooling_device;

struct rocket_devfreq {
	struct devfreq *devfreq;
	struct thermal_cooling_device *cooling;
	struct devfreq_simple_ondemand_data gov_data;

	/*
	 * The core the devfreq device hangs off: the first in devicetree order
	 * to carry an OPP table. NULL when no core does.
	 */
	struct rocket_core *owner;

	/*
	 * The OPP clock and supply configuration is attached to the owning
	 * core, which is not the device this driver is probing when it is set
	 * up, so it cannot be devres. Zero means nothing is attached.
	 */
	int opp_token;

	/*
	 * Serialises rate changes against each other and against the set of
	 * runtime PM references taken below.
	 *
	 * This is never taken from a runtime PM callback. A rate change
	 * resumes every core while holding it, so a core that took it on its
	 * way down would wait for a rate change that is waiting for that same
	 * core to finish suspending.
	 */
	struct mutex lock;
	unsigned long cur_freq;
	bool cores_held;

	/*
	 * The boot rate as an OPP: the lowest rate in the table that is not
	 * below rdev->npu_boot_rate. The raw boot rate is whatever the firmware
	 * reported at probe and need not be in the table, and comparing
	 * cur_freq against a rate that is not in the table would leave the
	 * cores held for good after the first change. Everything here compares
	 * against and returns to this instead.
	 */
	unsigned long boot_freq;

	/* Guards the utilisation fields of every core. */
	spinlock_t busy_lock;
};

int rocket_devfreq_init(struct rocket_device *rdev);
void rocket_devfreq_fini(struct rocket_device *rdev);
void rocket_devfreq_suspend(struct rocket_device *rdev);
int rocket_devfreq_set_boot_rate(struct rocket_device *rdev);
void rocket_devfreq_record_busy(struct rocket_core *core);
void rocket_devfreq_record_idle(struct rocket_core *core);

#endif /* __ROCKET_DEVFREQ_H__ */
