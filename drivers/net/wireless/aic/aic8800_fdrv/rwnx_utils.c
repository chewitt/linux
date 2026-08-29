// SPDX-License-Identifier: GPL-2.0
/*
 ****************************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @file rwnx_utils.c
 *
 * @brief IPC utility function declarations
 *
 ****************************************************************************************
 */

#include "rwnx_utils.h"
#include "ipc_host.h"
#include "rwnx_defs.h"
#include "rwnx_msg_rx.h"
#include "rwnx_prof.h"
#include "rwnx_rx.h"
#include "rwnx_tx.h"

int rwnx_init_aic(struct rwnx_hw *rwnx_hw)
{
	RWNX_DBG(RWNX_FN_ENTRY_STR);
#ifdef AICWF_SDIO_SUPPORT
	aicwf_sdio_host_init(&rwnx_hw->sdio_env, NULL, NULL, rwnx_hw);
#else
	aicwf_usb_host_init(&rwnx_hw->usb_env, NULL, NULL, rwnx_hw);
#endif
	rwnx_cmd_mgr_init(rwnx_hw->cmd_mgr);

	rwnx_hw->testmode = get_testmode();

	return 0;
}
