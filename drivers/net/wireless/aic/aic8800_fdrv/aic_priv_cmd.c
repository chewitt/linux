// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief private command definition
 *
 ******************************************************************************
 */

#include "aic_priv_cmd.h"
#include "aicwf_sdio.h"
#include "rwnx_defs.h"
#include "rwnx_main.h"
#include "rwnx_mod_params.h"
#include "rwnx_msg_tx.h"
#include "rwnx_platform.h"
#include <linux/ctype.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>
#ifdef CONFIG_AIC8800_POWER_LIMIT
#include "aicwf_compat_8800d80.h"
#endif

static void print_help(const char *cmd);

struct cmd_ef_usrdata {
	u8 func;
	u8 cnt;
	u8 reserved[2];
	u32 usrdata[3];
};

#define CMD_MAXARGS 224

static int parse_line(char *line, char *argv[])
{
	int nargs = 0;

	while (nargs < CMD_MAXARGS) {
		/* skip any white space */
		while ((*line == ' ') || (*line == '\t'))
			++line;

		if (*line == '\0') { /* end of line, no more args    */
			argv[nargs] = NULL;
			return nargs;
		}

		/* Argument include space should be bracketed by quotation mark */
		if (*line == '\"') {
			/* Skip quotation mark */
			line++;

			/* Begin of argument string */
			argv[nargs++] = line;

			/* Until end of argument */
			while (*line && (*line != '\"'))
				++line;
		} else {
			argv[nargs++] = line; /* begin of argument string    */

			/* find end of string */
			while (*line && (*line != ' ') && (*line != '\t'))
				++line;
		}

		if (*line == '\0') { /* end of line, no more args    */
			argv[nargs] = NULL;
			return nargs;
		}

		*line++ = '\0'; /* terminate current arg     */
	}

	pr_info("** Too many args (max. %d) **\n", CMD_MAXARGS);

	return nargs;
}

unsigned int command_strtoul(const char *cp, char **endp, unsigned int base)
{
	unsigned int result = 0, value, is_neg = 0;

	if (*cp == '0') {
		cp++;
		if ((*cp == 'x') && isxdigit(cp[1])) {
			base = 16;
			cp++;
		}
		if (!base)
			base = 8;
	}
	if (!base)
		base = 10;
	if (*cp == '-') {
		is_neg = 1;
		cp++;
	}
	while (isxdigit(*cp) &&
	       (value = isdigit(*cp) ? *cp - '0'
		: (islower(*cp) ? toupper(*cp) : *cp) - 'A' +
		10) < base) {
		result = result * base + value;
		cp++;
	}
	if (is_neg)
		result = (unsigned int)((int)result * (-1));

	if (endp)
		*endp = (char *)cp;
	return result;
}

/*
 * aic_priv_cmd handers.
 */

static int aic_priv_cmd_set_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	u8 mac_addr[6];

	if (argc < 7)
		return -EINVAL;

	mac_addr[5] = command_strtoul(argv[1], NULL, 16);
	mac_addr[4] = command_strtoul(argv[2], NULL, 16);
	mac_addr[3] = command_strtoul(argv[3], NULL, 16);
	mac_addr[2] = command_strtoul(argv[4], NULL, 16);
	mac_addr[1] = command_strtoul(argv[5], NULL, 16);
	mac_addr[0] = command_strtoul(argv[6], NULL, 16);
	AICWFDBG(LOGINFO, "set macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5],
		 mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
	rwnx_send_rftest_req(rwnx_hw, SET_MAC_ADDR, sizeof(mac_addr),
			     (u8 *)&mac_addr, NULL);
	return 0;
}

static int aic_priv_cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	aic_chip_priv_cmd_get_mac_addr(rwnx_hw, argc, argv, command);
	return 8;
}

static int aic_priv_cmd_set_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	u8 mac_addr[6];

	if (argc < 7)
		return -EINVAL;

	mac_addr[5] = command_strtoul(argv[1], NULL, 16);
	mac_addr[4] = command_strtoul(argv[2], NULL, 16);
	mac_addr[3] = command_strtoul(argv[3], NULL, 16);
	mac_addr[2] = command_strtoul(argv[4], NULL, 16);
	mac_addr[1] = command_strtoul(argv[5], NULL, 16);
	mac_addr[0] = command_strtoul(argv[6], NULL, 16);
	AICWFDBG(LOGINFO, "set bt macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5],
		 mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
	rwnx_send_rftest_req(rwnx_hw, SET_BT_MAC_ADDR, sizeof(mac_addr),
			     (u8 *)&mac_addr, NULL);
	return 0;
}

static int aic_priv_cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	aic_chip_priv_cmd_get_bt_mac_addr(rwnx_hw, argc, argv, command);
	return 8;
}

#ifdef CONFIG_AIC8800_AUTO_CUSTREG
static int aic_priv_cmd_country_set(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	int ret = 0;

	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}

	AICWFDBG(LOGINFO, "cmd country_set: %s\n", argv[1]);
	if (strncmp(argv[1], "AUTO", 4) == 0) {
		rwnx_hw->ccode.auto_set = true;
		rwnx_hw->ccode.ccode_set = false;
		rwnx_hw->ccode.ccode_cnt = 0;
		rwnx_hw->ccode.ccode_rssi = -100;
	} else if (strncmp(argv[1], "MANUAL", 6) == 0) {
		rwnx_hw->ccode.auto_set = false;
		rwnx_hw->ccode.ccode_set = true;
	} else {
		rwnx_hw->ccode.ccode_set = true;
		ret = rwnx_regulatory_set_wiphy_regd(rwnx_hw->wiphy,
						     get_regdomain_from_rwnx_db(rwnx_hw->wiphy,
										argv[1]));
		memcpy(rwnx_hw->country_abbr, argv[1], 2);
		rwnx_hw->ccode.ccode_cnt = 0;
#ifdef CONFIG_AIC8800_REGION_PW
		rwnx_send_txpwr_lvl_v3_req(rwnx_hw, get_ccode_region(rwnx_hw->country_abbr));
#endif
#ifdef CONFIG_AIC8800_POWER_LIMIT
		aic_chip_powerlimit_load(rwnx_hw);
		if (!rwnx_hw->testmode)
			rwnx_send_me_chan_config_req(rwnx_hw);
#endif
	}
	return ret;
}

static int aic_priv_cmd_country_get(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	const struct ieee80211_regdomain *regd;
	u8 buf[3];
	int bytes_written = 0;

	rcu_read_lock();
	regd = rcu_dereference(rwnx_hw->wiphy->regd);
	if (!regd) {
		rcu_read_unlock();
		return -ENODATA;
	}
	buf[0] = regd->alpha2[0];
	buf[1] = regd->alpha2[1];
	rcu_read_unlock();
	buf[2] = rwnx_hw->ccode.auto_set;
	memcpy(command, &buf[0], 3);
	bytes_written = 3;
	AICWFDBG(LOGINFO, "cmd country_get: %c%c\n", command[0], command[1]);

	return bytes_written;
}
#endif

#ifdef CONFIG_AIC8800_TEMP_CONTROL
static int aic_priv_cmd_temp_ctrl_sw(struct rwnx_hw *rwnx_hw, int argc,
				     char *argv[], char *command)
{
	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}

	if (command_strtoul(argv[1], NULL, 10) == 0) {
		AICWFDBG(LOGINFO, "tp to off\n");
		rwnx_hw->sdiodev->tp_ctrl.on_off = false;
		rwnx_hw->sdiodev->tp_ctrl.get_level = 0;
		spin_lock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
		rwnx_hw->sdiodev->tp_ctrl.tm_start = 0;
		if (timer_pending(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer))
			//del_timer_sync(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer);
			timer_delete_sync(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer);
		spin_unlock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
	} else if (command_strtoul(argv[1], NULL, 10) == 1) {
		AICWFDBG(LOGINFO, "tp to on\n");
		rwnx_hw->sdiodev->tp_ctrl.on_off = true;
		spin_lock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
		rwnx_hw->sdiodev->tp_ctrl.tm_start = 1;
		mod_timer(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer,
			  jiffies + msecs_to_jiffies(TEMP_GET_INTERVAL));
		spin_unlock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
	} else {
		AICWFDBG(LOGINFO, "tp err param\n");
		return -1;
	}

	return 0;
}

static int aic_priv_cmd_temp_sget(struct rwnx_hw *rwnx_hw, int argc,
				  char *argv[], char *command)
{
	u8 func = 0;
	int bytes_written = 0;
	s8 tp_res[4];

	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}

	func = (u8)command_strtoul(argv[1], NULL, 10);
	if (func == 0) {                            // get
		if (rwnx_hw->sdiodev->tp_ctrl.on_off) { // on
			tp_res[0] = 1;
			if (rwnx_hw->sdiodev->tp_ctrl.set_level == 0)
				tp_res[1] = rwnx_hw->sdiodev->tp_ctrl.get_level;
			else
				tp_res[1] = rwnx_hw->sdiodev->tp_ctrl.set_level;
			AICWFDBG(LOGINFO, "tp_get on-off: %d, ctrl-level: %d\n", tp_res[0],
				 tp_res[1]);
			memcpy(command, &tp_res[0], 2);
			bytes_written = 2;
		} else { // off
			tp_res[0] = 0;
			AICWFDBG(LOGINFO, "tp_get on-off: %d\n", tp_res[0]);
			memcpy(command, &tp_res[0], 1);
			bytes_written = 1;
		}
	} else if (func == 1) { // set
		if (!rwnx_hw->sdiodev->tp_ctrl.on_off) {
			AICWFDBG(LOGINFO, "tp_set sw is off, return\n");
			tp_res[0] = 0;
			memcpy(command, &tp_res[0], 1);
			bytes_written = 1;
		} else {
			if (argc < 3) {
				AICWFDBG(LOGINFO, "%s param err\n", __func__);
				return -1;
			}
			rwnx_hw->sdiodev->tp_ctrl.set_level =
				command_strtoul(argv[2], NULL, 10);
			if (rwnx_hw->sdiodev->tp_ctrl.set_level < 0 ||
			    rwnx_hw->sdiodev->tp_ctrl.set_level > 2) {
				AICWFDBG(LOGINFO, "set_level out of range\n");
				rwnx_hw->sdiodev->tp_ctrl.set_level = 0;
			}
			rwnx_hw->sdiodev->tp_ctrl.get_level = 0;
			tp_res[0] = 1;
			tp_res[1] = rwnx_hw->sdiodev->tp_ctrl.set_level;
			AICWFDBG(LOGINFO, "tp_set ctrl-level: %d\n",
				 rwnx_hw->sdiodev->tp_ctrl.set_level);
			memcpy(command, &tp_res[0], 2);
			bytes_written = 2;

			if (rwnx_hw->sdiodev->tp_ctrl.set_level != 0) {
				spin_lock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
				rwnx_hw->sdiodev->tp_ctrl.tm_start = 0;
				if (timer_pending(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer))
					//del_timer_sync(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer);
					timer_delete_sync(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer);
				spin_unlock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
			} else if (rwnx_hw->sdiodev->tp_ctrl.set_level == 0) {
				spin_lock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
				rwnx_hw->sdiodev->tp_ctrl.tm_start = 1;
				mod_timer(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer,
					  jiffies + msecs_to_jiffies(TEMP_GET_INTERVAL));
				spin_unlock_bh(&rwnx_hw->sdiodev->tp_ctrl.tm_lock);
			}
		}
	} else {
		AICWFDBG(LOGINFO, "tp command err\n");
		return -1;
	}

	return bytes_written;
}

static int aic_priv_cmd_set_tmr_intval(struct rwnx_hw *rwnx_hw, int argc,
				       char *argv[], char *command)
{
	u8 func = 0;
	int bytes_written = 0;

	if (argc < 3) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}

	func = (u8)command_strtoul(argv[1], NULL, 10);
	if (func == 1) {
		rwnx_hw->sdiodev->tp_ctrl.interval_t1 =
			command_strtoul(argv[2], NULL, 10);
		AICWFDBG(LOGDEBUG, "set tmr_intval_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.interval_t1);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.interval_t1, 4);
		bytes_written = 4;
	} else if (func == 2) {
		rwnx_hw->sdiodev->tp_ctrl.interval_t2 =
			command_strtoul(argv[2], NULL, 10);
		AICWFDBG(LOGDEBUG, "set tmr_intval_2: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.interval_t2);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.interval_t2, 4);
		bytes_written = 4;
	} else {
		AICWFDBG(LOGERROR, "%s command err\n", __func__);
		return -1;
	}

	return bytes_written;
}

static int aic_priv_cmd_get_tmr_intval(struct rwnx_hw *rwnx_hw, int argc,
				       char *argv[], char *command)
{
	u8 func = 0;
	int bytes_written = 0;

	if (argc < 2) {
		AICWFDBG(LOGINFO, "%s param err\n", __func__);
		return -1;
	}
	func = (u8)command_strtoul(argv[1], NULL, 10);
	if (func == 1) {
		AICWFDBG(LOGDEBUG, "get tmr_intval_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.interval_t1);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.interval_t1, 4);
		bytes_written = 4;
	} else if (func == 2) {
		AICWFDBG(LOGDEBUG, "get tmr_intval_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.interval_t2);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.interval_t2, 4);
		bytes_written = 4;
	} else {
		AICWFDBG(LOGERROR, "%s command err\n", __func__);
		return -1;
	}

	return bytes_written;
}

static int aic_priv_cmd_temp_get(struct rwnx_hw *rwnx_hw, int argc,
				 char *argv[], char *command)
{
	int bytes_written = 0;
	struct mm_set_vendor_swconfig_cfm tp_cfm;

	if (timer_pending(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer)) {
		if (jiffies_to_msecs(jiffies - rwnx_hw->started_jiffies) < 5000) {
			AICWFDBG(LOGINFO, "tp_get temp_1: %d\n", rwnx_hw->temp);
			memcpy(command, &rwnx_hw->temp, 1);
		} else {
			if (rwnx_send_get_temp_req(rwnx_hw, &tp_cfm))
				return -1;
			AICWFDBG(LOGINFO, "tp_get temp_2: %d\n",
				 tp_cfm.temp_comp_get_cfm.degree);
			rwnx_hw->sdiodev->tp_ctrl.cur_temp =
				tp_cfm.temp_comp_get_cfm.degree;
			memcpy(command, &tp_cfm.temp_comp_get_cfm.degree, 1);
		}
	} else {
		if (rwnx_send_get_temp_req(rwnx_hw, &tp_cfm))
			return -1;
		AICWFDBG(LOGINFO, "tp_get temp_3: %d\n",
			 tp_cfm.temp_comp_get_cfm.degree);
		memcpy(command, &tp_cfm.temp_comp_get_cfm.degree, 1);
	}
	bytes_written = 1;

	return bytes_written;
}

static int aic_priv_cmd_tp_thd_set(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	u8 func = 0;
	int bytes_written = 0;

	if (argc < 3) {
		AICWFDBG(LOGERROR, "%s param err\n", __func__);
		return -1;
	}
	func = (u8)command_strtoul(argv[1], NULL, 10);

	if (func == 1) {
		rwnx_hw->sdiodev->tp_ctrl.tp_thd_1 = command_strtoul(argv[2], NULL, 10);
		AICWFDBG(LOGINFO, "set tp_thd_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.tp_thd_1);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.tp_thd_1, 1);
		bytes_written = 1;
	} else if (func == 2) {
		rwnx_hw->sdiodev->tp_ctrl.tp_thd_2 = command_strtoul(argv[2], NULL, 10);
		AICWFDBG(LOGINFO, "set tp_thd_2: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.tp_thd_2);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.tp_thd_2, 1);
		bytes_written = 1;
	} else {
		AICWFDBG(LOGERROR, "%s command err\n", __func__);
		return -1;
	}
	return bytes_written;
}

static int aic_priv_cmd_tp_thd_get(struct rwnx_hw *rwnx_hw, int argc,
				   char *argv[], char *command)
{
	u8 func = 0;
	int bytes_written = 0;

	if (argc < 2) {
		AICWFDBG(LOGERROR, "%s param err\n", __func__);
		return -1;
	}
	func = (u8)command_strtoul(argv[1], NULL, 10);

	if (func == 1) {
		AICWFDBG(LOGINFO, "get tp_thd_1: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.tp_thd_1);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.tp_thd_1, 1);
		bytes_written = 1;
	} else if (func == 2) {
		AICWFDBG(LOGINFO, "set tp_thd_2: %d\n",
			 rwnx_hw->sdiodev->tp_ctrl.tp_thd_2);
		memcpy(command, &rwnx_hw->sdiodev->tp_ctrl.tp_thd_2, 1);
		bytes_written = 1;
	} else {
		AICWFDBG(LOGERROR, "%s command err\n", __func__);
		return -1;
	}
	return bytes_written;
}

#endif
static int aic_priv_cmd_set_suspend(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	u8 func = 0;
	s8 err = -1;
	int ret = 0;

	if (argc < 2 || rwnx_hw->testmode != 0) {
		AICWFDBG(LOGERROR, "%s param err or in rf_test mode\n", __func__);
		return -1;
	}
	func = (u8)command_strtoul(argv[1], NULL, 10);

#ifndef CONFIG_AIC8800_AUTO_POWERSAVE
	if (func == 0) {
		AICWFDBG(LOGINFO, "priv_cmd suspend 0\n");
		ret = rwnx_send_me_set_lp_level(rwnx_hw, 0, 1);
	} else if (func == 1) {
		AICWFDBG(LOGINFO, "priv_cmd suspend 1\n");
		ret = rwnx_send_me_set_lp_level(rwnx_hw, 1, 0);
		if (rwnx_hw->scan_request && rwnx_hw->scanning) {
			pr_info("AICWF enter suspend, stop scan\n");
			ret = rwnx_send_scanu_cancel_req(rwnx_hw, NULL);
			/* make sure fw take effect */
			msleep(50);
			if (ret) {
				pr_info("AICWF %s scanu_cancel fail\n", __func__);
				return ret;
			}
		}
	} else {
		AICWFDBG(LOGERROR, "param err\n");
		ret = -1;
	}
#else
	func = 15;
#endif

	if (ret == 0) {
		memcpy(command, &func, 1);
		return 1;
	}
	memcpy(command, &err, 1);
	return 1;
}

static int aic_priv_cmd_get_version(struct rwnx_hw *rwnx_hw, int argc,
				    char *argv[], char *command)
{
	int bytes_written = 0;

	AICWFDBG(LOGINFO, "Firmware Version: %s\n", rwnx_hw->fw_version);
	memcpy(command, rwnx_hw->fw_version, 32);
	bytes_written = 32;
	return bytes_written;
}

static int aic_priv_cmd_get_link_status(struct rwnx_hw *rwnx_hw, int argc,
					char *argv[], char *command)
{
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_vif *rwnx_vif_st = NULL;
	struct wf_bss_info bi;

	bi.length = 0;
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif && rwnx_vif->up &&
		    (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION))
			rwnx_vif_st = rwnx_vif;
	}
	if (!rwnx_vif_st) {
		AICWFDBG(LOGINFO, "rwnx_vif_st is NULL\n");
		memcpy(command, &bi, 4);
		return 4;
	}
	if (atomic_read(&rwnx_vif_st->drv_conn_state) !=
		(int)RWNX_DRV_STATUS_CONNECTED) {
		AICWFDBG(LOGINFO, "rwnx_vif_st is not conncet\n");
		memcpy(command, &bi, 4);
		return 4;
	}

	bi.ssid_len = rwnx_vif_st->sta.ssid_len;
	bi.band = rwnx_vif_st->sta.ap->band;
	bi.width = rwnx_vif_st->sta.ap->width;
	bi.center_freq = rwnx_vif_st->sta.ap->center_freq;
	bi.center_freq1 = rwnx_vif_st->sta.ap->center_freq1;
	bi.center_freq2 = rwnx_vif_st->sta.ap->center_freq2;
	bi.ht = rwnx_vif_st->sta.ap->ht;
	bi.vht = rwnx_vif_st->sta.ap->vht;
	bi.chan = ieee80211_frequency_to_channel(bi.center_freq);
	memcpy(bi.bssid, rwnx_vif_st->sta.bssid, ETH_ALEN);
	memset(bi.ssid, 0, sizeof(bi.ssid));
	memcpy(bi.ssid, rwnx_vif_st->sta.ssid, rwnx_vif_st->sta.ssid_len);
	bi.length = sizeof(bi);

	memcpy(command, &bi, bi.length);
	return bi.length;
}

static int aic_priv_cmd_get_auth_type(struct rwnx_hw *rwnx_hw, int argc,
				      char *argv[], char *command)
{
	struct rwnx_vif *rwnx_vif = NULL;
	s32 val = -1;

	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif && rwnx_vif->up &&
		    (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) &&
			atomic_read(&rwnx_vif->drv_conn_state) ==
				(int)RWNX_DRV_STATUS_CONNECTED)
			val = rwnx_vif->sta.auth_type;
	}

	memcpy(command, &val, 4);
	return 4;
}

static int aic_priv_cmd_help(struct rwnx_hw *rwnx_hw, int argc, char *argv[],
			     char *command)
{
	print_help(argc > 0 ? argv[0] : NULL);
	return 0;
}

struct aic_priv_cmd {
	const char *cmd;
	int (*handler)(struct rwnx_hw *rwnx_hw, int argc, char *argv[],
		       char *command);
	const char *usage;
};

static const struct aic_priv_cmd aic_priv_commands[] = {
	{"set_mac_addr", aic_priv_cmd_set_mac_addr,
	 "= write WiFi MAC into efuse or flash is limited to a maximum of two times"
	 },
	{"get_mac_addr", aic_priv_cmd_get_mac_addr,
	 "= display WiFi MAC stored in efuse or flash"},
	{"set_bt_mac_addr", aic_priv_cmd_set_bt_mac_addr,
	 "= write BT MAC into efuse or flash is limited to a maximum of two times"},
	{"get_bt_mac_addr", aic_priv_cmd_get_bt_mac_addr,
	 "= display BT MAC stored in efuse or flash"},

	/* The following is not an RF cmd */
#ifdef CONFIG_AIC8800_AUTO_CUSTREG
	{"country_set", aic_priv_cmd_country_set, "<ccode>"},
	{"country_get", aic_priv_cmd_country_get, "no param"},
#endif
#ifdef CONFIG_AIC8800_TEMP_CONTROL
	{"TEMP_CTRL_SW", aic_priv_cmd_temp_ctrl_sw, "<val> 1--open, 0--close"},
	{"TEMP_CTRL_SET_GET", aic_priv_cmd_temp_sget,
	 "<option> <val> option--0-get,1-set; val--0/1/2"},
	{"SET_TMR_INTVAL", aic_priv_cmd_set_tmr_intval,
	 "<index> <time> index--0/1, time ms"},
	{"GET_TMR_INTVAL", aic_priv_cmd_get_tmr_intval, "<index> index--0/1"},
	{"TEMP_GET", aic_priv_cmd_temp_get, "no param"},
	{"TEMP_THRESHOLD_SET", aic_priv_cmd_tp_thd_set,
	 "<index> <val> index--0/1, val--degree centigrade"},
	{"TEMP_THRESHOLD_GET", aic_priv_cmd_tp_thd_get, "<index> inddex--0/1"},
#endif
	{"set_suspend", aic_priv_cmd_set_suspend,
	 "<mode>, 1/0----enter/exit lp_level"},
	{"get_version", aic_priv_cmd_get_version, "no param, get fw version"},
	{"status", aic_priv_cmd_get_link_status, "no param, get link status"},
	{"wpa_auth", aic_priv_cmd_get_auth_type,
	 "no param, get AuthenticationType"},

	// Reserve for new aic_priv_cmd.
	{"help", aic_priv_cmd_help, "= show usage help"},
	{NULL, NULL, NULL}

};

/*
 * Prints command usage, lines are padded with the specified string.
 */
static void print_help(const char *cmd)
{
	int n;

	pr_info("commands:\n");
	for (n = 0; aic_priv_commands[n].cmd; n++) {
		if (cmd)
			pr_info("%s %s\n", aic_priv_commands[n].cmd,
				aic_priv_commands[n].usage);
	}
}

int handle_private_cmd(struct net_device *net, char *command, u32 cmd_len)
{
	const struct aic_priv_cmd *cmd, *match = NULL;
	int count;
	int bytes_written = 0;
	char **argv = NULL;
	int argc;
	struct rwnx_vif *vif =
		container_of(net->ieee80211_ptr, struct rwnx_vif, wdev);
	struct rwnx_hw *p_rwnx_hw = vif->rwnx_hw;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

	argv = kzalloc((CMD_MAXARGS + 1) * sizeof(char *), GFP_KERNEL);
	if (!argv) {
		AICWFDBG(LOGERROR, "%s alloc argv fail\n", __func__);
		return -ENOMEM;
	}

	argc = parse_line(command, argv);
	if (argc == 0 || argc > CMD_MAXARGS) {
		AICWFDBG(LOGERROR, "%s params error, count: %d\n", __func__, argc);
		kfree(argv);
		return -EINVAL;
	}

	count = 0;
	cmd = aic_priv_commands;
	while (cmd->cmd) {
		if (strncasecmp(cmd->cmd, argv[0], strlen(argv[0])) == 0 &&
		    strncasecmp(cmd->cmd, argv[0], strlen(cmd->cmd)) == 0) {
			match = cmd;
			if (strcasecmp(cmd->cmd, argv[0]) == 0) {
				/* we have an exact match */
				count = 1;
				break;
			}
			count++;
		}
		cmd++;
	}

	if (count > 1) {
		AICWFDBG(LOGINFO,
			 "Ambiguous command '%s'; possible commands:", argv[0]);
		cmd = aic_priv_commands;
		while (cmd->cmd) {
			if (strncasecmp(cmd->cmd, argv[0], strlen(argv[0])) == 0)
				AICWFDBG(LOGINFO, " %s", cmd->cmd);
			cmd++;
		}
		AICWFDBG(LOGINFO, "\n");
	} else if (count == 0) {
		AICWFDBG(LOGERROR, "Unknown command '%s'\n", argv[0]);
		kfree(argv);
		return -EINVAL;
	}
	AICWFDBG(LOGINFO, "match %s", match->cmd);
	bytes_written = match->handler(p_rwnx_hw, argc, &argv[0], command);

	if (bytes_written < 0)
		AICWFDBG(LOGERROR, "wrong param\n");

	kfree(argv);
	return bytes_written;
}
