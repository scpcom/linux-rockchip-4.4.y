/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2010	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IEEE80211_I_H
#define IEEE80211_I_H
#include <generated/uapi/linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/ieee80211_xr.h>
#include <linux/if_ether.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/etherdevice.h>
#include <linux/leds.h>
#include <linux/idr.h>
#include <linux/rhashtable.h>
#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>
#include <net/mac80211_xr.h>
#include <net/fq.h>

#include "key.h"
#include "sta_info.h"

struct ieee80211_local;

/* Maximum number of broadcast/multicast frames to buffer when some of the
 * associated stations are using power saving. */
#define AP_MAX_BC_BUFFER 128

/* Maximum number of frames buffered to all STAs, including multicast frames.
 * Note: increasing this limit increases the potential memory requirement. Each
 * frame can be up to about 2 kB long. */
#define TOTAL_MAX_TX_BUFFER 512

/* Required encryption head and tailroom */
#define IEEE80211_ENCRYPT_HEADROOM 20
#define IEEE80211_ENCRYPT_TAILROOM 18

/* IEEE 802.11 (Ch. 9.5 Defragmentation) requires support for concurrent
 * reception of at least three fragmented frames. This limit can be increased
 * by changing this define, at the cost of slower frame reassembly and
 * increased memory use (about 2 kB of RAM per entry). */
#define IEEE80211_FRAGMENT_MAX 4


#define IEEE80211_DEFAULT_UAPSD_QUEUES \
	(IEEE80211_WMM_IE_STA_QOSINFO_AC_BK |	\
	 IEEE80211_WMM_IE_STA_QOSINFO_AC_BE |	\
	 IEEE80211_WMM_IE_STA_QOSINFO_AC_VI |	\
	 IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)

#define IEEE80211_DEFAULT_MAX_SP_LEN		\
	IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL

struct ieee80211_fragment_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int rx_queue;
	unsigned int last_frag;
	unsigned int extra_len;
	struct sk_buff_head skb_list;
	int ccmp; /* Whether fragments were encrypted with CCMP */
	u8 last_pn[6]; /* PN of the last fragment if CCMP was used */
};


struct ieee80211_bss {
	/* don't want to look up all the time */
	size_t ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];

	u8 dtim_period;

	bool wmm_used;
	bool uapsd_supported;

	unsigned long last_probe_resp;

#ifdef CONFIG_XRMAC_MESH
	u8 *mesh_id;
	size_t mesh_id_len;
	u8 *mesh_cfg;
#endif

#define IEEE80211_MAX_SUPP_RATES 32
	u8 supp_rates[IEEE80211_MAX_SUPP_RATES];
	size_t supp_rates_len;

	/*
	 * During association, we save an ERP value from a probe response so
	 * that we can feed ERP info to the driver when handling the
	 * association completes. these fields probably won't be up-to-date
	 * otherwise, you probably don't want to use them.
	 */
	bool has_erp_value;
	u8 erp_value;
};

static inline u8 *bss_mesh_cfg(struct ieee80211_bss *bss)
{
#ifdef CONFIG_XRMAC_MESH
	return bss->mesh_cfg;
#endif
	return NULL;
}

static inline u8 *bss_mesh_id(struct ieee80211_bss *bss)
{
#ifdef CONFIG_XRMAC_MESH
	return bss->mesh_id;
#endif
	return NULL;
}

static inline u8 bss_mesh_id_len(struct ieee80211_bss *bss)
{
#ifdef CONFIG_XRMAC_MESH
	return bss->mesh_id_len;
#endif
	return 0;
}


typedef unsigned __bitwise__ ieee80211_tx_result;
#define TX_CONTINUE	((__force ieee80211_tx_result) 0u)
#define TX_DROP		((__force ieee80211_tx_result) 1u)
#define TX_QUEUED	((__force ieee80211_tx_result) 2u)

#define IEEE80211_TX_UNICAST		BIT(1)
#define IEEE80211_TX_PS_BUFFERED	BIT(2)

struct ieee80211_tx_data {
	struct sk_buff *skb;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_key *key;

	struct ieee80211_channel *channel;

	u16 ethertype;
	unsigned int flags;
};


typedef unsigned __bitwise__ ieee80211_rx_result;
#define RX_CONTINUE		((__force ieee80211_rx_result) 0u)
#define RX_DROP_UNUSABLE	((__force ieee80211_rx_result) 1u)
#define RX_DROP_MONITOR		((__force ieee80211_rx_result) 2u)
#define RX_QUEUED		((__force ieee80211_rx_result) 3u)

/**
 * enum ieee80211_packet_rx_flags - packet RX flags
 * @IEEE80211_RX_RA_MATCH: frame is destined to interface currently processed
 *	(incl. multicast frames)
 * @IEEE80211_RX_IN_SCAN: received while scanning
 * @IEEE80211_RX_FRAGMENTED: fragmented frame
 * @IEEE80211_RX_AMSDU: a-MSDU packet
 * @IEEE80211_RX_MALFORMED_ACTION_FRM: action frame is malformed
 * @IEEE80211_RX_DEFERRED_RELEASE: frame was subjected to receive reordering
 *
 * These are per-frame flags that are attached to a frame in the
 * @rx_flags field of &struct ieee80211_rx_status.
 */
enum ieee80211_packet_rx_flags {
	IEEE80211_RX_IN_SCAN			= BIT(0),
	IEEE80211_RX_RA_MATCH			= BIT(1),
	IEEE80211_RX_FRAGMENTED			= BIT(2),
	IEEE80211_RX_AMSDU			= BIT(3),
	IEEE80211_RX_MALFORMED_ACTION_FRM	= BIT(4),
	IEEE80211_RX_DEFERRED_RELEASE		= BIT(5),
	IEEE80211_RX_ERP_BEACON			= BIT(6),
};

/**
 * enum ieee80211_rx_flags - RX data flags
 *
 * @IEEE80211_RX_CMNTR: received on cooked monitor already
 *
 * These flags are used across handling multiple interfaces
 * for a single frame.
 */
enum ieee80211_rx_flags {
	IEEE80211_RX_CMNTR		= BIT(0),
};

struct ieee80211_rx_data {
	struct sk_buff *skb;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_key *key;

	unsigned int flags;

	/*
	 * Index into sequence numbers array, 0..16
	 * since the last (16) is used for non-QoS,
	 * will be 16 on non-QoS frames.
	 */
	int seqno_idx;

	/*
	 * Index into the security IV/PN arrays, 0..16
	 * since the last (16) is used for CCMP-encrypted
	 * management frames, will be set to 16 on mgmt
	 * frames and 0 on non-QoS frames.
	 */
	int security_idx;

	u32 tkip_iv32;
	u16 tkip_iv16;
};

struct beacon_data {
	u8 *head, *tail;
	int head_len, tail_len;
	int dtim_period;
	struct rcu_head rcu_head;
};

#ifdef PROBE_RESP_EXTRA_IE
struct proberesp_data {
	u8 *head, *tail;
	int head_len, tail_len;
	u8 *proberesp_data_ies;
	int proberesp_data_ies_len;
};
#endif

struct ieee80211_if_ap {
	struct beacon_data __rcu *beacon;
#ifdef PROBE_RESP_EXTRA_IE
	struct proberesp_data __rcu *proberesp;
#endif

	struct list_head vlans;

	/* yes, this looks ugly, but guarantees that we can later use
	 * bitmap_empty :)
	 * NB: don't touch this bitmap, use sta_info_{set, clear}_tim_bit */
	u8 tim[sizeof(unsigned long) * BITS_TO_LONGS(IEEE80211_MAX_AID + 1)];
	struct sk_buff_head ps_bc_buf;
	atomic_t num_sta_ps; /* number of stations in PS mode */
	int dtim_count;
	bool dtim_bc_mc;
};

struct ieee80211_if_wds {
	struct sta_info *sta;
	u8 remote_addr[ETH_ALEN];
};

struct ieee80211_if_vlan {
	struct list_head list;

	/* used for all tx if the VLAN is configured to 4-addr mode */
	struct sta_info __rcu *sta;
};

struct mesh_stats {
	__u32 fwded_mcast;		/* Mesh forwarded multicast frames */
	__u32 fwded_unicast;		/* Mesh forwarded unicast frames */
	__u32 fwded_frames;		/* Mesh total forwarded frames */
	__u32 dropped_frames_ttl;	/* Not transmitted since mesh_ttl == 0*/
	__u32 dropped_frames_no_route;	/* Not transmitted, no route found */
	__u32 dropped_frames_congestion;/* Not forwarded due to congestion */
	atomic_t estab_plinks;
};

#define PREQ_Q_F_START		0x1
#define PREQ_Q_F_REFRESH	0x2
struct mesh_preq_queue {
	struct list_head list;
	u8 dst[ETH_ALEN];
	u8 flags;
};

#if HZ/100 == 0
#define IEEE80211_ROC_MIN_LEFT	1
#else
#define IEEE80211_ROC_MIN_LEFT	(HZ/100)
#endif

struct ieee80211_roc_work {
	struct list_head list;
	struct list_head dependents;

	struct delayed_work work;

	struct ieee80211_sub_if_data *sdata;

	struct ieee80211_channel *chan;

	bool started, abort, hw_begun, notified;

	unsigned long hw_start_time;

	u32 duration, req_duration;
	struct sk_buff *frame;
	u64 cookie, mgmt_tx_cookie;
	struct ieee80211_local *local;
	struct net_device *hw_roc_dev;
	enum nl80211_channel_type chan_type;
};
enum ieee80211_work_type {
	IEEE80211_WORK_ABORT,
	IEEE80211_WORK_DIRECT_PROBE,
	IEEE80211_WORK_AUTH,
	IEEE80211_WORK_ASSOC_BEACON_WAIT,
	IEEE80211_WORK_ASSOC,
	IEEE80211_WORK_REMAIN_ON_CHANNEL,
	IEEE80211_WORK_OFFCHANNEL_TX,
};

/**
 * enum work_done_result - indicates what to do after work was done
 *
 * @WORK_DONE_DESTROY: This work item is no longer needed, destroy.
 * @WORK_DONE_REQUEUE: This work item was reset to be reused, and
 *	should be requeued.
 */
enum work_done_result {
	WORK_DONE_DESTROY,
	WORK_DONE_REQUEUE,
};

struct ieee80211_work {
	struct list_head list;

	struct rcu_head rcu_head;

	struct ieee80211_sub_if_data *sdata;

	enum work_done_result (*done)(struct ieee80211_work *wk,
				      struct sk_buff *skb);

	struct ieee80211_channel *chan;
	enum nl80211_channel_type chan_type;

	unsigned long timeout;
	enum ieee80211_work_type type;

	u8 filter_ta[ETH_ALEN];

	bool started;

	union {
		struct {
			int tries;
			u16 algorithm, transaction;
			u8 ssid[IEEE80211_MAX_SSID_LEN];
			u8 ssid_len;
			u8 key[WLAN_KEY_LEN_WEP104];
			u8 key_len, key_idx;
			bool privacy;
			bool synced;
			struct cfg80211_bss *bss;
		} probe_auth;
		struct {
			struct cfg80211_bss *bss;
			const u8 *supp_rates;
			const u8 *ht_information_ie;
			enum ieee80211_smps_mode smps;
			int tries;
			u16 capability;
			u8 prev_bssid[ETH_ALEN];
			u8 ssid[IEEE80211_MAX_SSID_LEN];
			u8 ssid_len;
			u8 supp_rates_len;
			bool wmm_used, use_11n, uapsd_used;
			bool synced;
		} assoc;
		struct {
			u32 duration;
		} remain;
		struct {
			struct sk_buff *frame;
			u32 wait;
			bool status;
		} offchan_tx;
	};

	int ie_len;
	/* must be last */
	u8 ie[0];
};

/* flags used in struct ieee80211_if_managed.flags */
enum ieee80211_sta_flags {
	IEEE80211_STA_BEACON_POLL	= BIT(0),
	IEEE80211_STA_CONNECTION_POLL	= BIT(1),
	IEEE80211_STA_CONTROL_PORT	= BIT(2),
	IEEE80211_STA_DISABLE_11N	= BIT(4),
	IEEE80211_STA_CSA_RECEIVED	= BIT(5),
	IEEE80211_STA_MFP_ENABLED	= BIT(6),
	IEEE80211_STA_UAPSD_ENABLED	= BIT(7),
	IEEE80211_STA_NULLFUNC_ACKED	= BIT(8),
	IEEE80211_STA_RESET_SIGNAL_AVE	= BIT(9),
};

struct ieee80211_if_managed {
	struct timer_list timer;
	struct timer_list conn_mon_timer;
	struct timer_list bcn_mon_timer;
	struct timer_list chswitch_timer;
	struct work_struct monitor_work;
	struct work_struct chswitch_work;
	struct work_struct beacon_connection_loss_work;

	unsigned long beacon_timeout;
	unsigned long probe_timeout;
	int probe_send_count;
	bool nullfunc_failed;

	struct mutex mtx;
	struct cfg80211_bss *associated;

	u8 bssid[ETH_ALEN];

	u16 aid;

	unsigned long timers_running; /* used for quiesce/restart */
	bool powersave; /* powersave requested for this iface */
	bool broken_ap; /* AP is broken -- turn off powersave */
	enum ieee80211_smps_mode req_smps, /* requested smps mode */
				 ap_smps, /* smps mode AP thinks we're in */
				 driver_smps_mode; /* smps mode request */

	struct work_struct request_smps_work;

	unsigned int flags;

	bool beacon_crc_valid;
	u32 beacon_crc;

	enum {
		IEEE80211_MFP_DISABLED,
		IEEE80211_MFP_OPTIONAL,
		IEEE80211_MFP_REQUIRED
	} mfp; /* management frame protection */

	int wmm_last_param_set;

	char	p2p_last_ie[255];
	u8	p2p_last_ie_len;

	u8 use_4addr;

	/* Signal strength from the last Beacon frame in the current BSS. */
	int last_beacon_signal;

	/*
	 * Weighted average of the signal strength from Beacon frames in the
	 * current BSS. This is in units of 1/16 of the signal unit to maintain
	 * accuracy and to speed up calculations, i.e., the value need to be
	 * divided by 16 to get the actual value.
	 */
	int ave_beacon_signal;

	/*
	 * Number of Beacon frames used in ave_beacon_signal. This can be used
	 * to avoid generating less reliable cqm events that would be based
	 * only on couple of received frames.
	 */
	unsigned int count_beacon_signal;

	/*
	 * Last Beacon frame signal strength average (ave_beacon_signal / 16)
	 * that triggered a cqm event. 0 indicates that no event has been
	 * generated for the current association.
	 */
	int last_cqm_event_signal;

	/*
	 * State variables for keeping track of RSSI of the AP currently
	 * connected to and informing driver when RSSI has gone
	 * below/above a certain threshold.
	 */
	int rssi_min_thold, rssi_max_thold;
	int last_ave_beacon_signal;
#ifdef CONFIG_XRMAC_XR_ROAMING_CHANGES
	/*
	 * Set during reassociation to enable faster key removal.
	 */
	int roaming;
#endif
	u8 *assoc_req_ies;
	size_t assoc_req_ies_len;
};

struct ieee80211_if_ibss {
	struct timer_list timer;

	struct mutex mtx;

	unsigned long last_scan_completed;

	u32 basic_rates;

	bool timer_running;

	bool fixed_bssid;
	bool fixed_channel;
	bool privacy;

	u8 bssid[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len, ie_len;
	u8 *ie;
	struct ieee80211_channel *channel;

	unsigned long ibss_join_req;
	/* probe response/beacon for IBSS */
	struct sk_buff __rcu *presp;
	struct sk_buff *skb;

	enum {
		IEEE80211_IBSS_MLME_SEARCH,
		IEEE80211_IBSS_MLME_JOINED,
	} state;
};

struct ieee80211_if_mesh {
	struct timer_list housekeeping_timer;
	struct timer_list xrmac_mesh_path_timer;
	struct timer_list mesh_path_root_timer;

	unsigned long timers_running;

	unsigned long wrkq_flags;

	u8 mesh_id[IEEE80211_MAX_MESH_ID_LEN];
	size_t mesh_id_len;
	/* Active Path Selection Protocol Identifier */
	u8 mesh_pp_id;
	/* Active Path Selection Metric Identifier */
	u8 mesh_pm_id;
	/* Congestion Control Mode Identifier */
	u8 mesh_cc_id;
	/* Synchronization Protocol Identifier */
	u8 mesh_sp_id;
	/* Authentication Protocol Identifier */
	u8 mesh_auth_id;
	/* Local mesh Sequence Number */
	u32 sn;
	/* Last used PREQ ID */
	u32 preq_id;
	atomic_t mpaths;
	/* Timestamp of last SN update */
	unsigned long last_sn_update;
	/* Timestamp of last SN sent */
	unsigned long last_preq;
	struct mesh_rmc *rmc;
	spinlock_t mesh_preq_queue_lock;
	struct mesh_preq_queue preq_queue;
	int preq_queue_len;
	struct mesh_stats mshstats;
	struct mesh_config mshcfg;
	u32 mesh_seqnum;
	bool accepting_plinks;
	int num_gates;
	const u8 *ie;
	u8 ie_len;
	enum {
		IEEE80211_MESH_SEC_NONE = 0x0,
		IEEE80211_MESH_SEC_AUTHED = 0x1,
		IEEE80211_MESH_SEC_SECURED = 0x2,
	} security;
};

#ifdef CONFIG_XRMAC_MESH
#define IEEE80211_IFSTA_MESH_CTR_INC(msh, name)	\
	do { (msh)->mshstats.name++; } while (0)
#else
#define IEEE80211_IFSTA_MESH_CTR_INC(msh, name) \
	do { } while (0)
#endif

struct ieee80211_channel_state {
	struct ieee80211_channel_conf conf;

	/* Operational channel */
	enum nl80211_channel_type _oper_channel_type;
	struct ieee80211_channel *oper_channel;

	/* CSA channel .e.g for STA when AP switches channels */
	struct ieee80211_channel *csa_channel;

	/* Temporary remain-on-channel for off-channel operations */
	struct ieee80211_channel *tmp_channel;
	enum nl80211_channel_type tmp_channel_type;
};

/**
 * enum ieee80211_sub_if_data_flags - virtual interface flags
 *
 * @IEEE80211_SDATA_ALLMULTI: interface wants all multicast packets
 * @IEEE80211_SDATA_PROMISC: interface is promisc
 * @IEEE80211_SDATA_OPERATING_GMODE: operating in G-only mode
 * @IEEE80211_SDATA_DONT_BRIDGE_PACKETS: bridge packets between
 *	associated stations and deliver multicast frames both
 *	back to wireless media and to the local net stack.
 * @IEEE80211_SDATA_DISCONNECT_RESUME: Disconnect after resume.
 */
enum ieee80211_sub_if_data_flags {
	IEEE80211_SDATA_ALLMULTI		= BIT(0),
	IEEE80211_SDATA_PROMISC			= BIT(1),
	IEEE80211_SDATA_OPERATING_GMODE		= BIT(2),
	IEEE80211_SDATA_DONT_BRIDGE_PACKETS	= BIT(3),
	IEEE80211_SDATA_DISCONNECT_RESUME	= BIT(4),
};

/**
 * enum ieee80211_sdata_state_bits - virtual interface state bits
 * @SDATA_STATE_RUNNING: virtual interface is up & running; this
 *	mirrors netif_running() but is separate for interface type
 *	change handling while the interface is up
 * @SDATA_STATE_OFFCHANNEL: This interface is currently in offchannel
 *	mode, so queues are stopped
 */
enum ieee80211_sdata_state_bits {
	SDATA_STATE_RUNNING,
	SDATA_STATE_OFFCHANNEL,
};

/**
 * enum ieee80211_4way_state - the state of 4-way handshake
 * @SDATA_4WAY_STATE_NONE:the sta don't send any 4-way handshake EAPOL-KEY
 * @SDATA_4WAY_STATE_FINISH2:the sta has send second frame(acked) , it means begin
 * 4-way handshake.
 * @SDATA_4WAY_STATE_FINISH4:the sta has send fourth frame(acked), it means
 * 4-way handshake finish.
 *
 */
enum ieee80211_4way_state{
	SDATA_4WAY_STATE_NONE,
	SDATA_4WAY_STATE_FINISH2,
	SDATA_4WAY_STATE_FINISH4,
};

/* IEEE 802.11, 8.5.2 EAPOL-Key frames */
#define IEEE80211_KEY_INFO_TYPE_MASK ((u16) (BIT(0) | BIT(1) | BIT(2)))
#define IEEE80211_KEY_INFO_TYPE_AKM_DEFINED 0
#define IEEE80211_KEY_INFO_TYPE_HMAC_MD5_RC4 BIT(0)
#define IEEE80211_KEY_INFO_TYPE_HMAC_SHA1_AES BIT(1)
#define IEEE80211_KEY_INFO_TYPE_AES_128_CMAC 3
#define IEEE80211_KEY_INFO_KEY_TYPE BIT(3) /* 1 = Pairwise, 0 = Group key */
/* bit4..5 is used in WPA, but is reserved in IEEE 802.11i/RSN */
#define IEEE80211_KEY_INFO_KEY_INDEX_MASK (BIT(4) | BIT(5))
#define IEEE80211_KEY_INFO_KEY_INDEX_SHIFT 4
#define IEEE80211_KEY_INFO_INSTALL BIT(6) /* pairwise */
#define IEEE80211_KEY_INFO_TXRX BIT(6) /* group */
#define IEEE80211_KEY_INFO_ACK BIT(7)
#define IEEE80211_KEY_INFO_MIC BIT(8)
#define IEEE80211_KEY_INFO_SECURE BIT(9)
#define IEEE80211_KEY_INFO_ERROR BIT(10)
#define IEEE80211_KEY_INFO_REQUEST BIT(11)
#define IEEE80211_KEY_INFO_ENCR_KEY_DATA BIT(12) /* IEEE 802.11i/RSN only */
#define IEEE80211_KEY_INFO_SMK_MESSAGE BIT(13)


#define IEEE80211_801X_REPLAY_COUNTER_LEN 8
#define IEEE80211_801X_NONCE_LEN 32
#define IEEE80211_801X_KEY_RSC_LEN 8

struct ieee80211_eapol_key {
	u8 type;
	/* Note: key_info, key_length, and key_data_length are unaligned */
	u8 key_info[2]; /* big endian */
	u8 key_length[2]; /* big endian */
	u8 replay_counter[IEEE80211_801X_REPLAY_COUNTER_LEN];
	u8 key_nonce[IEEE80211_801X_NONCE_LEN];
	u8 key_iv[16];
	u8 key_rsc[IEEE80211_801X_KEY_RSC_LEN];
	u8 key_id[8]; /* Reserved in IEEE 802.11i/RSN */
	u8 key_mic[16];
	u8 key_data_length[2]; /* big endian */
	/* followed by key_data_length bytes of key_data */
} __attribute__ ((packed));

struct ieee802_1x_hdr {
	u8 version;
	u8 type;
	u16 length;
	/* followed by length octets of data */
} __attribute__ ((packed));


struct ieee80211_sub_if_data {
	struct list_head list;

	struct wireless_dev wdev;

	/* keys */
	struct list_head key_list;

	/* count for keys needing tailroom space allocation */
	int crypto_tx_tailroom_needed_cnt;

	struct net_device *dev;
	struct ieee80211_local *local;

	unsigned int flags;

	unsigned long state;

	int drop_unencrypted;
#ifdef CONFIG_XRMAC_XR_ROAMING_CHANGES
	int queues_locked;
#endif

	char name[IFNAMSIZ];

	/*
	 * keep track of whether the HT opmode (stored in
	 * vif.bss_info.ht_operation_mode) is valid.
	 */
	bool ht_opmode_valid;

	/* to detect idle changes */
	bool old_idle;

	bool pspolling;
	bool offchannel_ps_enabled;
	bool ps_allowed;
	struct work_struct dynamic_ps_enable_work;
	struct work_struct dynamic_ps_disable_work;
	struct timer_list dynamic_ps_timer;

	/*
	 * The dynamic ps timeout configured from user space via WEXT -
	 * this will override whatever chosen by mac80211 internally.
	 */
	int dynamic_ps_forced_timeout;
	int dynamic_ps_user_timeout;
	bool disable_dynamic_ps;

	/* Fragment table for host-based reassembly */
	struct ieee80211_fragment_entry	fragments[IEEE80211_FRAGMENT_MAX];
	unsigned int fragment_next;

	struct ieee80211_key __rcu *keys[NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS];
	struct ieee80211_key __rcu *default_unicast_key;
	struct ieee80211_key __rcu *default_multicast_key;
	struct ieee80211_key __rcu *default_mgmt_key;

	u16 sequence_number;
	__be16 control_port_protocol;
	bool control_port_no_encrypt;
	int encrypt_headroom;
	wait_queue_head_t setkey_wq;
	enum ieee80211_4way_state fourway_state;

	struct ieee80211_tx_queue_params tx_conf[IEEE80211_NUM_ACS];

	struct work_struct work;
	struct sk_buff_head skb_queue;

	bool arp_filter_state;

#ifdef IPV6_FILTERING
	bool ndp_filter_state;
#endif /*IPV6_FILTERING*/

	/*
	 * AP this belongs to: self in AP mode and
	 * corresponding AP in VLAN mode, NULL for
	 * all others (might be needed later in IBSS)
	 */
	struct ieee80211_if_ap *bss;

	/* Channel state */
	struct ieee80211_channel_state chan_state;

	/* bitmap of allowed (non-MCS) rate indexes for rate control */
	u32 rc_rateidx_mask[NUM_NL80211_BANDS];

	union {
		struct ieee80211_if_ap ap;
		struct ieee80211_if_wds wds;
		struct ieee80211_if_vlan vlan;
		struct ieee80211_if_managed mgd;
		struct ieee80211_if_ibss ibss;
		struct ieee80211_if_mesh mesh;
		u32 mntr_flags;
	} u;

#ifdef CONFIG_XRMAC_DEBUGFS
	struct {
		struct dentry *dir;
		struct dentry *subdir_stations;
		struct dentry *default_unicast_key;
		struct dentry *default_multicast_key;
		struct dentry *default_mgmt_key;
	} debugfs;
#endif

	/* used for uploading changed mc list */
	struct work_struct reconfig_filter;

	unsigned int filter_flags, req_filt_flags; /* FIF_* */

	/* aggregated multicast list */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	struct netdev_hw_addr_list mc_list;
#else
	struct dev_addr_list *mc_list;
	int mc_count;
#endif

	/* must be last, dynamically sized area in this! */
	struct ieee80211_vif vif;
};

static inline
struct ieee80211_sub_if_data *vif_to_sdata(struct ieee80211_vif *p)
{
	return container_of(p, struct ieee80211_sub_if_data, vif);
}

enum sdata_queue_type {
	IEEE80211_SDATA_QUEUE_TYPE_FRAME	= 0,
	IEEE80211_SDATA_QUEUE_AGG_START		= 1,
	IEEE80211_SDATA_QUEUE_AGG_STOP		= 2,
};

enum {
	IEEE80211_RX_MSG	= 1,
	IEEE80211_TX_STATUS_MSG	= 2,
	IEEE80211_EOSP_MSG	= 3,
};

struct skb_eosp_msg_data {
	u8 sta[ETH_ALEN], iface[ETH_ALEN];
};

enum queue_stop_reason {
	IEEE80211_QUEUE_STOP_REASON_DRIVER,
	IEEE80211_QUEUE_STOP_REASON_PS,
	IEEE80211_QUEUE_STOP_REASON_CSA,
	IEEE80211_QUEUE_STOP_REASON_AGGREGATION,
	IEEE80211_QUEUE_STOP_REASON_SUSPEND,
	IEEE80211_QUEUE_STOP_REASON_SKB_ADD,
	IEEE80211_QUEUE_STOP_REASON_CHTYPE_CHANGE,
};

#ifdef CONFIG_XRMAC_LEDS
struct tpt_led_trigger {
	struct led_trigger trig;
	char name[32];
	const struct ieee80211_tpt_blink *blink_table;
	unsigned int blink_table_len;
	struct timer_list timer;
	unsigned long prev_traffic;
	unsigned long tx_bytes, rx_bytes;
	unsigned int active, want;
	bool running;
};
#endif

/**
 * mac80211 scan flags - currently active scan mode
 *
 * @SCAN_SW_SCANNING: We're currently in the process of scanning but may as
 *	well be on the operating channel
 * @SCAN_HW_SCANNING: The hardware is scanning for us, we have no way to
 *	determine if we are on the operating channel or not
 * @SCAN_OFF_CHANNEL: We're off our operating channel for scanning,
 *	gets only set in conjunction with SCAN_SW_SCANNING
 * @SCAN_COMPLETED: Set for our scan work function when the driver reported
 *	that the scan completed.
 * @SCAN_ABORTED: Set for our scan work function when the driver reported
 *	a scan complete for an aborted scan.
 */
enum {
	SCAN_SW_SCANNING,
	SCAN_HW_SCANNING,
	SCAN_OFF_CHANNEL,
	SCAN_COMPLETED,
	SCAN_ABORTED,
};

/**
 * enum mac80211_scan_state - scan state machine states
 *
 * @SCAN_DECISION: Main entry point to the scan state machine, this state
 *	determines if we should keep on scanning or switch back to the
 *	operating channel
 * @SCAN_SET_CHANNEL: Set the next channel to be scanned
 * @SCAN_SEND_PROBE: Send probe requests and wait for probe responses
 * @SCAN_LEAVE_OPER_CHANNEL: Leave the operating channel, notify the AP
 *	about us leaving the channel and stop all associated STA interfaces
 * @SCAN_ENTER_OPER_CHANNEL: Enter the operating channel again, notify the
 *	AP about us being back and restart all associated STA interfaces
 */
enum mac80211_scan_state {
	SCAN_DECISION,
	SCAN_SET_CHANNEL,
	SCAN_SEND_PROBE,
	SCAN_LEAVE_OPER_CHANNEL,
	SCAN_ENTER_OPER_CHANNEL,
};

struct ieee80211_local {
	/* embed the driver visible part.
	 * don't cast (use the static inlines below), but we keep
	 * it first anyway so they become a no-op */
	struct ieee80211_hw hw;

	const struct ieee80211_ops *ops;

	/*
	 * work stuff, potentially off-channel (in the future)
	 */
	struct list_head work_list;
	struct timer_list work_timer;
	struct work_struct work_work;
	struct sk_buff_head work_skb_queue;

	/*
	 * private workqueue to mac80211. mac80211 makes this accessible
	 * via xr_mac80211_queue_work()
	 */
	struct workqueue_struct *workqueue;

	unsigned long queue_stop_reasons[IEEE80211_MAX_QUEUES];
	/* also used to protect ampdu_ac_queue and amdpu_ac_stop_refcnt */
	spinlock_t queue_stop_reason_lock;

	int open_count;
	int monitors, cooked_mntrs;
	/* number of interfaces with corresponding FIF_ flags */
	int fif_fcsfail, fif_plcpfail, fif_control, fif_other_bss, fif_pspoll,
	    fif_probe_req;
	bool probe_req_reg;
	bool rx_mcast_action_reg;
	unsigned int filter_flags; /* FIF_* */

	bool wiphy_ciphers_allocated;

	/* protects the aggregated multicast list and filter calls */
	spinlock_t filter_lock;

	/* used to reconfigure hardware SM PS */
	struct work_struct recalc_smps;

	bool tim_in_locked_section; /* see ieee80211_beacon_get() */

	/*
	 * suspended is true if we finished all the suspend _and_ we have
	 * not yet come up from resume. This is to be used by mac80211
	 * to ensure driver sanity during suspend and mac80211's own
	 * sanity. It can eventually be used for WoW as well.
	 */
	bool suspended;

	/*
	 * Resuming is true while suspended, but when we're reprogramming the
	 * hardware -- at that time it's allowed to use xr_mac80211_queue_work()
	 * again even though some other parts of the stack are still suspended
	 * and we still drop received frames to avoid waking the stack.
	 */
	bool resuming;

	/*
	 * quiescing is true during the suspend process _only_ to
	 * ease timer cancelling etc.
	 */
	bool quiescing;

	/* device is started */
	bool started;


	/* device is during a HW reconfig */
	bool in_reconfig;

	/* wowlan is enabled -- don't reconfig on resume */
	bool wowlan;

	int tx_headroom; /* required headroom for hardware/radiotap */

	/* Tasklet and skb queue to process calls from IRQ mode. All frames
	 * added to skb_queue will be processed, but frames in
	 * skb_queue_unreliable may be dropped if the total length of these
	 * queues increases over the limit. */
#define IEEE80211_IRQSAFE_QUEUE_LIMIT 128
	struct tasklet_struct tasklet;
	struct sk_buff_head skb_queue;
	struct sk_buff_head skb_queue_unreliable;

	/*
	 * Internal FIFO queue which is shared between multiple rx path
	 * stages. Its main task is to provide a serialization mechanism,
	 * so all rx handlers can enjoy having exclusive access to their
	 * private data structures.
	 */
	struct sk_buff_head rx_skb_queue;
	bool running_rx_handler;	/* protected by rx_skb_queue.lock */

	/* Station data */
	/*
	 * The mutex only protects the list and counter,
	 * reads are done in RCU.
	 * Additionally, the lock protects the hash table,
	 * the pending list and each BSS's TIM bitmap.
	 */
	struct mutex sta_mtx;
	spinlock_t sta_lock;
	unsigned long num_sta;
	struct list_head sta_list, sta_pending_list;
	struct sta_info __rcu *sta_hash[STA_HASH_SIZE];
	struct timer_list sta_cleanup;
	struct work_struct sta_finish_work;
	int sta_generation;

	struct sk_buff_head pending[IEEE80211_MAX_QUEUES];
	struct tasklet_struct tx_pending_tasklet;

	atomic_t agg_queue_stop[IEEE80211_MAX_QUEUES];

	struct rate_control_ref *rate_ctrl;

	struct arc4_ctx wep_tx_ctx;
	struct arc4_ctx wep_rx_ctx;

	u32 wep_iv;

	/* see iface.c */
	struct list_head interfaces;
	struct mutex iflist_mtx;

	/*
	 * Key mutex, protects sdata's key_list and sta_info's
	 * key pointers (write access, they're RCU.)
	 */
	struct mutex key_mtx;

	/* mutex for scan and work locking */
	struct mutex mtx;

	/* Scanning and BSS list */
	unsigned long scanning;
	struct cfg80211_ssid scan_ssid;
	struct cfg80211_scan_request *int_scan_req;
	struct cfg80211_scan_request *scan_req, *hw_scan_req;
	struct ieee80211_channel *scan_channel;
	enum nl80211_band hw_scan_band;
	int scan_channel_idx;
	int scan_ies_len;
	struct cfg80211_scan_info scan_info;

	bool sched_scanning;
	struct ieee80211_sched_scan_ies sched_scan_ies;
	struct work_struct sched_scan_stopped_work;

	unsigned long leave_oper_channel_time;
	enum mac80211_scan_state next_scan_state;
	struct delayed_work scan_work;
	struct ieee80211_sub_if_data *scan_sdata;

	/* Channel state */
	struct ieee80211_channel_state chan_state;

	/* SNMP counters */
	/* dot11CountersTable */
	u32 dot11TransmittedFragmentCount;
	u32 dot11MulticastTransmittedFrameCount;
	u32 dot11FailedCount;
	u32 dot11RetryCount;
	u32 dot11MultipleRetryCount;
	u32 dot11FrameDuplicateCount;
	u32 dot11ReceivedFragmentCount;
	u32 dot11MulticastReceivedFrameCount;
	u32 dot11TransmittedFrameCount;

#ifdef CONFIG_XRMAC_LEDS
	int tx_led_counter, rx_led_counter;
	struct led_trigger *tx_led, *rx_led, *assoc_led, *radio_led;
	struct tpt_led_trigger *tpt_led_trigger;
	char tx_led_name[32], rx_led_name[32],
	     assoc_led_name[32], radio_led_name[32];
#endif

#ifdef CONFIG_XRMAC_DEBUG_COUNTERS
	/* TX/RX handler statistics */
	unsigned int tx_handlers_drop;
	unsigned int tx_handlers_queued;
	unsigned int tx_handlers_drop_unencrypted;
	unsigned int tx_handlers_drop_fragment;
	unsigned int tx_handlers_drop_wep;
	unsigned int tx_handlers_drop_not_assoc;
	unsigned int tx_handlers_drop_unauth_port;
	unsigned int rx_handlers_drop;
	unsigned int rx_handlers_queued;
	unsigned int rx_handlers_drop_nullfunc;
	unsigned int rx_handlers_drop_defrag;
	unsigned int rx_handlers_drop_short;
	unsigned int rx_handlers_drop_passive_scan;
	unsigned int tx_expand_skb_head;
	unsigned int tx_expand_skb_head_cloned;
	unsigned int rx_expand_skb_head;
	unsigned int rx_expand_skb_head2;
	unsigned int rx_handlers_fragments;
	unsigned int tx_status_drop;
#define I802_DEBUG_INC(c) ((c)++)
#else /* CONFIG_XRMAC_DEBUG_COUNTERS */
#define I802_DEBUG_INC(c) do { } while (0)
#endif /* CONFIG_XRMAC_DEBUG_COUNTERS */


	int total_ps_buffered; /* total number of all buffered unicast and
				* multicast packets for power saving stations
				*/
	int wifi_wme_noack_test;
	unsigned int wmm_acm; /* bit field of ACM bits (BIT(802.1D tag)) */
#ifdef USE_RSSI_SMOOTH
	int dis_rssi_smooth;
#endif
#ifdef CONFIG_XRADIO_TESTMODE
	unsigned int wmm_admitted_ups; /* bit field of admitted priorities */
#endif /*CONFIG_XRADIO_TESTMODE*/
	/*
	 * Bitmask of enabled u-apsd queues,
	 * IEEE80211_WMM_IE_STA_QOSINFO_AC_BE & co. Needs a new association
	 * to take effect.
	 */
	unsigned int uapsd_queues;

	/*
	 * Maximum number of buffered frames AP can deliver during a
	 * service period, IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL or similar.
	 * Needs a new association to take effect.
	 */
	unsigned int uapsd_max_sp_len;

	struct notifier_block network_latency_notifier;
	struct notifier_block ifa_notifier;

#ifdef IPV6_FILTERING
	struct notifier_block ifa6_notifier;
#endif /*IPV6_FILTERING*/

	int user_power_level; /* in dBm */
	int power_constr_level; /* in dBm */

	enum ieee80211_smps_mode smps_mode;

	struct work_struct restart_work;

#ifdef IPV6_FILTERING
#ifdef CONFIG_INET
	struct work_struct ifa6_changed_work;
	struct ieee80211_sub_if_data *ifa6_sdata;
#endif
#endif /*IPV6_FILTERING*/

#ifdef CONFIG_XRMAC_DEBUGFS
	struct local_debugfsdentries {
		struct dentry *rcdir;
		struct dentry *keys;
	} debugfs;
#endif

	/*
	 * Remain-on-channel support
	 */
	struct list_head roc_list;
	struct work_struct hw_roc_start, hw_roc_done;
	unsigned long hw_roc_start_time;
	u64 roc_cookie_counter;
	/* Temporary remain-on-channel for off-channel operations */
	struct ieee80211_channel *tmp_channel;
	struct ieee80211_channel *hw_roc_channel;

	u64 roc_cookie;

	struct ieee80211_sub_if_data __rcu *p2p_sdata;
};

static inline struct ieee80211_sub_if_data *
IEEE80211_DEV_TO_SUB_IF(struct net_device *dev)
{
	return netdev_priv(dev);
}

/* this struct represents 802.11n's RA/TID combination */
struct ieee80211_ra_tid {
	u8 ra[ETH_ALEN];
	u16 tid;
};

static inline struct ieee80211_sub_if_data *
IEEE80211_WDEV_TO_SUB_IF(struct wireless_dev *wdev)
{
	return container_of(wdev, struct ieee80211_sub_if_data, wdev);
}

/* Parsed Information Elements */
struct ieee802_11_elems {
	u8 *ie_start;
	size_t total_len;

	/* pointers to IEs */
	u8 *ssid;
	u8 *supp_rates;
	u8 *fh_params;
	u8 *ds_params;
	u8 *cf_params;
	struct ieee80211_tim_ie *tim;
	u8 *ibss_params;
	u8 *challenge;
	u8 *wpa;
	u8 *rsn;
	u8 *erp_info;
	u8 *ext_supp_rates;
	u8 *wmm_info;
	u8 *wmm_param;
	u8 *p2p_ie;
	struct ieee80211_ht_cap *ht_cap_elem;
	struct ieee80211_ht_operation *ht_info_elem;
	struct ieee80211_meshconf_ie *mesh_config;
	u8 *mesh_id;
	u8 *peering;
	u8 *preq;
	u8 *prep;
	u8 *perr;
	struct ieee80211_rann_ie *rann;
	u8 *ch_switch_elem;
	u8 *country_elem;
	u8 *pwr_constr_elem;
	u8 *quiet_elem;	/* first quite element */
	u8 *timeout_int;

	/* length of them, respectively */
	u8 ssid_len;
	u8 supp_rates_len;
	u8 fh_params_len;
	u8 ds_params_len;
	u8 cf_params_len;
	u8 tim_len;
	u8 ibss_params_len;
	u8 challenge_len;
	u8 wpa_len;
	u8 rsn_len;
	u8 erp_info_len;
	u8 ext_supp_rates_len;
	u8 wmm_info_len;
	u8 wmm_param_len;
	u8 p2p_ie_len;
	u8 mesh_id_len;
	u8 peering_len;
	u8 preq_len;
	u8 prep_len;
	u8 perr_len;
	u8 ch_switch_elem_len;
	u8 country_elem_len;
	u8 pwr_constr_elem_len;
	u8 quiet_elem_len;
	u8 num_of_quiet_elem;	/* can be more the one */
	u8 timeout_int_len;

	/* whether a parse error occurred while retrieving these elements */
	bool parse_error;
};

static inline struct ieee80211_local *hw_to_local(
	struct ieee80211_hw *hw)
{
	return container_of(hw, struct ieee80211_local, hw);
}

static inline struct ieee80211_hw *local_to_hw(
	struct ieee80211_local *local)
{
	return &local->hw;
}


static inline int ieee80211_bssid_match(const u8 *raddr, const u8 *addr)
{
	return compare_ether_addr(raddr, addr) == 0 ||
	       is_broadcast_ether_addr(raddr);
}

/**
 * ieee80211_is_8021x - check if frame payload is a 8021x frame
 * @frame: frame
 */
unsigned int __attribute_const__ ieee80211_hdrlen(__le16 fc);

#define ETH_P_PAE 0x888E

static inline int ieee80211_is_8021x(struct ieee80211_hdr *frame)
{
	const int LLC_TYPE_OFF = 6;
	if (ieee80211_is_data(frame->frame_control)) {
		const unsigned int machdrlen = ieee80211_hdrlen(frame->frame_control);
		u8 *llc_data = (u8 *)frame + machdrlen;
		return (bool)(*(u16 *)(llc_data+LLC_TYPE_OFF) == cpu_to_be16(ETH_P_PAE));
	}
	return false;
}

/**
 * ieee80211_is_8021x - check if frame payload is a 8021x eapol key frame
 * @frame: frame
 */
static inline int ieee80211_is_eapol_key(struct ieee80211_hdr *frame)
{
	const int LLC_TYPE_OFF = 6;
	if (ieee80211_is_8021x(frame)) {
		const unsigned int machdrlen = ieee80211_hdrlen(frame->frame_control);
		u8 *eapol_frame = (u8 *)frame + machdrlen + LLC_TYPE_OFF;
		return eapol_frame[3] == 3;
	}
	return false;
}

void ieee80211_notify_channel_change(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata);
int mac80211_hw_config(struct ieee80211_local *local, u32 changed);
void mac80211_tx_set_protected(struct ieee80211_tx_data *tx);
void mac80211_bss_info_change_notify(struct ieee80211_sub_if_data *sdata,
				      u32 changed);
void mac80211_configure_filter(struct ieee80211_sub_if_data *sdata);
u32 mac80211_reset_erp_info(struct ieee80211_sub_if_data *sdata);

/* STA code */
void mac80211_sta_setup_sdata(struct ieee80211_sub_if_data *sdata);
int mac80211_mgd_auth(struct ieee80211_sub_if_data *sdata,
		       struct cfg80211_auth_request *req);
int mac80211_mgd_assoc(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_assoc_request *req);
int mac80211_mgd_deauth(struct ieee80211_sub_if_data *sdata,
			 struct cfg80211_deauth_request *req);
int mac80211_mgd_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_disassoc_request *req);
void mac80211_send_pspoll(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata);
void mac80211_recalc_ps(struct ieee80211_local *local, s32 latency);
int mac80211_max_network_latency(struct notifier_block *nb,
				  unsigned long data, void *dummy);
int ieee80211_set_arp_filter(struct ieee80211_sub_if_data *sdata);
void mac80211_sta_process_chanswitch(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_channel_sw_ie *sw_elem,
				      struct ieee80211_bss *bss,
				      u64 timestamp);
void mac80211_sta_quiesce(struct ieee80211_sub_if_data *sdata);
void mac80211_sta_restart(struct ieee80211_sub_if_data *sdata);
void mac80211_sta_work(struct ieee80211_sub_if_data *sdata);
void mac80211_sta_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb);
int ieee80211_get_local_erp(struct ieee80211_mgmt *mgmt, size_t len);
#define ERP_INFO_BYTE_OFFSET 2
void mac80211_sta_reset_beacon_monitor(struct ieee80211_sub_if_data *sdata);
void mac80211_sta_reset_conn_monitor(struct ieee80211_sub_if_data *sdata);

/* IBSS code */
void mac80211_ibss_notify_scan_completed(struct ieee80211_local *local);
void mac80211_ibss_setup_sdata(struct ieee80211_sub_if_data *sdata);
struct sta_info *ieee80211_ibss_add_sta(struct ieee80211_sub_if_data *sdata,
					u8 *bssid, u8 *addr, u32 supp_rates,
					gfp_t gfp);
int mac80211_ibss_join(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_ibss_params *params);
int mac80211_ibss_leave(struct ieee80211_sub_if_data *sdata);
void mac80211_ibss_quiesce(struct ieee80211_sub_if_data *sdata);
void mac80211_ibss_restart(struct ieee80211_sub_if_data *sdata);
void mac80211_ibss_work(struct ieee80211_sub_if_data *sdata);
void mac80211_ibss_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb);

/* mesh code */
void mac80211_mesh_work(struct ieee80211_sub_if_data *sdata);
void mac80211_mesh_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb);

/* scan/BSS handling */
void mac80211_scan_work(struct work_struct *work);
int ieee80211_request_internal_scan(struct ieee80211_sub_if_data *sdata,
				    const u8 *ssid, u8 ssid_len,
				    struct ieee80211_channel *chan);
int mac80211_request_scan(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_scan_request *req);
void mac80211_scan_cancel(struct ieee80211_local *local);
ieee80211_rx_result
mac80211_scan_rx(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb);

void mac80211_mlme_notify_scan_completed(struct ieee80211_local *local);
struct ieee80211_bss *
mac80211_bss_info_update(struct ieee80211_local *local,
			  struct ieee80211_rx_status *rx_status,
			  struct ieee80211_mgmt *mgmt,
			  size_t len,
			  struct ieee802_11_elems *elems,
			  struct ieee80211_channel *channel,
			  bool beacon);
struct ieee80211_bss *
xr_mac80211_rx_bss_get(struct ieee80211_local *local, u8 *bssid, int freq,
		     u8 *ssid, u8 ssid_len);
void xr_mac80211_rx_bss_put(struct ieee80211_local *local,
			  struct ieee80211_bss *bss);

/* scheduled scan handling */
int mac80211_request_sched_scan_start(struct ieee80211_sub_if_data *sdata,
				       struct cfg80211_sched_scan_request *req);
int mac80211_request_sched_scan_stop(struct ieee80211_sub_if_data *sdata);
void xr_mac80211_sched_scan_stopped_work(struct work_struct *work);

/* off-channel helpers */
void ieee80211_offchannel_stop_beaconing(struct ieee80211_local *local);
void ieee80211_offchannel_stop_station(struct ieee80211_local *local);
void mac80211_offchannel_return(struct ieee80211_local *local,
				 bool enable_beaconing);
void mac80211_hw_roc_setup(struct ieee80211_local *local);
void mac80211_start_next_roc(struct ieee80211_local *local);
void mac80211_roc_purge(struct ieee80211_sub_if_data *sdata);
void mac80211_roc_notify_destroy(struct ieee80211_roc_work *roc);
void mac80211_sw_roc_work(struct work_struct *work);
void mac80211_handle_roc_started(struct ieee80211_roc_work *roc);

/* interface handling */
int mac80211_iface_init(void);
void mac80211_iface_exit(void);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
int mac80211_if_add(struct ieee80211_local *local, const char *name,
		     unsigned char name_assign_type,
		     struct wireless_dev **new_wdev, enum nl80211_iftype type,
		     struct vif_params *params);
#else
int mac80211_if_add(struct ieee80211_local *local, const char *name,
		     struct net_device **new_dev, enum nl80211_iftype type,
		     struct vif_params *params);
#endif

int mac80211_if_change_type(struct ieee80211_sub_if_data *sdata,
			     enum nl80211_iftype type);
void mac80211_if_remove(struct ieee80211_sub_if_data *sdata);
void mac80211_remove_interfaces(struct ieee80211_local *local);
u32 __mac80211_recalc_idle(struct ieee80211_local *local);
void mac80211_recalc_idle(struct ieee80211_local *local);
void mac80211_adjust_monitor_flags(struct ieee80211_sub_if_data *sdata,
				    const int offset);
int ieee80211_do_open(struct wireless_dev *wdev, bool coming_up);
void ieee80211_sdata_stop(struct ieee80211_sub_if_data *sdata);

static inline bool ieee80211_sdata_running(struct ieee80211_sub_if_data *sdata)
{
	return test_bit(SDATA_STATE_RUNNING, &sdata->state);
}

static inline struct ieee80211_channel_state *ieee80211_get_channel_state(
			struct ieee80211_local *local,
			struct ieee80211_sub_if_data *sdata) {
	if (local->hw.flags & IEEE80211_HW_SUPPORTS_MULTI_CHANNEL) {
		BUG_ON(!sdata);
		return &sdata->chan_state;
	} else {
		return &local->chan_state;
	}
}

/* tx handling */
void mac80211_clear_tx_pending(struct ieee80211_local *local);
void mac80211_tx_pending(unsigned long data);
netdev_tx_t mac80211_monitor_start_xmit(struct sk_buff *skb,
					 struct net_device *dev);
netdev_tx_t mac80211_subif_start_xmit(struct sk_buff *skb,
				       struct net_device *dev);

/* HT */
void mac80211_ht_cap_ie_to_sta_ht_cap(struct ieee80211_supported_band *sband,
				const struct ieee80211_ht_cap *ht_cap_ie,
				       struct ieee80211_sta_ht_cap *ht_cap);
void mac80211_send_delba(struct ieee80211_sub_if_data *sdata,
			  const u8 *da, u16 tid,
			  u16 initiator, u16 reason_code);
int mac80211_send_smps_action(struct ieee80211_sub_if_data *sdata,
			       enum ieee80211_smps_mode smps, const u8 *da,
			       const u8 *bssid);
void xr_mac80211_request_smps_work(struct work_struct *work);

void ___mac80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				     u16 initiator, u16 reason, bool stop);
void __mac80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				    u16 initiator, u16 reason, bool stop);
void mac80211_sta_tear_down_BA_sessions(struct sta_info *sta, bool tx);
void mac80211_process_delba(struct ieee80211_sub_if_data *sdata,
			     struct sta_info *sta,
			     struct ieee80211_mgmt *mgmt, size_t len);
void mac80211_process_addba_resp(struct ieee80211_local *local,
				  struct sta_info *sta,
				  struct ieee80211_mgmt *mgmt,
				  size_t len);
void mac80211_process_addba_request(struct ieee80211_local *local,
				     struct sta_info *sta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len);

int __mac80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				   enum ieee80211_back_parties initiator,
				   bool tx);
int ___mac80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				    enum ieee80211_back_parties initiator,
				    bool tx);
void mac80211_start_tx_ba_cb(struct ieee80211_vif *vif, u8 *ra, u16 tid);
void mac80211_stop_tx_ba_cb(struct ieee80211_vif *vif, u8 *ra, u8 tid);
void mac80211_ba_session_work(struct work_struct *work);
void mac80211_tx_ba_session_handle_start(struct sta_info *sta, int tid);
void mac80211_release_reorder_timeout(struct sta_info *sta, int tid);

/* Spectrum management */
void mac80211_process_measurement_req(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_mgmt *mgmt,
				       size_t len);

/* Suspend/resume and hw reconfiguration */
int mac80211_reconfig(struct ieee80211_local *local);
void mac80211_stop_device(struct ieee80211_local *local);

#ifdef CONFIG_PM
int __mac80211_suspend(struct ieee80211_hw *hw,
			struct cfg80211_wowlan *wowlan);

static inline int __ieee80211_resume(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	WARN(test_bit(SCAN_HW_SCANNING, &local->scanning),
		"%s: resume with hardware scan still in progress\n",
		wiphy_name(hw->wiphy));

	return mac80211_reconfig(hw_to_local(hw));
}
#else
static inline int __mac80211_suspend(struct ieee80211_hw *hw,
				      struct cfg80211_wowlan *wowlan)
{
	return 0;
}

static inline int __ieee80211_resume(struct ieee80211_hw *hw)
{
	return 0;
}
#endif

/* utility functions/constants */
extern void *xrmac_wiphy_privid; /* for wiphy privid */
u8 *mac80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
			enum nl80211_iftype type);
int mac80211_frame_duration(enum nl80211_band band, size_t len,
			     int rate, int erp, int short_preamble);
void xrmac_ev_michael_mic_failure(struct ieee80211_sub_if_data *sdata, int keyidx,
				     struct ieee80211_hdr *hdr, const u8 *tsc,
				     gfp_t gfp);
void mac80211_set_wmm_default(struct ieee80211_sub_if_data *sdata);
void mac80211_xmit(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb);
void ieee80211_tx_skb(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb);
void mac802_11_parse_elems(u8 *start, size_t len,
			    struct ieee802_11_elems *elems);
u32 mac802_11_parse_elems_crc(u8 *start, size_t len,
			       struct ieee802_11_elems *elems,
			       u64 filter, u32 crc);
u32 mac80211_mandatory_rates(struct ieee80211_local *local,
			      enum nl80211_band band);

void mac80211_dynamic_ps_enable_work(struct work_struct *work);
void mac80211_dynamic_ps_disable_work(struct work_struct *work);
void mac80211_dynamic_ps_timer(struct timer_list *t);
void mac80211_send_nullfunc(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata,
			     int powersave);
void mac80211_sta_rx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr);
void mac80211_sta_tx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr, bool ack);
void mac80211_beacon_connection_loss_work(struct work_struct *work);

void xr_mac80211_wake_queues_by_reason(struct ieee80211_hw *hw,
				     enum queue_stop_reason reason);
void xr_mac80211_stop_queues_by_reason(struct ieee80211_hw *hw,
				     enum queue_stop_reason reason);
void xr_mac80211_wake_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason);
void xr_mac80211_stop_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason);
void mac80211_propagate_queue_wake(struct ieee80211_local *local, int queue);
void mac80211_add_pending_skb(struct ieee80211_local *local,
			       struct sk_buff *skb);
void mac80211_add_pending_skbs(struct ieee80211_local *local,
				struct sk_buff_head *skbs);
void mac80211_add_pending_skbs_fn(struct ieee80211_local *local,
				   struct sk_buff_head *skbs,
				   void (*fn)(void *data), void *data);

void mac80211_send_auth(struct ieee80211_sub_if_data *sdata,
			 u16 transaction, u16 auth_alg,
			 u8 *extra, size_t extra_len, const u8 *bssid,
			 const u8 *key, u8 key_len, u8 key_idx);
int mac80211_build_preq_ies(struct ieee80211_local *local, u8 *buffer,
			     const u8 *ie, size_t ie_len,
			     enum nl80211_band band, u32 rate_mask,
			     u8 channel);
struct sk_buff *mac80211_build_probe_req(struct ieee80211_sub_if_data *sdata,
					  u8 *dst, u32 ratemask,
					  const u8 *ssid, size_t ssid_len,
					  const u8 *ie, size_t ie_len,
					  bool directed);
void mac80211_send_probe_req(struct ieee80211_sub_if_data *sdata, u8 *dst,
			      const u8 *ssid, size_t ssid_len,
			      const u8 *ie, size_t ie_len,
			      u32 ratemask, bool directed, bool no_cck);

void mac80211_sta_def_wmm_params(struct ieee80211_sub_if_data *sdata,
				  const size_t supp_rates_len,
				  const u8 *supp_rates);
u32 mac80211_sta_get_rates(struct ieee80211_local *local,
			    struct ieee802_11_elems *elems,
			    enum nl80211_band band);
int __mac80211_request_smps(struct ieee80211_sub_if_data *sdata,
			     enum ieee80211_smps_mode smps_mode);
void mac80211_recalc_smps(struct ieee80211_local *local);

size_t mac80211_ie_split(const u8 *ies, size_t ielen,
			  const u8 *ids, int n_ids, size_t offset);
size_t mac80211_ie_split_vendor(const u8 *ies, size_t ielen, size_t offset);

/* internal work items */
void mac80211_work_init(struct ieee80211_local *local);
void mac80211_add_work(struct ieee80211_work *wk);
void xrmac_free_work(struct ieee80211_work *wk);
void mac80211_work_purge(struct ieee80211_sub_if_data *sdata);
ieee80211_rx_result ieee80211_work_rx_mgmt(struct ieee80211_sub_if_data *sdata,
					   struct sk_buff *skb);
int mac80211_wk_remain_on_channel(struct ieee80211_sub_if_data *sdata,
				   struct ieee80211_channel *chan,
				   enum nl80211_channel_type channel_type,
				   unsigned int duration, u64 *cookie);
int mac80211_wk_cancel_remain_on_channel(
	struct ieee80211_sub_if_data *sdata, u64 cookie);

/* channel management */
enum ieee80211_chan_mode {
	CHAN_MODE_UNDEFINED,
	CHAN_MODE_HOPPING,
	CHAN_MODE_FIXED,
};

enum ieee80211_chan_mode
mac80211_get_channel_mode(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *ignore);
bool mac80211_set_channel_type(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata,
				enum nl80211_channel_type chantype);

bool ieee80211_cs_valid(const struct ieee80211_cipher_scheme *cs);
bool ieee80211_cs_list_valid(const struct ieee80211_cipher_scheme *cs, int n);
const struct ieee80211_cipher_scheme *
ieee80211_cs_get(struct ieee80211_local *local, u32 cipher,
		 enum nl80211_iftype iftype);
int ieee80211_cs_headroom(struct ieee80211_local *local,
			  struct cfg80211_crypto_settings *crypto,
			  enum nl80211_iftype iftype);

#ifdef CONFIG_XRMAC_NOINLINE
#define debug_noinline noinline
#else
#define debug_noinline
#endif

static inline void drv_config_iface_filter(struct ieee80211_local *local,
					   struct ieee80211_sub_if_data *sdata,
					   unsigned int filter_flags,
					   unsigned int changed_flags)
{
	might_sleep();

	if (local->ops->config_iface_filter)
		local->ops->config_iface_filter(&local->hw, &sdata->vif,
						filter_flags,
						changed_flags);
}

#endif /* IEEE80211_I_H */
