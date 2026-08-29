// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief mesh function
 *
 ******************************************************************************
 */

#include "rwnx_mesh.h"

struct rwnx_mesh_proxy *rwnx_get_mesh_proxy_info(struct rwnx_vif *p_rwnx_vif,
						 u8 *p_sta_addr, bool local)
{
	struct rwnx_mesh_proxy *p_mesh_proxy = NULL;
	struct rwnx_mesh_proxy *p_cur_proxy;

	/* Look for proxied devices with provided address */
	list_for_each_entry(p_cur_proxy, &p_rwnx_vif->ap.proxy_list, list) {
		if (p_cur_proxy->local != local)
			continue;

		if (!memcmp(&p_cur_proxy->ext_sta_addr, p_sta_addr, ETH_ALEN)) {
			p_mesh_proxy = p_cur_proxy;
			break;
		}
	}

	/* Return the found information */
	return p_mesh_proxy;
}
