/* SPDX-License-Identifier: GPL-2.0 */
/*
 ******************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @brief Shared data between both IPC modules
 *
 ******************************************************************************
 */

#ifndef _IPC_SHARED_H_
#define _IPC_SHARED_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "ipc_compat.h"
#include "lmac_mac.h"
#include "rwnx_compat.h"

/*
 * DEFINES AND MACROS
 ****************************************************************************************
 */
#define CO_BIT(pos) (1U << (pos))

#define IPC_TXQUEUE_CNT NX_TXQ_CNT
#define NX_TXDESC_CNT0  8
#define NX_TXDESC_CNT1  64
#define NX_TXDESC_CNT2  64
#define NX_TXDESC_CNT3  32
#if NX_TXQ_CNT == 5
#define NX_TXDESC_CNT4 8
#endif

/*
 * Number of Host buffers available for Data Rx handling (through DMA)
 */
#define IPC_RXBUF_CNT 128

/*
 * Number of shared descriptors available for Data RX handling
 */
#define IPC_RXDESC_CNT 128

/*
 * Number of Host buffers available for Radar events handling (through DMA)
 */
#define IPC_RADARBUF_CNT 16

/*
 * Number of Host buffers available for unsupported Rx vectors handling (through
 * DMA)
 */
#define IPC_UNSUPRXVECBUF_CNT 8

/*
 *  Size of RxVector
 */
#define IPC_RXVEC_SIZE 16

/*
 * Number of Host buffers available for Emb->App MSGs sending (through DMA)
 */
#ifdef CONFIG_RWNX_FULLMAC
#define IPC_MSGE2A_BUF_CNT 64
#endif
/*
 * Number of Host buffers available for Debug Messages sending (through DMA)
 */
#define IPC_DBGBUF_CNT 32

/*
 * Length used in MSGs structures
 */
#define IPC_A2E_MSG_BUF_SIZE 127 // size in 4-byte words
#ifdef CONFIG_RWNX_FULLMAC
#define IPC_E2A_MSG_SIZE_BASE 256 // size in 4-byte words
#endif

#define IPC_E2A_MSG_PARAM_SIZE IPC_E2A_MSG_SIZE_BASE

/*
 * Debug messages buffers size (in bytes)
 */
#define IPC_DBG_PARAM_SIZE 256

/*
 * Define used for Rx hostbuf validity.
 * This value should appear only when hostbuf was used for a Reception.
 */
#define RX_DMA_OVER_PATTERN 0xAAAAAA00

/*
 * Define used for MSG buffers validity.
 * This value will be written only when a MSG buffer is used for sending from
 * Emb to App.
 */
#define IPC_MSGE2A_VALID_PATTERN 0xADDEDE2A

/*
 * Define used for Debug messages buffers validity.
 * This value will be written only when a DBG buffer is used for sending from
 * Emb to App.
 */
#define IPC_DBG_VALID_PATTERN 0x000CACA0

/*
 *  Length of the receive vectors, in bytes
 */
#define DMA_HDR_PHYVECT_LEN 36

/*
 * Maximum number of payload addresses and lengths present in the descriptor
 */
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
#define NX_TX_PAYLOAD_MAX 6
#else
#define NX_TX_PAYLOAD_MAX 1
#endif

/*
 * Message struct/ID API version
 */
#define MSG_API_VER 33

/*
 ****************************************************************************************
 */
// c.f LMAC/src/tx/tx_swdesc.h
/// Descriptor filled by the Host
struct hostdesc {
	/// Pointer to packet payload
	// u32 packet_addr;
	/// Size of the payload
	u16 packet_len;
	u16 flags_ext;

	u32 hostid;
#ifdef CONFIG_RWNX_FULLMAC
	/// Address of the status descriptor in host memory (used for confirmation
	/// upload)
	// u32 status_desc_addr;
	/// Destination Address
	struct mac_addr eth_dest_addr;
	/// Source Address
	struct mac_addr eth_src_addr;
	/// Ethernet Type
	__be16 ethertype;
#else /* ! CONFIG_RWNX_FULLMAC */
#ifdef CONFIG_RWNX_AGG_TX
	/// Sequence Number for AMPDU MPDUs - for quick check if it's allowed within
	/// window
	u16 sn;
#endif /* CONFIG_RWNX_AGG_TX */
	/// Padding between the buffer control structure and the MPDU in host memory
	u8 padding;
#endif /* CONFIG_RWNX_FULLMAC */
	u8 ac;
	/// Packet TID (0xFF if not a QoS frame)
	u8 tid;
	/// Interface Id
	u8 vif_idx;
	/// Station Id (0xFF if station is unknown)
	u8 staid;
#ifdef CONFIG_RWNX_MUMIMO_TX
	/// MU-MIMO information (GroupId and User Position in the group) - The
	/// GroupId is located on bits 0-5 and the User Position on bits 6-7. The
	/// GroupId value is set to 63 if MU-MIMO shall not be used
	u8 mumimo_info;
#endif /* CONFIG_RWNX_MUMIMO_TX */
#ifdef CONFIG_RWNX_FULLMAC
	/// TX flags
	u16 flags;
#endif /* CONFIG_RWNX_FULLMAC */
};

/// Descriptor filled by the UMAC
struct umacdesc {
#ifdef CONFIG_RWNX_AGG_TX
	/// First Sequence Number of the BlockAck window
	u16 sn_win;
	/// Flags from UMAC (match tx_hd.macctrlinfo2 format)
	u32 flags;
	/// PHY related flags field - rate, GI type, BW type - filled by driver
	u32 phy_flags;
#endif //(CONFIG_RWNX_AGG_TX)
};

struct txdesc_api {
	/// Information provided by Host
	struct hostdesc host;
};

struct txdesc_host {
	u32 ready;

	/// API of the embedded part
	struct txdesc_api api;
};

/// Comes from ipc_dma.h
/// Element in the pool of TX DMA bridge descriptors.
struct dma_desc {
	/* Application subsystem address which is used as source address for DMA
	 * payload transfer
	 */
	u32 src;
	/** Points to the start of the embedded data buffer associated with this
	 * descriptor. This address acts as the destination address for the DMA
	 * payload transfer
	 */
	u32 dest;
	/// Complete length of the buffer in memory
	u16 length;
	/// Control word for the DMA engine (e.g. for interrupt generation)
	u16 ctrl;
	/// Pointer to the next element of the chained list
	u32 next;
};

// Comes from la.h
/// Length of the configuration data of a logic analyzer
#define LA_CONF_LEN 10

/// Structure containing the configuration data of a logic analyzer
struct la_conf_tag {
	u32 conf[LA_CONF_LEN];
	u32 trace_len;
	u32 diag_conf;
};

/// Size of a logic analyzer memory
#define LA_MEM_LEN (1024 * 1024)

/// Type of errors
enum {
	/// Recoverable error, not requiring any action from Upper MAC
	DBG_ERROR_RECOVERABLE = 0,
	/// Fatal error, requiring Upper MAC to reset Lower MAC and HW and restart
	/// operation
	DBG_ERROR_FATAL
};

/// Maximum length of the SW diag trace
#define DBG_SW_DIAG_MAX_LEN 1024

/// Maximum length of the error trace
#define DBG_ERROR_TRACE_SIZE 256

/// Number of MAC diagnostic port banks
#define DBG_DIAGS_MAC_MAX 48

/// Number of PHY diagnostic port banks
#define DBG_DIAGS_PHY_MAX 32

/// Maximum size of the RX header descriptor information in the debug dump
#define DBG_RHD_MEM_LEN (5 * 1024)

/// Maximum size of the RX buffer descriptor information in the debug dump
#define DBG_RBD_MEM_LEN (5 * 1024)

/// Maximum size of the TX header descriptor information in the debug dump
#define DBG_THD_MEM_LEN (10 * 1024)

/// Structure containing the information about the PHY channel that is used
struct phy_channel_info {
	/// PHY channel information 1
	u32 info1;
	/// PHY channel information 2
	u32 info2;
};

/// Debug information forwarded to host when an error occurs
struct dbg_debug_info_tag {
	/// Type of error (0: recoverable, 1: fatal)
	u32 error_type;
	/// Pointer to the first RX Header Descriptor chained to the MAC HW
	u32 rhd;
	/// Size of the RX header descriptor buffer
	u32 rhd_len;
	/// Pointer to the first RX Buffer Descriptor chained to the MAC HW
	u32 rbd;
	/// Size of the RX buffer descriptor buffer
	u32 rbd_len;
	/// Pointer to the first TX Header Descriptors chained to the MAC HW
	u32 thd[NX_TXQ_CNT];
	/// Size of the TX header descriptor buffer
	u32 thd_len[NX_TXQ_CNT];
	/// MAC HW diag configuration
	u32 hw_diag;
	/// Error message
	u32 error[DBG_ERROR_TRACE_SIZE / 4];
	/// SW diag configuration length
	u32 sw_diag_len;
	/// SW diag configuration
	u32 sw_diag[DBG_SW_DIAG_MAX_LEN / 4];
	/// PHY channel information
	struct phy_channel_info chan_info;
	/// Embedded LA configuration
	struct la_conf_tag la_conf;
	/// MAC diagnostic port state
	u16 diags_mac[DBG_DIAGS_MAC_MAX];
	/// PHY diagnostic port state
	u16 diags_phy[DBG_DIAGS_PHY_MAX];
	/// MAC HW RX Header descriptor pointer
	u32 rhd_hw_ptr;
	/// MAC HW RX Buffer descriptor pointer
	u32 rbd_hw_ptr;
};

/// Full debug dump that is forwarded to host in case of error
struct dbg_debug_dump_tag {
	/// Debug information
	struct dbg_debug_info_tag dbg_info;

	/// RX header descriptor memory
	u32 rhd_mem[DBG_RHD_MEM_LEN / 4];

	/// RX buffer descriptor memory
	u32 rbd_mem[DBG_RBD_MEM_LEN / 4];

	/// TX header descriptor memory
	u32 thd_mem[NX_TXQ_CNT][DBG_THD_MEM_LEN / 4];

	/// Logic analyzer memory
	u32 la_mem[LA_MEM_LEN / 4];
};

/// Number of pulses in a radar event structure
#define RADAR_PULSE_MAX 4

/// Definition of an array of radar pulses
struct radar_pulse_array_desc {
	/// Buffer containing the radar pulses
	u32 pulse[RADAR_PULSE_MAX];
	/// Index of the radar detection chain that detected those pulses
	u32 idx;
	/// Number of valid pulses in the buffer
	u32 cnt;
};

/// Bit mapping inside a radar pulse element
struct radar_pulse {
	s32
		freq : 6;  /** Freq (resolution is 2Mhz range is [-Fadc/4 .. Fadc/4]) */
	u32 fom : 4; /** Figure of Merit */
	u32 len : 6; /** Length of the current radar pulse (resolution is 2us) */
	u32 rep : 16; /* Time interval between the previous radar event
			 * and the current one (in us)
			 */
};

/// Definition of a RX vector descriptor
struct rx_vector_desc {
	/// PHY channel information
	struct phy_channel_info phy_info;

	/// RX vector 1
	u32 rx_vect1[IPC_RXVEC_SIZE / 4];

	/// Used to print a valid rx vector
	u32 pattern;
};

///
struct rxdesc_tag {
	/// Host Buffer Address
	u32 host_id;
	/// Length
	u32 frame_len;
	/// Status
	u16 status;
};

/*
 ****************************************************************************************
 * @addtogroup IPC_TX IPC Tx path
 *  @ingroup IPC
 *  @brief IPC Tx path structures and functions
 *
 * A typical use case of the IPC Tx path API:
 * @msc
 * hscale = "2";
 *
 * a [label=Driver],
 * b [label="IPC host"],
 * c [label="IPC emb"],
 * d [label=Firmware];
 *
 * ---   [label="Tx descriptor queue example"];
 * a=>a  [label="Driver receives a Tx packet from OS"];
 * a=>b  [label="ipc_host_txdesc_get()"];
 * a<<b  [label="struct txdesc_host *"];
 * a=>a  [label="Driver fill the descriptor"];
 * a=>b  [label="ipc_host_txdesc_push()"];
 * ...   [label="(several Tx desc can be pushed)"];
 * b:>c  [label="Tx desc queue filled IRQ"];
 * c=>>d [label="EDCA sub-scheduler callback"];
 * c<<d  [label="Tx desc queue to pop"];
 * c=>>d [label="UMAC Tx desc callback"];
 * ...   [label="(several Tx desc can be popped)"];
 * d=>d  [label="Packets are sent or discarded"];
 * ---   [label="Tx confirm queue example"];
 * c<=d  [label="ipc_emb_txcfm_push()"];
 * c>>d  [label="Request accepted"];
 * ...   [label="(several Tx cfm can be pushed)"];
 * b<:c  [label="Tx cfm queue filled IRQ"];
 * a<<=b [label="Driver's Tx Confirm callback"];
 * a=>b  [label="ipc_host_txcfm_pop()"];
 * a<<b  [label="struct ipc_txcfm"];
 * a<=a  [label="Packets are freed by the driver"];
 * @endmsc
 *
 * @{
 ****************************************************************************************
 */

/// @} IPC_TX

/*
 ****************************************************************************************
 *  @defgroup IPC_RX IPC Rx path
 *  @ingroup IPC
 *  @brief IPC Rx path functions and structures
 *
 * A typical use case of the IPC Rx path API:
 * @msc
 * hscale = "2";
 *
 * a [label=Firmware],
 * b [label="IPC emb"],
 * c [label="IPC host"],
 * d [label=Driver];
 *
 * ---   [label="Rx buffer and desc queues usage example"];
 * d=>c  [label="ipc_host_rxbuf_push()"];
 * d=>c  [label="ipc_host_rxbuf_push()"];
 * d=>c  [label="ipc_host_rxbuf_push()"];
 * ...   [label="(several Rx buffer are pushed)"];
 * a=>a  [label=" Frame is received\n from the medium"];
 * a<<b  [label="struct ipc_rxbuf"];
 * a=>a  [label=" Firmware fill the buffer\n with received frame"];
 * a<<b  [label="Push accepted"];
 * ...   [label="(several Rx desc can be pushed)"];
 * b:>c  [label="Rx desc queue filled IRQ"];
 * c=>>d [label="Driver Rx packet callback"];
 * c<=d  [label="ipc_host_rxdesc_pop()"];
 * d=>d  [label="Rx packet is handed\nover to the OS "];
 * ...   [label="(several Rx desc can be poped)"];
 * ---   [label="Rx buffer request exemple"];
 * b:>c  [label="Low Rx buffer count IRQ"];
 * a<<b  [label="struct ipc_rxbuf"];
 * c=>>d [label="Driver Rx buffer callback"];
 * d=>c  [label="ipc_host_rxbuf_push()"];
 * d=>c  [label="ipc_host_rxbuf_push()"];
 * d=>c  [label="ipc_host_rxbuf_push()"];
 * ...   [label="(several Rx buffer are pushed)"];
 * @endmsc
 *
 * @addtogroup IPC_RX
 * @{
 ****************************************************************************************
 */

/// @} IPC_RX

/*
 ****************************************************************************************
 *  @defgroup IPC_MISC IPC Misc
 *  @ingroup IPC
 *  @brief IPC miscellaneous functions
 ****************************************************************************************
 */
/** IPC header structure.  This structure is stored at the beginning of every
 * IPC message.
 * @warning This structure's size must NOT exceed 4 bytes in length.
 */
struct ipc_header {
	/// IPC message type.
	u16 type;
	/// IPC message size in number of bytes.
	u16 size;
};

struct ipc_msg_elt {
	/// Message header (alignment forced on word size, see allocation in shared
	/// env).
	struct ipc_header header __ALIGN4;
};

/// Message structure for MSGs from Emb to App
struct ipc_e2a_msg {
	u16 id; ///< Message id.
	u16 dummy_dest_id;
	u16 dummy_src_id;
	u16 param_len;                     ///< Parameter embedded struct length.
	u32 pattern;                       ///< Used to stamp a valid MSG buffer
	u32 param[IPC_E2A_MSG_PARAM_SIZE]; ///< Parameter embedded struct. Must be
										 ///< word-aligned.
};

/// Message structure for Debug messages from Emb to App
struct ipc_dbg_msg {
	u32 string[IPC_DBG_PARAM_SIZE / 4]; ///< Debug string
	u32 pattern;                        ///< Used to stamp a valid buffer
};

/// Message structure for MSGs from App to Emb.
/// Actually a sub-structure will be used when filling the messages.
struct ipc_a2e_msg {
	u32 dummy_word; // used to cope with kernel message structure
	u32 msg[IPC_A2E_MSG_BUF_SIZE]; // body of the msg
};

struct ipc_shared_rx_buf {
	/// < ptr to hostbuf client (ipc_host client) structure
	u32 hostid;
	/// < ptr to real hostbuf dma address
	u32 dma_addr;
};

struct ipc_shared_rx_desc {
	/// DMA Address
	u32 dma_addr;
};

/// Structure containing FW characteristics for compatibility checking
struct compatibility_tag {
	/// Size of IPC shared memory
	u16 ipc_shared_size;
	/// Message struct/ID API version
	u16 msg_api;
	/// Version of IPC shared
	u8 ipc_shared_version;
	/// Number of host buffers available for Emb->App MSGs sending
	u8 msge2a_buf_cnt;
	/// Number of host buffers available for Debug Messages sending
	u8 dbgbuf_cnt;
	/// Number of host buffers available for Radar events handling
	u8 radarbuf_cnt;
	/// Number of host buffers available for unsupported Rx vectors handling
	u8 unsuprxvecbuf_cnt;
	/// Number of shared descriptors available for Data RX handling
	u8 rxdesc_cnt;
	/// Number of host buffers available for Data Rx handling
	u8 rxbuf_cnt;
	/// Number of descriptors in BK TX queue (power of 2, min 4, max 64)
	u8 bk_txq;
	/// Number of descriptors in BE TX queue (power of 2, min 4, max 64)
	u8 be_txq;
	/// Number of descriptors in VI TX queue (power of 2, min 4, max 64)
	u8 vi_txq;
	/// Number of descriptors in VO TX queue (power of 2, min 4, max 64)
	u8 vo_txq;
	/// Number of descriptors in BCN TX queue (power of 2, min 4, max 64)
	u8 bcn_txq;
};

/*
 * TYPE and STRUCT DEFINITIONS
 ****************************************************************************************
 */

// Indexes are defined in the MIB shared structure
struct ipc_shared_env_tag {
	struct compatibility_tag comp_info; // FW characteristics

	struct ipc_a2e_msg
		msg_a2e_buf; // room for MSG to be sent from App to Emb

	// Fields for MSGs sending from Emb to App
	struct ipc_e2a_msg
		msg_e2a_buf; // room to build the MSG to be DMA Xferred
	struct dma_desc
		msg_dma_desc; // DMA descriptor for Emb->App MSGs Xfers
	u32 msg_e2a_hostbuf_addr[IPC_MSGE2A_BUF_CNT]; // buffers @ for DMA Xfers

	// Fields for Debug MSGs sending from Emb to App
	struct ipc_dbg_msg
		dbg_buf; // room to build the MSG to be DMA Xferred
	struct dma_desc
		dbg_dma_desc; // DMA descriptor for Emb->App MSGs Xfers
	u32 dbg_hostbuf_addr[IPC_DBGBUF_CNT]; // buffers @ for MSGs DMA Xfers
	u32 la_dbginfo_addr; // Host buffer address for the debug information
	u32 pattern_addr;

	u32 radarbuf_hostbuf[IPC_RADARBUF_CNT]; // buffers @ for Radar Events
	u32 unsuprxvecbuf_hostbuf[IPC_UNSUPRXVECBUF_CNT]; // buffers @ for
	// unsupported Rx vectors
	struct txdesc_host txdesc0[CONFIG_USER_MAX][NX_TXDESC_CNT0];
	struct txdesc_host txdesc1[CONFIG_USER_MAX][NX_TXDESC_CNT1];
	struct txdesc_host txdesc2[CONFIG_USER_MAX][NX_TXDESC_CNT2];
	struct txdesc_host txdesc3[CONFIG_USER_MAX][NX_TXDESC_CNT3];
#if NX_TXQ_CNT == 5
	struct txdesc_host txdesc4[1][NX_TXDESC_CNT4];
#endif
#ifdef CONFIG_RWNX_FULLMAC
	// RX Descriptors Array
	struct ipc_shared_rx_desc host_rxdesc[IPC_RXDESC_CNT];
	// RX Buffers Array
	struct ipc_shared_rx_buf host_rxbuf[IPC_RXBUF_CNT];
#else
	// buffers @ for Data Rx
	u32 host_rxbuf[IPC_RXBUF_CNT];
#endif /* CONFIG_RWNX_FULLMAC */

	u32 buffered[NX_REMOTE_STA_MAX][TID_MAX];

	u16 trace_pattern;
	u32 trace_start;
	u32 trace_end;
	u32 trace_size;
	u32 trace_offset;
	u32 trace_nb_compo;
	u32 trace_offset_compo;
};

extern struct ipc_shared_env_tag ipc_shared_env;

/*
 * TYPE and STRUCT DEFINITIONS
 ****************************************************************************************
 */

// IRQs from app to emb
/// Interrupts bits used for the TX descriptors of the AC queues
#ifdef CONFIG_RWNX_MUMIMO_TX
/// Interrupts bits used
#if CONFIG_USER_MAX > 3
#define IPC_IRQ_A2E_USER_MSK 0xF
#elif CONFIG_USER_MAX > 2
#define IPC_IRQ_A2E_USER_MSK 0x7
#else
#define IPC_IRQ_A2E_USER_MSK 0x3
#endif

/// Offset of the interrupts for AC0
#define IPC_IRQ_A2E_AC0_OFT 8
/// Mask of the interrupts for AC0
#define IPC_IRQ_A2E_AC0_MSK (IPC_IRQ_A2E_USER_MSK << IPC_IRQ_A2E_AC0_OFT)
/// Offset of the interrupts for AC1
#define IPC_IRQ_A2E_AC1_OFT (IPC_IRQ_A2E_AC0_OFT + CONFIG_USER_MAX)
/// Mask of the interrupts for AC1
#define IPC_IRQ_A2E_AC1_MSK (IPC_IRQ_A2E_USER_MSK << IPC_IRQ_A2E_AC1_OFT)
/// Offset of the interrupts for AC2
#define IPC_IRQ_A2E_AC2_OFT (IPC_IRQ_A2E_AC1_OFT + CONFIG_USER_MAX)
/// Mask of the interrupts for AC2
#define IPC_IRQ_A2E_AC2_MSK (IPC_IRQ_A2E_USER_MSK << IPC_IRQ_A2E_AC2_OFT)
/// Offset of the interrupts for AC3
#define IPC_IRQ_A2E_AC3_OFT (IPC_IRQ_A2E_AC2_OFT + CONFIG_USER_MAX)
/// Mask of the interrupts for AC3
#define IPC_IRQ_A2E_AC3_MSK (IPC_IRQ_A2E_USER_MSK << IPC_IRQ_A2E_AC3_OFT)
/// Offset of the interrupts for BCN
#define IPC_IRQ_A2E_BCN_OFT (IPC_IRQ_A2E_AC3_OFT + CONFIG_USER_MAX)
/// Mask of the interrupts for BCN
#define IPC_IRQ_A2E_BCN_MSK CO_BIT(IPC_IRQ_A2E_BCN_OFT)

#define IPC_IRQ_A2E_AC_TXDESC                                                  \
	(IPC_IRQ_A2E_AC0_MSK | IPC_IRQ_A2E_AC1_MSK | IPC_IRQ_A2E_AC2_MSK |         \
	 IPC_IRQ_A2E_AC3_MSK)

/// Interrupts bits used for the TX descriptors of the BCN queue
#if NX_TXQ_CNT < 5
#define IPC_IRQ_A2E_BCN_TXDESC 0
#else
#define IPC_IRQ_A2E_BCN_TXDESC (0x01 << IPC_IRQ_A2E_BCN_OFT)
#endif

/// IPC TX descriptor interrupt mask
#define IPC_IRQ_A2E_TXDESC (IPC_IRQ_A2E_AC_TXDESC | IPC_IRQ_A2E_BCN_TXDESC)
#else
/// IPC TX descriptor interrupt mask
#define IPC_IRQ_A2E_TXDESC 0xFF00
#endif

#define IPC_IRQ_A2E_TXDESC_FIRSTBIT (8)
#define IPC_IRQ_A2E_RXBUF_BACK      CO_BIT(5)
#define IPC_IRQ_A2E_RXDESC_BACK     CO_BIT(4)

#define IPC_IRQ_A2E_MSG CO_BIT(1)
#define IPC_IRQ_A2E_DBG CO_BIT(0)

#define IPC_IRQ_A2E_ALL (IPC_IRQ_A2E_TXDESC | IPC_IRQ_A2E_MSG | IPC_IRQ_A2E_DBG)

// IRQs from emb to app
#define IPC_IRQ_E2A_TXCFM_POS 7

#ifdef CONFIG_RWNX_MUMIMO_TX
/// Interrupts bits used
#if CONFIG_USER_MAX > 3
#define IPC_IRQ_E2A_USER_MSK 0xF
#elif CONFIG_USER_MAX > 2
#define IPC_IRQ_E2A_USER_MSK 0x7
#else
#define IPC_IRQ_E2A_USER_MSK 0x3
#endif

/// Offset of the interrupts for AC0
#define IPC_IRQ_E2A_AC0_OFT IPC_IRQ_E2A_TXCFM_POS
/// Mask of the interrupts for AC0
#define IPC_IRQ_E2A_AC0_MSK (IPC_IRQ_E2A_USER_MSK << IPC_IRQ_E2A_AC0_OFT)
/// Offset of the interrupts for AC1
#define IPC_IRQ_E2A_AC1_OFT (IPC_IRQ_E2A_AC0_OFT + CONFIG_USER_MAX)
/// Mask of the interrupts for AC1
#define IPC_IRQ_E2A_AC1_MSK (IPC_IRQ_E2A_USER_MSK << IPC_IRQ_E2A_AC1_OFT)
/// Offset of the interrupts for AC2
#define IPC_IRQ_E2A_AC2_OFT (IPC_IRQ_E2A_AC1_OFT + CONFIG_USER_MAX)
/// Mask of the interrupts for AC2
#define IPC_IRQ_E2A_AC2_MSK (IPC_IRQ_E2A_USER_MSK << IPC_IRQ_E2A_AC2_OFT)
/// Offset of the interrupts for AC3
#define IPC_IRQ_E2A_AC3_OFT (IPC_IRQ_E2A_AC2_OFT + CONFIG_USER_MAX)
/// Mask of the interrupts for AC3
#define IPC_IRQ_E2A_AC3_MSK (IPC_IRQ_E2A_USER_MSK << IPC_IRQ_E2A_AC3_OFT)
/// Offset of the interrupts for BCN
#define IPC_IRQ_E2A_BCN_OFT (IPC_IRQ_E2A_AC3_OFT + CONFIG_USER_MAX)
/// Mask of the interrupts for BCN
#define IPC_IRQ_E2A_BCN_MSK CO_BIT(IPC_IRQ_E2A_BCN_OFT)

#define IPC_IRQ_E2A_AC_TXCFM                                                   \
	(IPC_IRQ_E2A_AC0_MSK | IPC_IRQ_E2A_AC1_MSK | IPC_IRQ_E2A_AC2_MSK |         \
	 IPC_IRQ_E2A_AC3_MSK)

/// Interrupts bits used for the TX descriptors of the BCN queue
#if NX_TXQ_CNT < 5
#define IPC_IRQ_E2A_BCN_TXCFM 0
#else
#define IPC_IRQ_E2A_BCN_TXCFM (0x01 << IPC_IRQ_E2A_BCN_OFT)
#endif

/// IPC TX descriptor interrupt mask
#define IPC_IRQ_E2A_TXCFM (IPC_IRQ_E2A_AC_TXCFM | IPC_IRQ_E2A_BCN_TXCFM)

#else

#define IPC_IRQ_E2A_TXCFM (((1 << NX_TXQ_CNT) - 1) << IPC_IRQ_E2A_TXCFM_POS)

#endif /* CONFIG_RWNX_MUMIMO_TX */

#define IPC_IRQ_E2A_UNSUP_RX_VEC CO_BIT(7)
#define IPC_IRQ_E2A_RADAR        CO_BIT(6)
#define IPC_IRQ_E2A_TBTT_SEC     CO_BIT(5)
#define IPC_IRQ_E2A_TBTT_PRIM    CO_BIT(4)
#define IPC_IRQ_E2A_RXDESC       CO_BIT(3)
#define IPC_IRQ_E2A_MSG_ACK      CO_BIT(2)
#define IPC_IRQ_E2A_MSG          CO_BIT(1)
#define IPC_IRQ_E2A_DBG          CO_BIT(0)

#define IPC_IRQ_E2A_ALL                                                        \
	(IPC_IRQ_E2A_TXCFM | IPC_IRQ_E2A_RXDESC | IPC_IRQ_E2A_MSG_ACK |            \
	 IPC_IRQ_E2A_MSG | IPC_IRQ_E2A_DBG | IPC_IRQ_E2A_TBTT_PRIM |               \
	 IPC_IRQ_E2A_TBTT_SEC | IPC_IRQ_E2A_RADAR | IPC_IRQ_E2A_UNSUP_RX_VEC)

// FLAGS for RX desc
#define IPC_RX_FORWARD  CO_BIT(1)
#define IPC_RX_INTRABSS CO_BIT(0)

// IPC message TYPE
enum {
	IPC_MSG_NONE = 0,
	IPC_MSG_WRAP,
	IPC_MSG_KMSG,

	IPC_DBG_STRING,

};

#endif // _IPC_SHARED_H_
