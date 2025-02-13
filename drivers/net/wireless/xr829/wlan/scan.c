/*
 * Scan implementation for XRadio drivers
 *
 * Copyright (c) 2013
 * Xradio Technology Co., Ltd. <www.xradiotech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include "xradio.h"
#include "scan.h"
#include "sta.h"
#include "pm.h"

static void xradio_scan_restart_delayed(struct xradio_vif *priv);

#ifdef CONFIG_XRADIO_TESTMODE
static int xradio_advance_scan_start(struct xradio_common *hw_priv)
{
	int tmo = 0;
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	tmo += hw_priv->advanceScanElems.duration;
#ifdef CONFIG_PM
	xradio_pm_stay_awake(&hw_priv->pm_state, tmo * HZ / 1000);
#endif
	/* Invoke Advance Scan Duration Timeout Handler */
	queue_delayed_work(hw_priv->workqueue,
		&hw_priv->advance_scan_timeout, tmo * HZ / 1000);
	return 0;
}
#endif

static void xradio_remove_wps_p2p_ie(struct wsm_template_frame *frame)
{
	u8 *ies;
	u32 ies_len;
	u32 ie_len;
	u32 p2p_ie_len = 0;
	u32 wps_ie_len = 0;
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	ies = &frame->skb->data[sizeof(struct ieee80211_hdr_3addr)];
	ies_len = frame->skb->len - sizeof(struct ieee80211_hdr_3addr);
	while (ies_len >= 6) {
		ie_len = ies[1] + 2;
		ies_len -= ie_len;
		if ((ies[0] == WLAN_EID_VENDOR_SPECIFIC) &&
			  (ies[2] == 0x00 && ies[3] == 0x50 &&
			   ies[4] == 0xf2 && ies[5] == 0x04)) {
			wps_ie_len = ie_len;
			memmove(ies, ies + ie_len, ies_len);
		} else if ((ies[0] == WLAN_EID_VENDOR_SPECIFIC) &&
			   (ies[2] == 0x50 && ies[3] == 0x6f &&
			    ies[4] == 0x9a && ies[5] == 0x09)) {
			p2p_ie_len = ie_len;
			memmove(ies, ies + ie_len, ies_len);
		} else {
			ies += ie_len;
		}
	}

	if (p2p_ie_len || wps_ie_len) {
		skb_trim(frame->skb, frame->skb->len - (p2p_ie_len + wps_ie_len));
	}
}

#ifdef CONFIG_XRADIO_TESTMODE
static int xradio_disable_filtering(struct xradio_vif *priv)
{
	int ret = 0;
	bool bssid_filtering = 0;
	struct wsm_rx_filter rx_filter;
	struct wsm_beacon_filter_control bf_control;
	struct xradio_common *hw_priv = xrwl_vifpriv_to_hwpriv(priv);
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	/* RX Filter Disable */
	rx_filter.promiscuous = 0;
	rx_filter.bssid = 0;
	rx_filter.fcs = 0;
	ret = wsm_set_rx_filter(hw_priv, &rx_filter, priv->if_id);

	/* Beacon Filter Disable */
	bf_control.enabled = 0;
	bf_control.bcn_count = 1;
	if (!ret)
		ret = wsm_beacon_filter_control(hw_priv, &bf_control, priv->if_id);

	/* BSSID Filter Disable */
	if (!ret)
		ret = wsm_set_bssid_filtering(hw_priv, bssid_filtering, priv->if_id);

	return ret;
}
#endif

static int xradio_scan_start(struct xradio_vif *priv, struct wsm_scan *scan)
{
	int ret, i;
#ifdef FPGA_SETUP
	int tmo = SCAN_DEFAULT_TIMEOUT;
#else
	int tmo = SCAN_DEFAULT_TIMEOUT;
#endif
	struct xradio_common *hw_priv = xrwl_vifpriv_to_hwpriv(priv);
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	for (i = 0; i < scan->numOfChannels; ++i)
		tmo += scan->ch[i].maxChannelTime + 10;

	atomic_set(&hw_priv->scan.in_progress, 1);
	atomic_set(&hw_priv->recent_scan, 1);
#ifdef CONFIG_PM
	xradio_pm_stay_awake(&hw_priv->pm_state, tmo * HZ / 1000);
#endif
	schedule_delayed_work(&hw_priv->scan.timeout, tmo * HZ / 1000);
#ifdef SCAN_FAILED_WORKAROUND_OF_FW_EXCEPTION
	hw_priv->scan.scan_failed_timestamp = jiffies;
#endif
#ifdef P2P_MULTIVIF
	ret = wsm_scan(hw_priv, scan, priv->if_id ? 1 : 0);
#else
	ret = wsm_scan(hw_priv, scan, priv->if_id);
#endif
	if (unlikely(ret)) {
		scan_printk(XRADIO_DBG_WARN, "%s, wsm_scan failed!\n", __func__);
		atomic_set(&hw_priv->scan.in_progress, 0);
		cancel_delayed_work(&hw_priv->scan.timeout);
		xradio_scan_restart_delayed(priv);
	}
	return ret;
}

#ifdef ROAM_OFFLOAD
static int xradio_sched_scan_start(struct xradio_vif *priv,
					   struct wsm_scan *scan)
{
	int ret;
	struct xradio_common *hw_priv = xrwl_vifpriv_to_hwpriv(priv);
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	ret = wsm_scan(hw_priv, scan, priv->if_id);
	if (unlikely(ret)) {
		atomic_set(&hw_priv->scan.in_progress, 0);
		scan_printk(XRADIO_DBG_WARN, "%s, wsm_scan failed!\n", __func__);
	}
	return ret;
}
#endif /*ROAM_OFFLOAD*/

int xradio_hw_scan(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   struct cfg80211_scan_request *req)
{
	struct xradio_common *hw_priv = hw->priv;
	struct xradio_vif *priv = xrwl_get_vif_from_ieee80211(vif);
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_REQUEST,
	};
	int i;
#ifdef CONFIG_XRADIO_TESTMODE
	int ret = 0;
	u16 advance_scan_req_channel;
#endif
	int suspend_lock_state;
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	/* Scan when P2P_GO corrupt firmware MiniAP mode */
	if (priv->join_status == XRADIO_JOIN_STATUS_AP) {
		scan_printk(XRADIO_DBG_WARN, "%s, can't scan in AP mode!\n",
			    __func__);
		return -EOPNOTSUPP;
	}

#ifdef HW_RESTART
	if (hw_priv->hw_restart) {
		scan_printk(XRADIO_DBG_NIY, "Ignoring scan in hw reset!\n");
		return -EBUSY;
	}
#endif

	if (hw_priv->bh_error) {
		scan_printk(XRADIO_DBG_WARN, "Ignoring scan bh error occur!\n");
		return -EBUSY;
	}

	if (!atomic_read(&priv->enabled)) {
		scan_printk(XRADIO_DBG_WARN, "Ignoring scan vif is not enable!\n");
		return -EBUSY;
	}

	if (work_pending(&priv->offchannel_work) ||
			(hw_priv->roc_if_id != -1)) {
		scan_printk(XRADIO_DBG_WARN, "Offchannel work pending, "
			    "ignoring scan work %d\n",  hw_priv->roc_if_id);
		return -EBUSY;
	}

	if (req->n_ssids == 1 && !req->ssids[0].ssid_len)
		req->n_ssids = 0;

	scan_printk(XRADIO_DBG_NIY, "vif%d Scan request(%s-%dchs) for %d SSIDs.\n",
		priv->if_id, (req->channels[0]->band == NL80211_BAND_2GHZ) ? "2.4G" : "5G",
		req->n_channels, req->n_ssids);

	if (xradio_is_bt_block(hw_priv)) {
		scan_printk(XRADIO_DBG_WARN, "%s:BT is busy, Delay scan!\n", __func__);
		return -EBUSY;
	}

	/*delay multiple ssids scan of vif0 for 3s when connnetting to a node*/
	if (hw_priv->scan_delay_status[0] == XRADIO_SCAN_DELAY &&
	    req->n_ssids == 0 && priv->if_id == 0) {
		unsigned long timedelay = hw_priv->scan_delay_time[0] + SCAN_MAX_DELAY;
		if (time_before(jiffies, timedelay)) {
			scan_printk(XRADIO_DBG_NIY, "vif0 connectting, scan delay %ldms\n",
				(long)(timedelay - jiffies)*1000/HZ);
			return -EBUSY;
		}
		hw_priv->scan_delay_status[0] = XRADIO_SCAN_ALLOW;
	}

	if (req->n_ssids > hw->wiphy->max_scan_ssids) {
		scan_printk(XRADIO_DBG_ERROR, "%s: ssids is too much(%d)\n",
			__func__, req->n_ssids);
		return -EINVAL;
	}

	suspend_lock_state = atomic_cmpxchg(&hw_priv->suspend_lock_state,
								XRADIO_SUSPEND_LOCK_IDEL, XRADIO_SUSPEND_LOCK_OTHERS);
	if (suspend_lock_state == XRADIO_SUSPEND_LOCK_SUSPEND) {
		scan_printk(XRADIO_DBG_WARN,
			   "%s:refuse because of suspend\n", __func__);
		return -EBUSY;
	}

	frame.skb = xr_mac80211_probereq_get(hw, vif, NULL, 0, req->ie, req->ie_len);
	if (!frame.skb) {
		scan_printk(XRADIO_DBG_ERROR, "%s: xr_mac80211_probereq_get failed!\n",
			__func__);
		atomic_set(&hw_priv->suspend_lock_state, XRADIO_SUSPEND_LOCK_IDEL);
		return -ENOMEM;
	}

#ifdef ROAM_OFFLOAD
	if (priv->join_status != XRADIO_JOIN_STATUS_STA) {
		if (req->channels[0]->band == NL80211_BAND_2GHZ)
			hw_priv->num_scanchannels = 0;
		else
			hw_priv->num_scanchannels = hw_priv->num_2g_channels;

		for (i = 0; i < req->n_channels; i++) {
			hw_priv->scan_channels[hw_priv->num_scanchannels + i].number = \
				req->channels[i]->hw_value;
			if (req->channels[i]->flags & IEEE80211_CHAN_PASSIVE_SCAN) {
				hw_priv->scan_channels[hw_priv->num_scanchannels \
							+ i].minChannelTime = 50;
				hw_priv->scan_channels[hw_priv->num_scanchannels \
							+ i].maxChannelTime = 110;
			} else {
				hw_priv->scan_channels[hw_priv->num_scanchannels \
						       + i].minChannelTime = 10;
				hw_priv->scan_channels[hw_priv->num_scanchannels \
						       + i].maxChannelTime = 40;
				hw_priv->scan_channels[hw_priv->num_scanchannels \
						       + i].number |= \
					XRADIO_SCAN_TYPE_ACTIVE;
			}
			hw_priv->scan_channels[hw_priv->num_scanchannels \
					       + i].txPowerLevel = \
					       req->channels[i]->max_power;
			if (req->channels[0]->band == NL80211_BAND_5GHZ)
				hw_priv->scan_channels[hw_priv->num_scanchannels \
						       + i].number |= XRADIO_SCAN_BAND_5G;
		}
		if (req->channels[0]->band == NL80211_BAND_2GHZ)
			hw_priv->num_2g_channels = req->n_channels;
		else
			hw_priv->num_5g_channels = req->n_channels;
	}
	hw_priv->num_scanchannels = hw_priv->num_2g_channels + \
				    hw_priv->num_5g_channels;
#endif /*ROAM_OFFLOAD*/

	/* will be unlocked in xradio_scan_work() */
	down(&hw_priv->scan.lock);
	down(&hw_priv->conf_lock);
	atomic_set(&hw_priv->suspend_lock_state, XRADIO_SUSPEND_LOCK_IDEL);

#ifdef CONFIG_XRADIO_TESTMODE
	/* Active Scan - Serving Channel Request Handling */
	advance_scan_req_channel = req->channels[0]->hw_value;
	if (hw_priv->enable_advance_scan &&
	    (hw_priv->advanceScanElems.scanMode ==
	     XRADIO_SCAN_MEASUREMENT_ACTIVE) &&
	    (priv->join_status == XRADIO_JOIN_STATUS_STA) &&
	    (hw_priv->channel->hw_value == advance_scan_req_channel)) {
		SYS_BUG(hw_priv->scan.req);
		/* wsm_lock_tx(hw_priv); */
		wsm_vif_lock_tx(priv);
		hw_priv->scan.if_id = priv->if_id;
		/* Disable Power Save */
		if (priv->powersave_mode.pmMode & WSM_PSM_PS) {
			struct wsm_set_pm pm = priv->powersave_mode;
			pm.pmMode = WSM_PSM_ACTIVE;
			wsm_set_pm(hw_priv, &pm, priv->if_id);
		}
		/* Disable Rx Beacon and Bssid filter */
		ret = xradio_disable_filtering(priv);
		if (ret)
			scan_printk(XRADIO_DBG_ERROR,
				    "%s: Disable BSSID or Beacon filtering "
				    "failed: %d.\n", __func__, ret);
		wsm_unlock_tx(hw_priv);
		up(&hw_priv->conf_lock);
		/* Transmit Probe Request with Broadcast SSID */
		xradio_tx(hw, frame.skb);
		/* Start Advance Scan Timer */
		xradio_advance_scan_start(hw_priv);
	} else {
#endif /* CONFIG_XRADIO_TESTMODE */

		if (frame.skb) {
			int ret = 0;
			if (priv->if_id == 0)
				xradio_remove_wps_p2p_ie(&frame);
#ifdef P2P_MULTIVIF
			ret = wsm_set_template_frame(hw_priv, &frame, priv->if_id ? 1 : 0);
#else
			ret = wsm_set_template_frame(hw_priv, &frame, priv->if_id);
#endif
			if (ret) {
				up(&hw_priv->conf_lock);
				up(&hw_priv->scan.lock);
				dev_kfree_skb(frame.skb);
				scan_printk(XRADIO_DBG_ERROR,
					    "%s: wsm_set_template_frame failed: %d.\n",
					     __func__, ret);
				return ret;
			}
		}

		wsm_vif_lock_tx(priv);

		SYS_BUG(hw_priv->scan.req);
		hw_priv->scan.req     = req;
		hw_priv->scan.n_ssids = 0;
		hw_priv->scan.status  = 0;
		hw_priv->scan.begin   = &req->channels[0];
		hw_priv->scan.curr    = hw_priv->scan.begin;
		hw_priv->scan.end     = &req->channels[req->n_channels];
		hw_priv->scan.output_power = hw_priv->output_power;
		hw_priv->scan.if_id = priv->if_id;
		/* TODO:COMBO: Populate BIT4 in scanflags to decide on which MAC
		 * address the SCAN request will be sent */

		for (i = 0; i < req->n_ssids; ++i) {
			struct wsm_ssid *dst = &hw_priv->scan.ssids[hw_priv->scan.n_ssids];
			SYS_BUG(req->ssids[i].ssid_len > sizeof(dst->ssid));
			memcpy(&dst->ssid[0], req->ssids[i].ssid, sizeof(dst->ssid));
			dst->length = req->ssids[i].ssid_len;
			++hw_priv->scan.n_ssids;
		}

		up(&hw_priv->conf_lock);

		if (frame.skb)
			dev_kfree_skb(frame.skb);
		queue_work(hw_priv->workqueue, &hw_priv->scan.work);

#ifdef CONFIG_XRADIO_TESTMODE
	}
#endif

	return 0;
}

#ifdef ROAM_OFFLOAD
int xradio_hw_sched_scan_start(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   struct cfg80211_sched_scan_request *req,
		   struct ieee80211_sched_scan_ies *ies)
{
	struct xradio_common *hw_priv = hw->priv;
	struct xradio_vif *priv = xrwl_get_vif_from_ieee80211(vif);
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_REQUEST,
	};
	int i;
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	scan_printk(XRADIO_DBG_WARN, "Scheduled scan request-->.\n");
	if (!priv->vif)
		return -EINVAL;

	/* Scan when P2P_GO corrupt firmware MiniAP mode */
	if (priv->join_status == XRADIO_JOIN_STATUS_AP) {
		scan_printk(XRADIO_DBG_WARN, "%s, can't scan in AP mode!\n", __func__);
		return -EOPNOTSUPP;
	}

#ifdef HW_RESTART
	if (hw_priv->hw_restart) {
		scan_printk(XRADIO_DBG_NIY, "Ignoring scan in hw reset!\n");
		return -EBUSY;
	}
#endif

	if (hw_priv->bh_error) {
		scan_printk(XRADIO_DBG_WARN, "Ignoring scan bh error occur!\n");
		return -EBUSY;
	}

	if (!atomic_read(&priv->enabled)) {
		scan_printk(XRADIO_DBG_WARN, "Ignoring scan vif is not enable!\n");
		return -EBUSY;
	}


	scan_printk(XRADIO_DBG_WARN,
		    "Scheduled scan: n_ssids %d, ssid[0].len = %d\n",
		    req->n_ssids, req->ssids[0].ssid_len);
	if (req->n_ssids == 1 && !req->ssids[0].ssid_len)
		req->n_ssids = 0;

	scan_printk(XRADIO_DBG_NIY, "[SCAN] Scan request for %d SSIDs.\n",
		req->n_ssids);

	if (req->n_ssids > hw->wiphy->max_scan_ssids) {
		scan_printk(XRADIO_DBG_ERROR, "%s: ssids is too much(%d)\n",
			__func__, req->n_ssids);
		return -EINVAL;
	}

	frame.skb = xr_mac80211_probereq_get(hw, priv->vif, NULL, 0,
			ies->ie[0], ies->len[0]);
	if (!frame.skb) {
		scan_printk(XRADIO_DBG_ERROR, "%s: xr_mac80211_probereq_get failed!\n",
			__func__);
		return -ENOMEM;
	}

	/* will be unlocked in xradio_scan_work() */
	down(&hw_priv->scan.lock);
	down(&hw_priv->conf_lock);
	if (frame.skb) {
		int ret;
		if (priv->if_id == 0)
			xradio_remove_wps_p2p_ie(&frame);
		ret = wsm_set_template_frame(hw_priv, &frame, priv->if_id);
		if (0 == ret) {
			/* Host want to be the probe responder. */
			ret = wsm_set_probe_responder(priv, true);
		}
		if (ret) {
			up(&hw_priv->conf_lock);
			up(&hw_priv->scan.lock);
			dev_kfree_skb(frame.skb);
			scan_printk(XRADIO_DBG_ERROR,
				    "%s: wsm_set_probe_responder failed: %d.\n",
					__func__, ret);
			return ret;
		}
	}

	wsm_lock_tx(hw_priv);
	SYS_BUG(hw_priv->scan.req);
	hw_priv->scan.sched_req = req;
	hw_priv->scan.n_ssids = 0;
	hw_priv->scan.status = 0;
	hw_priv->scan.begin = &req->channels[0];
	hw_priv->scan.curr = hw_priv->scan.begin;
	hw_priv->scan.end = &req->channels[req->n_channels];
	hw_priv->scan.output_power = hw_priv->output_power;

	for (i = 0; i < req->n_ssids; ++i) {
		u8 j;
		struct wsm_ssid *dst = &hw_priv->scan.ssids[hw_priv->scan.n_ssids];
		SYS_BUG(req->ssids[i].ssid_len > sizeof(dst->ssid));
		memcpy(&dst->ssid[0], req->ssids[i].ssid, sizeof(dst->ssid));
		dst->length = req->ssids[i].ssid_len;
		++hw_priv->scan.n_ssids;
		scan_printk(XRADIO_DBG_NIY, "SSID %d\n", i);
		for (j = 0; j < req->ssids[i].ssid_len; j++)
			scan_printk(XRADIO_DBG_NIY, "0x%x\n", req->ssids[i].ssid[j]);
	}
	up(&hw_priv->conf_lock);

	if (frame.skb)
		dev_kfree_skb(frame.skb);
	queue_work(hw_priv->workqueue, &hw_priv->scan.swork);
	scan_printk(XRADIO_DBG_NIY, "<-- Scheduled scan request.\n");
	return 0;
}
#endif /*ROAM_OFFLOAD*/

void xradio_scan_work(struct work_struct *work)
{
	struct xradio_common *hw_priv = container_of(work,
						struct xradio_common,
						scan.work);
	struct xradio_vif *priv;
	struct ieee80211_channel **it;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
	struct cfg80211_scan_info info;
#endif
	struct wsm_scan scan = {

#ifndef SUPPORT_HT40

		.scanType = WSM_SCAN_TYPE_FOREGROUND,

#endif

		.scanFlags = 0, /* TODO:COMBO */
		/*.scanFlags = WSM_SCAN_FLAG_SPLIT_METHOD, */
	};
	bool first_run;
	int i;
	const u32 ProbeRequestTime  = 2;
	const u32 ChannelRemainTime = 15;
	u32 maxChannelTime;
#ifdef CONFIG_XRADIO_TESTMODE
	int ret = 0;
	u16 advance_scan_req_channel = hw_priv->scan.begin[0]->hw_value;
#endif
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	priv = __xrwl_hwpriv_to_vifpriv(hw_priv, hw_priv->scan.if_id);

	/*TODO: COMBO: introduce locking so vif is not removed in meanwhile */
	if (!priv) {
		scan_printk(XRADIO_DBG_WARN, "interface removed, "
			"ignoring scan work\n");
		return;
	}

#ifdef SUPPORT_HT40

	SET_SCAN_TYPE(&scan.scanFlags, WSM_SCAN_TYPE_FOREGROUND);

#endif

#ifdef SUPPORT_HT40

	if (priv->if_id)
		SET_SCAN_FLAG(&scan.scanFlags, WSM_FLAG_MAC_INSTANCE_1);
	else
		CLR_SCAN_FLAG(&scan.scanFlags, WSM_FLAG_MAC_INSTANCE_1);

#else

	if (priv->if_id)
		scan.scanFlags |= WSM_FLAG_MAC_INSTANCE_1;
	else
		scan.scanFlags &= ~WSM_FLAG_MAC_INSTANCE_1;

#endif

	/* No need to set WSM_SCAN_FLAG_FORCE_BACKGROUND in BSS_LOSS work.*/
	/*
	xradio_for_each_vif(hw_priv, vif, i) {
		if (!vif)
			continue;
		if (vif->bss_loss_status > XRADIO_BSS_LOSS_NONE)
			scan.scanFlags |= WSM_SCAN_FLAG_FORCE_BACKGROUND;
	} */

	first_run = (hw_priv->scan.begin == hw_priv->scan.curr &&
			hw_priv->scan.begin != hw_priv->scan.end);
	if (first_run) {
		/* Firmware gets crazy if scan request is sent
		 * when STA is joined but not yet associated.
		 * Force unjoin in this case. */
		if (cancel_delayed_work_sync(&priv->join_timeout) > 0)
			xradio_join_timeout(&priv->join_timeout.work);
	}

	down(&hw_priv->conf_lock);
	if (first_run) {

#ifdef CONFIG_XRADIO_TESTMODE
		/* Passive Scan - Serving Channel Request Handling */
		if (hw_priv->enable_advance_scan &&
		   (hw_priv->advanceScanElems.scanMode ==
		    XRADIO_SCAN_MEASUREMENT_PASSIVE) &&
		   (priv->join_status == XRADIO_JOIN_STATUS_STA) &&
		   (hw_priv->channel->hw_value == advance_scan_req_channel)) {
			/* If Advance Scan Request is for Serving Channel Device
			 * should be Active and Filtering Should be Disable */
			if (priv->powersave_mode.pmMode & WSM_PSM_PS) {
				struct wsm_set_pm pm = priv->powersave_mode;
				pm.pmMode = WSM_PSM_ACTIVE;
				wsm_set_pm(hw_priv, &pm, priv->if_id);
			}
			/* Disable Rx Beacon and Bssid filter */
			ret = xradio_disable_filtering(priv);
			if (ret)
				scan_printk(XRADIO_DBG_ERROR, "%s: Disable BSSID or Beacon"
					    "filtering failed: %d.\n", __func__, ret);
		} else if (hw_priv->enable_advance_scan  &&
			   (hw_priv->advanceScanElems.scanMode ==
			    XRADIO_SCAN_MEASUREMENT_PASSIVE) &&
			   (priv->join_status == XRADIO_JOIN_STATUS_STA)) {
			if (priv->join_status == XRADIO_JOIN_STATUS_STA &&
			    !(priv->powersave_mode.pmMode & WSM_PSM_PS)) {
				struct wsm_set_pm pm = priv->powersave_mode;
				pm.pmMode = WSM_PSM_PS;
				xradio_set_pm(priv, &pm);
			}
		} else {
#endif

#if 0
			if (priv->join_status == XRADIO_JOIN_STATUS_STA &&
			    !(priv->powersave_mode.pmMode & WSM_PSM_PS)) {
				struct wsm_set_pm pm = priv->powersave_mode;
				pm.pmMode = WSM_PSM_PS;
				xradio_set_pm(priv, &pm);
			} else
#endif
			if (priv->join_status == XRADIO_JOIN_STATUS_MONITOR) {
				/* FW bug: driver has to restart p2p-dev mode
				 * after scan */
				xradio_disable_listening(priv);
			}
#ifdef CONFIG_XRADIO_TESTMODE
		}
#endif
	}

	if (!hw_priv->scan.req || (hw_priv->scan.curr == hw_priv->scan.end)) {

#ifdef CONFIG_XRADIO_TESTMODE
		if (hw_priv->enable_advance_scan &&
		    (hw_priv->advanceScanElems.scanMode ==
		     XRADIO_SCAN_MEASUREMENT_PASSIVE) &&
		    (priv->join_status == XRADIO_JOIN_STATUS_STA) &&
		    (hw_priv->channel->hw_value == advance_scan_req_channel)) {
			/* WSM Lock should be held here for WSM APIs */
			wsm_vif_lock_tx(priv);

			/* wsm_lock_tx(priv); */
			/* Once Duration is Over, enable filtering
			 * and Revert Back Power Save */
			if (priv->powersave_mode.pmMode & WSM_PSM_PS)
				wsm_set_pm(hw_priv, &priv->powersave_mode, priv->if_id);
			xradio_update_filtering(priv);
		} else if (!hw_priv->enable_advance_scan) {
#endif
			if (hw_priv->scan.output_power != hw_priv->output_power) {
			/* TODO:COMBO: Change when mac80211 implementation
			 * is available for output power also */
#ifdef P2P_MULTIVIF
				WARN_ON(wsm_set_output_power(hw_priv,
							     hw_priv->output_power * 10,
							     priv->if_id ? 1 : 0));
#else
				WARN_ON(wsm_set_output_power(hw_priv,
							     hw_priv->output_power * 10,
							     priv->if_id));
#endif
			}
#ifdef CONFIG_XRADIO_TESTMODE
		}
#endif

#if 0
		if (priv->join_status == XRADIO_JOIN_STATUS_STA &&
		    !(priv->powersave_mode.pmMode & WSM_PSM_PS))
			xradio_set_pm(priv, &priv->powersave_mode);
#endif

		if (hw_priv->scan.status < 0)
			scan_printk(XRADIO_DBG_ERROR, "Scan failed (%d).\n",
				    hw_priv->scan.status);
		else if (hw_priv->scan.req)
			scan_printk(XRADIO_DBG_NIY, "Scan completed.\n");
		else
			scan_printk(XRADIO_DBG_NIY, "Scan canceled.\n");

		hw_priv->scan.req = NULL;
		xradio_scan_restart_delayed(priv);
#ifdef CONFIG_XRADIO_TESTMODE
		hw_priv->enable_advance_scan = false;
#endif /* CONFIG_XRADIO_TESTMODE */
		wsm_unlock_tx(hw_priv);
		up(&hw_priv->conf_lock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
		memset(&info, 0, sizeof(info));
		info.aborted = hw_priv->scan.status ? 1 : 0;
		xr_mac80211_scan_completed(hw_priv->hw, &info);
#else
		xr_mac80211_scan_completed(hw_priv->hw, hw_priv->scan.status ? 1 : 0);
#endif
		up(&hw_priv->scan.lock);
#ifdef SCAN_FAILED_WORKAROUND_OF_FW_EXCEPTION
		/*Timeout waiting for scan complete notification all the time,
		 * then driver restarts.*/
		if (hw_priv->scan.scan_failed_cnt >= 5) {
			scan_printk(XRADIO_DBG_ERROR, "%s:Too many scan timeout=%d",
					__func__, hw_priv->scan.scan_failed_cnt);
			hw_priv->bh_error = 1 ;
			hw_priv->scan.scan_failed_cnt = 0;
		}
#endif
		return;

	} else {
		struct ieee80211_channel *first = *hw_priv->scan.curr;
		for (it = hw_priv->scan.curr + 1, i = 1;
		     it != hw_priv->scan.end && i < WSM_SCAN_MAX_NUM_OF_CHANNELS;
		     ++it, ++i) {
			if ((*it)->band != first->band)
				break;
			if (((*it)->flags ^ first->flags) & (IEEE80211_CHAN_NO_IR |
						IEEE80211_CHAN_RADAR))
				break;
			if (!(first->flags & (IEEE80211_CHAN_NO_IR |
						IEEE80211_CHAN_RADAR)) &&
			    (*it)->max_power != first->max_power)
				break;
		}
		scan.band = first->band;

#ifdef SUPPORT_HT40

		if (hw_priv->scan.req->no_cck)
			scan.TransmitRateEntry =
				((RATE_MODEM_LEGACY  << MODEMTYPE_SHIFT)
				| (RATE_BANDWIDTH_20M << BANDWIDTH_SHIFT)
				| (WSM_TRANSMIT_RATE_6 << 4));
		else
			scan.TransmitRateEntry =
				((RATE_MODEM_LEGACY  << MODEMTYPE_SHIFT)
				| (RATE_BANDWIDTH_20M << BANDWIDTH_SHIFT)
				| (WSM_TRANSMIT_RATE_1 << 4));

#else

		if (hw_priv->scan.req->no_cck)
			scan.maxTransmitRate = WSM_TRANSMIT_RATE_6;
		else
			scan.maxTransmitRate = WSM_TRANSMIT_RATE_1;

#endif

#ifdef CONFIG_XRADIO_TESTMODE
		if (hw_priv->enable_advance_scan) {
			if (hw_priv->advanceScanElems.scanMode ==
				XRADIO_SCAN_MEASUREMENT_PASSIVE)
				scan.numOfProbeRequests = 0;
			else
				scan.numOfProbeRequests = 1;
		} else {
#endif
			/* TODO: Is it optimal? */
			scan.numOfProbeRequests =
			    (first->flags & (IEEE80211_CHAN_NO_IR |
						IEEE80211_CHAN_RADAR)) ? 0 : 4;
#ifdef CONFIG_XRADIO_TESTMODE
		}
#endif /* CONFIG_XRADIO_TESTMODE */

		scan.numOfSSIDs = hw_priv->scan.n_ssids;
		scan.ssids = &hw_priv->scan.ssids[0];
		scan.numOfChannels = it - hw_priv->scan.curr;
		/* TODO: Is it optimal? */
		scan.probeDelay = 200;

		scan_printk(XRADIO_DBG_NIY, "Scan split ch(%d).\n", scan.numOfChannels);
		/* It is not stated in WSM specification, however
		 * FW team says that driver may not use FG scan
		 * when joined. */
		if (priv->join_status == XRADIO_JOIN_STATUS_STA) {

#ifdef SUPPORT_HT40

			SET_SCAN_TYPE(&scan.scanFlags, WSM_SCAN_TYPE_BACKGROUND);
			SET_SCAN_FLAG(&scan.scanFlags, WSM_SCAN_FLAG_FORCE_BACKGROUND);

#else

			scan.scanType = WSM_SCAN_TYPE_BACKGROUND;
			scan.scanFlags = WSM_SCAN_FLAG_FORCE_BACKGROUND;

#endif
		}
		scan.ch = (struct wsm_scan_ch *)xr_kzalloc(
				sizeof(struct wsm_scan_ch) * (it - hw_priv->scan.curr), false);
		if (!scan.ch) {
			hw_priv->scan.status = -ENOMEM;
			scan_printk(XRADIO_DBG_ERROR, "xr_kzalloc wsm_scan_ch failed.\n");
			goto fail;
		}
		maxChannelTime = (scan.numOfSSIDs * scan.numOfProbeRequests *
				  ProbeRequestTime) + ChannelRemainTime;
		maxChannelTime = (maxChannelTime < 100) ? 100 : maxChannelTime;
		for (i = 0; i < scan.numOfChannels; ++i) {
			scan.ch[i].number = hw_priv->scan.curr[i]->hw_value;

#ifdef CONFIG_XRADIO_TESTMODE
			if (hw_priv->enable_advance_scan) {
				scan.ch[i].minChannelTime = hw_priv->advanceScanElems.duration;
				scan.ch[i].maxChannelTime = hw_priv->advanceScanElems.duration;
			} else {
#endif
				if (hw_priv->scan.curr[i]->flags &
					(IEEE80211_CHAN_NO_IR |
						IEEE80211_CHAN_RADAR)) {
					scan.ch[i].minChannelTime = 50;
					scan.ch[i].maxChannelTime = 110;
				} else {
					scan.ch[i].minChannelTime = 50;
					scan.ch[i].maxChannelTime = maxChannelTime;
				}

#ifdef CONFIG_XRADIO_TESTMODE
			}
#endif
		}

#ifdef CONFIG_XRADIO_TESTMODE
		if (!hw_priv->enable_advance_scan) {
#endif
			if (!(first->flags & (IEEE80211_CHAN_NO_IR |
						IEEE80211_CHAN_RADAR)) &&
			    hw_priv->scan.output_power != first->max_power) {
			    hw_priv->scan.output_power = first->max_power;
				/* TODO:COMBO: Change after mac80211 implementation
				* complete */
#ifdef P2P_MULTIVIF
				WARN_ON(wsm_set_output_power(hw_priv,
							     hw_priv->scan.output_power * 10,
							     priv->if_id ? 1 : 0));
#else
				WARN_ON(wsm_set_output_power(hw_priv,
							     hw_priv->scan.output_power * 10,
							     priv->if_id));
#endif
			}
#ifdef CONFIG_XRADIO_TESTMODE
		}
#endif

#ifdef CONFIG_XRADIO_TESTMODE
		if (hw_priv->enable_advance_scan &&
			(hw_priv->advanceScanElems.scanMode ==
				XRADIO_SCAN_MEASUREMENT_PASSIVE) &&
			(priv->join_status == XRADIO_JOIN_STATUS_STA) &&
			(hw_priv->channel->hw_value == advance_scan_req_channel)) {
				/* Start Advance Scan Timer */
				hw_priv->scan.status = xradio_advance_scan_start(hw_priv);
				wsm_unlock_tx(hw_priv);
		} else
#endif
			down(&hw_priv->scan.status_lock);
			hw_priv->scan.status = xradio_scan_start(priv, &scan);

		kfree(scan.ch);
		if (WARN_ON(hw_priv->scan.status)) {
			scan_printk(XRADIO_DBG_ERROR, "scan failed, status=%d.\n",
				    hw_priv->scan.status);
			up(&hw_priv->scan.status_lock);
			goto fail;
		}
		up(&hw_priv->scan.status_lock);
		hw_priv->scan.curr = it;
	}
	up(&hw_priv->conf_lock);
	return;

fail:
	hw_priv->scan.curr = hw_priv->scan.end;
	up(&hw_priv->conf_lock);
	if (queue_work(hw_priv->workqueue, &hw_priv->scan.work) <= 0)
		scan_printk(XRADIO_DBG_ERROR, "%s queue scan work failed\n", __func__);
	return;
}

#ifdef ROAM_OFFLOAD
void xradio_sched_scan_work(struct work_struct *work)
{
	struct xradio_common *hw_priv = container_of(work, struct xradio_common,
		scan.swork);
	struct wsm_scan scan;
	struct wsm_ssid scan_ssid;
	int i;
	struct xradio_vif *priv = NULL;
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	priv = xrwl_hwpriv_to_vifpriv(hw_priv, hw_priv->scan.if_id);
	if (unlikely(!priv)) {
		WARN_ON(1);
		return;
	}

	spin_unlock(&priv->vif_lock);
	/* Firmware gets crazy if scan request is sent
	 * when STA is joined but not yet associated.
	 * Force unjoin in this case. */
	if (cancel_delayed_work_sync(&priv->join_timeout) > 0) {
		xradio_join_timeout(&priv->join_timeout.work);
	}
	down(&hw_priv->conf_lock);
	hw_priv->auto_scanning = 1;
	scan.band = 0;

#ifdef SUPPORT_HT40

	if (priv->join_status == XRADIO_JOIN_STATUS_STA) /* auto background */
		SET_SCAN_TYPE(&scan.scanFlags, WSM_SCAN_TYPE_AUTOBACKGROUND);
	else /* auto foreground */
		SET_SCAN_TYPE(&scan.scanFlags, WSM_SCAN_TYPE_AUTOFOREGROUND);

	/* bit 0 set => forced background scan */
	CLR_SCAN_FLAG(&scan.scanFlags, SCANFLAG_MASK);
	SET_SCAN_FLAG(&scan.scanFlags, WSM_SCAN_FLAG_FORCE_BACKGROUND);

#else

	if (priv->join_status == XRADIO_JOIN_STATUS_STA)
		scan.scanType = 3; /* auto background */
	else
		scan.scanType = 2; /* auto foreground */

	scan.scanFlags = 0x01; /* bit 0 set => forced background scan */

#endif

#ifdef SUPPORT_HT40

	scan.TransmitRateEntry = ((RATE_MODEM_LEGACY  << MODEMTYPE_SHIFT)
				| (RATE_BANDWIDTH_20M << BANDWIDTH_SHIFT)
				| (AG_RATE_INDEX << 4));

#else

	scan.maxTransmitRate = WSM_TRANSMIT_RATE_6;

#endif

	scan.autoScanInterval = (0xba << 24)|(30 * 1024); /* 30 seconds, -70 rssi */
	scan.numOfProbeRequests = 1;
	/*scan.numOfChannels = 11;*/
	scan.numOfChannels = hw_priv->num_scanchannels;
	scan.numOfSSIDs = 1;
	scan.probeDelay = 100;
	scan_ssid.length = priv->ssid_length;
	memcpy(scan_ssid.ssid, priv->ssid, priv->ssid_length);
	scan.ssids = &scan_ssid;

	scan.ch = xr_kzalloc(sizeof(struct wsm_scan_ch[scan.numOfChannels]), false);
	if (!scan.ch) {
		scan_printk(XRADIO_DBG_ERROR, "xr_kzalloc wsm_scan_ch failed.\n");
		hw_priv->scan.status = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < scan.numOfChannels; i++) {
		scan.ch[i].number = hw_priv->scan_channels[i].number;
		scan.ch[i].minChannelTime = hw_priv->scan_channels[i].minChannelTime;
		scan.ch[i].maxChannelTime = hw_priv->scan_channels[i].maxChannelTime;
		scan.ch[i].txPowerLevel = hw_priv->scan_channels[i].txPowerLevel;
	}

#if 0
	for (i = 1; i <= scan.numOfChannels; i++) {
		scan.ch[i-1].number = i;
		scan.ch[i-1].minChannelTime = 10;
		scan.ch[i-1].maxChannelTime = 40;
	}
#endif

	hw_priv->scan.status = xradio_sched_scan_start(priv, &scan);
	kfree(scan.ch);
	if (hw_priv->scan.status) {
		scan_printk(XRADIO_DBG_ERROR, "scan failed, status=%d.\n",
				    hw_priv->scan.status);
		goto fail;
	}
	up(&hw_priv->conf_lock);
	return;

fail:
	up(&hw_priv->conf_lock);
	queue_work(hw_priv->workqueue, &hw_priv->scan.swork);
	return;
}

void xradio_hw_sched_scan_stop(struct xradio_common *hw_priv)
{
	struct xradio_vif *priv = NULL;
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);
	priv = xrwl_hwpriv_to_vifpriv(hw_priv, hw_priv->scan.if_id);
	if (unlikely(!priv))
		return;

	spin_unlock(&priv->vif_lock);
	wsm_stop_scan(hw_priv, priv->if_id);

	return;
}
#endif /*ROAM_OFFLOAD*/


static void xradio_scan_restart_delayed(struct xradio_vif *priv)
{
	struct xradio_common *hw_priv = xrwl_vifpriv_to_hwpriv(priv);
	struct xradio_vif *tmp_vif = NULL;
	int i = 0;
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	if (priv->delayed_link_loss) {
		int tmo = hw_priv->scan.direct_probe ? 0 : priv->cqm_beacon_loss_count;

		priv->delayed_link_loss = 0;
		/* Restart beacon loss timer and requeue
		   BSS loss work. */
		scan_printk(XRADIO_DBG_WARN, "[CQM] Requeue BSS loss in %d "
			"beacons.\n", tmo);
		cancel_delayed_work_sync(&priv->bss_loss_work);
		queue_delayed_work(hw_priv->workqueue, &priv->bss_loss_work,
			tmo * HZ / 10);

	}

	/* FW bug: driver has to restart p2p-dev mode after scan. */
	if (priv->join_status == XRADIO_JOIN_STATUS_MONITOR) {
		/*xradio_enable_listening(priv);*/
		WARN_ON(1);
		xradio_update_filtering(priv);
		scan_printk(XRADIO_DBG_WARN, "driver has to restart "
			"p2p-dev mode after scan");
	}

	for (i = 0; i < 2; i++) {
		tmp_vif = __xrwl_hwpriv_to_vifpriv(hw_priv, i);
		if (tmp_vif == NULL)
			continue;
		if (atomic_xchg(&tmp_vif->delayed_unjoin, 0)) {
			if (queue_work(hw_priv->workqueue, &tmp_vif->unjoin_work) <= 0)
				wsm_unlock_tx(hw_priv);
		}
	}
}

static void xradio_scan_complete(struct xradio_common *hw_priv, int if_id)
{
	struct xradio_vif *priv;
	atomic_xchg(&hw_priv->recent_scan, 0);
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	if (hw_priv->scan.direct_probe) {
		down(&hw_priv->conf_lock);
		priv = __xrwl_hwpriv_to_vifpriv(hw_priv, if_id);
		if (priv) {
			scan_printk(XRADIO_DBG_NIY, "Direct probe complete.\n");
			xradio_scan_restart_delayed(priv);
		} else {
			scan_printk(XRADIO_DBG_WARN,
				    "Direct probe complete without interface!\n");
		}
		up(&hw_priv->conf_lock);
		hw_priv->scan.direct_probe = 0;
		up(&hw_priv->scan.lock);
		wsm_unlock_tx(hw_priv);
	} else {
		xradio_scan_work(&hw_priv->scan.work);
	}
}

void xradio_scan_complete_cb(struct xradio_common *hw_priv,
			struct wsm_scan_complete *arg)
{
	struct xradio_vif *priv = xrwl_hwpriv_to_vifpriv(hw_priv,
					hw_priv->scan.if_id);
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	if (unlikely(!priv))
		return;

#ifdef ROAM_OFFLOAD
	if (hw_priv->auto_scanning)
		schedule_delayed_work(&hw_priv->scan.timeout, 0);
#endif /*ROAM_OFFLOAD*/

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED)) {
		/* STA is stopped. */
		spin_unlock(&priv->vif_lock);
		scan_printk(XRADIO_DBG_WARN, "%s: priv->mode UNSPECIFIED.\n",
			    __func__);
		return;
	}
	spin_unlock(&priv->vif_lock);

	down(&hw_priv->scan.status_lock);
	if (hw_priv->scan.status == -ETIMEDOUT) {
		up(&hw_priv->scan.status_lock);
		scan_printk(XRADIO_DBG_WARN, "Scan timeout already occured. "
			"Don't cancel work");
	} else {
		hw_priv->scan.status = 1;
		up(&hw_priv->scan.status_lock);
		if (cancel_delayed_work_sync(&hw_priv->scan.timeout) > 0)
			schedule_delayed_work(&hw_priv->scan.timeout, 0);
	}

}

void xradio_scan_timeout(struct work_struct *work)
{
	struct xradio_common *hw_priv =
		container_of(work, struct xradio_common, scan.timeout.work);
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	if (likely(atomic_xchg(&hw_priv->scan.in_progress, 0))) {
		down(&hw_priv->scan.status_lock);
		if (hw_priv->scan.status > 0) {
			hw_priv->scan.status = 0;
			up(&hw_priv->scan.status_lock);
#ifdef SCAN_FAILED_WORKAROUND_OF_FW_EXCEPTION
			hw_priv->scan.scan_failed_cnt = 0;
#endif
		} else if (!hw_priv->scan.status) {
			scan_printk(XRADIO_DBG_WARN, "Timeout waiting for scan "
				    "complete notification.\n");
#ifdef SCAN_FAILED_WORKAROUND_OF_FW_EXCEPTION
			if (time_after(jiffies, (hw_priv->scan.scan_failed_timestamp +
						SCAN_DEFAULT_TIMEOUT*HZ/1000))) {
				if (!hw_priv->BT_active)
					hw_priv->scan.scan_failed_cnt += 1;
				scan_printk(XRADIO_DBG_WARN, "%s:scan timeout cnt=%d",
					__func__, hw_priv->scan.scan_failed_cnt);
			}
#endif
			hw_priv->scan.status = -ETIMEDOUT;
			up(&hw_priv->scan.status_lock);
			hw_priv->scan.curr   = hw_priv->scan.end;
			WARN_ON(wsm_stop_scan(hw_priv, hw_priv->scan.if_id ? 1 : 0));
		}
		xradio_scan_complete(hw_priv, hw_priv->scan.if_id);
#ifdef ROAM_OFFLOAD
	} else if (hw_priv->auto_scanning) {
		hw_priv->auto_scanning = 0;
		mac80211_sched_scan_results(hw_priv->hw);
#endif /*ROAM_OFFLOAD*/
	}
}

#ifdef CONFIG_XRADIO_TESTMODE
void xradio_advance_scan_timeout(struct work_struct *work)
{
	struct xradio_common *hw_priv =
		container_of(work, struct xradio_common, advance_scan_timeout.work);

	struct xradio_vif *priv = xrwl_hwpriv_to_vifpriv(hw_priv,
					hw_priv->scan.if_id);
	scan_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	if (WARN_ON(!priv))
		return;
	spin_unlock(&priv->vif_lock);

	hw_priv->scan.status = 0;
	if (hw_priv->advanceScanElems.scanMode ==
	    XRADIO_SCAN_MEASUREMENT_PASSIVE) {
		/* Passive Scan on Serving Channel
		 * Timer Expire */
		xradio_scan_complete(hw_priv, hw_priv->scan.if_id);
	} else {
		/* Active Scan on Serving Channel
		 * Timer Expire */
		down(&hw_priv->conf_lock);
		/*wsm_lock_tx(priv);*/
		wsm_vif_lock_tx(priv);
		/* Once Duration is Over, enable filtering
		 * and Revert Back Power Save */
		if ((priv->powersave_mode.pmMode & WSM_PSM_PS))
			wsm_set_pm(hw_priv, &priv->powersave_mode, priv->if_id);
		hw_priv->scan.req = NULL;
		xradio_update_filtering(priv);
		hw_priv->enable_advance_scan = false;
		wsm_unlock_tx(hw_priv);
		up(&hw_priv->conf_lock);
		xr_mac80211_scan_completed(hw_priv->hw,
				 hw_priv->scan.status ? true : false);
		up(&hw_priv->scan.lock);
	}
}
#endif

void xradio_probe_work(struct work_struct *work)
{
	struct xradio_common *hw_priv =
		container_of(work, struct xradio_common, scan.probe_work.work);
	struct xradio_vif *priv;
	u8 queueId = xradio_queue_get_queue_id(hw_priv->pending_frame_id);
	struct xradio_queue *queue = &hw_priv->tx_queue[queueId];
	const struct xradio_txpriv *txpriv;
	struct wsm_tx *wsm;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_REQUEST,
	};
	struct wsm_ssid ssids[1] = {{
		.length = 0,
	} };
	struct wsm_scan_ch ch[1] = {{
		.minChannelTime = 50,
		.maxChannelTime = 80,
	} };
	struct wsm_scan scan = {

#ifdef SUPPORT_HT40

		.scanFlags = 0,

#else

		.scanType = WSM_SCAN_TYPE_FOREGROUND,

#endif

		.numOfProbeRequests = 1,
		.probeDelay = 0,
		.numOfChannels = 1,
		.ssids = ssids,
		.ch = ch,
	};
	u8 *ies;
	size_t ies_len;
	int ret = 1;
	scan_printk(XRADIO_DBG_NIY, "%s:Direct probe.\n", __func__);

	SYS_BUG(queueId >= 4);
	SYS_BUG(!hw_priv->channel);

	down(&hw_priv->conf_lock);
	if (unlikely(down_trylock(&hw_priv->scan.lock))) {
		/* Scan is already in progress. Requeue self. */
		schedule();
		queue_delayed_work(hw_priv->workqueue, &hw_priv->scan.probe_work,
				   HZ / 10);
		up(&hw_priv->conf_lock);
		return;
	}

	if (xradio_queue_get_skb(queue,	hw_priv->pending_frame_id,
				     &frame.skb, &txpriv)) {
		up(&hw_priv->scan.lock);
		up(&hw_priv->conf_lock);
		wsm_unlock_tx(hw_priv);
		scan_printk(XRADIO_DBG_ERROR, "%s:xradio_queue_get_skb error!\n",
			    __func__);
		return;
	}
	priv = __xrwl_hwpriv_to_vifpriv(hw_priv, txpriv->if_id);
	if (!priv) {
		up(&hw_priv->scan.lock);
		up(&hw_priv->conf_lock);
		scan_printk(XRADIO_DBG_ERROR, "%s:priv error!\n", __func__);
		return;
	}
	wsm = (struct wsm_tx *)frame.skb->data;

#ifdef SUPPORT_HT40

	scan.TransmitRateEntry = wsm->TxRateEntry;

#else

	scan.maxTransmitRate = wsm->maxTxRate;

#endif

	scan.band = (hw_priv->channel->band == NL80211_BAND_5GHZ) ?
		     WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G;

#ifdef SUPPORT_HT40

	SET_SCAN_TYPE(&scan.scanFlags, WSM_SCAN_TYPE_FOREGROUND);
	if (priv->join_status == XRADIO_JOIN_STATUS_STA) {
		SET_SCAN_TYPE(&scan.scanFlags, WSM_SCAN_TYPE_BACKGROUND);
		SET_SCAN_FLAG(&scan.scanFlags, WSM_SCAN_FLAG_FORCE_BACKGROUND);
		if (priv->if_id)
			SET_SCAN_FLAG(&scan.scanFlags, WSM_FLAG_MAC_INSTANCE_1);
		else
			CLR_SCAN_FLAG(&scan.scanFlags, WSM_FLAG_MAC_INSTANCE_1);
	}

#else

	if (priv->join_status == XRADIO_JOIN_STATUS_STA) {
		scan.scanType  = WSM_SCAN_TYPE_BACKGROUND;
		scan.scanFlags = WSM_SCAN_FLAG_FORCE_BACKGROUND;
		if (priv->if_id)
			scan.scanFlags |= WSM_FLAG_MAC_INSTANCE_1;
		else
			scan.scanFlags &= ~WSM_FLAG_MAC_INSTANCE_1;
	}

#endif

	/* No need to set WSM_SCAN_FLAG_FORCE_BACKGROUND in BSS_LOSS work.*/
	 /*
	xradio_for_each_vif(hw_priv, vif, i) {
		if (!vif)
			continue;
		if (vif->bss_loss_status > XRADIO_BSS_LOSS_NONE)
			scan.scanFlags |= WSM_SCAN_FLAG_FORCE_BACKGROUND;
	} */

	ch[0].number = hw_priv->channel->hw_value;
	skb_pull(frame.skb, txpriv->offset);
	ies = &frame.skb->data[sizeof(struct ieee80211_hdr_3addr)];
	ies_len = frame.skb->len - sizeof(struct ieee80211_hdr_3addr);

	if (ies_len) {
		u8 *ssidie = (u8 *)cfg80211_find_ie(WLAN_EID_SSID, ies, ies_len);
		if (ssidie && ssidie[1] && ssidie[1] <= sizeof(ssids[0].ssid)) {
			u8 *nextie = &ssidie[2 + ssidie[1]];
			/* Remove SSID from the IE list. It has to be provided
			 * as a separate argument in xradio_scan_start call */

			/* Store SSID localy */
			ssids[0].length = ssidie[1];
			memcpy(ssids[0].ssid, &ssidie[2], ssids[0].length);
			scan.numOfSSIDs = 1;

			/* Remove SSID from IE list */
			ssidie[1] = 0;
			memmove(&ssidie[2], nextie, &ies[ies_len] - nextie);
			skb_trim(frame.skb, frame.skb->len - ssids[0].length);
		}
	}

	if (priv->if_id == 0)
		xradio_remove_wps_p2p_ie(&frame);

	/* FW bug: driver has to restart p2p-dev mode after scan */
	if (priv->join_status == XRADIO_JOIN_STATUS_MONITOR) {
		WARN_ON(1);
		/*xradio_disable_listening(priv);*/
	}
	ret = WARN_ON(wsm_set_template_frame(hw_priv, &frame,
				priv->if_id));

	hw_priv->scan.direct_probe = 1;
	hw_priv->scan.if_id = priv->if_id;
	if (!ret) {
		wsm_flush_tx(hw_priv);
		ret = WARN_ON(xradio_scan_start(priv, &scan));
	}
	up(&hw_priv->conf_lock);

	skb_push(frame.skb, txpriv->offset);
	if (!ret)
		IEEE80211_SKB_CB(frame.skb)->flags |= IEEE80211_TX_STAT_ACK;
#ifdef CONFIG_XRADIO_TESTMODE
		SYS_BUG(xradio_queue_remove(hw_priv, queue,
					    hw_priv->pending_frame_id));
#else
		SYS_BUG(xradio_queue_remove(queue, hw_priv->pending_frame_id));
#endif

	if (ret) {
		hw_priv->scan.direct_probe = 0;
		up(&hw_priv->scan.lock);
		wsm_unlock_tx(hw_priv);
	}

	return;
}
