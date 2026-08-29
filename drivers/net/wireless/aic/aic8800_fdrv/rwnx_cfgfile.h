/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_CFGFILE_H_
#define _RWNX_CFGFILE_H_

#include "lmac_msg.h"
#include "rwnx_defs.h"
#include <linux/firmware.h>
#include <linux/if_ether.h>

/*
 * Structure used to retrieve information from the Config file used at
 * Initialization time
 */
struct rwnx_conf_file {
	u8 mac_addr[ETH_ALEN];
};

/*
 * Structure used to retrieve information from the PHY Config file used at
 * Initialization time
 */
struct rwnx_phy_conf_file {
	struct phy_trd_cfg_tag trd;
	struct phy_karst_cfg_tag karst;
};

int rwnx_parse_configfile(struct rwnx_hw *rwnx_hw, const char *filename,
			  struct rwnx_conf_file *config);

int rwnx_parse_phy_configfile(struct rwnx_hw *rwnx_hw, const char *filename,
			      struct rwnx_phy_conf_file *config, int path);

#endif /* _RWNX_CFGFILE_H_ */
