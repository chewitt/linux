/* SPDX-License-Identifier: GPL-2.0 */
/*
 ****************************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @file rwnx_strs.h
 *
 * @brief Miscellaneous debug strings
 *
 ****************************************************************************************
 */

#ifndef _RWNX_STRS_H_
#define _RWNX_STRS_H_

#include "lmac_msg.h"

#define RWNX_ID2STR(tag)                                               \
	({                                                                 \
		const typeof(tag) __tag = (tag);                               \
		((MSG_T(__tag) < ARRAY_SIZE(rwnx_id2str)) &&                   \
		 (rwnx_id2str[MSG_T(__tag)]) &&                                \
		 ((rwnx_id2str[MSG_T(__tag)])[MSG_I(__tag)]))                  \
		 ? (rwnx_id2str[MSG_T(__tag)])[MSG_I(__tag)]                \
		 : "unknown";                                               \
	})

extern const char *const *rwnx_id2str[TASK_LAST_EMB + 1];

#endif /* _RWNX_STRS_H_ */
