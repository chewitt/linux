// SPDX-License-Identifier: GPL-2.0
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief Set configuration according to modules parameters
 *
 ******************************************************************************
 */
#include <linux/module.h>
#include <linux/rtnetlink.h>

#include "hal_desc.h"
#include "reg_access.h"
#include "rwnx_cfgfile.h"
#include "rwnx_compat.h"
#include "rwnx_defs.h"
#include "rwnx_main.h"
#include "rwnx_tx.h"

#ifdef CONFIG_RWNX_FULLMAC
#define COMMON_PARAM(name, default) .name = default,
#define SOFTMAC_PARAM(name, default)
#define FULLMAC_PARAM(name, default) .name = default,
#endif /* CONFIG_RWNX_FULLMAC */

struct rwnx_mod_params rwnx_mod_params = {
	/* common parameters */
	COMMON_PARAM(ht_on, true)
	COMMON_PARAM(vht_on, true)
	COMMON_PARAM(he_on, true)
	COMMON_PARAM(mcs_map, IEEE80211_VHT_MCS_SUPPORT_0_9)
	COMMON_PARAM(he_mcs_map, IEEE80211_HE_MCS_SUPPORT_0_11)
	COMMON_PARAM(he_ul_on, false)
	COMMON_PARAM(ldpc_on, true)
	COMMON_PARAM(stbc_on, true)
	COMMON_PARAM(gf_rx_on, false)
	COMMON_PARAM(phy_cfg, 2)
	COMMON_PARAM(uapsd_timeout, 300)
	COMMON_PARAM(ap_uapsd_on, false)
	COMMON_PARAM(sgi, true)
	COMMON_PARAM(sgi80, false)
	COMMON_PARAM(use_2040, 1)
	COMMON_PARAM(nss, 1)
	COMMON_PARAM(amsdu_rx_max, 2)
	COMMON_PARAM(bfmee, true)
	COMMON_PARAM(bfmer, false)
	COMMON_PARAM(mesh, false)
	COMMON_PARAM(murx, true)
	COMMON_PARAM(mutx, true)
	COMMON_PARAM(mutx_on, true)
	COMMON_PARAM(use_80, false)
	COMMON_PARAM(custregd, true)
	COMMON_PARAM(custchan, false)
	COMMON_PARAM(roc_dur_max, 500)
	COMMON_PARAM(listen_itv, 0)
	COMMON_PARAM(listen_bcmc, true)
	COMMON_PARAM(lp_clk_ppm, 20)
	COMMON_PARAM(ps_on, true)
	COMMON_PARAM(tx_lft, RWNX_TX_LIFETIME_MS)
	COMMON_PARAM(amsdu_maxnb, NX_TX_PAYLOAD_MAX)
	// By default, only enable UAPSD for Voice queue (see
	// IEEE80211_DEFAULT_UAPSD_QUEUE comment)
	COMMON_PARAM(uapsd_queues, IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
	COMMON_PARAM(tdls, false)
	COMMON_PARAM(uf, false)
	COMMON_PARAM(auto_reply, false)
	COMMON_PARAM(ftl, "")
	COMMON_PARAM(dpsm, false)

	/* SOFTMAC only parameters */
	SOFTMAC_PARAM(mfp_on, false)
	SOFTMAC_PARAM(gf_on, false)
	SOFTMAC_PARAM(bwsig_on, true)
	SOFTMAC_PARAM(dynbw_on, true)
	SOFTMAC_PARAM(agg_tx, true)
	SOFTMAC_PARAM(amsdu_force, 2)
	SOFTMAC_PARAM(rc_probes_on, false)
	SOFTMAC_PARAM(cmon, true)
	SOFTMAC_PARAM(hwscan, true)
	SOFTMAC_PARAM(autobcn, true)
	SOFTMAC_PARAM(dpsm, true)

	/* FULLMAC only parameters */
	FULLMAC_PARAM(ant_div, true)
};

#ifdef CONFIG_RWNX_FULLMAC
/* FULLMAC specific parameters*/
module_param_named(ant_div, rwnx_mod_params.ant_div, bool, 0444);
MODULE_PARM_DESC(ant_div, "Enable Antenna Diversity (Default: 1)");
#endif /* CONFIG_RWNX_FULLMAC */

module_param_named(ht_on, rwnx_mod_params.ht_on, bool, 0444);
MODULE_PARM_DESC(ht_on, "Enable HT (Default: 1)");

module_param_named(vht_on, rwnx_mod_params.vht_on, bool, 0444);
MODULE_PARM_DESC(vht_on, "Enable VHT (Default: 1)");

module_param_named(he_on, rwnx_mod_params.he_on, bool, 0444);
MODULE_PARM_DESC(he_on, "Enable HE (Default: 1)");

module_param_named(mcs_map, rwnx_mod_params.mcs_map, int, 0444);
MODULE_PARM_DESC(mcs_map,
		 "VHT MCS map value  0: MCS0_7, 1: MCS0_8, 2: MCS0_9 (Default: 2)");

module_param_named(he_mcs_map, rwnx_mod_params.he_mcs_map, int, 0444);
MODULE_PARM_DESC(he_mcs_map,
		 "HE MCS map value  0: MCS0_7, 1: MCS0_9, 2: MCS0_11 (Default: 2)");

module_param_named(he_ul_on, rwnx_mod_params.he_ul_on, bool, 0444);
MODULE_PARM_DESC(he_ul_on, "Enable HE OFDMA UL (Default: 0)");

module_param_named(amsdu_maxnb, rwnx_mod_params.amsdu_maxnb, int,
		   0444 | 0200);
MODULE_PARM_DESC(amsdu_maxnb,
		 "Maximum number of MSDUs inside an A-MSDU in TX: (Default: NX_TX_PAYLOAD_MAX)");

module_param_named(ps_on, rwnx_mod_params.ps_on, bool, 0444);
MODULE_PARM_DESC(ps_on, "Enable PowerSaving (Default: 1-Enabled)");

module_param_named(tx_lft, rwnx_mod_params.tx_lft, int, 0644);
MODULE_PARM_DESC(tx_lft,
		 "Tx lifetime (ms) - setting it to 0 disables retries (Default: "
		 __stringify(RWNX_TX_LIFETIME_MS) ")");

module_param_named(ldpc_on, rwnx_mod_params.ldpc_on, bool, 0444);
MODULE_PARM_DESC(ldpc_on, "Enable LDPC (Default: 1)");

module_param_named(stbc_on, rwnx_mod_params.stbc_on, bool, 0444);
MODULE_PARM_DESC(stbc_on, "Enable STBC in RX (Default: 1)");

module_param_named(gf_rx_on, rwnx_mod_params.gf_rx_on, bool, 0444);
MODULE_PARM_DESC(gf_rx_on, "Enable HT greenfield in reception (Default: 1)");

module_param_named(phycfg, rwnx_mod_params.phy_cfg, int, 0444);
MODULE_PARM_DESC(phycfg, "0 <= phycfg <= 5 : RF Channel Conf (Default: 2(C0-A1-B2))");

module_param_named(uapsd_timeout, rwnx_mod_params.uapsd_timeout, int,
		   0444 | 0200);
MODULE_PARM_DESC(uapsd_timeout,
		 "UAPSD Timer timeout, in ms (Default: 300). If 0, UAPSD is disabled");

module_param_named(uapsd_queues, rwnx_mod_params.uapsd_queues, int,
		   0444 | 0200);
MODULE_PARM_DESC(uapsd_queues,
		 "UAPSD Queues, integer value, must be seen as a bitfield\n"
		 "        Bit 0 = VO\n"
		 "        Bit 1 = VI\n"
		 "        Bit 2 = BK\n"
		 "        Bit 3 = BE\n"
		 "     -> uapsd_queues=7 will enable uapsd for VO, VI and BK queues");

#if !IS_ENABLED(CONFIG_AIC8800_STA_ONLY)
module_param_named(ap_uapsd_on, rwnx_mod_params.ap_uapsd_on, bool, 0444);
MODULE_PARM_DESC(ap_uapsd_on, "Enable UAPSD in AP mode (Default: 0)");
#endif

module_param_named(sgi, rwnx_mod_params.sgi, bool, 0444);
MODULE_PARM_DESC(sgi, "Advertise Short Guard Interval support (Default: 1)");

module_param_named(sgi80, rwnx_mod_params.sgi80, bool, 0444);
MODULE_PARM_DESC(sgi80, "Advertise Short Guard Interval support for 80MHz (Default: 1)");

module_param_named(use_2040, rwnx_mod_params.use_2040, bool, 0444);
MODULE_PARM_DESC(use_2040, "Use tweaked 20-40MHz mode (Default: 1)");

module_param_named(use_80, rwnx_mod_params.use_80, bool, 0444);
MODULE_PARM_DESC(use_80, "Enable 80MHz (Default: 1)");

module_param_named(custregd, rwnx_mod_params.custregd, bool, 0444);
MODULE_PARM_DESC(custregd,
		 "Use permissive custom regulatory rules (for testing ONLY) (Default: 0)");

#if !IS_ENABLED(CONFIG_AIC8800_STA_ONLY)
module_param_named(custchan, rwnx_mod_params.custchan, bool, 0444);
MODULE_PARM_DESC(custchan,
		 "Extend channel set to non-standard channels (for testing ONLY) (Default: 0)");
#endif

module_param_named(nss, rwnx_mod_params.nss, int, 0444);
MODULE_PARM_DESC(nss,
		 "1 <= nss <= 2 : Supported number of Spatial Streams (Default: 1)");

module_param_named(amsdu_rx_max, rwnx_mod_params.amsdu_rx_max, int, 0444);
MODULE_PARM_DESC(amsdu_rx_max,
		 "0 <= amsdu_rx_max <= 2 : Maximum A-MSDU size supported in RX\n"
		 "        0: 3895 bytes\n"
		 "        1: 7991 bytes\n"
		 "        2: 11454 bytes\n"
		 "        This value might be reduced according to the FW capabilities.\n"
		 "        Default: 2");

module_param_named(bfmee, rwnx_mod_params.bfmee, bool, 0444);
MODULE_PARM_DESC(bfmee, "Enable Beamformee Capability (Default: 1-Enabled)");

module_param_named(bfmer, rwnx_mod_params.bfmer, bool, 0444);
MODULE_PARM_DESC(bfmer, "Enable Beamformer Capability (Default: 0-Disabled)");

#if !IS_ENABLED(CONFIG_AIC8800_STA_ONLY)
module_param_named(mesh, rwnx_mod_params.mesh, bool, 0444);
MODULE_PARM_DESC(mesh, "Enable Meshing Capability (Default: 0-Disabled)");
#endif

module_param_named(murx, rwnx_mod_params.murx, bool, 0444);
MODULE_PARM_DESC(murx, "Enable MU-MIMO RX Capability (Default: 1-Enabled)");

module_param_named(mutx, rwnx_mod_params.mutx, bool, 0444);
MODULE_PARM_DESC(mutx, "Enable MU-MIMO TX Capability (Default: 1-Enabled)");

module_param_named(mutx_on, rwnx_mod_params.mutx_on, bool, 0444 | 0200);
MODULE_PARM_DESC(mutx_on, "Enable MU-MIMO transmissions (Default: 1-Enabled)");

module_param_named(roc_dur_max, rwnx_mod_params.roc_dur_max, int, 0444);
MODULE_PARM_DESC(roc_dur_max, "Maximum Remain on Channel duration");

module_param_named(listen_itv, rwnx_mod_params.listen_itv, int, 0444);
MODULE_PARM_DESC(listen_itv, "Maximum listen interval");

module_param_named(listen_bcmc, rwnx_mod_params.listen_bcmc, bool, 0444);
MODULE_PARM_DESC(listen_bcmc, "Wait for BC/MC traffic following DTIM beacon");

module_param_named(lp_clk_ppm, rwnx_mod_params.lp_clk_ppm, int, 0444);
MODULE_PARM_DESC(lp_clk_ppm, "Low Power Clock accuracy of the local device");

#if !IS_ENABLED(CONFIG_AIC8800_STA_ONLY)
module_param_named(tdls, rwnx_mod_params.tdls, bool, 0444);
MODULE_PARM_DESC(tdls, "Enable TDLS (Default: 0-Disabled)");
#endif

module_param_named(uf, rwnx_mod_params.uf, bool, 0444 | 0200);
MODULE_PARM_DESC(uf,
		 "Enable Unsupported HT Frame Logging (Default: 0-Disabled)");

module_param_named(auto_reply, rwnx_mod_params.auto_reply, bool,
		   0444 | 0200);
MODULE_PARM_DESC(auto_reply,
		 "Enable Monitor MacAddr Auto-Reply (Default: 0-Disabled)");

module_param_named(ftl, rwnx_mod_params.ftl, charp, 0444);
MODULE_PARM_DESC(ftl, "Firmware trace level  (Default: \"\")");

module_param_named(dpsm, rwnx_mod_params.dpsm, bool, 0444);
MODULE_PARM_DESC(dpsm, "Enable Dynamic PowerSaving (Default: 1-Enabled)");

#ifdef CONFIG_AIC8800_AUTO_CUSTREG
static char default_ccode[4] = "XX";
#else
static char default_ccode[4] = "00";
#endif

static char country_code[4];
module_param_string(country_code, country_code, 4, 0600);

static const int mcs_map_to_rate[4][3] = {
	[PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_7] = 65,
	[PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_8] = 78,
	[PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_9] = 78,
	[PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_7] = 135,
	[PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_8] = 162,
	[PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_9] = 180,
	[PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_7] = 292,
	[PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_8] = 351,
	[PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_9] = 390,
	[PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_7] = 585,
	[PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_8] = 702,
	[PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_9] = 780,
};

#define MAX_VHT_RATE(map, nss, bw) (mcs_map_to_rate[bw][map] * (nss))

static char ccode_channels[200];
static int index_for_channel_list;
module_param_string(ccode_channels, ccode_channels, 200, 0600);

void rwnx_get_countrycode_channels(struct wiphy *wiphy,
				   const struct ieee80211_regdomain *regdomain)
{
	enum nl80211_band band;
	struct ieee80211_supported_band *sband;
	int channel_index;
	int rule_index;
	int band_num = 0;
	int rule_num = regdomain->n_reg_rules;
	int start_freq = 0;
	int end_freq = 0;
	int center_freq = 0;
	char channel[4];

	band_num = NUM_NL80211_BANDS;

	memset(ccode_channels, 0, 200);
	index_for_channel_list = 0;

	for (band = 0; band < band_num; band++) {
		sband = wiphy->bands[band]; // bands: 0:2.4G 1:5G 2:60G
		if (!sband)
			continue;

		for (channel_index = 0; channel_index < sband->n_channels;
			 channel_index++) {
			for (rule_index = 0; rule_index < rule_num; rule_index++) {
				start_freq =
				regdomain->reg_rules[rule_index].freq_range.start_freq_khz / 1000;
				end_freq =
				regdomain->reg_rules[rule_index].freq_range.end_freq_khz / 1000;
				center_freq = sband->channels[channel_index].center_freq;
				if ((center_freq - 10) >= start_freq &&
				    (center_freq + 10) <= end_freq) {
					sprintf(channel, "%d",
						ieee80211_frequency_to_channel(center_freq));
					memcpy(ccode_channels + index_for_channel_list, channel,
					       strlen(channel));
					index_for_channel_list += strlen(channel);
					memcpy(ccode_channels + index_for_channel_list, " ", 1);
					index_for_channel_list += 1;
					break;
				}
			}
		}
	}
	AICWFDBG(LOGDEBUG, "%s support channel:%s\r\n", __func__, ccode_channels);
}

const struct ieee80211_regdomain *
get_regdomain_from_rwnx_db_index(struct wiphy *wiphy, int index)
{
	u8 idx;

	idx = index;

	memset(country_code, 0, 4);
	country_code[0] = reg_regdb[idx]->alpha2[0];
	country_code[1] = reg_regdb[idx]->alpha2[1];

	AICWFDBG(LOGDEBUG, "%s set ccode:%s \r\n", __func__, country_code);

	rwnx_get_countrycode_channels(wiphy, reg_regdb[idx]);

	return reg_regdb[idx];
}

const struct ieee80211_regdomain *
get_regdomain_from_rwnx_db(struct wiphy *wiphy, const char *alpha2)
{
	u8 idx;

	memset(country_code, 0, 4);

	AICWFDBG(LOGDEBUG, "%s set ccode:%s \r\n", __func__, alpha2);
	idx = 0;

	while (reg_regdb[idx] && idx < reg_regdb_size) {
		if (reg_regdb[idx]->alpha2[0] == alpha2[0] &&
		    reg_regdb[idx]->alpha2[1] == alpha2[1]) {
			memcpy(country_code, alpha2, 2);
			rwnx_get_countrycode_channels(wiphy, reg_regdb[idx]);
			return reg_regdb[idx];
		}
		idx++;
	}

	AICWFDBG(LOGERROR, "%s(): Error, wrong country = %s\n", __func__, alpha2);
#ifdef CONFIG_AIC8800_AUTO_CUSTREG
	AICWFDBG(LOGERROR, "Set as default XX\n");
	memcpy(country_code, default_ccode, sizeof(default_ccode));
	rwnx_get_countrycode_channels(wiphy, reg_regdb[reg_regdb_size - 1]);
	return reg_regdb[reg_regdb_size - 1];
#else
	AICWFDBG(LOGERROR, "Set as default 00\n");
	memcpy(country_code, default_ccode, sizeof(default_ccode));
	rwnx_get_countrycode_channels(wiphy, reg_regdb[0]);
	return reg_regdb[0];
#endif
}

int rwnx_regulatory_set_wiphy_regd(struct wiphy *wiphy,
				   const struct ieee80211_regdomain *regd)
{
	/*
	 * cfg80211 copies the regulatory domain and does not modify the source,
	 * but regulatory_set_wiphy_regd() is not const-qualified.
	 */
	return regulatory_set_wiphy_regd(wiphy,
					 (struct ieee80211_regdomain *)regd);
}

int rwnx_regulatory_set_wiphy_regd_sync(struct wiphy *wiphy,
					const struct ieee80211_regdomain *regd)
{
	/* See rwnx_regulatory_set_wiphy_regd(). */
	return regulatory_set_wiphy_regd_sync(wiphy,
					      (struct ieee80211_regdomain *)regd);
}

static void rwnx_set_vht_capa(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	struct ieee80211_supported_band *band_5ghz =
		wiphy->bands[NL80211_BAND_5GHZ];
	struct ieee80211_supported_band *band_2ghz =
		wiphy->bands[NL80211_BAND_2GHZ];

	int i;
	int nss = rwnx_hw->mod_params->nss;
	int mcs_map;
	int mcs_map_max;
	int bw_max;

	if (!rwnx_hw->mod_params->vht_on)
		return;

	band_2ghz->vht_cap.vht_supported = true;
	if (rwnx_hw->mod_params->sgi80)
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
	if (rwnx_hw->mod_params->stbc_on)
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_RXSTBC_1;
	if (rwnx_hw->mod_params->ldpc_on)
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_RXLDPC;
	if (rwnx_hw->mod_params->bfmee) {
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
		band_2ghz->vht_cap.cap |= 3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
	}
	if (nss > 1)
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_TXSTBC;

	// Update the AMSDU max RX size (not shifted as located at offset 0 of the
	// VHT cap)
	band_2ghz->vht_cap.cap |= rwnx_hw->mod_params->amsdu_rx_max;

	if (rwnx_hw->mod_params->bfmer) {
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
		/* Set number of sounding dimensions */
		band_2ghz->vht_cap.cap |=
			(nss - 1) << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
	}
	if (rwnx_hw->mod_params->murx)
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
	if (rwnx_hw->mod_params->mutx)
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

	/*
	 * MCS map:
	 * This capabilities are filled according to the mcs_map module parameter.
	 * However currently we have some limitations due to FPGA clock constraints
	 * that prevent always using the range of MCS that is defined by the
	 * parameter:
	 *   - in RX, 2SS, we support up to MCS7
	 *   - in TX, 2SS, we support up to MCS8
	 */
	// Get max supported BW
	if (rwnx_hw->mod_params->use_80)
		bw_max = PHY_CHNL_BW_80;
	else if (rwnx_hw->mod_params->use_2040)
		bw_max = PHY_CHNL_BW_40;
	else
		bw_max = PHY_CHNL_BW_20;

	// Check if MCS map should be limited to MCS0_8 due to the standard. Indeed
	// in BW20, MCS9 is not supported in 1 and 2 SS
	if (rwnx_hw->mod_params->use_2040)
		mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_9;
	else
		mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_8;

	mcs_map = min_t(int, rwnx_hw->mod_params->mcs_map, mcs_map_max);

	nss = min_t(int, nss, 16);

	band_2ghz->vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(0);
	for (i = 0; i < nss; i++) {
		band_2ghz->vht_cap.vht_mcs.rx_mcs_map |=
			cpu_to_le16(mcs_map << (i * 2));
		band_2ghz->vht_cap.vht_mcs.rx_highest =
			cpu_to_le16(MAX_VHT_RATE(mcs_map, nss, bw_max));
		mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_7;
	}
	for (; i < 8; i++) {
		band_2ghz->vht_cap.vht_mcs.rx_mcs_map |=
			cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
	}

	mcs_map = min_t(int, rwnx_hw->mod_params->mcs_map, mcs_map_max);
	band_2ghz->vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(0);
	for (i = 0; i < nss; i++) {
		band_2ghz->vht_cap.vht_mcs.tx_mcs_map |=
			cpu_to_le16(mcs_map << (i * 2));
		band_2ghz->vht_cap.vht_mcs.tx_highest =
			cpu_to_le16(MAX_VHT_RATE(mcs_map, nss, bw_max));
		mcs_map = min_t(int, rwnx_hw->mod_params->mcs_map,
				IEEE80211_VHT_MCS_SUPPORT_0_8);
	}
	for (; i < 8; i++) {
		band_2ghz->vht_cap.vht_mcs.tx_mcs_map |=
			cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
	}

	if (!rwnx_hw->mod_params->use_80) {
#ifdef CONFIG_VENDOR_RWNX_VHT_NO80
		band_2ghz->vht_cap.cap |= IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
		band_2ghz->vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_80;
	}

	if (rwnx_hw->band_5g_support) {
		band_5ghz->vht_cap.vht_supported = true;
		if (rwnx_hw->mod_params->sgi80)
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
		if (rwnx_hw->mod_params->stbc_on)
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_RXSTBC_1;
		if (rwnx_hw->mod_params->ldpc_on)
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_RXLDPC;
		if (rwnx_hw->mod_params->bfmee) {
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
			band_5ghz->vht_cap.cap |= 3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
		}
		if (nss > 1)
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_TXSTBC;

		// Update the AMSDU max RX size (not shifted as located at offset 0 of
		// the VHT cap)
		band_5ghz->vht_cap.cap |= rwnx_hw->mod_params->amsdu_rx_max;

		if (rwnx_hw->mod_params->bfmer) {
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
			/* Set number of sounding dimensions */
			band_5ghz->vht_cap.cap |=
				(nss - 1) << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
		}
		if (rwnx_hw->mod_params->murx)
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
		if (rwnx_hw->mod_params->mutx)
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

		/*
		 * MCS map:
		 * This capabilities are filled according to the mcs_map module
		 * parameter. However currently we have some limitations due to FPGA
		 * clock constraints that prevent always using the range of MCS that is
		 * defined by the parameter:
		 *   - in RX, 2SS, we support up to MCS7
		 *   - in TX, 2SS, we support up to MCS8
		 */
		// Get max supported BW
		if (rwnx_hw->mod_params->use_80)
			bw_max = PHY_CHNL_BW_80;
		else if (rwnx_hw->mod_params->use_2040)
			bw_max = PHY_CHNL_BW_40;
		else
			bw_max = PHY_CHNL_BW_20;

		// Check if MCS map should be limited to MCS0_8 due to the standard.
		// Indeed in BW20, MCS9 is not supported in 1 and 2 SS
		if (rwnx_hw->mod_params->use_2040)
			mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_9;
		else
			mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_8;

		mcs_map = min_t(int, rwnx_hw->mod_params->mcs_map, mcs_map_max);
		band_5ghz->vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(0);
		for (i = 0; i < nss; i++) {
			band_5ghz->vht_cap.vht_mcs.rx_mcs_map |=
				cpu_to_le16(mcs_map << (i * 2));
			band_5ghz->vht_cap.vht_mcs.rx_highest =
				cpu_to_le16(MAX_VHT_RATE(mcs_map, nss, bw_max));
			mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_7;
		}
		for (; i < 8; i++) {
			band_5ghz->vht_cap.vht_mcs.rx_mcs_map |=
				cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
		}

		mcs_map = min_t(int, rwnx_hw->mod_params->mcs_map, mcs_map_max);
		band_5ghz->vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(0);
		for (i = 0; i < nss; i++) {
			band_5ghz->vht_cap.vht_mcs.tx_mcs_map |=
				cpu_to_le16(mcs_map << (i * 2));
			band_5ghz->vht_cap.vht_mcs.tx_highest =
				cpu_to_le16(MAX_VHT_RATE(mcs_map, nss, bw_max));
			mcs_map = min_t(int, rwnx_hw->mod_params->mcs_map,
					IEEE80211_VHT_MCS_SUPPORT_0_8);
		}
		for (; i < 8; i++) {
			band_5ghz->vht_cap.vht_mcs.tx_mcs_map |=
				cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
		}

		if (!rwnx_hw->mod_params->use_80) {
#ifdef CONFIG_VENDOR_RWNX_VHT_NO80
			band_5ghz->vht_cap.cap |= IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
			band_5ghz->vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_80;
		}
	}
}

static void rwnx_set_ht_capa(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	struct ieee80211_supported_band *band_5ghz =
		wiphy->bands[NL80211_BAND_5GHZ];
	struct ieee80211_supported_band *band_2ghz =
		wiphy->bands[NL80211_BAND_2GHZ];
	int i;
	int nss = rwnx_hw->mod_params->nss;

	if (!rwnx_hw->mod_params->ht_on) {
		band_2ghz->ht_cap.ht_supported = false;
		if (rwnx_hw->band_5g_support)
			band_5ghz->ht_cap.ht_supported = false;
		return;
	}

	if (rwnx_hw->mod_params->stbc_on)
		band_2ghz->ht_cap.cap |= 1 << IEEE80211_HT_CAP_RX_STBC_SHIFT;
	if (rwnx_hw->mod_params->ldpc_on)
		band_2ghz->ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
	if (rwnx_hw->mod_params->use_2040) {
		band_2ghz->ht_cap.mcs.rx_mask[4] = 0x1; /* MCS32 */
		band_2ghz->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		band_2ghz->ht_cap.mcs.rx_highest = cpu_to_le16(135 * nss);
	} else {
		band_2ghz->ht_cap.mcs.rx_highest = cpu_to_le16(65 * nss);
	}
	if (nss > 1)
		band_2ghz->ht_cap.cap |= IEEE80211_HT_CAP_TX_STBC;

	// Update the AMSDU max RX size
	if (rwnx_hw->mod_params->amsdu_rx_max)
		band_2ghz->ht_cap.cap |= IEEE80211_HT_CAP_MAX_AMSDU;

	if (rwnx_hw->mod_params->sgi) {
		band_2ghz->ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;
		if (rwnx_hw->mod_params->use_2040) {
			band_2ghz->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
			band_2ghz->ht_cap.mcs.rx_highest = cpu_to_le16(150 * nss);
		} else {
			band_2ghz->ht_cap.mcs.rx_highest = cpu_to_le16(72 * nss);
		}
	}
	if (rwnx_hw->mod_params->gf_rx_on)
		band_2ghz->ht_cap.cap |= IEEE80211_HT_CAP_GRN_FLD;

	nss = min_t(int, nss, IEEE80211_HT_MCS_MASK_LEN);

	for (i = 0; i < nss; i++)
		band_2ghz->ht_cap.mcs.rx_mask[i] = 0xFF;

	if (rwnx_hw->band_5g_support)
		band_5ghz->ht_cap = band_2ghz->ht_cap;
}

static void rwnx_set_he_capa(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	struct ieee80211_supported_band *band_5ghz =
		wiphy->bands[NL80211_BAND_5GHZ];
	struct ieee80211_supported_band *band_2ghz =
		wiphy->bands[NL80211_BAND_2GHZ];
	int i;
	int nss = rwnx_hw->mod_params->nss;
	struct ieee80211_sta_he_cap *he_cap;
	int mcs_map;

	if (!rwnx_hw->mod_params->he_on) {
		band_2ghz->iftype_data = NULL;
		band_2ghz->n_iftype_data = 0;

		if (rwnx_hw->band_5g_support) {
			band_5ghz->iftype_data = NULL;
			band_5ghz->n_iftype_data = 0;
		}
		return;
	}
	he_cap = &rwnx_he_capa[0].he_cap;
	he_cap->has_he = true;
	he_cap->he_cap_elem.mac_cap_info[2] |= IEEE80211_HE_MAC_CAP2_ALL_ACK;

	if (rwnx_hw->mod_params->use_2040) {
		he_cap->he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		he_cap->ppe_thres[0] |= 0x10;
	}
	if (rwnx_hw->mod_params->use_80) {
		he_cap->ppe_thres[0] |= 0x20;
		he_cap->ppe_thres[2] |= 0xc0;
		he_cap->ppe_thres[3] |= 0x07;
	}
	he_cap->he_cap_elem.phy_cap_info[0] |=
		IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
	if (rwnx_hw->mod_params->ldpc_on) {
		he_cap->he_cap_elem.phy_cap_info[1] |=
			IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
	} else {
		// If no LDPC is supported, we have to limit to MCS0_9, as LDPC is
		// mandatory for MCS 10 and 11
		rwnx_hw->mod_params->he_mcs_map = min_t(int, rwnx_hw->mod_params->mcs_map,
							IEEE80211_HE_MCS_SUPPORT_0_9);
	}
	he_cap->he_cap_elem.phy_cap_info[1] |=
		IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US |
		IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS;

	he_cap->he_cap_elem.phy_cap_info[2] |=
		IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS |
		IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
		IEEE80211_HE_PHY_CAP2_DOPPLER_RX;
	if (rwnx_hw->mod_params->stbc_on)
		he_cap->he_cap_elem.phy_cap_info[2] |=
			IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;

	he_cap->he_cap_elem.phy_cap_info[3] |=
		IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
		IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1 |
		IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU;

	if (rwnx_hw->mod_params->bfmee) {
		he_cap->he_cap_elem.phy_cap_info[4] |=
			IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
		he_cap->he_cap_elem.phy_cap_info[4] |=
			IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
	}
	he_cap->he_cap_elem.phy_cap_info[5] |=
		IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
		IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
	he_cap->he_cap_elem.phy_cap_info[6] |=
		IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
		IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
		IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
		IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
		IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT |
		IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;
	he_cap->he_cap_elem.phy_cap_info[7] |=
		IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
	he_cap->he_cap_elem.phy_cap_info[8] |=
		IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G;
	he_cap->he_cap_elem.phy_cap_info[9] |=
		IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
		IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB;

	if (rwnx_hw->chip_ops->is_old_ic) {
		mcs_map = min_t(int, rwnx_hw->mod_params->he_mcs_map,
				IEEE80211_HE_MCS_SUPPORT_0_9);
	} else {
		mcs_map = rwnx_hw->mod_params->he_mcs_map;
	}

	memset(&he_cap->he_mcs_nss_supp, 0, sizeof(he_cap->he_mcs_nss_supp));

	nss = min_t(int, nss, 16);

	for (i = 0; i < nss; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		he_cap->he_mcs_nss_supp.rx_mcs_80 |= cpu_to_le16(mcs_map << (i * 2));
		he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
		mcs_map = IEEE80211_HE_MCS_SUPPORT_0_7;
	}
	for (; i < 8; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		he_cap->he_mcs_nss_supp.rx_mcs_80 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
	}
	mcs_map = rwnx_hw->mod_params->he_mcs_map;
	for (i = 0; i < nss; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		he_cap->he_mcs_nss_supp.tx_mcs_80 |= cpu_to_le16(mcs_map << (i * 2));
		he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
		mcs_map = min_t(int, rwnx_hw->mod_params->he_mcs_map,
				IEEE80211_HE_MCS_SUPPORT_0_7);
	}
	for (; i < 8; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		he_cap->he_mcs_nss_supp.tx_mcs_80 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
	}

	if (rwnx_hw->band_5g_support) {
		he_cap = &rwnx_he_capa[0].he_cap;
		he_cap->has_he = true;
		he_cap->he_cap_elem.mac_cap_info[2] |= IEEE80211_HE_MAC_CAP2_ALL_ACK;
		if (rwnx_hw->mod_params->use_2040) {
			he_cap->he_cap_elem.phy_cap_info[0] |=
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
			he_cap->ppe_thres[0] |= 0x10;
		}
		if (rwnx_hw->mod_params->use_80) {
			he_cap->ppe_thres[0] |= 0x20;
			he_cap->ppe_thres[2] |= 0xc0;
			he_cap->ppe_thres[3] |= 0x07;
		}

		he_cap->he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;

		if (rwnx_hw->mod_params->ldpc_on) {
			he_cap->he_cap_elem.phy_cap_info[1] |=
				IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
		} else {
			// If no LDPC is supported, we have to limit to MCS0_9, as LDPC is
			// mandatory for MCS 10 and 11
			rwnx_hw->mod_params->he_mcs_map = min_t(int, rwnx_hw->mod_params->mcs_map,
								IEEE80211_HE_MCS_SUPPORT_0_9);
		}
		he_cap->he_cap_elem.phy_cap_info[1] |=
			IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US |
			IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS;
		he_cap->he_cap_elem.phy_cap_info[2] |=
			IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS |
			IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
			IEEE80211_HE_PHY_CAP2_DOPPLER_RX;
		if (rwnx_hw->mod_params->stbc_on)
			he_cap->he_cap_elem.phy_cap_info[2] |=
				IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;
		he_cap->he_cap_elem.phy_cap_info[3] |=
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1 |
			IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU;
		if (rwnx_hw->mod_params->bfmee) {
			he_cap->he_cap_elem.phy_cap_info[4] |=
				IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
			he_cap->he_cap_elem.phy_cap_info[4] |=
				IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
		}
		he_cap->he_cap_elem.phy_cap_info[5] |=
			IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
			IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
		he_cap->he_cap_elem.phy_cap_info[6] |=
			IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
			IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
			IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
			IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
			IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT |
			IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;

		he_cap->he_cap_elem.phy_cap_info[7] |=
			IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
		he_cap->he_cap_elem.phy_cap_info[8] |=
			IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G;
		he_cap->he_cap_elem.phy_cap_info[9] |=
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB;

		if (rwnx_hw->chip_ops->is_old_ic) {
			mcs_map = min_t(int, rwnx_hw->mod_params->he_mcs_map,
					IEEE80211_HE_MCS_SUPPORT_0_9);
		} else {
			mcs_map = rwnx_hw->mod_params->he_mcs_map;
		}

		memset(&he_cap->he_mcs_nss_supp, 0, sizeof(he_cap->he_mcs_nss_supp));
		for (i = 0; i < nss; i++) {
			__le16 unsup_for_ss =
				cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
			he_cap->he_mcs_nss_supp.rx_mcs_80 |=
				cpu_to_le16(mcs_map << (i * 2));
			he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
			he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
			mcs_map = IEEE80211_HE_MCS_SUPPORT_0_7;
		}
		for (; i < 8; i++) {
			__le16 unsup_for_ss =
				cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
			he_cap->he_mcs_nss_supp.rx_mcs_80 |= unsup_for_ss;
			he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
			he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
		}
		mcs_map = rwnx_hw->mod_params->he_mcs_map;
		for (i = 0; i < nss; i++) {
			__le16 unsup_for_ss =
				cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
			he_cap->he_mcs_nss_supp.tx_mcs_80 |=
				cpu_to_le16(mcs_map << (i * 2));
			he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
			he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
			mcs_map = min_t(int, rwnx_hw->mod_params->he_mcs_map,
					IEEE80211_HE_MCS_SUPPORT_0_7);
		}
		for (; i < 8; i++) {
			__le16 unsup_for_ss =
				cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
			he_cap->he_mcs_nss_supp.tx_mcs_80 |= unsup_for_ss;
			he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
			he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
		}
	}
}

static void rwnx_set_wiphy_params(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
#ifdef CONFIG_RWNX_FULLMAC
	/* FULLMAC specific parameters */
	wiphy->flags |= WIPHY_FLAG_REPORTS_OBSS;
	wiphy->max_scan_ssids = SCAN_SSID_MAX;
	wiphy->max_scan_ie_len = SCANU_MAX_IE_LEN;
#endif /* CONFIG_RWNX_FULLMAC */

#if !IS_ENABLED(CONFIG_AIC8800_STA_ONLY)
	if (rwnx_hw->mod_params->tdls) {
		/* TDLS support */
		wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
#ifdef CONFIG_RWNX_FULLMAC
		/* TDLS external setup support */
		wiphy->flags |= WIPHY_FLAG_TDLS_EXTERNAL_SETUP;
#endif
	}

	if (rwnx_hw->mod_params->ap_uapsd_on)
		wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
#endif

#ifdef CONFIG_RWNX_FULLMAC
	if (rwnx_hw->mod_params->ps_on)
		wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif

#if !IS_ENABLED(CONFIG_AIC8800_STA_ONLY)
	if (rwnx_hw->mod_params->custregd) {
		// Check if custom channel set shall be enabled. In such case only
		// monitor mode is supported
		if (rwnx_hw->mod_params->custchan) {
			wiphy->interface_modes = BIT(NL80211_IFTYPE_MONITOR);

			// Enable "extra" channels
			wiphy->bands[NL80211_BAND_2GHZ]->n_channels += 13;
			if (rwnx_hw->band_5g_support)
				wiphy->bands[NL80211_BAND_5GHZ]->n_channels += 59;
		}
	}
#endif
}

int rwnx_handle_dynparams(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	/* Set chip capabilities */
	aic_chip_init_capa(rwnx_hw);
	/* Set wiphy parameters */
	rwnx_set_wiphy_params(rwnx_hw, wiphy);
	/* Set VHT capabilities */
	rwnx_set_vht_capa(rwnx_hw, wiphy);
	/* Set HE capabilities */
	rwnx_set_he_capa(rwnx_hw, wiphy);
	/* Set HT capabilities */
	rwnx_set_ht_capa(rwnx_hw, wiphy);
	/* Set RF specific parameters (shall be done last as it might change some
	 * capabilities previously set)
	 */
	return 0;
}

void rwnx_custregd(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
// For older kernel version, the custom regulatory is applied before the wiphy
// registration (in rwnx_set_wiphy_params()), so nothing has to be done here
	if (!rwnx_hw->mod_params->custregd)
		return;

	wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED;

	rtnl_lock();
#ifdef CONFIG_AIC8800_AUTO_CUSTREG
	if (rwnx_regulatory_set_wiphy_regd_sync(wiphy,
						get_regdomain_from_rwnx_db(wiphy,
									   rwnx_hw->country_abbr)))
		AICWFDBG(LOGERROR, "Failed to set custom regdomain\n");
	else
		AICWFDBG(LOGINFO, "USING REGULATORY_WIPHY_SELF_MANAGED\n");
#else
	if (rwnx_regulatory_set_wiphy_regd_sync(wiphy,
						get_regdomain_from_rwnx_db(wiphy,
									   default_ccode))) {
		AICWFDBG(LOGERROR, "Failed to set custom regdomain\n");
	} else {
		AICWFDBG(LOGINFO, "USING REGULATORY_WIPHY_SELF_MANAGED\n");
	}
#endif
	rtnl_unlock();
}
