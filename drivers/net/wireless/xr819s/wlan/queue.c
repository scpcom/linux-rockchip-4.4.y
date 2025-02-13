/*
 * Queue implementation for XRadio drivers
 *
 * Copyright (c) 2013
 * Xradio Technology Co., Ltd. <www.xradiotech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/mac80211_xr.h>
#include <linux/sched.h>
#include "xradio.h"
#include "queue.h"
#ifdef CONFIG_XRADIO_TESTMODE
#include <linux/time.h>
#endif /*CONFIG_XRADIO_TESTMODE*/

/* private */ struct xradio_queue_item
{
	struct list_head	head;
	struct sk_buff		*skb;
	u32			packetID;
	unsigned long		queue_timestamp;
	unsigned long		xmit_timestamp;
#ifdef CONFIG_XRADIO_TESTMODE
	unsigned long		mdelay_timestamp;
	unsigned long		qdelay_timestamp;
#endif /*CONFIG_XRADIO_TESTMODE*/
	struct xradio_txpriv	txpriv;
	u8			generation;
	u8			pack_stk_wr;
};

static inline void __xradio_queue_lock(struct xradio_queue *queue)
{
	struct xradio_queue_stats *stats = queue->stats;
	if (queue->tx_locked_cnt++ == 0) {
		txrx_printk(XRADIO_DBG_MSG, "[TX] Queue %d is locked.\n",
				queue->queue_id);
		xr_mac80211_stop_queue(stats->hw_priv->hw, queue->queue_id);
	}
}

static inline void __xradio_queue_unlock(struct xradio_queue *queue)
{
	struct xradio_queue_stats *stats = queue->stats;
	SYS_BUG(!queue->tx_locked_cnt);
	if (--queue->tx_locked_cnt == 0) {
		txrx_printk(XRADIO_DBG_MSG, "[TX] Queue %d is unlocked.\n",
				queue->queue_id);
		xr_mac80211_wake_queue(stats->hw_priv->hw, queue->queue_id);
	}
}

static inline void xradio_queue_parse_id(u32 packetID, u8 *queue_generation,
						u8 *queue_id,
						u8 *item_generation,
						u8 *item_id,
						u8 *if_id,
						u8 *link_id)
{
	*item_id		= (packetID >>  0) & 0xFF;
	*item_generation	= (packetID >>  8) & 0xFF;
	*queue_id		= (packetID >> 16) & 0xF;
	*if_id			= (packetID >> 20) & 0xF;
	*link_id		= (packetID >> 24) & 0xF;
	*queue_generation	= (packetID >> 28) & 0xF;
}

static inline u32 xradio_queue_make_packet_id(u8 queue_generation,
						u8 queue_id,
						u8 item_generation, u8 item_id,
						u8 if_id, u8 link_id)
{
	/*TODO:COMBO: Add interfaceID to the packetID */
	return ((u32)item_id << 0) |
		((u32)item_generation << 8) |
		((u32)queue_id << 16) |
		((u32)if_id << 20) |
		((u32)link_id << 24) |
		((u32)queue_generation << 28);
}

static void xradio_queue_post_gc(struct xradio_queue_stats *stats)
{
	struct xradio_queue_item *item;

	while (!list_empty(&stats->gc_list)) {
		spin_lock_bh(&stats->lock);
		item = list_first_entry(
			&stats->gc_list, struct xradio_queue_item, head);
		list_del(&item->head);
		spin_unlock_bh(&stats->lock);
		stats->skb_dtor(stats->hw_priv, item->skb, &item->txpriv);
		kfree(item);
	}
}

static void xradio_queue_register_post_gc(struct xradio_queue_stats *stats,
				     struct xradio_queue_item *item)
{
	struct xradio_queue_item *gc_item;
	gc_item = xr_kmalloc(sizeof(struct xradio_queue_item), false);
	SYS_BUG(!gc_item);
	memcpy(gc_item, item, sizeof(struct xradio_queue_item));
	spin_lock_bh(&stats->lock);
	list_add_tail(&gc_item->head, &stats->gc_list);
	spin_unlock_bh(&stats->lock);
}

static void __xradio_queue_gc(struct xradio_queue *queue,
			      bool unlock)
{
	struct xradio_queue_stats *stats = queue->stats;
	struct xradio_queue_item *item = NULL;
	/* struct xradio_vif *priv; */
	int if_id;
	bool wakeup_stats = false;

	while (!list_empty(&queue->queue)) {
		struct xradio_txpriv *txpriv;
		item = list_first_entry(
			&queue->queue, struct xradio_queue_item, head);
		if (time_before(jiffies, item->queue_timestamp+queue->ttl))
			break;

		txpriv = &item->txpriv;
		if_id = txpriv->if_id;
		--queue->num_queued;
		--queue->num_queued_vif[if_id];
		--queue->link_map_cache[if_id][txpriv->link_id];
		spin_lock_bh(&stats->lock);
		--stats->num_queued[if_id];
		if (!--stats->link_map_cache[if_id][txpriv->link_id])
			wakeup_stats = true;
		spin_unlock_bh(&stats->lock);
		/*
		priv = xrwl_hwpriv_to_vifpriv(stats->hw_priv, if_id);
		if (priv) {
			xradio_debug_tx_ttl(priv);
			spin_unlock(&priv->vif_lock);
		} */
		xradio_queue_register_post_gc(stats, item);
		item->skb = NULL;
		list_move_tail(&item->head, &queue->free_pool);
	}

	if (wakeup_stats)
		wake_up(&stats->wait_link_id_empty);

	if (queue->overfull) {
		if (queue->num_queued <= ((stats->hw_priv->vif0_throttle +
						stats->hw_priv->vif1_throttle+2)>>1)) {
			queue->overfull = false;
			if (unlock) {
				__xradio_queue_unlock(queue);
			}
		} else if (item) {
			unsigned long tmo = item->queue_timestamp + queue->ttl;
			mod_timer(&queue->gc, tmo);
#ifdef CONFIG_PM
			xradio_pm_stay_awake(&stats->hw_priv->pm_state,
					tmo - jiffies);
#endif
		}
	}
}

static void xradio_queue_gc(struct timer_list *t)
{
	struct xradio_queue *queue = from_timer(queue, t, gc);

	spin_lock_bh(&queue->lock);
	__xradio_queue_gc(queue, true);
	spin_unlock_bh(&queue->lock);
	if (!list_empty(&queue->stats->gc_list))
		queue_work(queue->stats->hw_priv->workqueue, &queue->stats->gc_work);
}

void xradio_skb_post_gc(struct xradio_common *hw_priv,
		     struct sk_buff *skb,
		     const struct xradio_txpriv *txpriv)
{
	struct xradio_queue_item skb_drop;
	skb_drop.skb = skb;
	skb_drop.txpriv = *txpriv;
	xradio_queue_register_post_gc(&hw_priv->tx_queue_stats, &skb_drop);
	queue_work(hw_priv->workqueue, &hw_priv->tx_queue_stats.gc_work);
}


void xradio_queue_gc_work(struct work_struct *work)
{
	struct xradio_queue_stats *stats =
		container_of(work, struct xradio_queue_stats, gc_work);

	xradio_queue_post_gc(stats);
}

int xradio_queue_stats_init(struct xradio_queue_stats *stats,
			    size_t map_capacity,
			    xradio_queue_skb_dtor_t skb_dtor,
			    struct xradio_common *hw_priv)
{
	int i;

	memset(stats, 0, sizeof(*stats));
	stats->map_capacity = map_capacity;
	stats->skb_dtor = skb_dtor;
	stats->hw_priv = hw_priv;
	spin_lock_init(&stats->lock);
	init_waitqueue_head(&stats->wait_link_id_empty);
	INIT_WORK(&stats->gc_work, xradio_queue_gc_work);
	INIT_LIST_HEAD(&stats->gc_list);
	for (i = 0; i < XRWL_MAX_VIFS; i++) {
		stats->link_map_cache[i] = (int *)xr_kzalloc(sizeof(int) * map_capacity, false);
		if (!stats->link_map_cache[i]) {
			for (i--; i >= 0; i--)
				kfree(stats->link_map_cache[i]);
			return -ENOMEM;
		}
	}

	return 0;
}

int xradio_queue_init(struct xradio_queue *queue,
		      struct xradio_queue_stats *stats,
		      u8 queue_id,
		      size_t capacity,
		      unsigned long ttl)
{
	int i;

	memset(queue, 0, sizeof(*queue));
	queue->stats = stats;
	queue->capacity = capacity;
	queue->queue_id = queue_id;
	queue->ttl = ttl;
	INIT_LIST_HEAD(&queue->queue);
	INIT_LIST_HEAD(&queue->pending);
	INIT_LIST_HEAD(&queue->free_pool);
	spin_lock_init(&queue->lock);
	/*init_timer(&queue->gc);
	queue->gc.data = (unsigned long)queue;
	queue->gc.function = xradio_queue_gc;*/
	timer_setup(&queue->gc, xradio_queue_gc, 0);

	queue->pool = xr_kzalloc(sizeof(struct xradio_queue_item) * capacity,
					false);
	if (!queue->pool)
		return -ENOMEM;

	for (i = 0; i < XRWL_MAX_VIFS; i++) {
		queue->link_map_cache[i] =
			(int *)xr_kzalloc(sizeof(int) * stats->map_capacity, false);
		if (!queue->link_map_cache[i]) {
			for (i--; i >= 0; i--)
				kfree(queue->link_map_cache[i]);
			kfree(queue->pool);
			queue->pool = NULL;
			return -ENOMEM;
		}
	}

	for (i = 0; i < capacity; ++i)
		list_add_tail(&queue->pool[i].head, &queue->free_pool);

	return 0;
}

/* TODO:COMBO: Flush only a particular interface specific parts */
int xradio_queue_clear(struct xradio_queue *queue, int if_id)
{
	int i, cnt, iter;
	struct xradio_queue_stats *stats = queue->stats;
	struct xradio_queue_item *item = NULL, *tmp = NULL;

	cnt = 0;
	spin_lock_bh(&queue->lock);
	queue->generation++;
	queue->generation &= 0xf;
	//list_splice_tail_init(&queue->queue, &queue->pending);
	list_for_each_entry_safe(item, tmp, &queue->queue, head) {
		SYS_WARN(!item->skb);
		if (XRWL_ALL_IFS == if_id || item->txpriv.if_id == if_id) {
			xradio_queue_register_post_gc(stats, item);
			item->skb = NULL;
			list_move_tail(&item->head, &queue->free_pool);
			--queue->num_queued;
		}
	}
	list_for_each_entry_safe(item, tmp, &queue->pending, head) {
		SYS_WARN(!item->skb);
		if (XRWL_ALL_IFS == if_id || item->txpriv.if_id == if_id) {
			xradio_queue_register_post_gc(stats, item);
			item->skb = NULL;
			list_move_tail(&item->head, &queue->free_pool);
			--queue->num_pending;
			--queue->num_queued;
		}
	}
	if (XRWL_ALL_IFS != if_id) {
		queue->num_queued_vif[if_id] = 0;
		queue->num_pending_vif[if_id] = 0;
	} else {
		queue->num_queued = 0;
		queue->num_pending = 0;
		for (iter = 0; iter < XRWL_MAX_VIFS; iter++) {
			queue->num_queued_vif[iter] = 0;
			queue->num_pending_vif[iter] = 0;
		}
	}
	spin_lock_bh(&stats->lock);
	if (XRWL_ALL_IFS != if_id) {
		for (i = 0; i < stats->map_capacity; ++i) {
			stats->num_queued[if_id] -=
				queue->link_map_cache[if_id][i];
			stats->link_map_cache[if_id][i] -=
				queue->link_map_cache[if_id][i];
			queue->link_map_cache[if_id][i] = 0;
		}
	} else {
		for (iter = 0; iter < XRWL_MAX_VIFS; iter++) {
			for (i = 0; i < stats->map_capacity; ++i) {
				stats->num_queued[iter] -=
					queue->link_map_cache[iter][i];
				stats->link_map_cache[iter][i] -=
					queue->link_map_cache[iter][i];
				queue->link_map_cache[iter][i] = 0;
			}
		}
	}
	spin_unlock_bh(&stats->lock);
	if (unlikely(queue->overfull)) {
		if (queue->num_queued <= ((stats->hw_priv->vif0_throttle +
			stats->hw_priv->vif1_throttle+2)>>1)) {
			queue->overfull = false;
			__xradio_queue_unlock(queue);
		}
	}
	spin_unlock_bh(&queue->lock);
	wake_up(&stats->wait_link_id_empty);
	if (!list_empty(&stats->gc_list))
		xradio_queue_post_gc(stats);
	return 0;
}

void xradio_queue_stats_deinit(struct xradio_queue_stats *stats)
{
	int i;

	for (i = 0; i < XRWL_MAX_VIFS ; i++) {
		kfree(stats->link_map_cache[i]);
		stats->link_map_cache[i] = NULL;
	}
}

void xradio_queue_deinit(struct xradio_queue *queue)
{
	int i;

	xradio_queue_clear(queue, XRWL_ALL_IFS);
	del_timer_sync(&queue->gc);
	INIT_LIST_HEAD(&queue->free_pool);
	kfree(queue->pool);
	for (i = 0; i < XRWL_MAX_VIFS; i++) {
		kfree(queue->link_map_cache[i]);
		queue->link_map_cache[i] = NULL;
	}
	queue->pool = NULL;
	queue->capacity = 0;
}

size_t xradio_queue_get_num_queued(struct xradio_vif *priv,
				   struct xradio_queue *queue,
				   u32 link_id_map)
{
	size_t ret;
	int i, bit;
	size_t map_capacity = queue->stats->map_capacity;

	if (!link_id_map)
		return 0;

	spin_lock_bh(&queue->lock);
	if (likely(link_id_map == (u32) -1)) {
		ret = queue->num_queued_vif[priv->if_id] -
			queue->num_pending_vif[priv->if_id];
	} else {
		ret = 0;
		for (i = 0, bit = 1; i < map_capacity; ++i, bit <<= 1) {
			if (link_id_map & bit)
				ret +=
				queue->link_map_cache[priv->if_id][i];
		}
	}
	spin_unlock_bh(&queue->lock);
	return ret;
}

int xradio_queue_put(struct xradio_queue *queue, struct sk_buff *skb,
		     struct xradio_txpriv *txpriv)
{
	int ret = 0;
#ifdef CONFIG_XRADIO_TESTMODE
	struct timespec64 tmval;
#endif /*CONFIG_XRADIO_TESTMODE*/
	LIST_HEAD(gc_list);
	struct xradio_queue_stats *stats = queue->stats;
	/* TODO:COMBO: Add interface ID info to queue item */

	if (txpriv->link_id >= queue->stats->map_capacity)
		return -EINVAL;

	spin_lock_bh(&queue->lock);
	if (!SYS_WARN(list_empty(&queue->free_pool))) {
		struct xradio_queue_item *item = list_first_entry(
			&queue->free_pool, struct xradio_queue_item, head);
		SYS_BUG(item->skb);

		list_move_tail(&item->head, &queue->queue);
		item->skb = skb;
		item->txpriv = *txpriv;
		item->generation  = 1; /* avoid packet ID is 0.*/
		item->pack_stk_wr = 0;
		item->packetID = xradio_queue_make_packet_id(
			queue->generation, queue->queue_id,
			item->generation, item - queue->pool,
			txpriv->if_id, txpriv->raw_link_id);
		item->queue_timestamp = jiffies;
#ifdef CONFIG_XRADIO_TESTMODE
		xr_do_gettimeofday(&tmval);
		item->qdelay_timestamp = tmval.tv_usec;
#endif /*CONFIG_XRADIO_TESTMODE*/

#ifdef TES_P2P_0002_ROC_RESTART
		if (TES_P2P_0002_state == TES_P2P_0002_STATE_SEND_RESP) {
			TES_P2P_0002_packet_id = item->packetID;
			TES_P2P_0002_state = TES_P2P_0002_STATE_GET_PKTID;
			txrx_printk(XRADIO_DBG_WARN, "[ROC_RESTART_STATE_GET_PKTID]\n");
		}
#endif

		++queue->num_queued;
		++queue->num_queued_vif[txpriv->if_id];
		++queue->link_map_cache[txpriv->if_id][txpriv->link_id];

		spin_lock_bh(&stats->lock);
		++stats->num_queued[txpriv->if_id];
		++stats->link_map_cache[txpriv->if_id][txpriv->link_id];
		spin_unlock_bh(&stats->lock);

		/*
		 * TX may happen in parallel sometimes.
		 * Leave extra queue slots so we don't overflow.
		 */
		if (queue->overfull == false &&
				queue->num_queued >=
		((stats->hw_priv->vif0_throttle + stats->hw_priv->vif1_throttle)
				- (num_present_cpus() - 1))) {
			queue->overfull = true;
			__xradio_queue_lock(queue);
			mod_timer(&queue->gc, jiffies);
			txrx_printk(XRADIO_DBG_NIY, "!lock queue\n");
		}
	} else {
		ret = -ENOENT;
	}
#if 0
	txrx_printk(XRADIO_DBG_ERROR, "queue_put queue %d, %d, %d\n",
		queue->num_queued,
		queue->link_map_cache[txpriv->if_id][txpriv->link_id],
		queue->num_pending);
	txrx_printk(XRADIO_DBG_ERROR, "queue_put stats %d, %d\n", stats->num_queued,
		stats->link_map_cache[txpriv->if_id][txpriv->link_id]);
#endif
	spin_unlock_bh(&queue->lock);
	return ret;
}

int xradio_queue_get(struct xradio_queue *queue,
			int if_id,
		     u32 link_id_map,
		     struct wsm_tx **tx,
		     struct ieee80211_tx_info **tx_info,
		     struct xradio_txpriv **txpriv)
{
	int ret = -ENOENT;
	struct xradio_queue_item *item;
	struct xradio_queue_stats *stats = queue->stats;
	bool wakeup_stats = false;
#ifdef CONFIG_XRADIO_TESTMODE
	struct timespec64 tmval;
#endif /*CONFIG_XRADIO_TESTMODE*/

	spin_lock_bh(&queue->lock);
	list_for_each_entry(item, &queue->queue, head) {
		if ((item->txpriv.if_id == if_id) &&
			(link_id_map & BIT(item->txpriv.link_id))) {
			ret = 0;
			break;
		}
	}

	if (!SYS_WARN(ret)) {
		*tx = (struct wsm_tx *)item->skb->data;
		*tx_info = IEEE80211_SKB_CB(item->skb);
		*txpriv = &item->txpriv;
		(*tx)->packetID = __cpu_to_le32(item->packetID);
		list_move_tail(&item->head, &queue->pending);
		++queue->num_pending;
		++queue->num_pending_vif[item->txpriv.if_id];
		--queue->link_map_cache[item->txpriv.if_id]
				[item->txpriv.link_id];
		item->xmit_timestamp = jiffies;
#ifdef CONFIG_XRADIO_TESTMODE
		xr_do_gettimeofday(&tmval);
		item->mdelay_timestamp = tmval.tv_usec;
#endif /*CONFIG_XRADIO_TESTMODE*/

		spin_lock_bh(&stats->lock);
		--stats->num_queued[item->txpriv.if_id];
		if (!--stats->link_map_cache[item->txpriv.if_id]
					[item->txpriv.link_id])
			wakeup_stats = true;

		spin_unlock_bh(&stats->lock);
#if 0
		txrx_printk(XRADIO_DBG_ERROR, "queue_get queue %d, %d, %d\n",
		queue->num_queued,
		queue->link_map_cache[item->txpriv.if_id][item->txpriv.link_id],
		queue->num_pending);
		txrx_printk(XRADIO_DBG_ERROR, "queue_get stats %d, %d\n",
			    stats->num_queued,
		stats->link_map_cache[item->txpriv.if_id]
		[item->txpriv.link_id]);
#endif
	} else {/*add debug info for warning*/
		struct xradio_queue_item *item_tmp;
		txrx_printk(XRADIO_DBG_WARN,
			"%s, if_id=%d, link_id_map=%08x, queued=%zu, pending=%zu\n",
			__func__, if_id, link_id_map, queue->num_queued_vif[if_id],
			queue->num_pending_vif[if_id]);
		list_for_each_entry(item_tmp, &queue->queue, head) {
			txrx_printk(XRADIO_DBG_WARN, "%s, item_if_id=%d, item_link_id=%d\n",
				__func__, item_tmp->txpriv.if_id, item_tmp->txpriv.link_id);
		}
	}
	spin_unlock_bh(&queue->lock);
	if (wakeup_stats)
		wake_up(&stats->wait_link_id_empty);

	return ret;
}

#ifdef CONFIG_XRADIO_TESTMODE
int xradio_queue_requeue(struct xradio_common *hw_priv,
	struct xradio_queue *queue, u32 packetID, bool check)
#else
int xradio_queue_requeue(struct xradio_queue *queue, u32 packetID, bool check)
#endif
{
	int ret = 0;
	u8 queue_generation, queue_id, item_generation, item_id, if_id, link_id;
	struct xradio_queue_item *item;
	struct xradio_queue_stats *stats = queue->stats;

	xradio_queue_parse_id(packetID, &queue_generation, &queue_id,
				&item_generation, &item_id, &if_id, &link_id);

	item = &queue->pool[item_id];
#ifdef P2P_MULTIVIF
	if (check && item->txpriv.if_id == XRWL_GENERIC_IF_ID) {
#else
	if (check && item->txpriv.offchannel_if_id == XRWL_GENERIC_IF_ID) {
#endif
		txrx_printk(XRADIO_DBG_MSG, "Requeued frame dropped for "
						"generic interface id.\n");
#ifdef CONFIG_XRADIO_TESTMODE
		xradio_queue_remove(hw_priv, queue, packetID);
#else
		xradio_queue_remove(queue, packetID);
#endif
		return 0;
	}

#ifndef P2P_MULTIVIF
	if (!check)
		item->txpriv.offchannel_if_id = XRWL_GENERIC_IF_ID;
#endif

	/*if_id = item->txpriv.if_id;*/

	spin_lock_bh(&queue->lock);
	SYS_BUG(queue_id != queue->queue_id);
	if (unlikely(queue_generation != queue->generation)) {
		ret = -ENOENT;
	} else if (unlikely(item_id >= (unsigned) queue->capacity)) {
		SYS_WARN(1);
		ret = -EINVAL;
	} else if (unlikely(item->generation != item_generation)) {
		SYS_WARN(1);
		ret = -ENOENT;
	} else {
		--queue->num_pending;
		--queue->num_pending_vif[if_id];
		++queue->link_map_cache[if_id][item->txpriv.link_id];

		spin_lock_bh(&stats->lock);
		++stats->num_queued[item->txpriv.if_id];
		++stats->link_map_cache[if_id][item->txpriv.link_id];
		spin_unlock_bh(&stats->lock);

		item->generation = ++item_generation;
		item->packetID = xradio_queue_make_packet_id(
			queue_generation, queue_id, item_generation, item_id,
			if_id, link_id);
		list_move(&item->head, &queue->queue);
#if 0
		txrx_printk(XRADIO_DBG_ERROR, "queue_requeue queue %d, %d, %d\n",
		queue->num_queued,
		queue->link_map_cache[if_id][item->txpriv.link_id],
		queue->num_pending);
		txrx_printk(XRADIO_DBG_ERROR, "queue_requeue stats %d, %d\n",
		stats->num_queued,
		stats->link_map_cache[if_id][item->txpriv.link_id]);
#endif
	}
	spin_unlock_bh(&queue->lock);
	return ret;
}

int xradio_queue_requeue_all(struct xradio_queue *queue)
{
	struct xradio_queue_stats *stats = queue->stats;
	spin_lock_bh(&queue->lock);
	while (!list_empty(&queue->pending)) {
		struct xradio_queue_item *item = list_entry(
			queue->pending.prev, struct xradio_queue_item, head);

		--queue->num_pending;
		--queue->num_pending_vif[item->txpriv.if_id];
		++queue->link_map_cache[item->txpriv.if_id]
				[item->txpriv.link_id];

		spin_lock_bh(&stats->lock);
		++stats->num_queued[item->txpriv.if_id];
		++stats->link_map_cache[item->txpriv.if_id]
				[item->txpriv.link_id];
		spin_unlock_bh(&stats->lock);

		++item->generation;
		item->packetID = xradio_queue_make_packet_id(
			queue->generation, queue->queue_id,
			item->generation, item - queue->pool,
			item->txpriv.if_id, item->txpriv.raw_link_id);
		list_move(&item->head, &queue->queue);
	}
	spin_unlock_bh(&queue->lock);

	return 0;
}
#ifdef CONFIG_XRADIO_TESTMODE
int xradio_queue_remove(struct xradio_common *hw_priv,
				struct xradio_queue *queue, u32 packetID)
#else
int xradio_queue_remove(struct xradio_queue *queue, u32 packetID)
#endif /*CONFIG_XRADIO_TESTMODE*/
{
	int ret = 0;
	u8 queue_generation, queue_id, item_generation, item_id, if_id, link_id;
	struct xradio_queue_item *item;
	struct xradio_queue_stats *stats = queue->stats;
	struct sk_buff *gc_skb = NULL;
	struct xradio_txpriv gc_txpriv;

	xradio_queue_parse_id(packetID, &queue_generation, &queue_id,
				&item_generation, &item_id, &if_id, &link_id);

	item = &queue->pool[item_id];

	spin_lock_bh(&queue->lock);
	SYS_BUG(queue_id != queue->queue_id);
	/*TODO:COMBO:Add check for interface ID also */
	if (unlikely(queue_generation != queue->generation)) {
		ret = -ENOENT;
	} else if (unlikely(item_id >= (unsigned) queue->capacity)) {
		SYS_WARN(1);
		ret = -EINVAL;
	} else if (unlikely(item->generation != item_generation)) {
		SYS_WARN(1);
		ret = -ENOENT;
	} else {
		gc_txpriv = item->txpriv;
		gc_skb = item->skb;
		item->skb = NULL;
		--queue->num_pending;
		--queue->num_pending_vif[if_id];
		--queue->num_queued;
		--queue->num_queued_vif[if_id];
		++queue->num_sent;
		++item->generation;
#ifdef CONFIG_XRADIO_TESTMODE
		spin_lock_bh(&hw_priv->tsm_lock);
		if (hw_priv->start_stop_tsm.start) {
			if (queue_id == hw_priv->tsm_info.ac) {
				struct timespec64 tmval;
				unsigned long queue_delay;
				unsigned long media_delay;
				xr_do_gettimeofday(&tmval);

				if (tmval.tv_usec > item->qdelay_timestamp)
					queue_delay = tmval.tv_usec -
						item->qdelay_timestamp;
				else
					queue_delay = tmval.tv_usec +
					1000000 - item->qdelay_timestamp;

				if (tmval.tv_usec > item->mdelay_timestamp)
					media_delay = tmval.tv_usec -
						item->mdelay_timestamp;
				else
					media_delay = tmval.tv_usec +
					1000000 - item->mdelay_timestamp;
				hw_priv->tsm_info.sum_media_delay +=
							media_delay;
				hw_priv->tsm_info.sum_pkt_q_delay += queue_delay;
				if (queue_delay <= 10000)
					hw_priv->tsm_stats.bin0++;
				else if (queue_delay <= 20000)
					hw_priv->tsm_stats.bin1++;
				else if (queue_delay <= 40000)
					hw_priv->tsm_stats.bin2++;
				else
					hw_priv->tsm_stats.bin3++;
			}
		}
		spin_unlock_bh(&hw_priv->tsm_lock);
#endif /*CONFIG_XRADIO_TESTMODE*/
		/* Do not use list_move_tail here, but list_move:
		 * try to utilize cache row.
		 */
		list_move(&item->head, &queue->free_pool);

		if (unlikely(queue->overfull) &&
		    (queue->num_queued <= ((stats->hw_priv->vif0_throttle +
		     stats->hw_priv->vif1_throttle + 2)>>1))) {
			queue->overfull = false;
			__xradio_queue_unlock(queue);
		}
	}
	spin_unlock_bh(&queue->lock);

#if 0
	txrx_printk(XRADIO_DBG_ERROR, "queue_drop queue %d, %d, %d\n",
		queue->num_queued, queue->link_map_cache[if_id][0],
		queue->num_pending);
	txrx_printk(XRADIO_DBG_ERROR, "queue_drop stats %d, %d\n",
		    stats->num_queued, stats->link_map_cache[if_id][0]);
#endif
	if (gc_skb)
		stats->skb_dtor(stats->hw_priv, gc_skb, &gc_txpriv);

	return ret;
}

int xradio_queue_get_skb(struct xradio_queue *queue, u32 packetID,
			 struct sk_buff **skb,
			 const struct xradio_txpriv **txpriv)
{
	int ret = 0;
	u8 queue_generation, queue_id, item_generation, item_id, if_id, link_id;
	struct xradio_queue_item *item;

	xradio_queue_parse_id(packetID, &queue_generation, &queue_id,
				&item_generation, &item_id, &if_id, &link_id);

	item = &queue->pool[item_id];

	spin_lock_bh(&queue->lock);
	SYS_BUG(queue_id != queue->queue_id);
	/* TODO:COMBO: Add check for interface ID here */
	if (unlikely(queue_generation != queue->generation)) {
		txrx_printk(XRADIO_DBG_WARN, "%s, queue generation match failed!\n",
				__func__);
		ret = -ENOENT;
	} else if (unlikely(item_id >= (unsigned) queue->capacity)) {
		txrx_printk(XRADIO_DBG_WARN, "%s, invalid item_id!\n", __func__);
		ret = -EINVAL;
	} else if (unlikely(item->generation != item_generation)) {
		txrx_printk(XRADIO_DBG_WARN, "%s, item generation match failed!\n",
				__func__);
		ret = -ENOENT;
	} else {
		*skb = item->skb;
		*txpriv = &item->txpriv;
	}
	spin_unlock_bh(&queue->lock);
	return ret;
}

void xradio_queue_lock(struct xradio_queue *queue)
{
	spin_lock_bh(&queue->lock);
	__xradio_queue_lock(queue);
	spin_unlock_bh(&queue->lock);
}

void xradio_queue_unlock(struct xradio_queue *queue)
{
	spin_lock_bh(&queue->lock);
	__xradio_queue_unlock(queue);
	spin_unlock_bh(&queue->lock);
}

bool xradio_queue_get_xmit_timestamp(struct xradio_queue *queue,
				     unsigned long *timestamp, int if_id,
				     u32 pending_frameID, u32 *Old_frame_ID)
{
	struct xradio_queue_item *item;
	bool ret;

	spin_lock_bh(&queue->lock);
	ret = !list_empty(&queue->pending);
	if (ret) {
		list_for_each_entry(item, &queue->pending, head) {
			if (((if_id == XRWL_GENERIC_IF_ID) ||
				(if_id == XRWL_ALL_IFS) ||
					(item->txpriv.if_id == if_id)) &&
					(item->packetID != pending_frameID)) {
				if (time_before(item->xmit_timestamp,
							*timestamp)) {
					*timestamp = item->xmit_timestamp;
					*Old_frame_ID = item->packetID;
				}
			}
		}
	}
	spin_unlock_bh(&queue->lock);
	return ret;
}

bool xradio_queue_stats_is_empty(struct xradio_queue_stats *stats,
				 u32 link_id_map, int if_id)
{
	bool empty = true;

	spin_lock_bh(&stats->lock);
	if (link_id_map == (u32)-1)
		empty = stats->num_queued[if_id] == 0;
	else {
		int i, if_id;
		for (if_id = 0; if_id < XRWL_MAX_VIFS; if_id++) {
			for (i = 0; i < stats->map_capacity; ++i) {
				if (link_id_map & BIT(i)) {
					if (stats->link_map_cache[if_id][i]) {
						empty = false;
						break;
					}
				}
			}
		}
	}
	spin_unlock_bh(&stats->lock);

	return empty;
}

bool xradio_query_txpkt_timeout(struct xradio_common *hw_priv, int if_id,
				u32 pending_pkt_id, long *timeout)
{
	int i;
	bool pending = false;
	unsigned long timestamp = jiffies;
	struct xradio_queue      *queue = NULL;
	struct xradio_queue_item *item  = NULL;
	struct xradio_queue      *old_queue = NULL;
	struct xradio_queue_item *old_item  = NULL;
	u8 pack_stk_wr = 0;

	/* Get oldest frame.*/
	for (i = 0; i < AC_QUEUE_NUM; ++i) {
		queue = &hw_priv->tx_queue[i];
		spin_lock_bh(&queue->lock);
		if (!list_empty(&queue->pending)) {
			list_for_each_entry(item, &queue->pending, head) {
				if (((if_id == XRWL_GENERIC_IF_ID) ||
					 (if_id == XRWL_ALL_IFS) ||
					 (item->txpriv.if_id == if_id)) &&
					 (item->packetID != pending_pkt_id)) {
					if (time_before(item->xmit_timestamp, timestamp)) {
						timestamp   = item->xmit_timestamp;
						pack_stk_wr = item->pack_stk_wr;
						old_queue   = queue;
						old_item    = item;
					}
					pending = true;
				}
			}
		}
		spin_unlock_bh(&queue->lock);
	}
	if (!pending)
		return false;

	/* Check if frame transmission is timed out.
	 * add (WSM_CMD_LAST_CHANCE_TIMEOUT>>1) for stuck workaround.*/
	*timeout = timestamp + WSM_CMD_LAST_CHANCE_TIMEOUT - jiffies;
	if (unlikely(*timeout < 0) && !pack_stk_wr) {
		struct ieee80211_hdr *frame = NULL;
		const struct xradio_txpriv *txpriv = NULL;
		u16 fctl = 0x0;
		u32 len  = 0x0;
		u8 if_id = 0, link_id = 0, tid = 0;

		/* query the timeout frame. */
		spin_lock_bh(&old_queue->lock);
		if (likely(old_item->skb && !hw_priv->query_packetID)) {
			hw_priv->query_packetID = old_item->packetID;
			old_item->pack_stk_wr = 1;
			atomic_add(1, &hw_priv->query_cnt);

			/* Info of stuck frames for debug.*/
			txpriv = &old_item->txpriv;
			frame  = (struct ieee80211_hdr *)(&old_item->skb->data[txpriv->offset]);
			fctl   = frame->frame_control;
			len    = old_item->skb->len;
			if_id  = txpriv->if_id;
			link_id = txpriv->link_id;
			tid = txpriv->tid;
		}
		spin_unlock_bh(&old_queue->lock);
		/* Dump Info of stuck frames. */
		if (frame) {
			txrx_printk(XRADIO_DBG_ERROR, "TX confirm timeout(%ds).\n",
				    WSM_CMD_LAST_CHANCE_TIMEOUT/HZ);
			txrx_printk(XRADIO_DBG_ERROR, "if=%d, linkid=%d, tid=%d, " \
				    "old_packetID=0x%08x, fctl=0x%04x, len=%d, wr=%d\n",
				    if_id, link_id, tid,
				    hw_priv->query_packetID, fctl, len,
				    pack_stk_wr);
		}
		/* Return half of timeout for query packet. */
		*timeout = (WSM_CMD_LAST_CHANCE_TIMEOUT>>1);
	} else if (unlikely(pack_stk_wr)) {
		*timeout = *timeout + (WSM_CMD_LAST_CHANCE_TIMEOUT>>1);
		txrx_printk(XRADIO_DBG_MSG, "%s, wr and timeout=%ld\n",
			    __func__, *timeout);
	}
	return pending;
}
