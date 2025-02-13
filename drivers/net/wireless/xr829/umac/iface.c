/*
 * Interface handling (except master interface)
 *
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <generated/uapi/linux/version.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/mac80211_xr.h>
#include <net/ieee80211_radiotap.h>
#include "ieee80211_i.h"
#include "sta_info.h"
#include "debugfs_netdev.h"
#include "mesh.h"
#include "led.h"
#include "driver-ops.h"
#include "wme.h"
#include "rate.h"

/**
 * DOC: Interface list locking
 *
 * The interface list in each struct ieee80211_local is protected
 * three-fold:
 *
 * (1) modifications may only be done under the RTNL
 * (2) modifications and readers are protected against each other by
 *     the iflist_mtx.
 * (3) modifications are done in an RCU manner so atomic readers
 *     can traverse the list in RCU-safe blocks.
 *
 * As a consequence, reads (traversals) of the list can be protected
 * by either the RTNL, the iflist_mtx or RCU.
 */


static int ieee80211_change_mtu(struct net_device *dev, int new_mtu)
{
	int meshhdrlen;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	meshhdrlen = (sdata->vif.type == NL80211_IFTYPE_MESH_POINT) ? 5 : 0;

	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.3 frames. */
	if (new_mtu < 256 ||
	    new_mtu > IEEE80211_MAX_DATA_LEN - 24 - 6 - meshhdrlen) {
		return -EINVAL;
	}

#ifdef CONFIG_XRMAC_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: setting MTU %d\n", dev->name, new_mtu);
#endif /* CONFIG_XRMAC_VERBOSE_DEBUG */
	dev->mtu = new_mtu;
	return 0;
}

static int ieee80211_change_mac(struct net_device *dev, void *addr)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sockaddr *sa = addr;
	int ret;

	if (ieee80211_sdata_running(sdata))
		return -EBUSY;

	ret = drv_change_mac(local, sdata, sa);

	if (ret == 0)
		ret = eth_mac_addr(dev, sa);

	if (ret == 0)
		memcpy(sdata->vif.addr, sa->sa_data, ETH_ALEN);

	return ret;
}

static inline int identical_mac_addr_allowed(int type1, int type2)
{
	return type1 == NL80211_IFTYPE_MONITOR ||
		type2 == NL80211_IFTYPE_MONITOR ||
		type1 == NL80211_IFTYPE_P2P_DEVICE ||
		type2 == NL80211_IFTYPE_P2P_DEVICE ||
		(type1 == NL80211_IFTYPE_AP && type2 == NL80211_IFTYPE_WDS) ||
		(type1 == NL80211_IFTYPE_WDS &&
			(type2 == NL80211_IFTYPE_WDS ||
			 type2 == NL80211_IFTYPE_AP)) ||
		(type1 == NL80211_IFTYPE_AP && type2 == NL80211_IFTYPE_AP_VLAN) ||
		(type1 == NL80211_IFTYPE_AP_VLAN &&
			(type2 == NL80211_IFTYPE_AP ||
			 type2 == NL80211_IFTYPE_AP_VLAN));
}

static int ieee80211_check_concurrent_iface(struct ieee80211_sub_if_data *sdata,
					    enum nl80211_iftype iftype)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *nsdata;

	ASSERT_RTNL();

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(nsdata, &local->interfaces, list) {

		if (nsdata != sdata && ieee80211_sdata_running(nsdata)) {
			/*
			 * Allow only a single IBSS interface to be up at any
			 * time. This is restricted because beacon distribution
			 * cannot work properly if both are in the same IBSS.
			 *
			 * To remove this restriction we'd have to disallow them
			 * from setting the same SSID on different IBSS interfaces
			 * belonging to the same hardware. Then, however, we're
			 * faced with having to adopt two different TSF timers...
			 */
			if (iftype == NL80211_IFTYPE_ADHOC &&
			    nsdata->vif.type == NL80211_IFTYPE_ADHOC)
				return -EBUSY;

			/*
			 * The remaining checks are only performed for interfaces
			 * with the same MAC address.
			 */
			if (compare_ether_addr(sdata->vif.addr, nsdata->vif.addr))
				continue;

			/*
			 * check whether it may have the same address
			 */
			if (!identical_mac_addr_allowed(iftype,
							nsdata->vif.type))
				return -ENOTUNIQ;

			/*
			 * can only add VLANs to enabled APs
			 */
			if (iftype == NL80211_IFTYPE_AP_VLAN &&
			    nsdata->vif.type == NL80211_IFTYPE_AP)
				sdata->bss = &nsdata->u.ap;
		}
	}

	return 0;
}

static int ieee80211_check_queues(struct ieee80211_sub_if_data *sdata)
{
	int n_queues = sdata->local->hw.queues;
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		if (WARN_ON_ONCE(sdata->vif.hw_queue[i] ==
				 IEEE80211_INVAL_HW_QUEUE))
			return -EINVAL;
		if (WARN_ON_ONCE(sdata->vif.hw_queue[i] >=
				 n_queues))
			return -EINVAL;
	}

	if ((sdata->vif.type != NL80211_IFTYPE_AP) ||
	    !(sdata->local->hw.flags & IEEE80211_HW_QUEUE_CONTROL)) {
		sdata->vif.cab_queue = IEEE80211_INVAL_HW_QUEUE;
		return 0;
	}

	if (WARN_ON_ONCE(sdata->vif.cab_queue == IEEE80211_INVAL_HW_QUEUE))
		return -EINVAL;

	if (WARN_ON_ONCE(sdata->vif.cab_queue >= n_queues))
		return -EINVAL;

	return 0;
}

void mac80211_adjust_monitor_flags(struct ieee80211_sub_if_data *sdata,
				    const int offset)
{
	u32 flags = sdata->u.mntr_flags, req_flags = 0;

#define ADJUST(_f, _s) do {\
	if (flags & MONITOR_FLAG_##_f)\
		req_flags |= _s;\
	} while (0)

	ADJUST(PLCPFAIL, FIF_PLCPFAIL);
	ADJUST(CONTROL, FIF_PSPOLL);
	ADJUST(CONTROL, FIF_CONTROL);
	ADJUST(FCSFAIL, FIF_FCSFAIL);
	ADJUST(OTHER_BSS, FIF_OTHER_BSS);
	if (offset > 0)
		sdata->req_filt_flags |= req_flags;
	else
		sdata->req_filt_flags &= ~req_flags;

#undef ADJUST
}

static void ieee80211_set_default_queues(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		if (local->hw.flags & IEEE80211_HW_QUEUE_CONTROL)
			sdata->vif.hw_queue[i] = IEEE80211_INVAL_HW_QUEUE;
		else
			sdata->vif.hw_queue[i] = i;
	}
	sdata->vif.cab_queue = IEEE80211_INVAL_HW_QUEUE;
}

/*
 * NOTE: Be very careful when changing this function, it must NOT return
 * an error on interface type changes that have been pre-checked, so most
 * checks should be in ieee80211_check_concurrent_iface.
 */
int ieee80211_do_open(struct wireless_dev *wdev, bool coming_up)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct net_device *dev = wdev->netdev;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	u32 changed = 0;
	int res;
	u32 hw_reconf_flags = 0;

	sdata->vif.bss_conf.chan_conf = &sdata->chan_state.conf;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_WDS:
		if (!is_valid_ether_addr(sdata->u.wds.remote_addr))
			return -ENOLINK;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		if (!sdata->bss)
			return -ENOLINK;
		list_add(&sdata->u.vlan.list, &sdata->bss->vlans);
		break;
	case NL80211_IFTYPE_AP:
		sdata->bss = &sdata->u.ap;
		break;
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_P2P_DEVICE:
	case NL80211_IFTYPE_OCB:
	case NL80211_IFTYPE_NAN:
		/* no special treatment */
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
		/* cannot happen */
		WARN_ON(1);
		break;
	}

	if (local->open_count == 0) {
		res = drv_start(local);
		if (res)
			goto err_del_bss;
		/* we're brought up, everything changes */
		hw_reconf_flags = ~0;
		ieee80211_led_radio(local, true);
		ieee80211_mod_tpt_led_trig(local,
					   IEEE80211_TPT_LEDTRIG_FL_RADIO, 0);
	}

	/*
	 * Copy the hopefully now-present MAC address to
	 * this interface, if it has the special null one.
	 */
	if (dev && is_zero_ether_addr(dev->dev_addr)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		dev_addr_mod(dev, 0,
#else
		memcpy(dev->dev_addr,
#endif
		       local->hw.wiphy->perm_addr,
		       ETH_ALEN);
		memcpy(dev->perm_addr, dev->dev_addr, ETH_ALEN);

		if (!is_valid_ether_addr(dev->dev_addr)) {
			if (!local->open_count)
				drv_stop(local);
			return -EADDRNOTAVAIL;
		}
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		/* no need to tell driver */
		break;
	case NL80211_IFTYPE_MONITOR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs++;
			break;
		}

		/* must be before the call to mac80211_configure_filter */
		local->monitors++;
		if (local->monitors == 1) {
			local->hw.conf.flags |= IEEE80211_CONF_MONITOR;
			hw_reconf_flags |= IEEE80211_CONF_CHANGE_MONITOR;
		}

		if (coming_up) {
			res = drv_add_interface(local, &sdata->vif);
			if (res)
				goto err_stop;
		}

		mac80211_adjust_monitor_flags(sdata, 1);
		mac80211_configure_filter(sdata);

		netif_carrier_on(dev);
		break;
	default:
		if (coming_up) {
		printk(KERN_ERR "%s: vif_type=%d, p2p=%d, ch=%d, addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
		   __func__, sdata->vif.type, sdata->vif.p2p,
		   sdata->vif.bss_conf.chan_conf->channel->hw_value, sdata->vif.addr[0],
		   sdata->vif.addr[1], sdata->vif.addr[2], sdata->vif.addr[3], sdata->vif.addr[4], sdata->vif.addr[5]);
			res = drv_add_interface(local, &sdata->vif);
			if (res)
				goto err_stop;
			res = ieee80211_check_queues(sdata);
			if (res)
				goto err_del_interface;
		}

		if (sdata->vif.type == NL80211_IFTYPE_AP) {
			mac80211_configure_filter(sdata);
		}

		if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE)
			changed |= mac80211_reset_erp_info(sdata);
		mac80211_bss_info_change_notify(sdata, changed);

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_MESH_POINT:
		case NL80211_IFTYPE_OCB:
			netif_carrier_off(dev);
			break;
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_P2P_DEVICE:
		case NL80211_IFTYPE_NAN:
			break;
		default:
			netif_carrier_on(dev);
		}
	}

	set_bit(SDATA_STATE_RUNNING, &sdata->state);

	if (sdata->vif.type == NL80211_IFTYPE_WDS) {
		/* Create STA entry for the WDS peer */
		sta = xrmac_sta_info_alloc(sdata, sdata->u.wds.remote_addr,
				     GFP_KERNEL);
		if (!sta) {
			res = -ENOMEM;
			goto err_del_interface;
		}

		/* no atomic bitop required since STA is not live yet */
		set_sta_flag(sta, WLAN_STA_AUTHORIZED);

		res = xrmac_sta_info_insert(sta);
		if (res) {
			/* STA has been freed */
			goto err_del_interface;
		}

		rate_control_rate_init(sta);
	} else if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE) {
		rcu_assign_pointer(local->p2p_sdata, sdata);
	}

	mutex_lock(&local->mtx);
	hw_reconf_flags |= __mac80211_recalc_idle(local);
	mutex_unlock(&local->mtx);

	if (coming_up)
		local->open_count++;

	if (hw_reconf_flags) {
		mac80211_hw_config(local, hw_reconf_flags);
		/*
		 * set default queue parameters so drivers don't
		 * need to initialise the hardware if the hardware
		 * doesn't start up with sane defaults
		 */
		mac80211_set_wmm_default(sdata);
	}

	mac80211_recalc_ps(local, -1);

	if (dev)
		netif_tx_start_all_queues(dev);

	return 0;
 err_del_interface:
	drv_remove_interface(local, &sdata->vif);
 err_stop:
	if (!local->open_count)
		drv_stop(local);
 err_del_bss:
	sdata->bss = NULL;
	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		list_del(&sdata->u.vlan.list);
	clear_bit(SDATA_STATE_RUNNING, &sdata->state);
	return res;
}

static int ieee80211_open(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int err;

	/* fail early if user set an invalid address */
	if (!is_valid_ether_addr(dev->dev_addr))
		return -EADDRNOTAVAIL;

	err = ieee80211_check_concurrent_iface(sdata, sdata->vif.type);
	if (err)
		return err;

	return ieee80211_do_open(&sdata->wdev, true);
}

static void ieee80211_do_stop(struct ieee80211_sub_if_data *sdata,
			      bool going_down)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	unsigned long flags;
	struct sk_buff *skb, *tmp;
	u32 hw_reconf_flags = 0;
	int i;
	enum nl80211_channel_type orig_ct;

	clear_bit(SDATA_STATE_RUNNING, &sdata->state);

	if (local->scan_sdata == sdata)
		mac80211_scan_cancel(local);

	/*
	 * Stop TX on this interface first.
	 */
	if (sdata->dev)
		netif_tx_stop_all_queues(sdata->dev);

	mac80211_roc_purge(sdata);
	/*
	 * Purge work for this interface.
	 */
	mac80211_work_purge(sdata);

	/*
	 * Remove all stations associated with this interface.
	 *
	 * This must be done before calling ops->remove_interface()
	 * because otherwise we can later invoke ops->sta_notify()
	 * whenever the STAs are removed, and that invalidates driver
	 * assumptions about always getting a vif pointer that is valid
	 * (because if we remove a STA after ops->remove_interface()
	 * the driver will have removed the vif info already!)
	 *
	 * This is relevant only in AP, WDS and mesh modes, since in
	 * all other modes we've already removed all stations when
	 * disconnecting etc.
	 */
	xrmac_sta_info_flush(local, sdata);

	if (sdata->dev) {
		netif_addr_lock_bh(sdata->dev);
		spin_lock_bh(&local->filter_lock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
		__hw_addr_unsync(&sdata->mc_list, &sdata->dev->mc,
				 sdata->dev->addr_len);
#else
		__dev_addr_unsync(&sdata->mc_list, &sdata->mc_count,
				  &sdata->dev->mc_list, &sdata->dev->mc_count);
#endif
		spin_unlock_bh(&local->filter_lock);
		netif_addr_unlock_bh(sdata->dev);

		mac80211_configure_filter(sdata);
	}

	del_timer_sync(&sdata->dynamic_ps_timer);
	cancel_work_sync(&sdata->dynamic_ps_enable_work);

	/* APs need special treatment */
	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		struct ieee80211_sub_if_data *vlan, *tmpsdata;
		struct beacon_data *old_beacon =
			rtnl_dereference(sdata->u.ap.beacon);

		/* sdata_running will return false, so this will disable */
		mac80211_bss_info_change_notify(sdata,
						 BSS_CHANGED_BEACON_ENABLED);

		/* remove beacon */
		RCU_INIT_POINTER(sdata->u.ap.beacon, NULL);
		synchronize_rcu();
		kfree(old_beacon);

		/* down all dependent devices, that is VLANs */
		list_for_each_entry_safe(vlan, tmpsdata, &sdata->u.ap.vlans,
					 u.vlan.list)
			dev_close(vlan->dev);
		WARN_ON(!list_empty(&sdata->u.ap.vlans));

		/* free all potentially still buffered bcast frames */
		local->total_ps_buffered -= skb_queue_len(&sdata->u.ap.ps_bc_buf);
		skb_queue_purge(&sdata->u.ap.ps_bc_buf);
	}

	if (going_down)
		local->open_count--;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		list_del(&sdata->u.vlan.list);
		/* no need to tell driver */
		break;
	case NL80211_IFTYPE_MONITOR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs--;
			break;
		}

		local->monitors--;
		if (local->monitors == 0) {
			local->hw.conf.flags &= ~IEEE80211_CONF_MONITOR;
			hw_reconf_flags |= IEEE80211_CONF_CHANGE_MONITOR;
		}

		mac80211_adjust_monitor_flags(sdata, -1);
		mac80211_configure_filter(sdata);

		if (going_down)
			drv_remove_interface(local, &sdata->vif);

		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		/* relies on synchronize_rcu() below */
		rcu_assign_pointer(local->p2p_sdata, NULL);
		/* fall through */
	default:
		flush_work(&sdata->work);
		/*
		 * When we get here, the interface is marked down.
		 * Call synchronize_rcu() to wait for the RX path
		 * should it be using the interface and enqueuing
		 * frames at this very time on another CPU.
		 */
		synchronize_rcu();
		skb_queue_purge(&sdata->skb_queue);

		/*
		 * Disable beaconing here for mesh only, AP and IBSS
		 * are already taken care of.
		 */
		if (sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
			mac80211_bss_info_change_notify(sdata,
				BSS_CHANGED_BEACON_ENABLED);

		/*
		 * Free all remaining keys, there shouldn't be any,
		 * except maybe group keys in AP more or WDS?
		 */
		mac80211_free_keys(sdata);

		if (going_down)
			drv_remove_interface(local, &sdata->vif);
	}

	sdata->bss = NULL;

	mutex_lock(&local->mtx);
	hw_reconf_flags |= __mac80211_recalc_idle(local);
	mutex_unlock(&local->mtx);

	mac80211_recalc_ps(local, -1);

	if (local->open_count == 0) {
		mac80211_clear_tx_pending(local);
		mac80211_stop_device(local);

		/* no reconfiguring after stop! */
		hw_reconf_flags = 0;
	}

	/* Re-calculate channel-type, in case there are multiple vifs
	 * on different channel types.
	 */
	orig_ct = chan_state->_oper_channel_type;
	if (local->hw.flags & IEEE80211_HW_SUPPORTS_MULTI_CHANNEL)
		mac80211_set_channel_type(local, sdata, NL80211_CHAN_NO_HT);
	else
		mac80211_set_channel_type(local, NULL, NL80211_CHAN_NO_HT);

	/* do after stop to avoid reconfiguring when we stop anyway */
	if (hw_reconf_flags || (orig_ct != chan_state->_oper_channel_type))
		mac80211_hw_config(local, hw_reconf_flags);

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	for (i = 0; i < IEEE80211_MAX_QUEUES; i++) {
		skb_queue_walk_safe(&local->pending[i], skb, tmp) {
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
			if (info->control.vif == &sdata->vif) {
				__skb_unlink(skb, &local->pending[i]);
				dev_kfree_skb_irq(skb);
			}
		}
	}
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

static int ieee80211_stop(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_do_stop(sdata, true);

	return 0;
}

static void ieee80211_set_multicast_list(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	int allmulti, promisc, sdata_allmulti, sdata_promisc;

#if 0
	if (!(SDATA_STATE_RUNNING & sdata->state))
		return;
#endif

	allmulti = !!(dev->flags & IFF_ALLMULTI);
	promisc = !!(dev->flags & IFF_PROMISC);
	sdata_allmulti = !!(sdata->flags & IEEE80211_SDATA_ALLMULTI);
	sdata_promisc = !!(sdata->flags & IEEE80211_SDATA_PROMISC);

	if (allmulti != sdata_allmulti)
		sdata->flags ^= IEEE80211_SDATA_ALLMULTI;

	if (promisc != sdata_promisc)
		sdata->flags ^= IEEE80211_SDATA_PROMISC;

	spin_lock_bh(&local->filter_lock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	__hw_addr_sync(&sdata->mc_list, &dev->mc, dev->addr_len);
#else
	__dev_addr_sync(&sdata->mc_list, &sdata->mc_count,
			&dev->mc_list, &dev->mc_count);
#endif
	spin_unlock_bh(&local->filter_lock);
	xr_mac80211_queue_work(&local->hw, &sdata->reconfig_filter);
}

/*
 * Called when the netdev is removed or, by the code below, before
 * the interface type changes.
 */
static void ieee80211_teardown_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	int flushed;
	int i;

	/* free extra data */
	mac80211_free_keys(sdata);

#ifdef CONFIG_XRMAC_DEBUGFS
	mac80211_debugfs_remove_netdev(sdata);
#endif

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		__skb_queue_purge(&sdata->fragments[i].skb_list);
	sdata->fragment_next = 0;

	if (ieee80211_vif_is_mesh(&sdata->vif))
		xrmac_mesh_rmc_free(sdata);

	flushed = xrmac_sta_info_flush(local, sdata);
	WARN_ON(flushed);
}

static void ieee80211_uninit(struct net_device *dev)
{
	ieee80211_teardown_sdata(IEEE80211_DEV_TO_SUB_IF(dev));
}

static u16 ieee80211_netdev_select_queue(struct net_device *dev,
					 struct sk_buff *skb,
					 struct net_device *sb_dev)
{
	return mac80211_select_queue(IEEE80211_DEV_TO_SUB_IF(dev), skb);
}

static const struct net_device_ops ieee80211_dataif_ops = {
	.ndo_open		= ieee80211_open,
	.ndo_stop		= ieee80211_stop,
	.ndo_uninit		= ieee80211_uninit,
	.ndo_start_xmit		= mac80211_subif_start_xmit,
	.ndo_set_rx_mode	= ieee80211_set_multicast_list,
	.ndo_change_mtu 	= ieee80211_change_mtu,
	.ndo_set_mac_address 	= ieee80211_change_mac,
	.ndo_select_queue	= ieee80211_netdev_select_queue,
};

static u16 ieee80211_monitor_select_queue(struct net_device *dev,
					  struct sk_buff *skb,
					  struct net_device *sb_dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hdr *hdr;
	struct ieee80211_radiotap_header *rtap = (void *)skb->data;
	u8 *p;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return 0;

	if (skb->len < 4 ||
	    skb->len < le16_to_cpu(rtap->it_len) + 2 /* frame control */)
		return 0; /* doesn't matter, frame will be dropped */

	hdr = (void *)((u8 *)skb->data + le16_to_cpu(rtap->it_len));

	if (!ieee80211_is_data(hdr->frame_control)) {
		skb->priority = 7;
		return mac802_1d_to_ac[skb->priority];
	}
	if (!ieee80211_is_data_qos(hdr->frame_control)) {
		skb->priority = 0;
		return mac802_1d_to_ac[skb->priority];
	}

	p = ieee80211_get_qos_ctl(hdr);
	skb->priority = *p & IEEE80211_QOS_CTL_TAG1D_MASK;

	return mac80211_downgrade_queue(local, skb);
}

static const struct net_device_ops ieee80211_monitorif_ops = {
	.ndo_open		= ieee80211_open,
	.ndo_stop		= ieee80211_stop,
	.ndo_uninit		= ieee80211_uninit,
	.ndo_start_xmit		= mac80211_monitor_start_xmit,
	.ndo_set_rx_mode	= ieee80211_set_multicast_list,
	.ndo_change_mtu 	= ieee80211_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_select_queue	= ieee80211_monitor_select_queue,
};

static void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->netdev_ops = &ieee80211_dataif_ops;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29))
	/* Do we need this ? */
	/* we will validate the address ourselves in ->open */
	dev->validate_addr = NULL;
#endif
	dev->priv_destructor = free_netdev;
}
static void ieee80211_iface_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, work);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct sta_info *sta;
	struct ieee80211_ra_tid *ra_tid;
	struct cfg80211_bss *bss;

	if (!ieee80211_sdata_running(sdata))
		return;

	if (local->scanning)
		return;

	/*
	 * xr_mac80211_queue_work() should have picked up most cases,
	 * here we'll pick the rest.
	 */
	if (WARN(local->suspended,
		 "interface work scheduled while going to suspend\n"))
		return;

	/* first process frames */
	while ((skb = skb_dequeue(&sdata->skb_queue))) {
		struct ieee80211_mgmt *mgmt = (void *)skb->data;

		if (skb->pkt_type == IEEE80211_SDATA_QUEUE_AGG_START) {
			ra_tid = (void *)&skb->cb;
			mac80211_start_tx_ba_cb(&sdata->vif, ra_tid->ra,
						 ra_tid->tid);
		} else if (skb->pkt_type == IEEE80211_SDATA_QUEUE_AGG_STOP) {
			ra_tid = (void *)&skb->cb;
			mac80211_stop_tx_ba_cb(&sdata->vif, ra_tid->ra,
						ra_tid->tid);
		} else if (ieee80211_is_action(mgmt->frame_control) &&
			   mgmt->u.action.category == WLAN_CATEGORY_BACK) {
			int len = skb->len;

			mutex_lock(&local->sta_mtx);
			sta = xrmac_sta_info_get_bss(sdata, mgmt->sa);
			if (sta) {
				switch (mgmt->u.action.u.addba_req.action_code) {
				case WLAN_ACTION_ADDBA_REQ:
					mac80211_process_addba_request(
							local, sta, mgmt, len);
					break;
				case WLAN_ACTION_ADDBA_RESP:
					mac80211_process_addba_resp(local, sta,
								     mgmt, len);
					break;
				case WLAN_ACTION_DELBA:
					mac80211_process_delba(sdata, sta,
								mgmt, len);
					break;
				default:
					WARN_ON(1);
					break;
				}
			}
			mutex_unlock(&local->sta_mtx);
		} else if (ieee80211_is_data_qos(mgmt->frame_control)) {
			struct ieee80211_hdr *hdr = (void *)mgmt;
			/*
			 * So the frame isn't mgmt, but frame_control
			 * is at the right place anyway, of course, so
			 * the if statement is correct.
			 *
			 * Warn if we have other data frame types here,
			 * they must not get here.
			 */
			WARN_ON(hdr->frame_control &
					cpu_to_le16(IEEE80211_STYPE_NULLFUNC));
			WARN_ON(!(hdr->seq_ctrl &
					cpu_to_le16(IEEE80211_SCTL_FRAG)));
			/*
			 * This was a fragment of a frame, received while
			 * a block-ack session was active. That cannot be
			 * right, so terminate the session.
			 */
			mutex_lock(&local->sta_mtx);
			sta = xrmac_sta_info_get_bss(sdata, mgmt->sa);
			if (sta) {
				u16 tid = *ieee80211_get_qos_ctl(hdr) &
						IEEE80211_QOS_CTL_TID_MASK;

				__mac80211_stop_rx_ba_session(
					sta, tid, WLAN_BACK_RECIPIENT,
					WLAN_REASON_QSTA_REQUIRE_SETUP,
					true);
			}
			mutex_unlock(&local->sta_mtx);
		} else {
			switch (sdata->vif.type) {
			case NL80211_IFTYPE_AP:
			case NL80211_IFTYPE_P2P_GO:
				if (ieee80211_is_beacon(mgmt->frame_control)) {
					struct ieee80211_bss_conf *bss_conf =
						&sdata->vif.bss_conf;
					u32 bss_info_changed = 0, erp = 0;
					struct ieee80211_channel *channel =
						ieee80211_get_channel(local->hw.wiphy,
								IEEE80211_SKB_RXCB(skb)->freq);
					bss = cfg80211_inform_bss_frame(local->hw.wiphy, channel,
							mgmt, skb->len,
							IEEE80211_SKB_RXCB(skb)->signal, GFP_ATOMIC);

					/*
					erp = cfg80211_get_local_erp(local->hw.wiphy,
							IEEE80211_SKB_RXCB(skb)->freq);
					 */
					erp = ieee80211_get_local_erp(mgmt, skb->len);
					bss_conf->ap_rx_beacon_erp_info |= erp;
					if (!!bss_conf->use_cts_prot !=
							!!(bss_conf->ap_rx_beacon_erp_info & WLAN_ERP_USE_PROTECTION)) {
						bss_conf->use_cts_prot =
							!!(bss_conf->ap_rx_beacon_erp_info & WLAN_ERP_USE_PROTECTION);
						bss_info_changed |= BSS_CHANGED_ERP_CTS_PROT;
					}
					mac80211_bss_info_change_notify(sdata,
							bss_info_changed);
				};
				break;
			case NL80211_IFTYPE_STATION:
				mac80211_sta_rx_queued_mgmt(sdata, skb);
				break;
			case NL80211_IFTYPE_ADHOC:
				mac80211_ibss_rx_queued_mgmt(sdata, skb);
				break;
			case NL80211_IFTYPE_MESH_POINT:
				if (!ieee80211_vif_is_mesh(&sdata->vif))
					break;
				mac80211_mesh_rx_queued_mgmt(sdata, skb);
				break;
			default:
				WARN(1, "frame for unexpected interface type");
				break;
			}
		}
		kfree_skb(skb);
	}

	/* then other type-dependent work */
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		mac80211_sta_work(sdata);
		break;
	case NL80211_IFTYPE_ADHOC:
		mac80211_ibss_work(sdata);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		if (!ieee80211_vif_is_mesh(&sdata->vif))
			break;
		mac80211_mesh_work(sdata);
		break;
	default:
		break;
	}
}

static void mac80211_reconfig_filter(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     reconfig_filter);

	mac80211_configure_filter(sdata);
}

/*
 * Helper function to initialise an interface to a specific type.
 */
static void ieee80211_setup_sdata(struct ieee80211_sub_if_data *sdata,
				  enum nl80211_iftype type)
{
	/* clear type-dependent union */
	memset(&sdata->u, 0, sizeof(sdata->u));

#ifdef CONFIG_XRMAC_XR_ROAMING_CHANGES
	sdata->queues_locked = 0;
#endif
	/* and set some type-dependent values */
	sdata->vif.type = type;
	sdata->vif.p2p = false;
	sdata->wdev.iftype = type;

	sdata->control_port_protocol = cpu_to_be16(ETH_P_PAE);
	sdata->control_port_no_encrypt = false;

	/* only monitor/p2p-device differ */
	if (sdata->dev) {
		sdata->dev->netdev_ops = &ieee80211_dataif_ops;
		sdata->dev->type = ARPHRD_ETHER;
	}

	skb_queue_head_init(&sdata->skb_queue);
	INIT_WORK(&sdata->work, ieee80211_iface_work);
	INIT_WORK(&sdata->reconfig_filter, mac80211_reconfig_filter);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))

	__hw_addr_init(&sdata->mc_list);

#endif


	/*
	 * Initialize wiphy parameters to IEEE 802.11 MIB default values.
	 * RTS threshold is disabled by default with the special -1 value.
	 */
	sdata->vif.bss_conf.retry_short = sdata->wdev.wiphy->retry_short = 7;
	sdata->vif.bss_conf.retry_long = sdata->wdev.wiphy->retry_long = 4;
	sdata->wdev.wiphy->rts_threshold = (u32) -1;

	switch (type) {
	case NL80211_IFTYPE_P2P_GO:
		type = NL80211_IFTYPE_AP;
		sdata->vif.type = type;
		sdata->vif.p2p = true;
		/* fall through */
	case NL80211_IFTYPE_AP:
		skb_queue_head_init(&sdata->u.ap.ps_bc_buf);
		INIT_LIST_HEAD(&sdata->u.ap.vlans);
		sdata->local->uapsd_queues = IEEE80211_DEFAULT_UAPSD_QUEUES;
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		type = NL80211_IFTYPE_STATION;
		sdata->vif.type = type;
		sdata->vif.p2p = true;
		/* fall through */
	case NL80211_IFTYPE_STATION:
		mac80211_sta_setup_sdata(sdata);
		break;
	case NL80211_IFTYPE_OCB:
		printk(KERN_WARNING "not support yet!\n");
		break;
	case NL80211_IFTYPE_ADHOC:
		sdata->vif.bss_conf.bssid = sdata->u.ibss.bssid;
		mac80211_ibss_setup_sdata(sdata);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		if (ieee80211_vif_is_mesh(&sdata->vif))
			mac80211_mesh_init_sdata(sdata);
		break;
	case NL80211_IFTYPE_MONITOR:
		sdata->dev->type = ARPHRD_IEEE80211_RADIOTAP;
		sdata->dev->netdev_ops = &ieee80211_monitorif_ops;
		sdata->u.mntr_flags = MONITOR_FLAG_CONTROL |
				      MONITOR_FLAG_OTHER_BSS;
		break;
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_NAN:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_DEVICE:
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
		BUG();
		break;
	}

#ifdef CONFIG_XRMAC_DEBUGFS
	mac80211_debugfs_add_netdev(sdata);
#endif /* CONFIG_XRMAC_DEBUGFS */
	drv_bss_info_changed(sdata->local, sdata, &sdata->vif.bss_conf,
			     BSS_CHANGED_RETRY_LIMITS);
}

static int ieee80211_runtime_change_iftype(struct ieee80211_sub_if_data *sdata,
					   enum nl80211_iftype type)
{
	struct ieee80211_local *local = sdata->local;
	int ret, err;
	enum nl80211_iftype internal_type = type;
	bool p2p = false;

	ASSERT_RTNL();

	if (!local->ops->change_interface)
		return -EBUSY;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
		/*
		 * Could maybe also all others here?
		 * Just not sure how that interacts
		 * with the RX/config path e.g. for
		 * mesh.
		 */
		break;
	default:
		return -EBUSY;
	}

	switch (type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
		/*
		 * Could probably support everything
		 * but WDS here (WDS do_open can fail
		 * under memory pressure, which this
		 * code isn't prepared to handle).
		 */
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		p2p = true;
		internal_type = NL80211_IFTYPE_STATION;
		break;
	case NL80211_IFTYPE_P2P_GO:
		p2p = true;
		internal_type = NL80211_IFTYPE_AP;
		break;
	default:
		return -EBUSY;
	}

	ret = ieee80211_check_concurrent_iface(sdata, internal_type);
	if (ret)
		return ret;

	ieee80211_do_stop(sdata, false);

	ieee80211_teardown_sdata(sdata);

	ret = drv_change_interface(local, sdata, internal_type, p2p);
	if (ret)
		type = sdata->vif.type;

	/*
	 * Ignore return value here, there's not much we can do since
	 * the driver changed the interface type internally already.
	 * The warnings will hopefully make driver authors fix it :-)
	 */
	ieee80211_check_queues(sdata);

	ieee80211_setup_sdata(sdata, type);

	err = ieee80211_do_open(&sdata->wdev, false);
	WARN(err, "type change: do_open returned %d", err);

	return ret;
}

int mac80211_if_change_type(struct ieee80211_sub_if_data *sdata,
			     enum nl80211_iftype type)
{
	struct ieee80211_channel_state *chan_state =
			ieee80211_get_channel_state(sdata->local, sdata);
	int ret;

	ASSERT_RTNL();

	if (type == xr_ieee80211_vif_type_p2p(&sdata->vif))
		return 0;

	if (ieee80211_sdata_running(sdata)) {
		ret = ieee80211_runtime_change_iftype(sdata, type);
		if (ret)
			return ret;
	} else {
		/* Purge and reset type-dependent state. */
		ieee80211_teardown_sdata(sdata);
		ieee80211_setup_sdata(sdata, type);
	}

	/* reset some values that shouldn't be kept across type changes */
	sdata->vif.bss_conf.basic_rates =
		mac80211_mandatory_rates(sdata->local,
			chan_state->conf.channel->band);
	sdata->drop_unencrypted = 0;
	if (type == NL80211_IFTYPE_STATION)
		sdata->u.mgd.use_4addr = false;

	return 0;
}

static void ieee80211_assign_perm_addr(struct ieee80211_local *local,
				       u8 *perm_addr, enum nl80211_iftype type)
{
	struct ieee80211_sub_if_data *sdata;
	u64 mask, start, addr, val, inc;
	u8 *m;
	u8 tmp_addr[ETH_ALEN];
	int i;

	/* default ... something at least */
	memcpy(perm_addr, local->hw.wiphy->perm_addr, ETH_ALEN);

	if (is_zero_ether_addr(local->hw.wiphy->addr_mask) &&
	    local->hw.wiphy->n_addresses <= 1)
		return;


	mutex_lock(&local->iflist_mtx);

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
		/* doesn't matter */
		break;
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_AP_VLAN:
		/* match up with an AP interface */
		list_for_each_entry(sdata, &local->interfaces, list) {
			if (sdata->vif.type != NL80211_IFTYPE_AP)
				continue;
			memcpy(perm_addr, sdata->vif.addr, ETH_ALEN);
			break;
		}
		/* keep default if no AP interface present */
		break;
	default:
		/* assign a new address if possible -- try n_addresses first */
		for (i = 0; i < local->hw.wiphy->n_addresses; i++) {
			bool used = false;

			list_for_each_entry(sdata, &local->interfaces, list) {
				if (memcmp(local->hw.wiphy->addresses[i].addr,
					   sdata->vif.addr, ETH_ALEN) == 0) {
					used = true;
					break;
				}
			}

			if (!used) {
				memcpy(perm_addr,
				       local->hw.wiphy->addresses[i].addr,
				       ETH_ALEN);
				break;
			}
		}

		/* try mask if available */
		if (is_zero_ether_addr(local->hw.wiphy->addr_mask))
			break;

		m = local->hw.wiphy->addr_mask;
		mask =	((u64)m[0] << 5*8) | ((u64)m[1] << 4*8) |
			((u64)m[2] << 3*8) | ((u64)m[3] << 2*8) |
			((u64)m[4] << 1*8) | ((u64)m[5] << 0*8);

		if (__ffs64(mask) + hweight64(mask) != fls64(mask)) {
			/* not a contiguous mask ... not handled now! */
			printk(KERN_DEBUG "not contiguous\n");
			break;
		}

		m = local->hw.wiphy->perm_addr;
		start = ((u64)m[0] << 5*8) | ((u64)m[1] << 4*8) |
			((u64)m[2] << 3*8) | ((u64)m[3] << 2*8) |
			((u64)m[4] << 1*8) | ((u64)m[5] << 0*8);

		inc = 1ULL<<__ffs64(mask);
		val = (start & mask);
		addr = (start & ~mask) | (val & mask);
		do {
			bool used = false;

			tmp_addr[5] = addr >> 0*8;
			tmp_addr[4] = addr >> 1*8;
			tmp_addr[3] = addr >> 2*8;
			tmp_addr[2] = addr >> 3*8;
			tmp_addr[1] = addr >> 4*8;
			tmp_addr[0] = addr >> 5*8;

			val += inc;

			list_for_each_entry(sdata, &local->interfaces, list) {
				if (memcmp(tmp_addr, sdata->vif.addr,
							ETH_ALEN) == 0) {
					used = true;
					break;
				}
			}

			if (!used) {
				memcpy(perm_addr, tmp_addr, ETH_ALEN);
				break;
			}
			addr = (start & ~mask) | (val & mask);
		} while (addr != start);

		break;
	}

	mutex_unlock(&local->iflist_mtx);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
int mac80211_if_add(struct ieee80211_local *local, const char *name,
		     unsigned char name_assign_type,
		     struct wireless_dev **new_wdev, enum nl80211_iftype type,
		     struct vif_params *params)
#else
int mac80211_if_add(struct ieee80211_local *local, const char *name,
		     struct net_device **new_dev, enum nl80211_iftype type,
		     struct vif_params *params)
#endif
{
	struct net_device *ndev = NULL;
	struct ieee80211_sub_if_data *sdata = NULL;
	int ret, i;
	int txqs = 1;

	ASSERT_RTNL();

	if (type == NL80211_IFTYPE_P2P_DEVICE) {
		struct wireless_dev *wdev;

		sdata = kzalloc(sizeof(*sdata) + local->hw.vif_data_size,
				GFP_KERNEL);
		if (!sdata)
			return -ENOMEM;
		wdev = &sdata->wdev;

		sdata->dev = NULL;
		strlcpy(sdata->name, name, IFNAMSIZ);
		ieee80211_assign_perm_addr(local, wdev->address, type);
		memcpy(sdata->vif.addr, wdev->address, ETH_ALEN);
	} else {
		if (local->hw.queues >= IEEE80211_NUM_ACS)
			txqs = IEEE80211_NUM_ACS;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		ndev = alloc_netdev_mqs(sizeof(*sdata) + local->hw.vif_data_size,
					name, name_assign_type, ieee80211_if_setup, txqs, 1);
#else
		ndev = alloc_netdev_mqs(sizeof(*sdata) + local->hw.vif_data_size,
					name, ieee80211_if_setup, txqs, 1);
#endif
		if (!ndev)
			return -ENOMEM;
		dev_net_set(ndev, wiphy_net(local->hw.wiphy));

/* This is an optimization, just ignore for older kernels */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26))
		ndev->needed_headroom = local->tx_headroom +
					4*6 /* four MAC addresses */
					+ 2 + 2 + 2 + 2 /* ctl, dur, seq, qos */
					+ 6 /* mesh */
					+ 8 /* rfc1042/bridge tunnel */
					- ETH_HLEN /* ethernet hard_header_len */
					+ IEEE80211_ENCRYPT_HEADROOM;
		ndev->needed_tailroom = IEEE80211_ENCRYPT_TAILROOM;
#endif

		ret = dev_alloc_name(ndev, ndev->name);
		if (ret < 0) {
			free_netdev(ndev);
			return ret;
		}

		ieee80211_assign_perm_addr(local, ndev->perm_addr, type);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		dev_addr_mod(ndev, 0, ndev->perm_addr, ETH_ALEN);
#else
		memcpy(ndev->dev_addr, ndev->perm_addr, ETH_ALEN);
#endif
		SET_NETDEV_DEV(ndev, wiphy_dev(local->hw.wiphy));

		/* don't use IEEE80211_DEV_TO_SUB_IF -- it checks too much */
		sdata = netdev_priv(ndev);
		ndev->ieee80211_ptr = &sdata->wdev;
		memcpy(sdata->vif.addr, ndev->dev_addr, ETH_ALEN);
		memcpy(sdata->name, ndev->name, IFNAMSIZ);

		sdata->dev = ndev;
	}

	/* initialise type-independent data */
	sdata->wdev.wiphy = local->hw.wiphy;
	sdata->local = local;
#ifdef CONFIG_INET
	sdata->arp_filter_state = true;
#ifdef IPV6_FILTERING
	sdata->ndp_filter_state = true;
#endif /*IPV6_FILTERING*/
#endif

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		skb_queue_head_init(&sdata->fragments[i].skb_list);

	INIT_LIST_HEAD(&sdata->key_list);
	init_waitqueue_head(&sdata->setkey_wq);
	sdata->fourway_state = SDATA_4WAY_STATE_NONE;

	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		struct ieee80211_supported_band *sband;
		sband = local->hw.wiphy->bands[i];
		sdata->rc_rateidx_mask[i] =
			sband ? (1 << sband->n_bitrates) - 1 : 0;

		if (!sdata->chan_state.oper_channel) {
			/* init channel we're on */
			/* soumik: set default channel to non social channel */
			sdata->chan_state.conf.channel =
			/* sdata->chan_state.oper_channel = &sband->channels[0]; */
			sdata->chan_state.oper_channel = &sband->channels[2];
			sdata->chan_state.conf.channel_type = NL80211_CHAN_NO_HT;
		}
	}

	sdata->dynamic_ps_forced_timeout = -1;

	INIT_WORK(&sdata->dynamic_ps_enable_work,
		  mac80211_dynamic_ps_enable_work);
	INIT_WORK(&sdata->dynamic_ps_disable_work,
		  mac80211_dynamic_ps_disable_work);
	timer_setup(&sdata->dynamic_ps_timer, mac80211_dynamic_ps_timer, 0);

	sdata->vif.bss_conf.listen_interval = local->hw.max_listen_interval;

	ieee80211_set_default_queues(sdata);

	/* setup type-dependent data */
	ieee80211_setup_sdata(sdata, type);

	if (ndev) {
		if (params) {
			ndev->ieee80211_ptr->use_4addr = params->use_4addr;
			if (type == NL80211_IFTYPE_STATION)
				sdata->u.mgd.use_4addr = params->use_4addr;
		}


		ret = register_netdevice(ndev);
		if (ret) {
			free_netdev(ndev);
			return ret;
		}
	}

	mutex_lock(&local->iflist_mtx);
	list_add_tail_rcu(&sdata->list, &local->interfaces);
	mutex_unlock(&local->iflist_mtx);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	if (new_wdev)
		*new_wdev = &sdata->wdev;
#else
	if (new_dev)
		*new_dev = ndev;
#endif

	return 0;

}

void mac80211_if_remove(struct ieee80211_sub_if_data *sdata)
{
	ASSERT_RTNL();

	cancel_work_sync(&sdata->reconfig_filter);

	mutex_lock(&sdata->local->iflist_mtx);
	list_del_rcu(&sdata->list);
	mutex_unlock(&sdata->local->iflist_mtx);

	if (ieee80211_vif_is_mesh(&sdata->vif))
		xrmac_mesh_path_flush_by_iface(sdata);

	synchronize_rcu();

	if (sdata->dev) {
		unregister_netdevice(sdata->dev);
	} else {
		cfg80211_unregister_wdev(&sdata->wdev);
		ieee80211_teardown_sdata(sdata);
		kfree(sdata);
	}
}

void ieee80211_sdata_stop(struct ieee80211_sub_if_data *sdata)
{
	if (WARN_ON_ONCE(!test_bit(SDATA_STATE_RUNNING, &sdata->state)))
		return;
	ieee80211_do_stop(sdata, true);
}

/*
 * Remove all interfaces, may only be called at hardware unregistration
 * time because it doesn't do RCU-safe list removals.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))
void mac80211_remove_interfaces(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata, *tmp;
	LIST_HEAD(unreg_list);
	LIST_HEAD(wdev_list);

	ASSERT_RTNL();

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry_safe(sdata, tmp, &local->interfaces, list) {
		list_del(&sdata->list);

		if (ieee80211_vif_is_mesh(&sdata->vif))
			xrmac_mesh_path_flush_by_iface(sdata);

		if (sdata->dev)
			unregister_netdevice_queue(sdata->dev, &unreg_list);
		else
			list_add(&sdata->list, &wdev_list);
	}
	mutex_unlock(&local->iflist_mtx);
	unregister_netdevice_many(&unreg_list);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	/*list_del(&unreg_list);*/
#else
	list_del(&unreg_list);
#endif

	list_for_each_entry_safe(sdata, tmp, &wdev_list, list) {
		list_del(&sdata->list);
		cfg80211_unregister_wdev(&sdata->wdev);
		kfree(sdata);
	}
}
#else
void mac80211_remove_interfaces(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata, *tmp;

	ASSERT_RTNL();

	list_for_each_entry_safe(sdata, tmp, &local->interfaces, list) {
		mutex_lock(&local->iflist_mtx);
		list_del(&sdata->list);
		mutex_unlock(&local->iflist_mtx);

		unregister_netdevice(sdata->dev);
	}
}
#endif

static u32 ieee80211_idle_off(struct ieee80211_local *local,
			      const char *reason)
{
	if (!(local->hw.conf.flags & IEEE80211_CONF_IDLE))
		return 0;

#ifdef CONFIG_XRMAC_VERBOSE_DEBUG
	wiphy_debug(local->hw.wiphy, "device no longer idle - %s\n", reason);
#endif

	local->hw.conf.flags &= ~IEEE80211_CONF_IDLE;
	return IEEE80211_CONF_CHANGE_IDLE;
}

static u32 ieee80211_idle_on(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	if (local->hw.conf.flags & IEEE80211_CONF_IDLE)
		return 0;

#ifdef CONFIG_XRMAC_VERBOSE_DEBUG
	wiphy_debug(local->hw.wiphy, "device now idle\n");
#endif
	list_for_each_entry(sdata, &local->interfaces, list)
		drv_flush(local, sdata, false);

	local->hw.conf.flags |= IEEE80211_CONF_IDLE;
	return IEEE80211_CONF_CHANGE_IDLE;
}

u32 __mac80211_recalc_idle(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	int count = 0;
	bool working = false, scanning = false, hw_roc = false;
	struct ieee80211_work *wk;
	unsigned int led_trig_start = 0, led_trig_stop = 0;
	struct ieee80211_roc_work *roc;

#ifdef CONFIG_PROVE_LOCKING
	WARN_ON(debug_locks && !lockdep_rtnl_is_held() &&
		!lockdep_is_held(&local->iflist_mtx));
#endif
	lockdep_assert_held(&local->mtx);

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata)) {
			sdata->vif.bss_conf.idle = true;
			continue;
		}

		sdata->old_idle = sdata->vif.bss_conf.idle;

		/* do not count disabled managed interfaces */
		if (sdata->vif.type == NL80211_IFTYPE_STATION &&
		    !sdata->u.mgd.associated) {
			sdata->vif.bss_conf.idle = true;
			continue;
		}
		/* do not count unused IBSS interfaces */
		if (sdata->vif.type == NL80211_IFTYPE_ADHOC &&
		    !sdata->u.ibss.ssid_len) {
			sdata->vif.bss_conf.idle = true;
			continue;
		}

		if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE)
			continue;

		/* count everything else */
		count++;
	}

	if (!local->ops->remain_on_channel) {
		list_for_each_entry(roc, &local->roc_list, list) {
			working = true;
			roc->sdata->vif.bss_conf.idle = false;
		}
	}


	list_for_each_entry(wk, &local->work_list, list) {
		working = true;
		wk->sdata->vif.bss_conf.idle = false;
	}

	if (local->scan_sdata) {
		scanning = true;
		local->scan_sdata->vif.bss_conf.idle = false;
	}

	if (local->hw_roc_channel)
		hw_roc = true;

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type == NL80211_IFTYPE_MONITOR ||
		    sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
		    sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE)
			continue;
		if (sdata->old_idle == sdata->vif.bss_conf.idle)
			continue;
		if (!ieee80211_sdata_running(sdata))
			continue;
		mac80211_bss_info_change_notify(sdata, BSS_CHANGED_IDLE);
	}

	if (working || scanning || hw_roc)
		led_trig_start |= IEEE80211_TPT_LEDTRIG_FL_WORK;
	else
		led_trig_stop |= IEEE80211_TPT_LEDTRIG_FL_WORK;

	if (count)
		led_trig_start |= IEEE80211_TPT_LEDTRIG_FL_CONNECTED;
	else
		led_trig_stop |= IEEE80211_TPT_LEDTRIG_FL_CONNECTED;

	ieee80211_mod_tpt_led_trig(local, led_trig_start, led_trig_stop);

	if (hw_roc)
		return ieee80211_idle_off(local, "hw remain-on-channel");
	if (working)
		return ieee80211_idle_off(local, "working");
	if (scanning)
		return ieee80211_idle_off(local, "scanning");
	if (!count)
		return ieee80211_idle_on(local);
	else
		return ieee80211_idle_off(local, "in use");

	return 0;
}

void mac80211_recalc_idle(struct ieee80211_local *local)
{
	u32 chg;

	mutex_lock(&local->iflist_mtx);
	chg = __mac80211_recalc_idle(local);
	mutex_unlock(&local->iflist_mtx);
	if (chg)
		mac80211_hw_config(local, chg);
}

static int netdev_notify(struct notifier_block *nb,
			 unsigned long state,
			 void *ndev)
{
	struct net_device *dev = ndev;
	struct ieee80211_sub_if_data *sdata;

	if (state != NETDEV_CHANGENAME)
		return 0;

	if (!dev->ieee80211_ptr || !dev->ieee80211_ptr->wiphy)
		return 0;

	if (dev->ieee80211_ptr->wiphy->privid != xrmac_wiphy_privid)
		return 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	memcpy(sdata->name, dev->name, IFNAMSIZ);

#ifdef CONFIG_XRMAC_DEBUGFS
	mac80211_debugfs_rename_netdev(sdata);
#endif /* CONFIG_XRMAC_DEBUGFS */

	return 0;
}

static struct notifier_block mac80211_netdev_notifier = {
	.notifier_call = netdev_notify,
};

int mac80211_iface_init(void)
{
	return register_netdevice_notifier(&mac80211_netdev_notifier);
}

void mac80211_iface_exit(void)
{
	unregister_netdevice_notifier(&mac80211_netdev_notifier);
}
