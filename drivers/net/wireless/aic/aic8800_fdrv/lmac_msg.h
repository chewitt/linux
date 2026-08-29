/* SPDX-License-Identifier: GPL-2.0 */
/*
 ****************************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief Main definitions for message exchanges with LMAC
 *
 ****************************************************************************************
 */

#ifndef LMAC_MSG_H_
#define LMAC_MSG_H_

#include "lmac_mac.h"

/* Task identifiers for communication between LMAC and DRIVER */
enum {
	TASK_NONE = 0xff,

	// MAC Management task.
	TASK_MM = 0,
	// DEBUG task
	TASK_DBG,
	/// SCAN task
	TASK_SCAN,
	/// TDLS task
	TASK_TDLS,
	/// SCANU task
	TASK_SCANU,
	/// ME task
	TASK_ME,
	/// SM task
	TASK_SM,
	/// APM task
	TASK_APM,
	/// BAM task
	TASK_BAM,
	/// MESH task
	TASK_MESH,
	/// RXU task
	TASK_RXU,
	/// RM_task
	TASK_RM,
	/// TWT task
	TASK_TWT,
#if defined CONFIG_RWNX_FULLMAC
	// This is used to define the last task that is running on the EMB processor
	TASK_LAST_EMB = TASK_TWT,
#else
#error "Need to define SOFTMAC or FULLMAC"
#endif
	// nX API task
	TASK_API,
	TASK_MAX,
};

/// For MAC HW States copied from "hal_machw.h"
enum {
	/// MAC HW IDLE State.
	HW_IDLE = 0,
	/// MAC HW RESERVED State.
	HW_RESERVED,
	/// MAC HW DOZE State.
	HW_DOZE,
	/// MAC HW ACTIVE State.
	HW_ACTIVE
};

/// Power Save mode setting
enum mm_ps_mode_state {
	MM_PS_MODE_OFF,
	MM_PS_MODE_ON,
	MM_PS_MODE_ON_DYN,
};

/// Status/error codes used in the MAC software.
enum {
	CO_OK,
	CO_FAIL,
	CO_EMPTY,
	CO_FULL,
	CO_BAD_PARAM,
	CO_NOT_FOUND,
	CO_NO_MORE_ELT_AVAILABLE,
	CO_NO_ELT_IN_USE,
	CO_BUSY,
	CO_OP_IN_PROGRESS,
};

/// Remain on channel operation codes
enum mm_remain_on_channel_op {
	MM_ROC_OP_START = 0,
	MM_ROC_OP_CANCEL,
};

#define DRV_TASK_ID 100

/// Message Identifier. The number of messages is limited to 0xFFFF.
/// The message ID is divided in two parts:
/// - bits[15..10] : task index (no more than 64 tasks supported).
/// - bits[9..0] : message index (no more that 1024 messages per task).
typedef u16 lmac_msg_id_t;

typedef u16 lmac_task_id_t;

/// Build the first message ID of a task.
#define LMAC_FIRST_MSG(task) ((lmac_msg_id_t)((task) << 10))

#define MSG_T(msg) ((lmac_task_id_t)((msg) >> 10))
#define MSG_I(msg) ((msg) & ((1 << 10) - 1))

/// Message structure.
struct lmac_msg {
	lmac_msg_id_t id;       ///< Message id.
	lmac_task_id_t dest_id; ///< Destination kernel identifier.
	lmac_task_id_t src_id;  ///< Source kernel identifier.
	u16 param_len;          ///< Parameter embedded struct length.
	u32 param[]; ///< Parameter embedded struct. Must be word-aligned.
};

/// List of messages related to the task.
enum mm_msg_tag {
	/// RESET Request.
	MM_RESET_REQ = LMAC_FIRST_MSG(TASK_MM),
	/// RESET Confirmation.
	MM_RESET_CFM,
	/// START Request.
	MM_START_REQ,
	/// START Confirmation.
	MM_START_CFM,
	/// Read Version Request.
	MM_VERSION_REQ,
	/// Read Version Confirmation.
	MM_VERSION_CFM,
	/// ADD INTERFACE Request.
	MM_ADD_IF_REQ,
	/// ADD INTERFACE Confirmation.
	MM_ADD_IF_CFM,
	/// REMOVE INTERFACE Request.
	MM_REMOVE_IF_REQ,
	/// REMOVE INTERFACE Confirmation.
	MM_REMOVE_IF_CFM,
	/// STA ADD Request.
	MM_STA_ADD_REQ,
	/// STA ADD Confirm.
	MM_STA_ADD_CFM,
	/// STA DEL Request.
	MM_STA_DEL_REQ,
	/// STA DEL Confirm.
	MM_STA_DEL_CFM,
	/// RX FILTER CONFIGURATION Request.
	MM_SET_FILTER_REQ,
	/// RX FILTER CONFIGURATION Confirmation.
	MM_SET_FILTER_CFM,
	/// CHANNEL CONFIGURATION Request.
	MM_SET_CHANNEL_REQ,
	/// CHANNEL CONFIGURATION Confirmation.
	MM_SET_CHANNEL_CFM,
	/// DTIM PERIOD CONFIGURATION Request.
	MM_SET_DTIM_REQ,
	/// DTIM PERIOD CONFIGURATION Confirmation.
	MM_SET_DTIM_CFM,
	/// BEACON INTERVAL CONFIGURATION Request.
	MM_SET_BEACON_INT_REQ,
	/// BEACON INTERVAL CONFIGURATION Confirmation.
	MM_SET_BEACON_INT_CFM,
	/// BASIC RATES CONFIGURATION Request.
	MM_SET_BASIC_RATES_REQ,
	/// BASIC RATES CONFIGURATION Confirmation.
	MM_SET_BASIC_RATES_CFM,
	/// BSSID CONFIGURATION Request.
	MM_SET_BSSID_REQ,
	/// BSSID CONFIGURATION Confirmation.
	MM_SET_BSSID_CFM,
	/// EDCA PARAMETERS CONFIGURATION Request.
	MM_SET_EDCA_REQ,
	/// EDCA PARAMETERS CONFIGURATION Confirmation.
	MM_SET_EDCA_CFM,
	/// ABGN MODE CONFIGURATION Request.
	MM_SET_MODE_REQ,
	/// ABGN MODE CONFIGURATION Confirmation.
	MM_SET_MODE_CFM,
	/// Request setting the VIF active state (i.e associated or AP started)
	MM_SET_VIF_STATE_REQ,
	/// Confirmation of the @ref MM_SET_VIF_STATE_REQ message.
	MM_SET_VIF_STATE_CFM,
	/// SLOT TIME PARAMETERS CONFIGURATION Request.
	MM_SET_SLOTTIME_REQ,
	/// SLOT TIME PARAMETERS CONFIGURATION Confirmation.
	MM_SET_SLOTTIME_CFM,
	/// Power Mode Change Request.
	MM_SET_IDLE_REQ,
	/// Power Mode Change Confirm.
	MM_SET_IDLE_CFM,
	/// KEY ADD Request.
	MM_KEY_ADD_REQ,
	/// KEY ADD Confirm.
	MM_KEY_ADD_CFM,
	/// KEY DEL Request.
	MM_KEY_DEL_REQ,
	/// KEY DEL Confirm.
	MM_KEY_DEL_CFM,
	/// Block Ack agreement info addition
	MM_BA_ADD_REQ,
	/// Block Ack agreement info addition confirmation
	MM_BA_ADD_CFM,
	/// Block Ack agreement info deletion
	MM_BA_DEL_REQ,
	/// Block Ack agreement info deletion confirmation
	MM_BA_DEL_CFM,
	/// Indication of the primary TBTT to the upper MAC. Upon the reception of
	/// this
	// message the upper MAC has to push the beacon(s) to the beacon
	// transmission queue.
	MM_PRIMARY_TBTT_IND,
	/// Indication of the secondary TBTT to the upper MAC. Upon the reception of
	/// this
	// message the upper MAC has to push the beacon(s) to the beacon
	// transmission queue.
	MM_SECONDARY_TBTT_IND,
	/// Request for changing the TX power
	MM_SET_POWER_REQ,
	/// Confirmation of the TX power change
	MM_SET_POWER_CFM,
	/// Request to the LMAC to trigger the embedded logic analyzer and forward
	/// the debug
	/// dump.
	MM_DBG_TRIGGER_REQ,
	/// Set Power Save mode
	MM_SET_PS_MODE_REQ,
	/// Set Power Save mode confirmation
	MM_SET_PS_MODE_CFM,
	/// Request to add a channel context
	MM_CHAN_CTXT_ADD_REQ,
	/// Confirmation of the channel context addition
	MM_CHAN_CTXT_ADD_CFM,
	/// Request to delete a channel context
	MM_CHAN_CTXT_DEL_REQ,
	/// Confirmation of the channel context deletion
	MM_CHAN_CTXT_DEL_CFM,
	/// Request to link a channel context to a VIF
	MM_CHAN_CTXT_LINK_REQ,
	/// Confirmation of the channel context link
	MM_CHAN_CTXT_LINK_CFM,
	/// Request to unlink a channel context from a VIF
	MM_CHAN_CTXT_UNLINK_REQ,
	/// Confirmation of the channel context unlink
	MM_CHAN_CTXT_UNLINK_CFM,
	/// Request to update a channel context
	MM_CHAN_CTXT_UPDATE_REQ,
	/// Confirmation of the channel context update
	MM_CHAN_CTXT_UPDATE_CFM,
	/// Request to schedule a channel context
	MM_CHAN_CTXT_SCHED_REQ,
	/// Confirmation of the channel context scheduling
	MM_CHAN_CTXT_SCHED_CFM,
	/// Request to change the beacon template in LMAC
	MM_BCN_CHANGE_REQ,
	/// Confirmation of the beacon change
	MM_BCN_CHANGE_CFM,
	/// Request to update the TIM in the beacon (i.e to indicate traffic
	/// bufferized at AP)
	MM_TIM_UPDATE_REQ,
	/// Confirmation of the TIM update
	MM_TIM_UPDATE_CFM,
	/// Connection loss indication
	MM_CONNECTION_LOSS_IND,
	/// Channel context switch indication to the upper layers
	MM_CHANNEL_SWITCH_IND,
	/// Channel context pre-switch indication to the upper layers
	MM_CHANNEL_PRE_SWITCH_IND,
	/// Request to remain on channel or cancel remain on channel
	MM_REMAIN_ON_CHANNEL_REQ,
	/// Confirmation of the (cancel) remain on channel request
	MM_REMAIN_ON_CHANNEL_CFM,
	/// Remain on channel expired indication
	MM_REMAIN_ON_CHANNEL_EXP_IND,
	/// Indication of a PS state change of a peer device
	MM_PS_CHANGE_IND,
	/// Indication that some buffered traffic should be sent to the peer device
	MM_TRAFFIC_REQ_IND,
	/// Request to modify the STA Power-save mode options
	MM_SET_PS_OPTIONS_REQ,
	/// Confirmation of the PS options setting
	MM_SET_PS_OPTIONS_CFM,
	/// Indication of PS state change for a P2P VIF
	MM_P2P_VIF_PS_CHANGE_IND,
	/// Indication that CSA counter has been updated
	MM_CSA_COUNTER_IND,
	/// Channel occupation report indication
	MM_CHANNEL_SURVEY_IND,
	/// Message containing Beamformer Information
	MM_BFMER_ENABLE_REQ,
	/// Request to Start/Stop/Update NOA - GO Only
	MM_SET_P2P_NOA_REQ,
	/// Request to Start/Stop/Update Opportunistic PS - GO Only
	MM_SET_P2P_OPPPS_REQ,
	/// Start/Stop/Update NOA Confirmation
	MM_SET_P2P_NOA_CFM,
	/// Start/Stop/Update Opportunistic PS Confirmation
	MM_SET_P2P_OPPPS_CFM,
	/// P2P NoA Update Indication - GO Only
	MM_P2P_NOA_UPD_IND,
	/// Request to set RSSI threshold and RSSI hysteresis
	MM_CFG_RSSI_REQ,
	/// Indication that RSSI level is below or above the threshold
	MM_RSSI_STATUS_IND,
	/// Indication that CSA is done
	MM_CSA_FINISH_IND,
	/// Indication that CSA is in prorgess (resp. done) and traffic must be
	/// stopped (resp. restarted)
	MM_CSA_TRAFFIC_IND,
	/// Request to update the group information of a station
	MM_MU_GROUP_UPDATE_REQ,
	/// Confirmation of the @ref MM_MU_GROUP_UPDATE_REQ message
	MM_MU_GROUP_UPDATE_CFM,
	/// Request to initialize the antenna diversity algorithm
	MM_ANT_DIV_INIT_REQ,
	/// Request to stop the antenna diversity algorithm
	MM_ANT_DIV_STOP_REQ,
	/// Request to update the antenna switch status
	MM_ANT_DIV_UPDATE_REQ,
	/// Request to switch the antenna connected to path_0
	MM_SWITCH_ANTENNA_REQ,
	/// Indication that a packet loss has occurred
	MM_PKTLOSS_IND,

	MM_SET_ARPOFFLOAD_REQ,
	MM_SET_ARPOFFLOAD_CFM,
	MM_SET_AGG_DISABLE_REQ,
	MM_SET_AGG_DISABLE_CFM,
	MM_SET_COEX_REQ,
	MM_SET_COEX_CFM,
	MM_SET_RF_CONFIG_REQ,
	MM_SET_RF_CONFIG_CFM,
	MM_SET_RF_CALIB_REQ,
	MM_SET_RF_CALIB_CFM,

	/// MU EDCA PARAMETERS Configuration Request.
	MM_SET_MU_EDCA_REQ,
	/// MU EDCA PARAMETERS Configuration Confirmation.
	MM_SET_MU_EDCA_CFM,
	/// UORA PARAMETERS Configuration Request.
	MM_SET_UORA_REQ,
	/// UORA PARAMETERS Configuration Confirmation.
	MM_SET_UORA_CFM,
	/// TXOP RTS THRESHOLD Configuration Request.
	MM_SET_TXOP_RTS_THRES_REQ,
	/// TXOP RTS THRESHOLD Configuration Confirmation.
	MM_SET_TXOP_RTS_THRES_CFM,
	/// HE BSS Color Configuration Request.
	MM_SET_BSS_COLOR_REQ,
	/// HE BSS Color Configuration Confirmation.
	MM_SET_BSS_COLOR_CFM,

	MM_GET_MAC_ADDR_REQ,
	MM_GET_MAC_ADDR_CFM,

	MM_GET_STA_INFO_REQ,
	MM_GET_STA_INFO_CFM,

	MM_SET_TXPWR_IDX_LVL_REQ,
	MM_SET_TXPWR_IDX_LVL_CFM,

	MM_SET_TXPWR_OFST_REQ,
	MM_SET_TXPWR_OFST_CFM,

	MM_SET_STACK_START_REQ,
	MM_SET_STACK_START_CFM,

	MM_APM_STALOSS_IND,

	MM_SET_VENDOR_HWCONFIG_REQ,
	MM_SET_VENDOR_HWCONFIG_CFM,

	MM_GET_FW_VERSION_REQ,
	MM_GET_FW_VERSION_CFM,

	MM_SET_RESUME_RESTORE_REQ,
	MM_SET_RESUME_RESTORE_CFM,

	MM_GET_WIFI_DISABLE_REQ,
	MM_GET_WIFI_DISABLE_CFM,

	MM_CFG_RSSI_CFM,

	MM_SET_VENDOR_SWCONFIG_REQ,
	MM_SET_VENDOR_SWCONFIG_CFM,

	MM_SET_TXPWR_LVL_ADJ_REQ,
	MM_SET_TXPWR_LVL_ADJ_CFM,

	MM_RADAR_DETECT_IND,

	MM_SET_APF_PROG_REQ,
	MM_SET_APF_PROG_CFM,

	MM_GET_APF_PROG_REQ,
	MM_GET_APF_PROG_CFM,

	/// arp announcement frame reception indication
	MM_ARP_ANNOUNCEMENT_IND,

	/// MAX number of messages
	MM_MAX,
};

/// Interface types
enum {
	/// ESS STA interface
	MM_STA,
	/// IBSS STA interface
	MM_IBSS,
	/// AP interface
	MM_AP,
	// Mesh Point interface
	MM_MESH_POINT,
	// Monitor interface
	MM_MONITOR,
};

/// BA agreement types
enum {
	/// BlockAck agreement for TX
	BA_AGMT_TX,
	/// BlockAck agreement for RX
	BA_AGMT_RX,
};

/// BA agreement related status
enum {
	/// Correct BA agreement establishment
	BA_AGMT_ESTABLISHED,
	/// BA agreement already exists for STA+TID requested, cannot override it
	/// (should have been deleted first)
	BA_AGMT_ALREADY_EXISTS,
	/// Correct BA agreement deletion
	BA_AGMT_DELETED,
	/// BA agreement for the (STA, TID) doesn't exist so nothing to delete
	BA_AGMT_DOESNT_EXIST,
};

/// Features supported by LMAC - Positions
enum mm_features {
	/// Beaconing
	MM_FEAT_BCN_BIT = 0,
	/*
	 *	/// Autonomous Beacon Transmission
	 *	MM_FEAT_AUTOBCN_BIT,
	 *	/// Scan in LMAC
	 *	MM_FEAT_HWSCAN_BIT,
	 *	/// Connection Monitoring
	 *	MM_FEAT_CMON_BIT,
	 *	/// Multi Role
	 *	MM_FEAT_MROLE_BIT,
	 */
	/// Radar Detection
	MM_FEAT_RADAR_BIT,
	/// Power Save
	MM_FEAT_PS_BIT,
	/// UAPSD
	MM_FEAT_UAPSD_BIT,
	/// DPSM
	//  MM_FEAT_DPSM_BIT,
	/// A-MPDU
	MM_FEAT_AMPDU_BIT,
	/// A-MSDU
	MM_FEAT_AMSDU_BIT,
	/// Channel Context
	//  MM_FEAT_CHNL_CTXT_BIT,
	/// Packet reordering
	//  MM_FEAT_REORD_BIT,
	/// P2P
	MM_FEAT_P2P_BIT,
	/// P2P Go
	MM_FEAT_P2P_GO_BIT,
	/// UMAC Present
	MM_FEAT_UMAC_BIT,
	/// VHT support
	MM_FEAT_VHT_BIT,
	/// Beamformee
	MM_FEAT_BFMEE_BIT,
	/// Beamformer
	MM_FEAT_BFMER_BIT,
	/// WAPI
	MM_FEAT_WAPI_BIT,
	/// MFP
	MM_FEAT_MFP_BIT,
	/// Mu-MIMO RX support
	MM_FEAT_MU_MIMO_RX_BIT,
	/// Mu-MIMO TX support
	MM_FEAT_MU_MIMO_TX_BIT,
	/// Wireless Mesh Networking
	MM_FEAT_MESH_BIT,
	/// TDLS support
	MM_FEAT_TDLS_BIT,
	/// Antenna Diversity support
	MM_FEAT_ANT_DIV_BIT,
	/// UF support
	MM_FEAT_UF_BIT,
	/// A-MSDU maximum size (bit0)
	MM_AMSDU_MAX_SIZE_BIT0,
	/// A-MSDU maximum size (bit1)
	MM_AMSDU_MAX_SIZE_BIT1,
	/// MON_DATA support
	MM_FEAT_MON_DATA_BIT,
	/// HE (802.11ax) support
	MM_FEAT_HE_BIT,
	/// TWT support
	MM_FEAT_TWT_BIT,
};

/// Maximum number of words in the configuration buffer
#define PHY_CFG_BUF_SIZE 16

/// Structure containing the parameters of the PHY configuration
struct phy_cfg_tag {
	/// Buffer containing the parameters specific for the PHY used
	u32 parameters[PHY_CFG_BUF_SIZE];
};

/// Structure containing the parameters of the Trident PHY configuration
struct phy_trd_cfg_tag {
	/// MDM type(nxm)(upper nibble) and MDM2RF path mapping(lower nibble)
	u8 path_mapping;
	/// TX DC offset compensation
	u32 tx_dc_off_comp;
};

/// Structure containing the parameters of the Karst PHY configuration
struct phy_karst_cfg_tag {
	/// TX IQ mismatch compensation in 2.4GHz
	u32 tx_iq_comp_2_4G[2];
	/// RX IQ mismatch compensation in 2.4GHz
	u32 rx_iq_comp_2_4G[2];
	/// TX IQ mismatch compensation in 5GHz
	u32 tx_iq_comp_5G[2];
	/// RX IQ mismatch compensation in 5GHz
	u32 rx_iq_comp_5G[2];
	/// RF path used by default (0 or 1)
	u8 path_used;
};

/// Structure containing the parameters of the @ref MM_START_REQ message
struct mm_start_req {
	/// PHY configuration
	struct phy_cfg_tag phy_cfg;
	/// UAPSD timeout
	u32 uapsd_timeout;
	/// Local LP clock accuracy (in ppm)
	u16 lp_clk_accuracy;
};

/// Structure containing the parameters of the @ref MM_SET_CHANNEL_REQ message
struct mm_set_channel_req {
	/// Channel information
	struct mac_chan_op chan;
	/// Index of the RF for which the channel has to be set (0: operating
	/// (primary), 1: secondary RF (used for additional radar detection). This
	/// parameter is reserved if no secondary RF is available in the system
	u8 index;
};

/// Structure containing the parameters of the @ref MM_SET_CHANNEL_CFM message
struct mm_set_channel_cfm {
	/// Radio index to be used in policy table
	u8 radio_idx;
	/// TX power configured (in dBm)
	s8 power;
};

/// Structure containing the parameters of the @ref MM_SET_DTIM_REQ message
struct mm_set_dtim_req {
	/// DTIM period
	u8 dtim_period;
};

/// Structure containing the parameters of the @ref MM_SET_POWER_REQ message
struct mm_set_power_req {
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
	/// TX power (in dBm)
	s8 power;
};

/// Structure containing the parameters of the @ref MM_SET_POWER_CFM message
struct mm_set_power_cfm {
	/// Radio index to be used in policy table
	u8 radio_idx;
	/// TX power configured (in dBm)
	s8 power;
};

/// Structure containing the parameters of the @ref MM_SET_BEACON_INT_REQ
/// message
struct mm_set_beacon_int_req {
	/// Beacon interval
	u16 beacon_int;
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_BASIC_RATES_REQ
/// message
struct mm_set_basic_rates_req {
	/// Basic rate set (as expected by bssBasicRateSet field of Rates MAC HW
	/// register)
	u32 rates;
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
	/// Band on which the interface will operate
	u8 band;
};

/// Structure containing the parameters of the @ref MM_SET_BSSID_REQ message
struct mm_set_bssid_req {
	/// BSSID to be configured in HW
	struct mac_addr bssid;
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_FILTER_REQ message
struct mm_set_filter_req {
	/// RX filter to be put into rxCntrlReg HW register
	u32 filter;
};

/// Structure containing the parameters of the @ref MM_ADD_IF_REQ message.
struct mm_add_if_req {
	/// Type of the interface (AP, STA, ADHOC, ...)
	u8 type;
	/// MAC ADDR of the interface to start
	struct mac_addr addr;
	/// P2P Interface
	bool p2p;
};

/// Structure containing the parameters of the @ref MM_SET_EDCA_REQ message
struct mm_set_edca_req {
	/// EDCA parameters of the queue (as expected by edcaACxReg HW register)
	u32 ac_param;
	/// Flag indicating if UAPSD can be used on this queue
	bool uapsd;
	/// HW queue for which the parameters are configured
	u8 hw_queue;
	/// Index of the interface for which the parameters are configured
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_MU_EDCA_REQ message
struct mm_set_mu_edca_req {
	/// MU EDCA parameters of the different HE queues
	u32 param[AC_MAX];
};

/// Structure containing the parameters of the @ref MM_SET_UORA_REQ message
struct mm_set_uora_req {
	/// Minimum exponent of OFDMA Contention Window.
	u8 eocw_min;
	/// Maximum exponent of OFDMA Contention Window.
	u8 eocw_max;
};

/// Structure containing the parameters of the @ref MM_SET_TXOP_RTS_THRES_REQ
/// message
struct mm_set_txop_rts_thres_req {
	/// TXOP RTS threshold
	u16 txop_dur_rts_thres;
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_BSS_COLOR_REQ message
struct mm_set_bss_color_req {
	/// HE BSS color, formatted as per BSS_COLOR MAC HW register
	u32 bss_color;
};

struct mm_set_idle_req {
	u8 hw_idle;
};

/// Structure containing the parameters of the @ref MM_SET_SLOTTIME_REQ message
struct mm_set_slottime_req {
	/// Slot time expressed in us
	u8 slottime;
};

/// Structure containing the parameters of the @ref MM_SET_MODE_REQ message
struct mm_set_mode_req {
	/// abgnMode field of macCntrl1Reg register
	u8 abgnmode;
};

/// Structure containing the parameters of the @ref MM_SET_VIF_STATE_REQ message
struct mm_set_vif_state_req {
	/// Association Id received from the AP (valid only if the VIF is of STA
	/// type)
	__le16 aid;
	/// Flag indicating if the VIF is active or not
	bool active;
	/// Interface index
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_ADD_IF_CFM message.
struct mm_add_if_cfm {
	/// Status of operation (different from 0 if unsuccessful)
	u8 status;
	/// Interface index assigned by the LMAC
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_REMOVE_IF_REQ message.
struct mm_remove_if_req {
	/// Interface index assigned by the LMAC
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_VERSION_CFM message.
struct mm_version_cfm {
	/// Version of the LMAC FW
	u32 version_lmac;
	/// Version1 of the MAC HW (as encoded in version1Reg MAC HW register)
	u32 version_machw_1;
	/// Version2 of the MAC HW (as encoded in version2Reg MAC HW register)
	u32 version_machw_2;
	/// Version1 of the PHY (depends on actual PHY)
	u32 version_phy_1;
	/// Version2 of the PHY (depends on actual PHY)
	u32 version_phy_2;
	/// Supported Features
	u32 features;
	/// Maximum number of supported stations
	u16 max_sta_nb;
	/// Maximum number of supported virtual interfaces
	u8 max_vif_nb;
};

/// Structure containing the parameters of the @ref MM_STA_ADD_REQ message.
struct mm_sta_add_req {
	/// Bitfield showing some capabilities of the STA (@ref enum mac_sta_flags)
	u32 capa_flags;
	/// Maximum A-MPDU size, in bytes, for HE frames
	u32 ampdu_size_max_he;
	/// Maximum A-MPDU size, in bytes, for VHT frames
	u32 ampdu_size_max_vht;
	/// PAID/GID
	u32 paid_gid;
	/// Maximum A-MPDU size, in bytes, for HT frames
	u16 ampdu_size_max_ht;
	/// MAC address of the station to be added
	struct mac_addr mac_addr;
	/// A-MPDU spacing, in us
	u8 ampdu_spacing_min;
	/// Interface index
	u8 inst_nbr;
	/// TDLS station
	bool tdls_sta;
	/// Indicate if the station is TDLS link initiator station
	bool tdls_sta_initiator;
	/// Indicate if the TDLS Channel Switch is allowed
	bool tdls_chsw_allowed;
	/// nonTransmitted BSSID index, set to the BSSID index in case the STA added
	/// is an AP that is a nonTransmitted BSSID. Should be set to 0 otherwise
	u8 bssid_index;
	/// Maximum BSSID indicator, valid if the STA added is an AP that is a
	/// nonTransmitted BSSID
	u8 max_bssid_ind;
};

/// Structure containing the parameters of the @ref MM_STA_ADD_CFM message.
struct mm_sta_add_cfm {
	/// Status of the operation (different from 0 if unsuccessful)
	u8 status;
	/// Index assigned by the LMAC to the newly added station
	u8 sta_idx;
	/// MAC HW index of the newly added station
	u8 hw_sta_idx;
};

/// Structure containing the parameters of the @ref MM_STA_DEL_REQ message.
struct mm_sta_del_req {
	/// Index of the station to be deleted
	u8 sta_idx;
};

/// Structure containing the parameters of the @ref MM_STA_DEL_CFM message.
struct mm_sta_del_cfm {
	/// Status of the operation (different from 0 if unsuccessful)
	u8 status;
};

/// Structure containing the parameters of the SET_POWER_MODE REQ message.
struct mm_setpowermode_req {
	u8 mode;
	u8 sta_idx;
};

/// Structure containing the parameters of the SET_POWER_MODE CFM message.
struct mm_setpowermode_cfm {
	u8 status;
};

/// Structure containing the parameters of the @ref MM_KEY_ADD REQ message.
struct mm_key_add_req {
	/// Key index (valid only for default keys)
	u8 key_idx;
	/// STA index (valid only for pairwise or mesh group keys)
	u8 sta_idx;
	/// Key material
	struct mac_sec_key key;
	/// Cipher suite (WEP64, WEP128, TKIP, CCMP)
	u8 cipher_suite;
	/// Index of the interface for which the key is set (valid only for default
	/// keys or mesh group keys)
	u8 inst_nbr;
	/// A-MSDU SPP parameter
	u8 spp;
	/// Indicate if provided key is a pairwise key or not
	bool pairwise;
};

/// Structure containing the parameters of the @ref MM_KEY_ADD_CFM message.
struct mm_key_add_cfm {
	/// Status of the operation (different from 0 if unsuccessful)
	u8 status;
	/// HW index of the key just added
	u8 hw_key_idx;
};

/// Structure containing the parameters of the @ref MM_KEY_DEL_REQ message.
struct mm_key_del_req {
	/// HW index of the key to be deleted
	u8 hw_key_idx;
};

/// Structure containing the parameters of the @ref MM_BA_ADD_REQ message.
struct mm_ba_add_req {
	/// Type of agreement (0: TX, 1: RX)
	u8 type;
	/// Index of peer station with which the agreement is made
	u8 sta_idx;
	/// TID for which the agreement is made with peer station
	u8 tid;
	/// Buffer size - number of MPDUs that can be held in its buffer per TID
	u8 bufsz;
	/// Start sequence number negotiated during BA setup - the one in first
	/// aggregated MPDU counts more
	u16 ssn;
};

/// Structure containing the parameters of the @ref MM_BA_ADD_CFM message.
struct mm_ba_add_cfm {
	/// Index of peer station for which the agreement is being confirmed
	u8 sta_idx;
	/// TID for which the agreement is being confirmed
	u8 tid;
	/// Status of ba establishment
	u8 status;
};

/// Structure containing the parameters of the @ref MM_BA_DEL_REQ message.
struct mm_ba_del_req {
	/// Type of agreement (0: TX, 1: RX)
	u8 type;
	/// Index of peer station for which the agreement is being deleted
	u8 sta_idx;
	/// TID for which the agreement is being deleted
	u8 tid;
};

/// Structure containing the parameters of the @ref MM_BA_DEL_CFM message.
struct mm_ba_del_cfm {
	/// Index of peer station for which the agreement deletion is being
	/// confirmed
	u8 sta_idx;
	/// TID for which the agreement deletion is being confirmed
	u8 tid;
	/// Status of ba deletion
	u8 status;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_ADD_REQ message
struct mm_chan_ctxt_add_req {
	/// Operating channel
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_ADD_REQ message
struct mm_chan_ctxt_add_cfm {
	/// Status of the addition
	u8 status;
	/// Index of the new channel context
	u8 index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_DEL_REQ message
struct mm_chan_ctxt_del_req {
	/// Index of the new channel context to be deleted
	u8 index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_LINK_REQ
/// message
struct mm_chan_ctxt_link_req {
	/// VIF index
	u8 vif_index;
	/// Channel context index
	u8 chan_index;
	/// Indicate if this is a channel switch (unlink current ctx first if true)
	u8 chan_switch;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_UNLINK_REQ
/// message
struct mm_chan_ctxt_unlink_req {
	/// VIF index
	u8 vif_index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_UPDATE_REQ
/// message
struct mm_chan_ctxt_update_req {
	/// Channel context index
	u8 chan_index;
	/// New channel information
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_SCHED_REQ
/// message
struct mm_chan_ctxt_sched_req {
	/// VIF index
	u8 vif_index;
	/// Channel context index
	u8 chan_index;
	/// Type of the scheduling request (0: normal scheduling, 1: derogatory
	/// scheduling)
	u8 type;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_SWITCH_IND
/// message
struct mm_channel_switch_ind {
	/// Index of the channel context we will switch to
	u8 chan_index;
	/// Indicate if the switch has been triggered by a Remain on channel request
	bool roc;
	/// VIF on which remain on channel operation has been started (if roc == 1)
	u8 vif_index;
	/// Indicate if the switch has been triggered by a TDLS Remain on channel
	/// request
	bool roc_tdls;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_PRE_SWITCH_IND
/// message
struct mm_channel_pre_switch_ind {
	/// Index of the channel context we will switch to
	u8 chan_index;
};

/// Structure containing the parameters of the @ref MM_CONNECTION_LOSS_IND
/// message.
struct mm_connection_loss_ind {
	/// VIF instance number
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_DBG_TRIGGER_REQ message.
struct mm_dbg_trigger_req {
	/// Error trace to be reported by the LMAC
	char error[64];
};

/// Structure containing the parameters of the @ref MM_SET_PS_MODE_REQ message.
struct mm_set_ps_mode_req {
	/// Power Save is activated or deactivated
	u8 new_state;
};

/// Structure containing the parameters of the @ref MM_BCN_CHANGE_REQ message.
#define BCN_MAX_CSA_CPT 2
struct mm_bcn_change_req {
	/// Pointer, in host memory, to the new beacon template
	u32 bcn_ptr;
	/// Length of the beacon template
	u16 bcn_len;
	/// Offset of the TIM IE in the beacon
	u16 tim_oft;
	/// Length of the TIM IE
	u8 tim_len;
	/// Index of the VIF for which the beacon is updated
	u8 inst_nbr;
	/// Offset of CSA (channel switch announcement) counters (0 means no
	/// counter)
	u8 csa_oft[BCN_MAX_CSA_CPT];
};

/// Structure containing the parameters of the @ref MM_TIM_UPDATE_REQ message.
struct mm_tim_update_req {
	/// Association ID of the STA the bit of which has to be updated (0 for
	/// BC/MC traffic)
	u16 aid;
	/// Flag indicating the availability of data packets for the given STA
	u8 tx_avail;
	/// Index of the VIF for which the TIM is updated
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_REQ
/// message.
struct mm_remain_on_channel_req {
	/// Operation Code
	u8 op_code;
	/// VIF Index
	u8 vif_index;
	/// Band (2.4GHz or 5GHz)
	u8 band;
	/// Channel type: 20,40,80,160 or 80+80 MHz
	u8 type;
	/// Frequency for Primary 20MHz channel (in MHz)
	u16 prim20_freq;
	/// Frequency for Center of the contiguous channel or center of Primary
	/// 80+80
	u16 center1_freq;
	/// Frequency for Center of the non-contiguous secondary 80+80
	u16 center2_freq;
	/// Duration (in ms)
	u32 duration_ms;
	/// TX power (in dBm)
	s8 tx_power;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_CFM
/// message
struct mm_remain_on_channel_cfm {
	/// Operation Code
	u8 op_code;
	/// Status of the operation
	u8 status;
	/// Channel Context index
	u8 chan_ctxt_index;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_EXP_IND
/// message
struct mm_remain_on_channel_exp_ind {
	/// VIF Index
	u8 vif_index;
	/// Channel Context index
	u8 chan_ctxt_index;
};

/// Structure containing the parameters of the @ref MM_SET_UAPSD_TMR_REQ
/// message.
struct mm_set_uapsd_tmr_req {
	/// action: Start or Stop the timer
	u8 action;
	/// timeout value, in milliseconds
	u32 timeout;
};

/// Structure containing the parameters of the @ref MM_SET_UAPSD_TMR_CFM
/// message.
struct mm_set_uapsd_tmr_cfm {
	/// Status of the operation (different from 0 if unsuccessful)
	u8 status;
};

/// Structure containing the parameters of the @ref MM_PS_CHANGE_IND message
struct mm_ps_change_ind {
	/// Index of the peer device that is switching its PS state
	u8 sta_idx;
	/// New PS state of the peer device (0: active, 1: sleeping)
	u8 ps_state;
};

/// Structure containing the parameters of the @ref MM_P2P_VIF_PS_CHANGE_IND
/// message
struct mm_p2p_vif_ps_change_ind {
	/// Index of the P2P VIF that is switching its PS state
	u8 vif_index;
	/// New PS state of the P2P VIF interface (0: active, 1: sleeping)
	u8 ps_state;
};

/// Structure containing the parameters of the @ref MM_TRAFFIC_REQ_IND message
struct mm_traffic_req_ind {
	/// Index of the peer device that needs traffic
	u8 sta_idx;
	/// Number of packets that need to be sent (if 0, all buffered traffic shall
	/// be sent and if set to @ref PS_SP_INTERRUPTED, it means that current
	/// service period has been interrupted)
	u8 pkt_cnt;
	/// Flag indicating if the traffic request concerns U-APSD queues or not
	bool uapsd;
};

/// Structure containing the parameters of the @ref MM_SET_PS_OPTIONS_REQ
/// message.
struct mm_set_ps_options_req {
	/// VIF Index
	u8 vif_index;
	/// Listen interval (0 if wake up shall be based on DTIM period)
	u16 listen_interval;
	/// Flag indicating if we shall listen the BC/MC traffic or not
	bool dont_listen_bc_mc;
};

/// Structure containing the parameters of the @ref MM_CSA_COUNTER_IND message
struct mm_csa_counter_ind {
	/// Index of the VIF
	u8 vif_index;
	/// Updated CSA counter value
	u8 csa_count;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_SURVEY_IND
/// message
struct mm_channel_survey_ind {
	/// Frequency of the channel
	u16 freq;
	/// Noise in dbm
	s8 noise_dbm;
	/// Amount of time spent of the channel (in ms)
	u32 chan_time_ms;
	/// Amount of time the primary channel was sensed busy
	u32 chan_time_busy_ms;
};

/// Structure containing the parameters of the @ref MM_BFMER_ENABLE_REQ message.
struct mm_bfmer_enable_req {
	/**
	 * Address of the beamforming report space allocated in host memory
	 * (Valid only if vht_su_bfmee is true)
	 */
	u32 host_bfr_addr;
	/**
	 * Size of the beamforming report space allocated in host memory. This space
	 * should be twice the maximum size of the expected beamforming reports as
	 * the FW will divide it in two in order to be able to upload a new report
	 * while another one is used in transmission
	 */
	u16 host_bfr_size;
	/// AID
	u16 aid;
	/// Station Index
	u8 sta_idx;
	/// Maximum number of spatial streams the station can receive
	u8 rx_nss;
	/**
	 * Indicate if peer STA is MU Beamformee (VHT) capable
	 * (Valid only if vht_su_bfmee is true)
	 */
	bool vht_mu_bfmee;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_NOA_REQ message.
struct mm_set_p2p_noa_req {
	/// VIF Index
	u8 vif_index;
	/// Allocated NOA Instance Number - Valid only if count = 0
	u8 noa_inst_nb;
	/// Count
	u8 count;
	/// Indicate if NoA can be paused for traffic reason
	bool dyn_noa;
	/// Duration (in us)
	u32 duration_us;
	/// Interval (in us)
	u32 interval_us;
	/// Start Time offset from next TBTT (in us)
	u32 start_offset;
};

#ifdef AICWF_ARP_OFFLOAD
struct mm_set_arpoffload_en_req {
	u32 ipaddr;
	u8 enable;
	u8 vif_idx;
};

struct mm_set_arpoffload_en_cfm {
	u8 status;
};
#endif

struct mm_set_agg_disable_req {
	u8 disable;
	u8 staidx;
	u8 disable_rx;
};

struct mm_set_coex_req {
	u8 bt_on;
	u8 disable_coexnull;
	u8 enable_nullcts;
	u8 enable_periodic_timer;
	u8 coex_timeslot_set;
	u32 coex_timeslot[2];
};

struct mm_set_rf_config_req {
	u8 table_sel;
	u8 table_ofst;
	u8 table_num;
	u8 deft_page;
	u32 data[64];
};

struct mm_set_rf_calib_req {
	u32 cal_cfg_24g;
	u32 cal_cfg_5g;
	u32 param_alpha;
	u32 bt_calib_en;
	u32 bt_calib_param;
	u8 xtal_cap;
	u8 xtal_cap_fine;
};

struct mm_set_rf_calib_cfm {
	u32 rxgain_24g_addr;
	u32 rxgain_5g_addr;
	u32 txgain_24g_addr;
	u32 txgain_5g_addr;
};

struct mm_get_mac_addr_req {
	u32 get;
};

struct mm_get_mac_addr_cfm {
	u8 mac_addr[6];
};

struct mm_get_sta_info_req {
	u8 sta_idx;
};

struct mm_get_sta_info_cfm {
	u32 rate_info;
	u32 txfailed;
	u8 rssi;
};

#define REGION_NUM_MAX 5

struct txpwr_lvl_conf {
	u8 enable;
	u8 dsss;
	u8 ofdmlowrate_2g4;
	u8 ofdm64qam_2g4;
	u8 ofdm256qam_2g4;
	u8 ofdm1024qam_2g4;
	u8 ofdmlowrate_5g;
	u8 ofdm64qam_5g;
	u8 ofdm256qam_5g;
	u8 ofdm1024qam_5g;
};

struct txpwr_lvl_conf_v2 {
	u8 enable;
	s8 pwrlvl_11b_11ag_2g4[12];
	s8 pwrlvl_11n_11ac_2g4[10];
	s8 pwrlvl_11ax_2g4[12];
};

struct txpwr_lvl_conf_v3 {
	u8 enable;
	s8 pwrlvl_11b_11ag_2g4[12];
	s8 pwrlvl_11n_11ac_2g4[10];
	s8 pwrlvl_11ax_2g4[12];
	s8 pwrlvl_11a_5g[12];
	s8 pwrlvl_11n_11ac_5g[10];
	s8 pwrlvl_11ax_5g[12];
};

struct txpwr_lvl_adj_conf {
	u8 enable;
	s8 pwrlvl_adj_tbl_2g4[3];
	s8 pwrlvl_adj_tbl_5g[6];
};

struct txpwr_loss_conf {
	u8 loss_enable_2g4;
	s8 loss_value_2g4;
	u8 loss_enable_5g;
	s8 loss_value_5g;
};

struct mm_set_txpwr_lvl_req {
	union {
		struct txpwr_lvl_conf txpwr_lvl;
		struct txpwr_lvl_conf_v2 txpwr_lvl_v2;
		struct txpwr_lvl_conf_v3 txpwr_lvl_v3;
	};
};

struct mm_set_txpwr_lvl_adj_req {
	struct txpwr_lvl_adj_conf txpwr_lvl_adj;
};

struct txpwr_idx_conf {
	u8 enable;
	u8 dsss;
	u8 ofdmlowrate_2g4;
	u8 ofdm64qam_2g4;
	u8 ofdm256qam_2g4;
	u8 ofdm1024qam_2g4;
	u8 ofdmlowrate_5g;
	u8 ofdm64qam_5g;
	u8 ofdm256qam_5g;
	u8 ofdm1024qam_5g;
};

struct mm_set_txpwr_idx_req {
	struct txpwr_idx_conf txpwr_idx;
};

struct txpwr_ofst_conf {
	u8 enable;
	s8 chan_1_4;
	s8 chan_5_9;
	s8 chan_10_13;
	s8 chan_36_64;
	s8 chan_100_120;
	s8 chan_122_140;
	s8 chan_142_165;
};

/*
 * pwrofst2x_tbl_2g4[3][3]:
 * +---------------+----------+----------+----------+
 * | RateTyp\ChGrp |  CH_1_4  |  CH_5_9  | CH_10_13 |
 * +---------------+----------+----------+----------+
 * | DSSS          |  [0][0]  |  [0][1]  |  [0][2]  |
 * +---------------+----------+----------+----------+
 * | OFDM_HIGHRATE |  [1][0]  |  [1][1]  |  [1][2]  |
 * +---------------+----------+----------+----------+
 * | OFDM_LOWRATE  |  [2][0]  |  [2][1]  |  [2][2]  |
 * +---------------+----------+----------+----------+
 * pwrofst2x_tbl_5g[3][6]:
 * +---------------+--------------+--------------+----------------+----------------+--+--+
 * | RateTyp\ChGrp | CH_42(36~50) | CH_58(51~64) | CH_106(98~114) |
 * CH_122(115~130)| CH_138(131~146)| CH_155(147~166)|
 * +---------------+--------------+--------------+----------------+----------------+--+--+
 * | OFDM_LOWRATE  |    [0][0]    |    [0][1]    |     [0][2]     |     [0][3]
 * |     [0][4]     |     [0][5]     |
 * +---------------+--------------+--------------+----------------+----------------+--+--+
 * | OFDM_HIGHRATE |    [1][0]    |    [1][1]    |     [1][2]     |     [1][3]
 * |     [1][4]     |     [1][5]     |
 * +---------------+--------------+--------------+----------------+----------------+--+--+
 * | OFDM_MIDRATE  |    [2][0]    |    [2][1]    |     [2][2]     |     [2][3]
 * |     [2][4]     |     [2][5]     |
 * +---------------+--------------+--------------+----------------+----------------+--+--+
 */

struct txpwr_ofst2x_conf {
	s8 enable;
	s8 pwrofst2x_tbl_2g4[3][3];
	s8 pwrofst2x_tbl_5g[3][6];
};

struct xtal_cap_conf {
	u8 enable;
	u8 xtal_cap;
	u8 xtal_cap_fine;
};

struct mm_set_txpwr_ofst_req {
	union {
		struct txpwr_ofst_conf txpwr_ofst;
		struct txpwr_ofst2x_conf txpwr_ofst2x;
	};
};

struct mm_set_stack_start_req {
	u8 is_stack_start;
	u8 efuse_valid;
	u8 set_vendor_info;
	u8 fwtrace_redir;
};

struct mm_set_stack_start_cfm {
	u8 is_5g_support;
	u8 vendor_info;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_OPPPS_REQ
/// message.
struct mm_set_p2p_oppps_req {
	/// VIF Index
	u8 vif_index;
	/// CTWindow
	u8 ctwindow;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_NOA_CFM message.
struct mm_set_p2p_noa_cfm {
	/// Request status
	u8 status;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_OPPPS_CFM
/// message.
struct mm_set_p2p_oppps_cfm {
	/// Request status
	u8 status;
};

/// Structure containing the parameters of the @ref MM_P2P_NOA_UPD_IND message.
struct mm_p2p_noa_upd_ind {
	/// VIF Index
	u8 vif_index;
	/// NOA Instance Number
	u8 noa_inst_nb;
	/// NoA Type
	u8 noa_type;
	/// Count
	u8 count;
	/// Duration (in us)
	u32 duration_us;
	/// Interval (in us)
	u32 interval_us;
	/// Start Time
	u32 start_time;
};

/// Structure containing the parameters of the @ref MM_CFG_RSSI_REQ message
struct mm_cfg_rssi_req {
	/// Index of the VIF
	u8 vif_index;
	/// RSSI threshold
	s8 rssi_thold;
	/// RSSI hysteresis
	u8 rssi_hyst;
};

/// Structure containing the parameters of the @ref MM_RSSI_STATUS_IND message
struct mm_rssi_status_ind {
	/// Index of the VIF
	u8 vif_index;
	/// Status of the RSSI
	bool rssi_status;
	/// Current RSSI
	s8 rssi;
};

/// Structure containing the parameters of the @ref MM_PKTLOSS_IND message
struct mm_pktloss_ind {
	/// Index of the VIF
	u8 vif_index;
	/// Address of the STA for which there is a packet loss
	struct mac_addr mac_addr;
	/// Number of packets lost
	u32 num_packets;
};

/// Structure containing the parameters of the @ref MM_CSA_FINISH_IND message
struct mm_csa_finish_ind {
	/// Index of the VIF
	u8 vif_index;
	/// Status of the operation
	u8 status;
	/// New channel ctx index
	u8 chan_idx;
};

/// Structure containing the parameters of the @ref MM_CSA_TRAFFIC_IND message
struct mm_csa_traffic_ind {
	/// Index of the VIF
	u8 vif_index;
	/// Is tx traffic enable or disable
	bool enable;
};

/// Structure containing the parameters of the @ref MM_MU_GROUP_UPDATE_REQ
/// message. Size allocated for the structure depends of the number of group
struct mm_mu_group_update_req {
	/// Station index
	u8 sta_idx;
	/// Number of groups the STA belongs to
	u8 group_cnt;
	/// Group information
	struct {
		/// Group Id
		u8 group_id;
		/// User position
		u8 user_pos;
	} groups[];
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Scan messages
///////////////////////////////////////////////////////////////////////////////
enum scan_msg_tag {
	/// Scanning start Request.
	SCAN_START_REQ = LMAC_FIRST_MSG(TASK_SCAN),
	/// Scanning start Confirmation.
	SCAN_START_CFM,
	/// End of scanning indication.
	SCAN_DONE_IND,
	/// Cancel scan request
	SCAN_CANCEL_REQ,
	/// Cancel scan confirmation
	SCAN_CANCEL_CFM,

	/// MAX number of messages
	SCAN_MAX,
};

/// Maximum number of SSIDs in a scan request
#define SCAN_SSID_MAX 3

/// Maximum number of channels in a scan request
#define SCAN_CHANNEL_MAX (MAC_DOMAINCHANNEL_24G_MAX + MAC_DOMAINCHANNEL_5G_MAX)

/// Maximum length of the ProbeReq IEs (SoftMAC mode)
#define SCAN_MAX_IE_LEN 300

/// Maximum number of PHY bands supported
#define SCAN_BAND_MAX 2

/// Structure containing the parameters of the @ref SCAN_START_REQ message
struct scan_start_req {
	/// List of channel to be scanned
	struct mac_chan_def chan[SCAN_CHANNEL_MAX];
	/// List of SSIDs to be scanned
	struct mac_ssid ssid[SCAN_SSID_MAX];
	/// BSSID to be scanned
	struct mac_addr bssid;
	/// Pointer (in host memory) to the additional IEs that need to be added to
	/// the ProbeReq (following the SSID element)
	u32 add_ies;
	/// Length of the additional IEs
	u16 add_ie_len;
	/// Index of the VIF that is scanning
	u8 vif_idx;
	/// Number of channels to scan
	u8 chan_cnt;
	/// Number of SSIDs to scan for
	u8 ssid_cnt;
	/// no CCK - For P2P frames not being sent at CCK rate in 2GHz band.
	bool no_cck;
	/// Scan duration, in us
	u32 duration;
};

/// Structure containing the parameters of the @ref SCAN_START_CFM message
struct scan_start_cfm {
	/// Status of the request
	u8 status;
};

/// Structure containing the parameters of the @ref SCAN_CANCEL_REQ message
struct scan_cancel_req {
};

/// Structure containing the parameters of the @ref SCAN_START_CFM message
struct scan_cancel_cfm {
	/// Status of the request
	u8 status;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Scanu messages
///////////////////////////////////////////////////////////////////////////////
/// Messages that are logically related to the task.
enum {
	/// Scan request from host.
	SCANU_START_REQ = LMAC_FIRST_MSG(TASK_SCANU),
	/// Scanning start Confirmation.
	SCANU_START_CFM,
	/// Join request
	SCANU_JOIN_REQ,
	/// Join confirmation.
	SCANU_JOIN_CFM,
	/// Scan result indication.
	SCANU_RESULT_IND,
	/// Fast scan request from any other module.
	SCANU_FAST_REQ,
	/// Confirmation of fast scan request.
	SCANU_FAST_CFM,

	SCANU_VENDOR_IE_REQ,
	SCANU_VENDOR_IE_CFM,
	SCANU_START_CFM_ADDTIONAL,
	SCANU_CANCEL_REQ,
	SCANU_CANCEL_CFM,

	/// MAX number of messages
	SCANU_MAX,
};

/// Maximum length of the additional ProbeReq IEs (FullMAC mode)
#define SCANU_MAX_IE_LEN 200

/// Structure containing the parameters of the @ref SCANU_START_REQ message
struct scanu_start_req {
	/// List of channel to be scanned
	struct mac_chan_def chan[SCAN_CHANNEL_MAX];
	/// List of SSIDs to be scanned
	struct mac_ssid ssid[SCAN_SSID_MAX];
	/// BSSID to be scanned (or WILDCARD BSSID if no BSSID is searched in
	/// particular)
	struct mac_addr bssid;
	/// Address (in host memory) of the additional IEs that need to be added to
	/// the ProbeReq (following the SSID element)
	u32 add_ies;
	/// Length of the additional IEs
	u16 add_ie_len;
	/// Index of the VIF that is scanning
	u8 vif_idx;
	/// Number of channels to scan
	u8 chan_cnt;
	/// Number of SSIDs to scan for
	u8 ssid_cnt;
	/// no CCK - For P2P frames not being sent at CCK rate in 2GHz band.
	bool no_cck;
	/// Scan duration, in us
	u32 duration;
};

struct scanu_vendor_ie_req {
	u16 add_ie_len;
	u8 vif_idx;
	u8 ie[256];
};

/// Structure containing the parameters of the @ref SCANU_START_CFM message
struct scanu_start_cfm {
	/// Index of the VIF that was scanning
	u8 vif_idx;
	/// Status of the request
	u8 status;
	/// Number of scan results available
	u8 result_cnt;
};

/// Parameters of the @SCANU_RESULT_IND message
struct scanu_result_ind {
	/// Length of the frame
	u16 length;
	/// Frame control field of the frame.
	u16 framectrl;
	/// Center frequency on which we received the packet
	u16 center_freq;
	/// PHY band
	u8 band;
	/// Index of the station that sent the frame. 0xFF if unknown.
	u8 sta_idx;
	/// Index of the VIF that received the frame. 0xFF if unknown.
	u8 inst_nbr;
	/// RSSI of the received frame.
	s8 rssi;
	/// Frame payload.
	u32 payload[];
};

struct rxu_arp_announcement_ind {
	/// index of the vif
	u8 vif_index;
	u8 offset;
	/// length of the frame
	u16 length;
	u32 rx_status;
	/// frame payload
	u8 payload[];
};

/// Structure containing the parameters of the message.
struct scanu_fast_req {
	/// The SSID to scan in the channel.
	struct mac_ssid ssid;
	/// BSSID.
	struct mac_addr bssid;
	/// Probe delay.
	u16 probe_delay;
	/// Minimum channel time.
	u16 minch_time;
	/// Maximum channel time.
	u16 maxch_time;
	/// The channel number to scan.
	u16 ch_nbr;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For ME messages
///////////////////////////////////////////////////////////////////////////////
/// Messages that are logically related to the task.
enum {
	/// Configuration request from host.
	ME_CONFIG_REQ = LMAC_FIRST_MSG(TASK_ME),
	/// Configuration confirmation.
	ME_CONFIG_CFM,
	/// Configuration request from host.
	ME_CHAN_CONFIG_REQ,
	/// Configuration confirmation.
	ME_CHAN_CONFIG_CFM,
	/// Set control port state for a station.
	ME_SET_CONTROL_PORT_REQ,
	/// Control port setting confirmation.
	ME_SET_CONTROL_PORT_CFM,
	/// TKIP MIC failure indication.
	ME_TKIP_MIC_FAILURE_IND,
	/// Add a station to the FW (AP mode)
	ME_STA_ADD_REQ,
	/// Confirmation of the STA addition
	ME_STA_ADD_CFM,
	/// Delete a station from the FW (AP mode)
	ME_STA_DEL_REQ,
	/// Confirmation of the STA deletion
	ME_STA_DEL_CFM,
	/// Indication of a TX RA/TID queue credit update
	ME_TX_CREDITS_UPDATE_IND,
	/// Request indicating to the FW that there is traffic buffered on host
	ME_TRAFFIC_IND_REQ,
	/// Confirmation that the @ref ME_TRAFFIC_IND_REQ has been executed
	ME_TRAFFIC_IND_CFM,
	/// Request of RC statistics to a station
	ME_RC_STATS_REQ,
	/// RC statistics confirmation
	ME_RC_STATS_CFM,
	/// RC fixed rate request
	ME_RC_SET_RATE_REQ,
	/// Configure monitor interface
	ME_CONFIG_MONITOR_REQ,
	/// Configure monitor interface response
	ME_CONFIG_MONITOR_CFM,
	/// Setting power Save mode request from host
	ME_SET_PS_MODE_REQ,
	/// Set power Save mode confirmation
	ME_SET_PS_MODE_CFM,
	/// Setting Low Power level request from host
	ME_SET_LP_LEVEL_REQ,
	/// Set Low Power level confirmation
	ME_SET_LP_LEVEL_CFM,
	/// MAX number of messages
	ME_MAX,
};

/// Structure containing the parameters of the @ref ME_START_REQ message
struct me_config_req {
	/// HT Capabilities
	struct mac_htcapability ht_cap;
	/// VHT Capabilities
	struct mac_vhtcapability vht_cap;
	/// HE capabilities
	struct mac_hecapability he_cap;
	/// Lifetime of packets sent under a BlockAck agreement (expressed in TUs)
	u16 tx_lft;
	/// Maximum supported BW
	u8 phy_bw_max;
	/// Boolean indicating if HT is supported or not
	bool ht_supp;
	/// Boolean indicating if VHT is supported or not
	bool vht_supp;
	/// Boolean indicating if HE is supported or not
	bool he_supp;
	/// Boolean indicating if HE OFDMA UL is enabled or not
	bool he_ul_on;
	/// Boolean indicating if PS mode shall be enabled or not
	bool ps_on;
	/// Boolean indicating if Antenna Diversity shall be enabled or not
	bool ant_div_on;
	/// Boolean indicating if Dynamic PS mode shall be used or not
	bool dpsm;
};

/// Structure containing the parameters of the @ref ME_CHAN_CONFIG_REQ message
struct me_chan_config_req {
	/// List of 2.4GHz supported channels
	struct mac_chan_def chan2G4[MAC_DOMAINCHANNEL_24G_MAX];
	/// List of 5GHz supported channels
	struct mac_chan_def chan5G[MAC_DOMAINCHANNEL_5G_MAX];
	/// Number of 2.4GHz channels in the list
	u8 chan2G4_cnt;
	/// Number of 5GHz channels in the list
	u8 chan5G_cnt;
};

/// Structure containing the parameters of the @ref ME_SET_CONTROL_PORT_REQ
/// message
struct me_set_control_port_req {
	/// Index of the station for which the control port is opened
	u8 sta_idx;
	/// Control port state
	bool control_port_open;
};

/// Structure containing the parameters of the @ref ME_TKIP_MIC_FAILURE_IND
/// message
struct me_tkip_mic_failure_ind {
	/// Address of the sending STA
	struct mac_addr addr;
	/// TSC value
	u64 tsc;
	/// Boolean indicating if the packet was a group or unicast one (true if
	/// group)
	bool ga;
	/// Key Id
	u8 keyid;
	/// VIF index
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref ME_STA_ADD_REQ message
struct me_sta_add_req {
	/// MAC address of the station to be added
	struct mac_addr mac_addr;
	/// Supported legacy rates
	struct mac_rateset rate_set;
	/// HT Capabilities
	struct mac_htcapability ht_cap;
	/// VHT Capabilities
	struct mac_vhtcapability vht_cap;
	/// HE capabilities
	struct mac_hecapability he_cap;
	/// Flags giving additional information about the station (@ref
	/// mac_sta_flags)
	u32 flags;
	/// Association ID of the station
	__le16 aid;
	/// Bit field indicating which queues have U-APSD enabled
	u8 uapsd_queues;
	/// Maximum size, in frames, of a APSD service period
	u8 max_sp_len;
	/// Operation mode information (valid if bit @ref STA_OPMOD_NOTIF is
	/// set in the flags)
	u8 opmode;
	/// Index of the VIF the station is attached to
	u8 vif_idx;
	/// Whether the station is TDLS station
	bool tdls_sta;
	/// Indicate if the station is TDLS link initiator station
	bool tdls_sta_initiator;
	/// Indicate if the TDLS Channel Switch is allowed
	bool tdls_chsw_allowed;
};

/// Structure containing the parameters of the @ref ME_STA_ADD_CFM message
struct me_sta_add_cfm {
	/// Station index
	u8 sta_idx;
	/// Status of the station addition
	u8 status;
	/// PM state of the station
	u8 pm_state;
};

/// Structure containing the parameters of the @ref ME_STA_DEL_REQ message.
struct me_sta_del_req {
	/// Index of the station to be deleted
	u8 sta_idx;
	/// Whether the station is TDLS station
	bool tdls_sta;
};

/// Structure containing the parameters of the @ref ME_TX_CREDITS_UPDATE_IND
/// message.
struct me_tx_credits_update_ind {
	/// Index of the station for which the credits are updated
	u8 sta_idx;
	/// TID for which the credits are updated
	u8 tid;
	/// Offset to be applied on the credit count
	s8 credits;
};

/// Structure containing the parameters of the @ref ME_TRAFFIC_IND_REQ message.
struct me_traffic_ind_req {
	/// Index of the station for which UAPSD traffic is available on host
	u8 sta_idx;
	/// Flag indicating the availability of UAPSD packets for the given STA
	u8 tx_avail;
	/// Indicate if traffic is on uapsd-enabled queues
	bool uapsd;
};

struct mm_apm_staloss_ind {
	u8 sta_idx;
	u8 vif_idx;
	u8 mac_addr[6];
};

enum vendor_hwconfig_tag {
	ACS_TXOP_REQ = 0,
	CHANNEL_ACCESS_REQ,
	MAC_TIMESCALE_REQ,
	CCA_THRESHOLD_REQ,
	BWMODE_REQ,
	CHIP_TEMP_GET_REQ,
	AP_PS_LEVEL_SET_REQ,
	CUSTOMIZED_FREQ_REQ,
	WAKEUP_INFO_REQ,
	KEEPALIVE_PKT_REQ,
};

enum {
	BWMODE20M = 0,
	BWMODE10M,
	BWMODE5M,
};

struct mm_set_acs_txop_req {
	u32 hwconfig_id;
	u16 txop_bk;
	u16 txop_be;
	u16 txop_vi;
	u16 txop_vo;
};

struct mm_set_channel_access_req {
	u32 hwconfig_id;
	u32 edca[4];
	u8 vif_idx;
	u8 retry_cnt;
	u8 rts_en;
	u8 long_nav_en;
	u8 cfe_en;
	u8 rc_retry_cnt[3];
	s8 ccademod_th;
};

struct mm_set_mac_timescale_req {
	u32 hwconfig_id;
	u8 sifs_a_time;
	u8 sifs_b_time;
	u8 slot_time;
	u8 rx_startdelay_ofdm;
	u8 rx_startdelay_long;
	u8 rx_startdelay_short;
};

struct mm_set_cca_threshold_req {
	u32 hwconfig_id;
	u8 auto_cca_en;
	s8 cca20p_rise_th;
	s8 cca20s_rise_th;
	s8 cca20p_fall_th;
	s8 cca20s_fall_th;
};

struct mm_set_bwmode_req {
	u32 hwconfig_id;
	u8 bwmode;
};

struct mm_get_chip_temp_req {
	u32 hwconfig_id;
};

struct mm_get_chip_temp_cfm {
	/// Temp degree val
	s8 degree;
};

struct mm_set_ap_ps_level_req {
	u32 hwconfig_id;
	u8 ap_ps_level;
};

struct mm_set_vendor_hwconfig_cfm {
	u32 hwconfig_id;
	union {
		struct mm_get_chip_temp_cfm chip_temp_cfm;
	};
};

struct mm_set_customized_freq_req {
	u32 hwconfig_id;
	u16 raw_freq[4];
	u16 map_freq[4];
};

struct mm_set_keepalive_req {
	u32 hwconfig_id;
	u16 code;
	u16 length;
	u32 intv;
	u8 payload[];
};

struct mm_set_txop_req {
	u16 txop_bk;
	u16 txop_be;
	u16 txop_vi;
	u16 txop_vo;
	u8 long_nav_en;
	u8 cfe_en;
};

struct mm_set_wakeup_info_req {
	u32 hwconfig_id;
	u16 code;
	u16 offset;
	u16 length;
	u16 length_win;
	u8 mask_and_pattern[];
};

struct mm_get_fw_version_cfm {
	u8 fw_version_len;
	u8 fw_version[63];
};

struct mm_get_wifi_disable_cfm {
	u8 wifi_disable;
};

enum vendor_swconfig_tag {
	BCN_CFG_REQ = 0,
	TEMP_COMP_SET_REQ,
	TEMP_COMP_GET_REQ,
	EXT_FLAGS_SET_REQ,
	EXT_FLAGS_GET_REQ,
	EXT_FLAGS_MASK_SET_REQ,
};

struct mm_set_bcn_cfg_req {
	/// Ignore or not bcn tim bcmc bit
	bool tim_bcmc_ignored_enable;
};

struct mm_set_bcn_cfg_cfm {
	/// Request status
	bool tim_bcmc_ignored_status;
};

struct mm_set_temp_comp_req {
	/// Enable or not temp comp
	u8 enable;
	u8 reserved[3];
	u32 tmr_period_ms;
};

struct mm_set_temp_comp_cfm {
	/// Request status
	u8 status;
};

struct mm_get_temp_comp_cfm {
	/// Request status
	u8 status;
	/// Temp degree val
	s8 degree;
};

struct mm_set_ext_flags_req {
	u32 user_flags;
};

struct mm_set_ext_flags_cfm {
	u32 user_flags;
};

struct mm_get_ext_flags_cfm {
	u32 user_flags;
};

struct mm_mask_set_ext_flags_req {
	u32 user_flags_mask;
	u32 user_flags_val;
};

struct mm_mask_set_ext_flags_cfm {
	u32 user_flags;
};

struct mm_set_vendor_swconfig_req {
	u32 swconfig_id;
	union {
		struct mm_set_bcn_cfg_req bcn_cfg_req;
		struct mm_set_temp_comp_req temp_comp_set_req;
		struct mm_set_ext_flags_req ext_flags_set_req;
		struct mm_mask_set_ext_flags_req ext_flags_mask_set_req;
	};
};

struct mm_set_vendor_swconfig_cfm {
	u32 swconfig_id;
	union {
		struct mm_set_bcn_cfg_cfm bcn_cfg_cfm;
		struct mm_set_temp_comp_cfm temp_comp_set_cfm;
		struct mm_get_temp_comp_cfm temp_comp_get_cfm;
		struct mm_set_ext_flags_cfm ext_flags_set_cfm;
		struct mm_get_ext_flags_cfm ext_flags_get_cfm;
		struct mm_mask_set_ext_flags_cfm ext_flags_mask_set_cfm;
	};
};

/// Structure containing the parameters of the @ref ME_RC_STATS_REQ message.
struct me_rc_stats_req {
	/// Index of the station for which the RC statistics are requested
	u8 sta_idx;
};

/// Structure containing the rate control statistics
struct rc_rate_stats {
	/// Number of attempts (per sampling interval)
	u16 attempts;
	/// Number of success (per sampling interval)
	u16 success;
	/// Estimated probability of success (EWMA)
	u16 probability;
	/// Rate configuration of the sample
	u16 rate_config;
	union {
		struct {
			/// Number of times the sample has been skipped (per sampling
			/// interval)
			u8 sample_skipped;
			/// Whether the old probability is available
			bool old_prob_available;
			/// Whether the rate can be used in the retry chain
			bool rate_allowed;
		};
		struct {
			/// RU size and UL length received in the latest HE trigger frame
			u16 ru_and_length;
		};
	};
};

/// Number of RC samples
#define RC_MAX_N_SAMPLE 10
/// Index of the HE statistics element in the table
#define RC_HE_STATS_IDX RC_MAX_N_SAMPLE

/// Structure containing the parameters of the @ref ME_RC_STATS_CFM message.
struct me_rc_stats_cfm {
	/// Index of the station for which the RC statistics are provided
	u8 sta_idx;
	/// Number of samples used in the RC algorithm
	u16 no_samples;
	/// Number of MPDUs transmitted (per sampling interval)
	u16 ampdu_len;
	/// Number of AMPDUs transmitted (per sampling interval)
	u16 ampdu_packets;
	/// Average number of MPDUs in each AMPDU frame (EWMA)
	u32 avg_ampdu_len;
	// Current step 0 of the retry chain
	u8 sw_retry_step;
	/// Trial transmission period
	u8 sample_wait;
	/// Retry chain steps
	u16 retry_step_idx[4];
	/// RC statistics - Max number of RC samples, plus one for the HE TB
	/// statistics
	struct rc_rate_stats rate_stats[RC_MAX_N_SAMPLE + 1];
	/// Throughput - Max number of RC samples, plus one for the HE TB statistics
	u32 tp[RC_MAX_N_SAMPLE + 1];
};

/// Structure containing the parameters of the @ref ME_RC_SET_RATE_REQ message.
struct me_rc_set_rate_req {
	/// Index of the station for which the fixed rate is set
	u8 sta_idx;
	/// Rate configuration to be set
	u16 fixed_rate_cfg;
};

/// Structure containing the parameters of the @ref ME_CONFIG_MONITOR_REQ
/// message.
struct me_config_monitor_req {
	/// Channel to configure
	struct mac_chan_op chan;
	/// Is channel data valid
	bool chan_set;
	/// Enable report of unsupported HT frames
	bool uf;
	/// Enable auto-reply as the mac_addr matches
	bool auto_reply;
};

/// Structure containing the parameters of the @ref ME_CONFIG_MONITOR_CFM
/// message.
struct me_config_monitor_cfm {
	/// Channel context index
	u8 chan_index;
	/// Channel parameters
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref ME_SET_PS_MODE_REQ message.
struct me_set_ps_mode_req {
	/// Power Save is activated or deactivated
	u8 ps_state;
};

/// Structure containing the parameters of the @ref ME_SET_LP_LEVEL_REQ message.
struct me_set_lp_level_req {
	/// Low Power level
	u8 lp_level;
	u8 disable_filter;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For SM messages
///////////////////////////////////////////////////////////////////////////////
/// Message API of the SM task
enum sm_msg_tag {
	/// Request to connect to an AP
	SM_CONNECT_REQ = LMAC_FIRST_MSG(TASK_SM),
	/// Confirmation of connection
	SM_CONNECT_CFM,
	/// Indicates that the SM associated to the AP
	SM_CONNECT_IND,
	/// Request to disconnect
	SM_DISCONNECT_REQ,
	/// Confirmation of disconnection
	SM_DISCONNECT_CFM,
	/// Indicates that the SM disassociated the AP
	SM_DISCONNECT_IND,
	/// Request to start external authentication
	SM_EXTERNAL_AUTH_REQUIRED_IND,
	/// Response to external authentication request
	SM_EXTERNAL_AUTH_REQUIRED_RSP,
	/// Request to update assoc elements after FT over the air authentication
	SM_FT_AUTH_IND,
	/// Response to FT authentication with updated assoc elements
	SM_FT_AUTH_RSP,

	SM_RSP_TIMEOUT_IND,

	SM_COEX_TS_TIMEOUT_IND,

	SM_EXTERNAL_AUTH_REQUIRED_RSP_CFM,
	/// MAX number of messages
	SM_MAX,
};

/// Structure containing the parameters of @ref SM_CONNECT_REQ message.
struct sm_connect_req {
	/// SSID to connect to
	struct mac_ssid ssid;
	/// BSSID to connect to (if not specified, set this field to WILDCARD BSSID)
	struct mac_addr bssid;
	/// Channel on which we have to connect (if not specified, set -1 in the
	/// chan.freq field)
	struct mac_chan_def chan;
	/// Connection flags (see @ref mac_connection_flags)
	u32 flags;
	/// Control port Ethertype (in network endianness)
	__be16 ctrl_port_ethertype;
	/// Length of the association request IEs
	u16 ie_len;
	/// Listen interval to be used for this connection
	u16 listen_interval;
	/// Flag indicating if the we have to wait for the BC/MC traffic after
	/// beacon or not
	bool dont_wait_bcmc;
	/// Authentication type
	u8 auth_type;
	/// UAPSD queues (bit0: VO, bit1: VI, bit2: BE, bit3: BK)
	u8 uapsd_queues;
	/// VIF index
	u8 vif_idx;
	/// Buffer containing the additional information elements to be put in the
	/// association request
	u32 ie_buf[64];
};

/// Structure containing the parameters of the @ref SM_CONNECT_CFM message.
struct sm_connect_cfm {
	/// Status. If 0, it means that the connection procedure will be performed
	/// and that a subsequent @ref SM_CONNECT_IND message will be forwarded once
	/// the procedure is completed
	u8 status;
};

#define SM_ASSOC_IE_LEN 800
/// Structure containing the parameters of the @ref SM_CONNECT_IND message.
struct sm_connect_ind {
	/// Status code of the connection procedure
	u16 status_code;
	/// BSSID
	struct mac_addr bssid;
	/// Flag indicating if the indication refers to an internal roaming or from
	/// a host request
	bool roamed;
	/// Index of the VIF for which the association process is complete
	u8 vif_idx;
	/// Index of the STA entry allocated for the AP
	u8 ap_idx;
	/// Index of the LMAC channel context the connection is attached to
	u8 ch_idx;
	/// Flag indicating if the AP is supporting QoS
	bool qos;
	/// ACM bits set in the AP WMM parameter element
	u8 acm;
	/// Length of the AssocReq IEs
	u16 assoc_req_ie_len;
	/// Length of the AssocRsp IEs
	u16 assoc_rsp_ie_len;
	/// IE buffer
	u32 assoc_ie_buf[SM_ASSOC_IE_LEN / 4];

	u16 aid;
	u8 band;
	u16 center_freq;
	u8 width;
	u32 center_freq1;
	u32 center_freq2;

	/// EDCA parameters
	u32 ac_param[AC_MAX];
};

/// Structure containing the parameters of the @ref SM_DISCONNECT_REQ message.
struct sm_disconnect_req {
	/// Reason of the deauthentication.
	u16 reason_code;
	/// Index of the VIF.
	u8 vif_idx;
};

/// Structure containing the parameters of SM_ASSOCIATION_IND the message
struct sm_association_ind {
	// MAC ADDR of the STA
	struct mac_addr me_mac_addr;
};

/// Structure containing the parameters of the @ref SM_DISCONNECT_IND message.
struct sm_disconnect_ind {
	/// Reason of the disconnection.
	u16 reason_code;
	/// Index of the VIF.
	u8 vif_idx;
	/// FT over DS is ongoing
	bool ft_over_ds;
	u8 reassoc;
};

/// Structure containing the parameters of the @ref
/// SM_EXTERNAL_AUTH_REQUIRED_IND
struct sm_external_auth_required_ind {
	/// Index of the VIF.
	u8 vif_idx;
	/// SSID to authenticate to
	struct mac_ssid ssid;
	/// BSSID to authenticate to
	struct mac_addr bssid;
	/// AKM suite of the respective authentication
	u32 akm;
};

/// Structure containing the parameters of the @ref
/// SM_EXTERNAL_AUTH_REQUIRED_RSP
struct sm_external_auth_required_rsp {
	/// Index of the VIF.
	u8 vif_idx;
	/// Authentication status
	u16 status;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For APM messages
///////////////////////////////////////////////////////////////////////////////
/// Message API of the APM task
enum apm_msg_tag {
	/// Request to start the AP.
	APM_START_REQ = LMAC_FIRST_MSG(TASK_APM),
	/// Confirmation of the AP start.
	APM_START_CFM,
	/// Request to stop the AP.
	APM_STOP_REQ,
	/// Confirmation of the AP stop.
	APM_STOP_CFM,
	/// Request to start CAC
	APM_START_CAC_REQ,
	/// Confirmation of the CAC start
	APM_START_CAC_CFM,
	/// Request to stop CAC
	APM_STOP_CAC_REQ,
	/// Confirmation of the CAC stop
	APM_STOP_CAC_CFM,

	APM_SET_BEACON_IE_REQ,
	APM_SET_BEACON_IE_CFM,

	/// MAX number of messages
	APM_MAX,
};

/// Structure containing the parameters of the @ref APM_START_REQ message.
struct apm_start_req {
	/// Basic rate set
	struct mac_rateset basic_rates;
	/// Control channel on which we have to enable the AP
	struct mac_chan_def chan;
	/// Center frequency of the first segment
	u32 center_freq1;
	/// Center frequency of the second segment (only in 80+80 configuration)
	u32 center_freq2;
	/// Width of channel
	u8 ch_width;
	/// Address, in host memory, to the beacon template
	u32 bcn_addr;
	/// Length of the beacon template
	u16 bcn_len;
	/// Offset of the TIM IE in the beacon
	u16 tim_oft;
	/// Beacon interval
	u16 bcn_int;
	/// Flags (@ref mac_connection_flags)
	u32 flags;
	/// Control port Ethertype
	__be16 ctrl_port_ethertype;
	/// Length of the TIM IE
	u8 tim_len;
	/// Index of the VIF for which the AP is started
	u8 vif_idx;
};

struct apm_set_bcn_ie_req {
	u8 vif_idx;
	u16 bcn_ie_len;
	u8 bcn_ie[512];
};

/// Structure containing the parameters of the @ref APM_START_CFM message.
struct apm_start_cfm {
	/// Status of the AP starting procedure
	u8 status;
	/// Index of the VIF for which the AP is started
	u8 vif_idx;
	/// Index of the channel context attached to the VIF
	u8 ch_idx;
	/// Index of the STA used for BC/MC traffic
	u8 bcmc_idx;
};

/// Structure containing the parameters of the @ref APM_STOP_REQ message.
struct apm_stop_req {
	/// Index of the VIF for which the AP has to be stopped
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref APM_START_CAC_REQ message.
struct apm_start_cac_req {
	/// Control channel on which we have to start the CAC
	struct mac_chan_def chan;
	/// Center frequency of the first segment
	u32 center_freq1;
	/// Center frequency of the second segment (only in 80+80 configuration)
	u32 center_freq2;
	/// Width of channel
	u8 ch_width;
	/// Index of the VIF for which the CAC is started
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref APM_START_CAC_CFM message.
struct apm_start_cac_cfm {
	/// Status of the CAC starting procedure
	u8 status;
	/// Index of the channel context attached to the VIF for CAC
	u8 ch_idx;
};

/// Structure containing the parameters of the @ref APM_STOP_CAC_REQ message.
struct apm_stop_cac_req {
	/// Index of the VIF for which the CAC has to be stopped
	u8 vif_idx;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For MESH messages
///////////////////////////////////////////////////////////////////////////////

/// Maximum length of the Mesh ID
#define MESH_MESHID_MAX_LEN (32)

/// Message API of the MESH task
enum mesh_msg_tag {
	/// Request to start the MP
	MESH_START_REQ = LMAC_FIRST_MSG(TASK_MESH),
	/// Confirmation of the MP start.
	MESH_START_CFM,

	/// Request to stop the MP.
	MESH_STOP_REQ,
	/// Confirmation of the MP stop.
	MESH_STOP_CFM,

	// Request to update the MP
	MESH_UPDATE_REQ,
	/// Confirmation of the MP update
	MESH_UPDATE_CFM,

	/// Request information about a given link
	MESH_PEER_INFO_REQ,
	/// Response to the MESH_PEER_INFO_REQ message
	MESH_PEER_INFO_CFM,

	/// Request automatic establishment of a path with a given mesh STA
	MESH_PATH_CREATE_REQ,
	/// Confirmation to the MESH_PATH_CREATE_REQ message
	MESH_PATH_CREATE_CFM,

	/// Request a path update (delete path, modify next hop mesh STA)
	MESH_PATH_UPDATE_REQ,
	/// Confirmation to the MESH_PATH_UPDATE_REQ message
	MESH_PATH_UPDATE_CFM,

	/// Indication from Host that the indicated Mesh Interface is a proxy for an
	/// external STA
	MESH_PROXY_ADD_REQ,

	/// Indicate that a connection has been established or lost
	MESH_PEER_UPDATE_IND,
	/// Notification that a connection has been established or lost (when MPM
	/// handled by userspace)
	MESH_PEER_UPDATE_NTF = MESH_PEER_UPDATE_IND,

	/// Indicate that a path is now active or inactive
	MESH_PATH_UPDATE_IND,
	/// Indicate that proxy information have been updated
	MESH_PROXY_UPDATE_IND,

	/// MAX number of messages
	MESH_MAX,
};

/// Structure containing the parameters of the @ref MESH_START_REQ message.
struct mesh_start_req {
	/// Basic rate set
	struct mac_rateset basic_rates;
	/// Control channel on which we have to enable the AP
	struct mac_chan_def chan;
	/// Center frequency of the first segment
	u32 center_freq1;
	/// Center frequency of the second segment (only in 80+80 configuration)
	u32 center_freq2;
	/// Width of channel
	u8 ch_width;
	/// DTIM Period
	u8 dtim_period;
	/// Beacon Interval
	u16 bcn_int;
	/// Index of the VIF for which the MP is started
	u8 vif_index;
	/// Length of the Mesh ID
	u8 mesh_id_len;
	/// Mesh ID
	u8 mesh_id[MESH_MESHID_MAX_LEN];
	/// Address of the IEs to download
	u32 ie_addr;
	/// Length of the provided IEs
	u8 ie_len;
	/// Indicate if Mesh Peering Management (MPM) protocol is handled in
	/// userspace
	bool user_mpm;
	/// Indicate if Mesh Point is using authentication
	bool is_auth;
	/// Indicate which authentication method is used
	u8 auth_id;
};

/// Structure containing the parameters of the @ref MESH_START_CFM message.
struct mesh_start_cfm {
	/// Status of the MP starting procedure
	u8 status;
	/// Index of the VIF for which the MP is started
	u8 vif_idx;
	/// Index of the channel context attached to the VIF
	u8 ch_idx;
	/// Index of the STA used for BC/MC traffic
	u8 bcmc_idx;
};

/// Structure containing the parameters of the @ref MESH_STOP_REQ message.
struct mesh_stop_req {
	/// Index of the VIF for which the MP has to be stopped
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref MESH_STOP_CFM message.
struct mesh_stop_cfm {
	/// Index of the VIF for which the MP has to be stopped
	u8 vif_idx;
	/// Status
	u8 status;
};

/// Bit fields for mesh_update_req message's flags value
enum mesh_update_flags_bit {
	/// Root Mode
	MESH_UPDATE_FLAGS_ROOT_MODE_BIT = 0,
	/// Gate Mode
	MESH_UPDATE_FLAGS_GATE_MODE_BIT,
	/// Mesh Forwarding
	MESH_UPDATE_FLAGS_MESH_FWD_BIT,
	/// Local Power Save Mode
	MESH_UPDATE_FLAGS_LOCAL_PSM_BIT,
};

/// Structure containing the parameters of the @ref MESH_UPDATE_REQ message.
struct mesh_update_req {
	/// Flags, indicate fields which have been updated
	u8 flags;
	/// VIF Index
	u8 vif_idx;
	/// Root Mode
	u8 root_mode;
	/// Gate Announcement
	bool gate_announ;
	/// Mesh Forwarding
	bool mesh_forward;
	/// Local PS Mode
	u8 local_ps_mode;
};

/// Structure containing the parameters of the @ref MESH_UPDATE_CFM message.
struct mesh_update_cfm {
	/// Status
	u8 status;
};

/// Structure containing the parameters of the @ref MESH_PEER_INFO_REQ message.
struct mesh_peer_info_req {
	/// Index of the station allocated for the peer
	u8 sta_idx;
};

/// Structure containing the parameters of the @ref MESH_PEER_INFO_CFM message.
struct mesh_peer_info_cfm {
	/// Response status
	u8 status;
	/// Index of the station allocated for the peer
	u8 sta_idx;
	/// Local Link ID
	u16 local_link_id;
	/// Peer Link ID
	u16 peer_link_id;
	/// Local PS Mode
	u8 local_ps_mode;
	/// Peer PS Mode
	u8 peer_ps_mode;
	/// Non-peer PS Mode
	u8 non_peer_ps_mode;
	/// Link State
	u8 link_state;
};

/// Structure containing the parameters of the @ref MESH_PATH_CREATE_REQ
/// message.
struct mesh_path_create_req {
	/// Index of the interface on which path has to be created
	u8 vif_idx;
	/// Indicate if originator MAC Address is provided
	bool has_orig_addr;
	/// Path Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// Originator MAC Address
	struct mac_addr orig_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PATH_CREATE_CFM
/// message.
struct mesh_path_create_cfm {
	/// Confirmation status
	u8 status;
	/// VIF Index
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_REQ
/// message.
struct mesh_path_update_req {
	/// Indicate if path must be deleted
	bool delete;
	/// Index of the interface on which path has to be created
	u8 vif_idx;
	/// Path Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// Next Hop MAC Address
	struct mac_addr nhop_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_CFM
/// message.
struct mesh_path_update_cfm {
	/// Confirmation status
	u8 status;
	/// VIF Index
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref MESH_PROXY_ADD_REQ message.
struct mesh_proxy_add_req {
	/// VIF Index
	u8 vif_idx;
	/// MAC Address of the External STA
	struct mac_addr ext_sta_addr;
};

/// Structure containing the parameters of the @ref MESH_PROXY_UPDATE_IND
struct mesh_proxy_update_ind {
	/// Indicate if proxy information has been added or deleted
	bool delete;
	/// Indicate if we are a proxy for the external STA
	bool local;
	/// VIF Index
	u8 vif_idx;
	/// MAC Address of the External STA
	struct mac_addr ext_sta_addr;
	/// MAC Address of the proxy (only valid if local is false)
	struct mac_addr proxy_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PEER_UPDATE_IND
/// message.
struct mesh_peer_update_ind {
	/// Indicate if connection has been established or lost
	bool estab;
	/// VIF Index
	u8 vif_idx;
	/// STA Index
	u8 sta_idx;
	/// Peer MAC Address
	struct mac_addr peer_addr;
};

/// Structure containing the parameters of the @ref MESH_PEER_UPDATE_NTF
/// message.
struct mesh_peer_update_ntf {
	/// VIF Index
	u8 vif_idx;
	/// STA Index
	u8 sta_idx;
	/// Mesh Link State
	u8 state;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_IND
/// message.
struct mesh_path_update_ind {
	/// Indicate if path is deleted or not
	bool delete;
	/// Indicate if path is towards an external STA (not part of MBSS)
	bool ext_sta;
	/// VIF Index
	u8 vif_idx;
	/// Path Index
	u8 path_idx;
	/// Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// External STA MAC Address (only if ext_sta is true)
	struct mac_addr ext_sta_mac_addr;
	/// Next Hop STA Index
	u8 nhop_sta_idx;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Debug messages
///////////////////////////////////////////////////////////////////////////////

/// Messages related to Debug Task
enum dbg_msg_tag {
	/// Memory read request
	DBG_MEM_READ_REQ = LMAC_FIRST_MSG(TASK_DBG),
	/// Memory read confirm
	DBG_MEM_READ_CFM,
	/// Memory write request
	DBG_MEM_WRITE_REQ,
	/// Memory write confirm
	DBG_MEM_WRITE_CFM,
	/// Module filter request
	DBG_SET_MOD_FILTER_REQ,
	/// Module filter confirm
	DBG_SET_MOD_FILTER_CFM,
	/// Severity filter request
	DBG_SET_SEV_FILTER_REQ,
	/// Severity filter confirm
	DBG_SET_SEV_FILTER_CFM,
	/// LMAC/MAC HW fatal error indication
	DBG_ERROR_IND,
	/// Request to get system statistics
	DBG_GET_SYS_STAT_REQ,
	/// COnfirmation of system statistics
	DBG_GET_SYS_STAT_CFM,
	/// Memory block write request
	DBG_MEM_BLOCK_WRITE_REQ,
	/// Memory block write confirm
	DBG_MEM_BLOCK_WRITE_CFM,
	/// Start app request
	DBG_START_APP_REQ,
	/// Start app confirm
	DBG_START_APP_CFM,
	/// Start npc request
	DBG_START_NPC_REQ,
	/// Start npc confirm
	DBG_START_NPC_CFM,
	/// Memory mask write request
	DBG_MEM_MASK_WRITE_REQ,
	/// Memory mask write confirm
	DBG_MEM_MASK_WRITE_CFM,

	DBG_RFTEST_CMD_REQ,
	DBG_RFTEST_CMD_CFM,
	DBG_BINDING_REQ,
	DBG_BINDING_CFM,
	DBG_BINDING_IND,

	DBG_CUSTOM_MSG_REQ,
	DBG_CUSTOM_MSG_CFM,
	DBG_CUSTOM_MSG_IND,

	DBG_GPIO_WRITE_REQ,
	DBG_GPIO_WRITE_CFM,
	DBG_GPIO_READ_REQ,
	DBG_GPIO_READ_CFM,
	DBG_GPIO_INIT_REQ,
	DBG_GPIO_INIT_CFM,

	/// EF usrdata read request
	DBG_EF_USRDATA_READ_REQ,
	/// EF usrdata read confirm
	DBG_EF_USRDATA_READ_CFM,
	/// Memory block read request
	DBG_MEM_BLOCK_READ_REQ,
	/// Memory block read confirm
	DBG_MEM_BLOCK_READ_CFM,

	DBG_PWM_INIT_REQ,
	DBG_PWM_INIT_CFM,
	DBG_PWM_DEINIT_REQ,
	DBG_PWM_DEINIT_CFM,

	/// Max number of Debug messages
	DBG_MAX,
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_REQ message.
struct dbg_mem_read_req {
	u32 memaddr;
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_CFM message.
struct dbg_mem_read_cfm {
	u32 memaddr;
	u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_REQ message.
struct dbg_mem_write_req {
	u32 memaddr;
	u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_CFM message.
struct dbg_mem_write_cfm {
	u32 memaddr;
	u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_MASK_WRITE_REQ
/// message.
struct dbg_mem_mask_write_req {
	u32 memaddr;
	u32 memmask;
	u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_MASK_WRITE_CFM
/// message.
struct dbg_mem_mask_write_cfm {
	u32 memaddr;
	u32 memdata;
};

struct dbg_rftest_cmd_req {
	u32 cmd;
	u32 argc;
	u8 argv[30];
};

struct dbg_rftest_cmd_cfm {
	u32 rftest_result[18];
};

struct dbg_gpio_write_req {
	u8 gpio_idx;
	u8 gpio_val;
};

struct dbg_gpio_read_req {
	u8 gpio_idx;
};

struct dbg_gpio_read_cfm {
	u8 gpio_idx;
	u8 gpio_val;
};

struct dbg_gpio_init_req {
	u8 gpio_idx;
	u8 gpio_dir; // 1 output, 0 input;
	u8 gpio_val; // for output, 1 high, 0 low;
};

/// Structure containing the parameters of the @ref DBG_SET_MOD_FILTER_REQ
/// message.
struct dbg_set_mod_filter_req {
	/// Bit field indicating for each module if the traces are enabled or not
	u32 mod_filter;
};

/// Structure containing the parameters of the @ref DBG_SEV_MOD_FILTER_REQ
/// message.
struct dbg_set_sev_filter_req {
	/// Bit field indicating the severity threshold for the traces
	u32 sev_filter;
};

/// Structure containing the parameters of the @ref DBG_GET_SYS_STAT_CFM
/// message.
struct dbg_get_sys_stat_cfm {
	/// Time spent in CPU sleep since last reset of the system statistics
	u32 cpu_sleep_time;
	/// Time spent in DOZE since last reset of the system statistics
	u32 doze_time;
	/// Total time spent since last reset of the system statistics
	u32 stats_time;
};

/// Structure containing the parameters of the @ref DBG_MEM_BLOCK_WRITE_REQ
/// message.
struct dbg_mem_block_write_req {
	u32 memaddr;
	u32 memsize;
	u32 memdata[1024 / sizeof(u32)];
};

/// Structure containing the parameters of the @ref DBG_MEM_BLOCK_WRITE_CFM
/// message.
struct dbg_mem_block_write_cfm {
	u32 wstatus;
};

/// Structure containing the parameters of the @ref DBG_START_APP_REQ message.
struct dbg_start_app_req {
	u32 bootaddr;
	u32 boottype;
};

/// Structure containing the parameters of the @ref DBG_START_APP_CFM message.
struct dbg_start_app_cfm {
	u32 bootstatus;
};

enum {
	HOST_START_APP_AUTO = 1,
	HOST_START_APP_CUSTOM,
	HOST_START_APP_FNCALL = 4,
	HOST_START_APP_DUMMY = 5,
};

///////////////////////////////////////////////////////////////////////////////
/////////// For TDLS messages
///////////////////////////////////////////////////////////////////////////////

/// List of messages related to the task.
enum tdls_msg_tag {
	/// TDLS channel Switch Request.
	TDLS_CHAN_SWITCH_REQ = LMAC_FIRST_MSG(TASK_TDLS),
	/// TDLS channel switch confirmation.
	TDLS_CHAN_SWITCH_CFM,
	/// TDLS channel switch indication.
	TDLS_CHAN_SWITCH_IND,
	/// TDLS channel switch to base channel indication.
	TDLS_CHAN_SWITCH_BASE_IND,
	/// TDLS cancel channel switch request.
	TDLS_CANCEL_CHAN_SWITCH_REQ,
	/// TDLS cancel channel switch confirmation.
	TDLS_CANCEL_CHAN_SWITCH_CFM,
	/// TDLS peer power save indication.
	TDLS_PEER_PS_IND,
	/// TDLS peer traffic indication request.
	TDLS_PEER_TRAFFIC_IND_REQ,
	/// TDLS peer traffic indication confirmation.
	TDLS_PEER_TRAFFIC_IND_CFM,

	/// MAX number of messages
	TDLS_MAX
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_REQ message
struct tdls_chan_switch_req {
	/// Index of the VIF
	u8 vif_index;
	/// STA Index
	u8 sta_idx;
	/// MAC address of the TDLS station
	struct mac_addr peer_mac_addr;
	bool initiator;
	/// Band (2.4GHz or 5GHz)
	u8 band;
	/// Channel type: 20,40,80,160 or 80+80 MHz
	u8 type;
	/// Frequency for Primary 20MHz channel (in MHz)
	u16 prim20_freq;
	/// Frequency for Center of the contiguous channel or center of Primary
	/// 80+80
	u16 center1_freq;
	/// Frequency for Center of the non-contiguous secondary 80+80
	u16 center2_freq;
	/// TX power (in dBm)
	s8 tx_power;
	/// Operating class
	u8 op_class;
};

/// Structure containing the parameters of the @ref TDLS_CANCEL_CHAN_SWITCH_REQ
/// message
struct tdls_cancel_chan_switch_req {
	/// Index of the VIF
	u8 vif_index;
	/// STA Index
	u8 sta_idx;
	/// MAC address of the TDLS station
	struct mac_addr peer_mac_addr;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_CFM message
struct tdls_chan_switch_cfm {
	/// Status of the operation
	u8 status;
};

/// Structure containing the parameters of the @ref TDLS_CANCEL_CHAN_SWITCH_CFM
/// message
struct tdls_cancel_chan_switch_cfm {
	/// Status of the operation
	u8 status;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_IND message
struct tdls_chan_switch_ind {
	/// VIF Index
	u8 vif_index;
	/// Channel Context Index
	u8 chan_ctxt_index;
	/// Status of the operation
	u8 status;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_BASE_IND
/// message
struct tdls_chan_switch_base_ind {
	/// VIF Index
	u8 vif_index;
	/// Channel Context index
	u8 chan_ctxt_index;
};

/// Structure containing the parameters of the @ref TDLS_PEER_PS_IND message
struct tdls_peer_ps_ind {
	/// VIF Index
	u8 vif_index;
	/// STA Index
	u8 sta_idx;
	/// MAC ADDR of the TDLS STA
	struct mac_addr peer_mac_addr;
	/// Flag to indicate if the TDLS peer is going to sleep
	bool ps_on;
};

/// Structure containing the parameters of the @ref TDLS_PEER_TRAFFIC_IND_REQ
/// message
struct tdls_peer_traffic_ind_req {
	/// VIF Index
	u8 vif_index;
	/// STA Index
	u8 sta_idx;
	// MAC ADDR of the TDLS STA
	struct mac_addr peer_mac_addr;
	/// Dialog token
	u8 dialog_token;
	/// TID of the latest MPDU transmitted over the TDLS direct link to the TDLS
	/// STA
	u8 last_tid;
	/// Sequence number of the latest MPDU transmitted over the TDLS direct link
	/// to the TDLS STA
	u16 last_sn;
};

/// Structure containing the parameters of the @ref TDLS_PEER_TRAFFIC_IND_CFM
/// message
struct tdls_peer_traffic_ind_cfm {
	/// Status of the operation
	u8 status;
};

#endif // LMAC_MSG_H_
