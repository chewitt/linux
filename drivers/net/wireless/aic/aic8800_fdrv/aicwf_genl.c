// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief netlink interface private command definition
 *
 ******************************************************************************
 */

#include "aicwf_genl.h"
#include "aic_priv_cmd.h"

int aic_debug_lvl = LOG_INFO;
module_param(aic_debug_lvl, int, 0660);

static struct genl_family aicwf_genl_family;
static struct rwnx_hw *aicwf_genl_hw;
static bool aicwf_genl_registered;
static DEFINE_MUTEX(aicwf_genl_cmd_lock);

#define AICWF_GENL_MAX_ARGS    224
#define AICWF_GENL_MAX_CMD_LEN 1024
#define AICWF_GENL_REPLY_MAX_LEN 1024
#define RF_MODE_ERR            (-127)

static int genl_parse_sep(char *dst[], size_t dst_len, char *src,
			  const char *delimiter)
{
	size_t count = 0;
	char *token;

	while ((token = strsep(&src, delimiter))) {
		if (!token[0])
			continue;
		if (count == dst_len)
			return -E2BIG;
		dst[count++] = token;
	}

	return count;
}

static int genl_parse_u8(const char *str, unsigned int base, u8 *value)
{
	u8 parsed;
	int ret;

	if (!str || !str[0])
		return -EINVAL;

	ret = kstrtou8(str, base, &parsed);
	if (ret)
		return ret;

	*value = parsed;
	return 0;
}

#ifdef CONFIG_AIC8800_AUTO_CUSTREG
int cmd_country_set(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		    char *r_buf, unsigned long *r_len)
{
	int ret = 0;

	if (len != 1) {
		genl_debug(LOG_INFO, "%s param err\n", __func__);
		return -EINVAL;
	}

	genl_debug(LOG_DEBUG, "%s, %s\n", __func__, s_buf[0]);

	if (!strcmp(s_buf[0], "AUTO")) {
		rwnx_hw->ccode.auto_set = true;
		rwnx_hw->ccode.ccode_set = false;
		rwnx_hw->ccode.ccode_cnt = 0;
		rwnx_hw->ccode.ccode_rssi = -100;
	} else if (!strcmp(s_buf[0], "MANUAL")) {
		rwnx_hw->ccode.auto_set = false;
		rwnx_hw->ccode.ccode_set = true;
	} else {
		if (strlen(s_buf[0]) != 2 || !isalpha(s_buf[0][0]) ||
		    !isalpha(s_buf[0][1]))
			return -EINVAL;
		rwnx_hw->ccode.ccode_set = true;
		ret =
		rwnx_regulatory_set_wiphy_regd(rwnx_hw->wiphy,
					       get_regdomain_from_rwnx_db(rwnx_hw->wiphy,
									  s_buf[0]));
		memcpy(rwnx_hw->country_abbr, s_buf[0], 2);
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

int cmd_country_get(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		    char *r_buf, unsigned long *r_len)
{
	const struct ieee80211_regdomain *regd;
	int ret = 0;
	u8 buf[3];

	if (len)
		return -EINVAL;

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
	memcpy(r_buf, &buf[0], 3);
	*r_len = 3;

	genl_debug(LOG_DEBUG, "%c%c\n", r_buf[0], r_buf[1]);
	return ret;
}
#endif

int cmd_temp_get(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		 char *r_buf, unsigned long *r_len)
{
	int ret = 0;
	struct mm_set_vendor_swconfig_cfm tp_cfm;

	if (len)
		return -EINVAL;

#ifdef CONFIG_AIC8800_TEMP_CONTROL
	if (timer_pending(&rwnx_hw->sdiodev->tp_ctrl.tp_ctrl_timer)) {
		if (jiffies_to_msecs(jiffies - rwnx_hw->started_jiffies) < 5000) {
			genl_debug(LOG_INFO, "tp_get temp_1: %d\n", rwnx_hw->temp);
			memcpy(r_buf, &rwnx_hw->temp, 1);
		} else {
			if (rwnx_send_get_temp_req(rwnx_hw, &tp_cfm))
				return -1;
			genl_debug(LOGINFO, "tp_get temp_2: %d\n",
				   tp_cfm.temp_comp_get_cfm.degree);
			rwnx_hw->sdiodev->tp_ctrl.cur_temp =
				tp_cfm.temp_comp_get_cfm.degree;
			memcpy(r_buf, &tp_cfm.temp_comp_get_cfm.degree, 1);
		}
	} else
#endif
	{
		if (rwnx_send_get_temp_req(rwnx_hw, &tp_cfm))
			return -1;
		genl_debug(LOGINFO, "tp_get temp_3: %d\n",
			   tp_cfm.temp_comp_get_cfm.degree);
		memcpy(r_buf, &tp_cfm.temp_comp_get_cfm.degree, 1);
	}
	*r_len = 1;

	genl_debug(LOG_DEBUG, "%c\n", r_buf[0]);
	return ret;
}

int cmd_get_fw_version(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		       char *r_buf, unsigned long *r_len)
{
	int ret = 0;

	if (len)
		return -EINVAL;

	genl_debug(LOG_DEBUG, "Firmware Version: %s\n", rwnx_hw->fw_version);
	memcpy(r_buf, rwnx_hw->fw_version, 32);
	*r_len = 32;
	return ret;
}

int cmd_get_link_status(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			char *r_buf, unsigned long *r_len)
{
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_vif *rwnx_vif_st = NULL;
	struct wf_bss_info bi;

	if (len)
		return -EINVAL;

	bi.length = 0;
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif && rwnx_vif->up &&
		    (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION))
			rwnx_vif_st = rwnx_vif;
	}
	if (!rwnx_vif_st) {
		genl_debug(LOG_INFO, "rwnx_vif_st is NULL\n");
		memcpy(r_buf, &bi, 4);
		*r_len = 4;
		return 0;
	}
	if (atomic_read(&rwnx_vif_st->drv_conn_state) !=
		(int)RWNX_DRV_STATUS_CONNECTED) {
		genl_debug(LOG_INFO, "rwnx_vif_st is not conncet\n");
		memcpy(r_buf, &bi, 4);
		*r_len = 4;
		return 0;
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

	memcpy(r_buf, &bi, bi.length);
	*r_len = bi.length;
	return 0;
}

int cmd_get_auth_type(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		      char *r_buf, unsigned long *r_len)
{
	struct rwnx_vif *rwnx_vif = NULL;
	s32 val = -1;

	if (len)
		return -EINVAL;

	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif && rwnx_vif->up &&
		    (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) &&
			atomic_read(&rwnx_vif->drv_conn_state) ==
				(int)RWNX_DRV_STATUS_CONNECTED)
			val = rwnx_vif->sta.auth_type;
	}

	memcpy(r_buf, &val, 4);
	*r_len = 4;
	return 0;
}

int cmd_set_suspend_mode(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			 char *r_buf, unsigned long *r_len)
{
#ifndef CONFIG_AIC8800_AUTO_POWERSAVE
	u8 val;
#endif
	int ret = 0;

	if (len != 1)
		return -EINVAL;

	if (rwnx_hw->testmode == 1) {
		pr_err("AICWF %s, sleep cmd is not supported in RF test mode\n", __func__);
		return -EOPNOTSUPP;
	}

#ifndef CONFIG_AIC8800_AUTO_POWERSAVE
	ret = genl_parse_u8(s_buf[0], 10, &val);
	if (ret)
		return ret;
	if (val > 1)
		return -ERANGE;

	if (val == 1) {
		ret = rwnx_send_me_set_lp_level(rwnx_hw, 1, 0); //dynamic switch

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
	} else if (val == 0) {
		ret = rwnx_send_me_set_lp_level(rwnx_hw, 0, 1); //dynamic switch
	}

	if (ret == 0) {
		r_buf[0] = (u8)val;
		*r_len = 1;
	}
#else
	r_buf[0] = 15;
	*r_len = 1;
#endif

	return ret;
}

int cmd_set_mac_addr(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		     char *r_buf, unsigned long *r_len)
{
	u8 mac_addr[6];
	int i, ret;

	if (!rwnx_hw->testmode) {
		genl_debug(LOG_ERROR, "%s not in testmode !!!\n", __func__);
		ret = RF_MODE_ERR;
		return ret;
	}

	if (len != (int)ARRAY_SIZE(mac_addr)) {
		genl_debug(LOG_ERROR, "%s param err\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < (int)ARRAY_SIZE(mac_addr); i++) {
		ret = genl_parse_u8(s_buf[i], 16,
				    &mac_addr[ARRAY_SIZE(mac_addr) - 1 - i]);
		if (ret)
			return ret;
	}

	genl_debug(LOG_INFO, "set macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5],
		   mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
	ret = rwnx_send_rftest_req(rwnx_hw, SET_MAC_ADDR, sizeof(mac_addr),
				   (u8 *)&mac_addr, NULL);
	return ret;
}

int cmd_get_mac_addr(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
		     char *r_buf, unsigned long *r_len)
{
	u32 addr0, addr1;
	int ret = 0;

	if (len)
		return -EINVAL;

	if (!rwnx_hw->testmode) {
		genl_debug(LOG_ERROR, "ERROR, %s not in testmode !!!\n", __func__);
		ret = RF_MODE_ERR;
		return ret;
	}

	ret = rwnx_send_rftest_req(rwnx_hw, GET_MAC_ADDR, 0, NULL, &cfm);
	memcpy(r_buf, &cfm.rftest_result[0], 8);
	addr0 = cfm.rftest_result[0];
	addr1 = cfm.rftest_result[1];
	genl_debug(LOG_INFO, "0x%x,0x%x\n", addr0, addr1);
	*r_len = 8;

	return ret;
}

int cmd_set_bt_mac_addr(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			char *r_buf, unsigned long *r_len)
{
	u8 mac_addr[6];
	int i, ret;

	if (!rwnx_hw->testmode) {
		genl_debug(LOG_ERROR, "%s not in testmode !!!\n", __func__);
		ret = RF_MODE_ERR;
		return ret;
	}

	if (len != (int)ARRAY_SIZE(mac_addr)) {
		genl_debug(LOG_ERROR, "%s param err\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < (int)ARRAY_SIZE(mac_addr); i++) {
		ret = genl_parse_u8(s_buf[i], 16,
				    &mac_addr[ARRAY_SIZE(mac_addr) - 1 - i]);
		if (ret)
			return ret;
	}

	genl_debug(LOG_INFO, "set bt macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5],
		   mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
	ret = rwnx_send_rftest_req(rwnx_hw, SET_BT_MAC_ADDR, sizeof(mac_addr),
				   (u8 *)&mac_addr, NULL);
	return ret;
}

int cmd_get_bt_mac_addr(struct rwnx_hw *rwnx_hw, char *s_buf[], int len,
			char *r_buf, unsigned long *r_len)
{
	u32 addr0, addr1;
	int ret = 0;

	if (len)
		return -EINVAL;

	if (!rwnx_hw->testmode) {
		genl_debug(LOG_ERROR, "ERROR, %s not in testmode !!!\n", __func__);
		ret = RF_MODE_ERR;
		return ret;
	}

	ret = rwnx_send_rftest_req(rwnx_hw, GET_BT_MAC_ADDR, 0, NULL, &cfm);
	memcpy(r_buf, &cfm.rftest_result[0], 8);
	addr0 = cfm.rftest_result[0];
	addr1 = cfm.rftest_result[1];
	genl_debug(LOG_INFO, "0x%x,0x%x\n", addr0, addr1);
	*r_len = 8;

	return ret;
}

static const struct cmd_table_ops g_cmd_table[] = {
#ifdef CONFIG_AIC8800_AUTO_CUSTREG
	{
		.name = "country_set",
		.func = cmd_country_set,
	},
	{
		.name = "country_get",
		.func = cmd_country_get,
	},
#endif
	{
		.name = "temp_get",
		.func = cmd_temp_get,
	},
	{
		.name = "get_version",
		.func = cmd_get_fw_version,
	},
	{
		.name = "status",
		.func = cmd_get_link_status,
	},
	{
		.name = "wpa_auth",
		.func = cmd_get_auth_type,
	},
	{
		.name = "set_suspend",
		.func = cmd_set_suspend_mode,
	},
	{
		.name = "set_mac_addr",
		.func = cmd_set_mac_addr,
	},
	{
		.name = "get_mac_addr",
		.func = cmd_get_mac_addr,
	},
	{
		.name = "set_bt_mac_addr",
		.func = cmd_set_bt_mac_addr,
	},
	{
		.name = "get_bt_mac_addr",
		.func = cmd_get_bt_mac_addr,
	},
};

int match_cmd_table(struct rwnx_hw *rwnx_hw, char *s_buf, char *r_buf,
		    unsigned long *r_len)
{
	const struct cmd_table_ops *cmd_table = NULL;
	size_t i;
	int ret, len;
	char **cmd = NULL;

	cmd = kcalloc(AICWF_GENL_MAX_ARGS, sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	len = genl_parse_sep(cmd, AICWF_GENL_MAX_ARGS, s_buf, " \t");
	if (len <= 0) {
		genl_debug(LOG_ERROR, "%s params error, count: %d\n", __func__, len);
		kfree(cmd);
		return len ? len : -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(g_cmd_table); i++) {
		if (strcmp(cmd[0], g_cmd_table[i].name) != 0)
			continue;

		cmd_table = &g_cmd_table[i];
		break;
	}

	if (!cmd_table) {
		genl_debug(LOG_INFO, "%s cmd does not exist\n", __func__);
		kfree(cmd);
		return -EINVAL;
	}

	ret = cmd_table->func(rwnx_hw, &cmd[1], len - 1, r_buf, r_len);

	kfree(cmd);
	return ret;
}

static int genl_send_generic(struct genl_info *info, u8 attr, u8 cmd, u32 len,
			     u8 *data)
{
	struct sk_buff *skb;
	void *hdr;
	int ret;

	skb = genlmsg_new(nla_total_size(len), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq, &aicwf_genl_family,
			  0, cmd);
	if (!hdr) {
		genl_debug(LOG_INFO, "%s fail\n", __func__);
		ret = -EMSGSIZE;
		goto err_put;
	}
	if (nla_put(skb, attr, len, data)) {
		ret = -EMSGSIZE;
		goto err_put;
	}

	genlmsg_end(skb, hdr);
	return genlmsg_reply(skb, info);

err_put:
	nlmsg_free(skb);
	return ret;
}

static int aicwf_genl_cmd_handler(struct sk_buff *skb, struct genl_info *info)
{
	struct rwnx_hw *rwnx_hw;
	unsigned long r_len = 0;
	char *s_buf, *r_buf;
	int ret;

	if (!info) {
		genl_debug(LOG_INFO, "%s genl_info is NULL\n", __func__);
		return -EINVAL;
	}

	if (!info->attrs[WL_NL_ATTR_AP2CP]) {
		genl_debug(LOG_INFO, "%s invalid content\n", __func__);
		return -EINVAL;
	}

	rwnx_hw = READ_ONCE(aicwf_genl_hw);
	if (!rwnx_hw)
		return -ENODEV;

	s_buf = nla_strdup(info->attrs[WL_NL_ATTR_AP2CP], GFP_KERNEL);
	if (!s_buf)
		return -ENOMEM;

	r_buf = kzalloc_objs(*r_buf, AICWF_GENL_REPLY_MAX_LEN, GFP_KERNEL);
	if (!r_buf) {
		ret = -ENOMEM;
		goto out_free_cmd;
	}

	genl_debug(LOG_DEBUG, "s_len: %d, %s\n",
		   nla_len(info->attrs[WL_NL_ATTR_AP2CP]), s_buf);

	mutex_lock(&aicwf_genl_cmd_lock);
	if (rwnx_hw != READ_ONCE(aicwf_genl_hw))
		ret = -ENODEV;
	else
		ret = match_cmd_table(rwnx_hw, s_buf, r_buf, &r_len);
	mutex_unlock(&aicwf_genl_cmd_lock);
	if (ret) {
		if (ret == RF_MODE_ERR) {
			strscpy(r_buf, "aic: cmd_error, enter this cmd in testmode",
				AICWF_GENL_REPLY_MAX_LEN);
			r_len = strlen(r_buf) + 1;
		} else {
			genl_debug(LOG_ERROR, "%s handler fail\n", __func__);
			goto out_free_reply;
		}
	}

	if (r_len == 0) {
		strscpy(r_buf, "aic genl_msg send ok",
			AICWF_GENL_REPLY_MAX_LEN);
		r_len = strlen(r_buf) + 1;
	}
	if (r_len > AICWF_GENL_REPLY_MAX_LEN) {
		ret = -EMSGSIZE;
		goto out_free_reply;
	}
	genl_debug(LOG_DEBUG, "r_len: %lu\n", r_len);

	ret = genl_send_generic(info, WL_NL_ATTR_CP2AP, WL_NL_CMD_MSG, r_len,
				(u8 *)r_buf);

out_free_reply:
	kfree(r_buf);
out_free_cmd:
	kfree(s_buf);
	return ret;
}

static int aicwf_genl_get_info_handler(struct sk_buff *skb,
				       struct genl_info *info)
{
	unsigned char r_buf = 1;

	if (!info)
		return -EINVAL;
	if (!READ_ONCE(aicwf_genl_hw))
		return -ENODEV;

	return genl_send_generic(info, WL_NL_ATTR_CP2AP, WL_NL_CMD_GET_INFO,
				  sizeof(r_buf), &r_buf);
}

static const struct nla_policy genl_policy[WL_NL_ATTR_MAX + 1] = {
	[WL_NL_ATTR_IFINDEX] = {.type = NLA_U32},
	[WL_NL_ATTR_AP2CP] = {
		.type = NLA_NUL_STRING,
		.len = AICWF_GENL_MAX_CMD_LEN,
	},
	[WL_NL_ATTR_CP2AP] = {.type = NLA_REJECT},
};

static const struct genl_ops aicwf_genl_ops[] = {
	{
		.cmd = WL_NL_CMD_MSG,
		.flags = GENL_ADMIN_PERM,
		.doit = aicwf_genl_cmd_handler,
	},
	{
		.cmd = WL_NL_CMD_GET_INFO,
		.flags = GENL_ADMIN_PERM,
		.doit = aicwf_genl_get_info_handler,
	},
};

static struct genl_family aicwf_genl_family = {
	.hdrsize = 0,
	.name = GENL_FAMILY_NAME,
	.version = 1,
	.maxattr = WL_NL_ATTR_MAX,
	.policy = genl_policy,
	.module = THIS_MODULE,
	.n_ops = ARRAY_SIZE(aicwf_genl_ops),
	.ops = aicwf_genl_ops,
};

int aicwf_init_genl(struct rwnx_hw *rwnx_hw)
{
	int ret;

	if (!rwnx_hw)
		return -EINVAL;
	if (aicwf_genl_registered)
		return -EALREADY;

	WRITE_ONCE(aicwf_genl_hw, rwnx_hw);
	ret = genl_register_family(&aicwf_genl_family);
	if (ret) {
		WRITE_ONCE(aicwf_genl_hw, NULL);
		genl_debug(LOG_INFO, "genl_register_family error: %d\n", ret);
		return ret;
	}

	aicwf_genl_registered = true;
	return 0;
}

void aicwf_deinit_genl(struct rwnx_hw *rwnx_hw)
{
	int ret;

	if (!aicwf_genl_registered)
		return;
	if (rwnx_hw != READ_ONCE(aicwf_genl_hw)) {
		genl_debug(LOG_ERROR, "generic netlink device mismatch\n");
		return;
	}

	ret = genl_unregister_family(&aicwf_genl_family);
	if (ret) {
		genl_debug(LOG_INFO, "unregister family %d\n", ret);
		return;
	}

	mutex_lock(&aicwf_genl_cmd_lock);
	aicwf_genl_registered = false;
	WRITE_ONCE(aicwf_genl_hw, NULL);
	mutex_unlock(&aicwf_genl_cmd_lock);
}
