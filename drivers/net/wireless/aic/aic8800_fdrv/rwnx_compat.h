/* SPDX-License-Identifier: GPL-2.0 */
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief Ensure driver compilation for linux 3.16 to 3.19
 * To avoid too many #if LINUX_VERSION_CODE if the code, when prototype change
 * between different kernel version:
 * - For external function, define a macro whose name is the function name with
 *   _compat suffix and prototype (actually the number of parameter) of the
 *   latest version. Then latest version this macro simply call the function
 *   and for older kernel version it call the function adapting the api.
 * - For internal function (e.g. cfg80211_ops) do the same but the macro name
 *   doesn't need to have the _compat suffix when the function is not used
 *   directly by the driver
 *
 ******************************************************************************
 */

#ifndef _RWNX_COMPAT_H_
#define _RWNX_COMPAT_H_
#include <linux/version.h>

#include <linux/bitfield.h>

/* CFG80211 */

#ifdef CONFIG_RWNX_MUMIMO_TX
#define CONFIG_USER_MAX 2
#else
#define CONFIG_USER_MAX 1
#endif

#define NX_VIRT_DEV_MAX 4

#define NX_REMOTE_STA_MAX_FOR_OLD_IC 10
#define NX_REMOTE_STA_MAX 32

#define NX_MU_GROUP_MAX 62
#define NX_TXDESC_CNT 64
#define NX_TX_MAX_RATES 4
#define NX_CHAN_CTXT_CNT 3

#ifdef CONFIG_RWNX_BCMC
#define NX_TXQ_CNT 5
#else
#define NX_TXQ_CNT 4
#endif

#define IEEE80211_MAX_AMPDU_BUF IEEE80211_MAX_AMPDU_BUF_HE
#define IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB                            \
	IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB                            \
	IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB
#define IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA                    \
	IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU

#define CCFS0(vht) ((vht)->center_freq_seg0_idx)
#define CCFS1(vht) ((vht)->center_freq_seg1_idx)

#define SURVEY_TIME(s)      ((s)->time)
#define SURVEY_TIME_BUSY(s) ((s)->time_busy)
#define STA_TDLS_INITIATOR(sta) ((sta)->tdls_initiator)

#define RX_ENC_HT(s) ((s)->encoding = RX_ENC_HT)
#define RX_ENC_VHT(s) ((s)->encoding = RX_ENC_VHT)
#define RX_ENC_HE(s) ((s)->encoding = RX_ENC_HE)
#define RX_ENC_FLAG_SHORT_GI(s)  ((s)->enc_flags |= RX_ENC_FLAG_SHORT_GI)
#define RX_ENC_FLAG_SHORT_PRE(s) ((s)->enc_flags |= RX_ENC_FLAG_SHORTPRE)
#define RX_ENC_FLAG_LDPC(s)      ((s)->enc_flags |= RX_ENC_FLAG_LDPC)
#define RX_BW_40MHZ(s)           ((s)->bw = RATE_INFO_BW_40)
#define RX_BW_80MHZ(s)           ((s)->bw = RATE_INFO_BW_80)
#define RX_BW_160MHZ(s)          ((s)->bw = RATE_INFO_BW_160)
#define RX_NSS(s)                ((s)->nss)

#ifndef CONFIG_VENDOR_RWNX_AMSDUS_TX
#endif /* CONFIG_VENDOR_RWNX_AMSDUS_TX */

#endif /* _RWNX_COMPAT_H_ */
