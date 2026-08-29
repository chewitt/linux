/* SPDX-License-Identifier: GPL-2.0 */
/*
 ****************************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @file rwnx_v7.h
 *
 ****************************************************************************************
 */

#ifndef _RWNX_V7_H_
#define _RWNX_V7_H_

#include "rwnx_platform.h"
#include <linux/pci.h>

int rwnx_v7_platform_init(struct pci_dev *pci_dev,
			  struct rwnx_plat **rwnx_plat);

#endif /* _RWNX_V7_H_ */
