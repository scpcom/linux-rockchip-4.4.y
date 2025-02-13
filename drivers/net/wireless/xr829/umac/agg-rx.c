/*
 * HT handling
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007-2010, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * DOC: RX A-MPDU aggregation
 *
 * Aggregation on the RX side requires only implementing the
 * @ampdu_action callback that is invoked to start/stop any
 * block-ack sessions for RX aggregation.
 *
 * When RX aggregation is started by the peer, the driver is
 * notified via @ampdu_action function, with the
 * %IEEE80211_AMPDU_RX_START action, and may reject the request
 * in which case a negative response is sent to the peer, if it
 * accepts it a positive response is sent.
 *
 * While the session is active, the device/driver are required
 * to de-aggregate frames and pass them up one by one to mac80211,
 * which will handle the reorder buffer.
 *
 * When the aggregation session is stopped again by the peer or
 * ourselves, the driver's @ampdu_action function will be called
 * with the action %IEEE80211_AMPDU_RX_STOP. In this case, the
 * call must not fail.
 */

#include <linux/ieee80211.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211_xr.h>
#include "ieee80211_i.h"
#include "driver-ops.h"

static void ieee80211_free_tid_rx(struct rcu_head *h)
{
	struct tid_ampdu_rx *tid_rx =
		container_of(h, struct tid_ampdu_rx, rcu_head);
	int i;

	for (i = 0; i < tid_rx->buf_size; i++)
		dev_kfree_skb(tid_rx->reorder_buf[i]);
	kfree(tid_rx->reorder_buf);
	kfree(tid_rx->reorder_time);
	kfree(tid_rx);
}

void ___mac80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				     u16 initiator, u16 reason, bool tx)
{
	struct ieee80211_local *local = sta->local;
	struct tid_ampdu_rx *tid_rx;

	lockdep_assert_held(&sta->ampdu_mlme.mtx);

	tid_rx = rcu_dereference_protected(sta->ampdu_mlme.tid_rx[tid],
					lockdep_is_held(&sta->ampdu_mlme.mtx));

	if (!tid_rx)
		return;

	RCU_INIT_POINTER(sta->ampdu_mlme.tid_rx[tid], NULL);

#ifdef CONFIG_XRMAC_HT_DEBUG
	printk(KERN_DEBUG "Rx BA session stop requested for %pM tid %u\n",
	       sta->sta.addr, tid);
#endif /* CONFIG_XRMAC_HT_DEBUG */

	if (drv_ampdu_action(local, sta->sdata, IEEE80211_AMPDU_RX_STOP,
			     &sta->sta, tid, NULL, 0))
		printk(KERN_DEBUG "HW problem - can not stop rx "
				"aggregation for tid %d\n", tid);

	/* check if this is a self generated aggregation halt */
	if (initiator == WLAN_BACK_RECIPIENT && tx)
		mac80211_send_delba(sta->sdata, sta->sta.addr,
				     tid, 0, reason);

	del_timer_sync(&tid_rx->session_timer);
	del_timer_sync(&tid_rx->reorder_timer);

	call_rcu(&tid_rx->rcu_head, ieee80211_free_tid_rx);
}

void __mac80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				    u16 initiator, u16 reason, bool tx)
{
	mutex_lock(&sta->ampdu_mlme.mtx);
	___mac80211_stop_rx_ba_session(sta, tid, initiator, reason, tx);
	mutex_unlock(&sta->ampdu_mlme.mtx);
}

void mac80211_stop_rx_ba_session(struct ieee80211_vif *vif, u16 ba_rx_bitmap,
				  const u8 *addr)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct sta_info *sta;
	int i;

	rcu_read_lock();
	sta = xrmac_sta_info_get(sdata, addr);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	for (i = 0; i < STA_TID_NUM; i++)
		if (ba_rx_bitmap & BIT(i))
			set_bit(i, sta->ampdu_mlme.tid_rx_stop_requested);

	xr_mac80211_queue_work(&sta->local->hw, &sta->ampdu_mlme.work);
	rcu_read_unlock();
}

/*
 * After accepting the AddBA Request we activated a timer,
 * resetting it after each frame that arrives from the originator.
 */
static void sta_rx_agg_session_timer_expired(struct timer_list *t)
{
	struct tid_ampdu_rx *tid_rx = from_timer(tid_rx, t, session_timer);
	struct sta_info *sta = tid_rx->sta;
	u8 tid = tid_rx->tid;
	unsigned long timeout;

	timeout = tid_rx->last_rx + TU_TO_JIFFIES(tid_rx->timeout);
	if (time_is_after_jiffies(timeout)) {
		mod_timer(&tid_rx->session_timer, timeout);
		return;
	}

#ifdef CONFIG_XRMAC_HT_DEBUG
	printk(KERN_DEBUG  "RX session timer expired on %pM tid %d\n",
			sta->sta.addr, tid);
#endif

	set_bit(tid, sta->ampdu_mlme.tid_rx_timer_expired);
	xr_mac80211_queue_work(&sta->local->hw, &sta->ampdu_mlme.work);
}

static void sta_rx_agg_reorder_timer_expired(struct timer_list *t)
{
	struct tid_ampdu_rx *tid_rx = from_timer(tid_rx, t, reorder_timer);

	rcu_read_lock();
	mac80211_release_reorder_timeout(tid_rx->sta, tid_rx->tid);
	rcu_read_unlock();
}

static void ieee80211_send_addba_resp(struct ieee80211_sub_if_data *sdata, u8 *da, u16 tid,
				      u8 dialog_token, u16 status, u16 policy,
				      u16 buf_size, u16 timeout)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u16 capab;

	skb = dev_alloc_skb(sizeof(*mgmt) + local->hw.extra_tx_headroom);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	if (sdata->vif.type == NL80211_IFTYPE_AP ||
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		memcpy(mgmt->bssid, sdata->u.mgd.bssid, ETH_ALEN);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	skb_put(skb, 1 + sizeof(mgmt->u.action.u.addba_resp));
	mgmt->u.action.category = WLAN_CATEGORY_BACK;
	mgmt->u.action.u.addba_resp.action_code = WLAN_ACTION_ADDBA_RESP;
	mgmt->u.action.u.addba_resp.dialog_token = dialog_token;

	capab = (u16)(policy << 1);	/* bit 1 aggregation policy */
	capab |= (u16)(tid << 2); 	/* bit 5:2 TID number */
	capab |= (u16)(buf_size << 6);	/* bit 15:6 max size of aggregation */

	mgmt->u.action.u.addba_resp.capab = cpu_to_le16(capab);
	mgmt->u.action.u.addba_resp.timeout = cpu_to_le16(timeout);
	mgmt->u.action.u.addba_resp.status = cpu_to_le16(status);

	ieee80211_tx_skb(sdata, skb);
}

void mac80211_process_addba_request(struct ieee80211_local *local,
				     struct sta_info *sta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	struct tid_ampdu_rx *tid_agg_rx;
	u16 capab, tid, timeout, ba_policy, buf_size, start_seq_num, status;
	u8 dialog_token;
	int ret = -EOPNOTSUPP;

	/* extract session parameters from addba request frame */
	dialog_token = mgmt->u.action.u.addba_req.dialog_token;
	timeout = le16_to_cpu(mgmt->u.action.u.addba_req.timeout);
	start_seq_num =
		le16_to_cpu(mgmt->u.action.u.addba_req.start_seq_num) >> 4;

	capab = le16_to_cpu(mgmt->u.action.u.addba_req.capab);
	ba_policy = (capab & IEEE80211_ADDBA_PARAM_POLICY_MASK) >> 1;
	tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;
	buf_size = (capab & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK) >> 6;

	status = WLAN_STATUS_REQUEST_DECLINED;

	if (test_sta_flag(sta, WLAN_STA_BLOCK_BA)) {
#ifdef CONFIG_XRMAC_HT_DEBUG
		printk(KERN_DEBUG "Suspend in progress. "
		       "Denying ADDBA request\n");
#endif
		goto end_no_lock;
	}

	/* sanity check for incoming parameters:
	 * check if configuration can support the BA policy
	 * and if buffer size does not exceeds max value */
	/* XXX: check own ht delayed BA capability?? */
	if (((ba_policy != 1) &&
	     (!(sta->sta.ht_cap.cap & IEEE80211_HT_CAP_DELAY_BA))) ||
	    (buf_size > IEEE80211_MAX_AMPDU_BUF_HE)) {
		status = WLAN_STATUS_INVALID_QOS_PARAM;
#ifdef CONFIG_XRMAC_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "AddBA Req with bad params from "
				"%pM on tid %u. policy %d, buffer size %d\n",
				mgmt->sa, tid, ba_policy,
				buf_size);
#endif /* CONFIG_XRMAC_HT_DEBUG */
		goto end_no_lock;
	}
	/* determine default buffer size */
	if (buf_size == 0)
		buf_size = IEEE80211_MAX_AMPDU_BUF_HE;

	/* make sure the size doesn't exceed the maximum supported by the hw */
	if (buf_size > local->hw.max_rx_aggregation_subframes)
		buf_size = local->hw.max_rx_aggregation_subframes;

	/* examine state machine */
	mutex_lock(&sta->ampdu_mlme.mtx);

	if (sta->ampdu_mlme.tid_rx[tid]) {
#ifdef CONFIG_XRMAC_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "unexpected AddBA Req from "
				"%pM on tid %u\n",
				mgmt->sa, tid);
#endif /* CONFIG_XRMAC_HT_DEBUG */

		/* delete existing Rx BA session on the same tid */
		___mac80211_stop_rx_ba_session(sta, tid, WLAN_BACK_RECIPIENT,
						WLAN_STATUS_UNSPECIFIED_QOS,
						false);
	}

	/* prepare A-MPDU MLME for Rx aggregation */
	tid_agg_rx = kmalloc(sizeof(struct tid_ampdu_rx), GFP_KERNEL);
	if (!tid_agg_rx)
		goto end;

	spin_lock_init(&tid_agg_rx->reorder_lock);

	/* rx timer */
	timer_setup(&tid_agg_rx->session_timer,
		    sta_rx_agg_session_timer_expired, TIMER_DEFERRABLE);

	/* rx reorder timer */
	timer_setup(&tid_agg_rx->reorder_timer,
		    sta_rx_agg_reorder_timer_expired, 0);

	/* prepare reordering buffer */
	tid_agg_rx->reorder_buf =
		kcalloc(buf_size, sizeof(struct sk_buff *), GFP_KERNEL);
	tid_agg_rx->reorder_time =
		kcalloc(buf_size, sizeof(unsigned long), GFP_KERNEL);
	if (!tid_agg_rx->reorder_buf || !tid_agg_rx->reorder_time) {
		kfree(tid_agg_rx->reorder_buf);
		kfree(tid_agg_rx->reorder_time);
		kfree(tid_agg_rx);
		goto end;
	}

	ret = drv_ampdu_action(local, sta->sdata, IEEE80211_AMPDU_RX_START,
			       &sta->sta, tid, &start_seq_num, 0);
#ifdef CONFIG_XRMAC_HT_DEBUG
	printk(KERN_DEBUG "Rx A-MPDU request on tid %d result %d\n", tid, ret);
#endif /* CONFIG_XRMAC_HT_DEBUG */

	if (ret) {
		kfree(tid_agg_rx->reorder_buf);
		kfree(tid_agg_rx->reorder_time);
		kfree(tid_agg_rx);
		goto end;
	}

	/* update data */
	tid_agg_rx->dialog_token = dialog_token;
	tid_agg_rx->ssn = start_seq_num;
	tid_agg_rx->head_seq_num = start_seq_num;
	tid_agg_rx->buf_size = buf_size;
	tid_agg_rx->timeout = timeout;
	tid_agg_rx->stored_mpdu_num = 0;
	tid_agg_rx->reorder_buf_filtered = 0;
	tid_agg_rx->tid = tid;
	tid_agg_rx->sta = sta;
	status = WLAN_STATUS_SUCCESS;

	/* activate it for RX */
	rcu_assign_pointer(sta->ampdu_mlme.tid_rx[tid], tid_agg_rx);

	if (timeout)
		mod_timer(&tid_agg_rx->session_timer, TU_TO_EXP_TIME(timeout));

end:
	mutex_unlock(&sta->ampdu_mlme.mtx);

end_no_lock:
	ieee80211_send_addba_resp(sta->sdata, sta->sta.addr, tid,
				  dialog_token, status, 1, buf_size, timeout);
}
