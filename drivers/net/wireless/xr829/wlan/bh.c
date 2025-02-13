/*
 * Data Transmission thread implementation for XRadio drivers
 *
 * Copyright (c) 2013
 * Xradio Technology Co., Ltd. <www.xradiotech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/mac80211_xr.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>

#include "xradio.h"
#include "bh.h"
#include "hwio.h"
#include "wsm.h"
#include "sbus.h"

#ifdef SUPPORT_FW_DBG_INF
#include "fw_dbg_inf.h"
#endif

/* TODO: Verify these numbers with WSM specification. */
#define DOWNLOAD_BLOCK_SIZE_WR	(0x1000 - 4)
/* an SPI message cannot be bigger than (2"12-1)*2 bytes
 * "*2" to cvt to bytes */
#define MAX_SZ_RD_WR_BUFFERS	(DOWNLOAD_BLOCK_SIZE_WR*2)
#define PIGGYBACK_CTRL_REG	(2)
#define EFFECTIVE_BUF_SIZE	(MAX_SZ_RD_WR_BUFFERS - PIGGYBACK_CTRL_REG)

#define DEV_WAKEUP_MAX_TIME  (HZ<<1)   /* =HZ*2 = 2s*/
#define DEV_WAKEUP_WAIT_TIME (HZ/50)   /*=20ms*/
#define BH_TX_BURST_NONTXOP  (16)

#if (SDIO_BLOCK_SIZE > 500)
#define SKB_CACHE_LEN   (SDIO_BLOCK_SIZE)
#elif (SDIO_BLOCK_SIZE > 250)
#define SKB_CACHE_LEN   (SDIO_BLOCK_SIZE<<1)
#else
#define SKB_CACHE_LEN    xr_sdio_blksize_align(500)
#endif
#define SKB_RESV_MAX    (1900)

int tx_burst_limit = BH_TX_BURST_NONTXOP;

/* Suspend state privates */
enum xradio_bh_pm_state {
	XRADIO_BH_RESUMED = 0,
	XRADIO_BH_SUSPEND,
	XRADIO_BH_SUSPENDED,
	XRADIO_BH_RESUME,
};
typedef int (*xradio_wsm_handler) (struct xradio_common *hw_priv, u8 *data,
				   size_t size);

static inline u32 bh_time_interval(struct timespec64 *oldtime)
{
	u32 time_int;
	struct timespec64 newtime;
	xr_do_gettimeofday(&newtime);
	time_int = (newtime.tv_sec - oldtime->tv_sec) * 1000000 + \
			   (long)((newtime.tv_nsec - oldtime->tv_nsec) / 1000);
	return time_int;
}


#ifdef MCAST_FWDING
int wsm_release_buffer_to_fw(struct xradio_vif *priv, int count);
#endif
static int xradio_bh(void *arg);
static void xradio_put_skb(struct xradio_common *hw_priv, struct sk_buff *skb);
static struct sk_buff *xradio_get_skb(struct xradio_common *hw_priv, size_t len, u8 *flags);
static inline int xradio_put_resv_skb(struct xradio_common *hw_priv,
									  struct sk_buff *skb, u8 flags);

#ifdef BH_PROC_THREAD
static int xradio_proc(void *arg);
int bh_proc_init(struct xradio_common *hw_priv)
{
	int ret = 0;
	int i;
	struct bh_items *pool = NULL;
	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);

	memset(&hw_priv->proc, 0, sizeof(struct bh_proc));
	/* init locks and wait_queue */
	spin_lock_init(&hw_priv->proc.lock);
	init_waitqueue_head(&hw_priv->proc.proc_wq);

	/* init pool and txrx queues */
	atomic_set(&hw_priv->proc.proc_tx, 0);
	atomic_set(&hw_priv->proc.rx_queued, 0);
	atomic_set(&hw_priv->proc.tx_queued, 0);
	INIT_LIST_HEAD(&hw_priv->proc.bh_tx);
	INIT_LIST_HEAD(&hw_priv->proc.bh_rx);
	INIT_LIST_HEAD(&hw_priv->proc.bh_free);
	pool = xr_kzalloc(PROC_POOL_SIZE, false);
	if (!pool)
		return -ENOMEM;
	else
		hw_priv->proc.bh_pool[0] = pool;
	for (i = 0; i < ITEM_RESERVED; ++i)
		list_add_tail(&pool[i].head, &hw_priv->proc.bh_free);

	/* init proc thread.*/
	hw_priv->proc.proc_state = 0;
	hw_priv->proc.proc_thread =
		kthread_create(&xradio_proc, hw_priv, XRADIO_PROC_THREAD);
	if (IS_ERR(hw_priv->proc.proc_thread)) {
		ret = PTR_ERR(hw_priv->proc.proc_thread);
		hw_priv->proc.proc_thread = NULL;
	} else {
#ifdef HAS_PUT_TASK_STRUCT
		get_task_struct(hw_priv->proc.proc_thread);
#endif
		wake_up_process(hw_priv->proc.proc_thread);
	}
	return ret;
}

int bh_proc_flush_txqueue(struct xradio_common *hw_priv, int if_id)
{
	struct bh_items *item = NULL, *tmp = NULL;
	spin_lock(&hw_priv->proc.lock);
	/*flush proc tx queue, no need to dev_kfree_skb */
	list_for_each_entry_safe(item, tmp, &hw_priv->proc.bh_tx, head) {
		if (item) {
			if (XRWL_ALL_IFS == if_id || item->if_id == if_id) {
				item->data = NULL;
				list_move_tail(&item->head, &hw_priv->proc.bh_free);
				atomic_sub(1, &hw_priv->proc.tx_queued);
			}
		} else {
			bh_printk(XRADIO_DBG_ERROR,
					"%s tx item is NULL!\n", __func__);
		}
	}
	if (XRWL_ALL_IFS == if_id) {
		INIT_LIST_HEAD(&hw_priv->proc.bh_tx);
		atomic_set(&hw_priv->proc.tx_queued, 0);
		atomic_set(&hw_priv->proc.proc_tx, 0);
	}
	spin_unlock(&hw_priv->proc.lock);
	return 0;
}
int bh_proc_flush_queue(struct xradio_common *hw_priv)
{
	struct bh_items *item = NULL;
	spin_lock(&hw_priv->proc.lock);
	/*flush proc rx queue */
	while (!list_empty(&hw_priv->proc.bh_rx)) {
		item = list_first_entry(&hw_priv->proc.bh_rx,
							struct bh_items, head);
		if (item) {
			if (item->data) {
				dev_kfree_skb((struct sk_buff *)item->data);
				item->data = NULL;
			} else {
				bh_printk(XRADIO_DBG_ERROR,
					"%s item->data is NULL!\n", __func__);
			}
			list_move_tail(&item->head, &hw_priv->proc.bh_free);
		} else {
			bh_printk(XRADIO_DBG_ERROR,
					"%s rx item is NULL!\n", __func__);
		}
	}
	INIT_LIST_HEAD(&hw_priv->proc.bh_rx);
	atomic_set(&hw_priv->proc.rx_queued, 0);
	spin_unlock(&hw_priv->proc.lock);

	/*flush proc tx queue, no need to dev_kfree_skb */
	bh_proc_flush_txqueue(hw_priv, XRWL_ALL_IFS);
	return 0;
}

void bh_proc_deinit(struct xradio_common *hw_priv)
{
	struct task_struct *thread = hw_priv->proc.proc_thread;
	int i = 0;
	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);

	/* deinit proc thread */
	if (thread) {
		hw_priv->proc.proc_thread = NULL;
		kthread_stop(thread);
	#ifdef HAS_PUT_TASK_STRUCT
		put_task_struct(thread);
	#endif
	} else {
		bh_printk(XRADIO_DBG_WARN,
			"%s thread is NULL!\n", __func__);
	}

	/* clear tx/rx queue */
	bh_proc_flush_queue(hw_priv);

	/* clear free queue */
	INIT_LIST_HEAD(&hw_priv->proc.bh_free);

	/*free proc pool*/
	for (i = 0; i < PROC_POOL_NUM; i++) {
		if (hw_priv->proc.bh_pool[i]) {
			kfree(hw_priv->proc.bh_pool[i]);
			hw_priv->proc.bh_pool[i] = NULL;
		} else if (i == 0) {
			bh_printk(XRADIO_DBG_WARN,
				"%s bh_pool[0] is NULL!\n", __func__);
		}
	}

	return ;
}

int bh_proc_reinit(struct xradio_common *hw_priv)
{
	bh_proc_deinit(hw_priv);
	return bh_proc_init(hw_priv);
}

static struct bh_items *xradio_get_free_item(struct xradio_common *hw_priv)
{
	struct bh_items *item = NULL;
	if (likely(!list_empty(&hw_priv->proc.bh_free))) {
		item = list_first_entry(&hw_priv->proc.bh_free,
			struct bh_items, head);
	} else {
		int i = 0;
		struct bh_items *pool = NULL;
		for (i = 0; i < PROC_POOL_NUM; i++) {
			if (!hw_priv->proc.bh_pool[i]) {
				pool = xr_kzalloc(PROC_POOL_SIZE, false);
				hw_priv->proc.bh_pool[i] = pool;
				break;
			}
		}
		if (pool) {
			bh_printk(XRADIO_DBG_WARN, "%s alloc pool%d!\n",
				__func__, i);
			for (i = 0; i < ITEM_RESERVED; ++i)
				list_add_tail(&pool[i].head, &hw_priv->proc.bh_free);
			item = list_first_entry(&hw_priv->proc.bh_free,
				struct bh_items, head);
		} else {
			bh_printk(XRADIO_DBG_ERROR, "%s Failed alloc pool%d!\n",
				__func__, i);
		}
	}
	return item;
}

void xradio_proc_wakeup(struct xradio_common *hw_priv)
{
	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);
#if BH_PROC_TX
	if (atomic_add_return(1, &hw_priv->proc.proc_tx) == 1) {
		bh_printk(XRADIO_DBG_NIY, "%s\n", __func__);
		wake_up(&hw_priv->proc.proc_wq);
	}
#else
	xradio_bh_wakeup(hw_priv);
#endif
}

#if PERF_INFO_TEST
struct timespec64 proc_start_time;
#endif

#if BH_PROC_DPA
#define PROC_HIGH_IDX  0
#define PROC_LOW_IDX  4
const struct thread_dpa g_dpa[] = {
	{SCHED_FIFO,  25},
	{SCHED_FIFO,  50},
	{SCHED_FIFO,  75},
	{SCHED_FIFO,  99},
	{SCHED_NORMAL, 0}
};
int thread_dpa_up(struct task_struct *p, s8 *prio_index)
{
	int ret = 0;
	s8  idx = 0;
	if (unlikely(!p || !prio_index)) {
		bh_printk(XRADIO_DBG_ERROR,
			"%s, task_struct=%p, prio_index=%p\n",
			__func__, p, prio_index);
		return -EINVAL;
	}
	idx = (*prio_index) - 1;
	if (idx > PROC_HIGH_IDX) {
		struct sched_param param = {
			.sched_priority = g_dpa[idx].priority
		};
		bh_printk(XRADIO_DBG_NIY, "%s=%d\n", __func__, idx);
		ret = sched_setscheduler(p, g_dpa[idx].policy, &param);
		if (!ret)
			*prio_index = idx;
		else
			bh_printk(XRADIO_DBG_ERROR,
				"%s, sched_setscheduler failed, idx=%d\n",
				__func__, idx);
		return ret;
	} else {
		bh_printk(XRADIO_DBG_NIY, "%s, prio_index=%d\n",
			__func__, idx + 1);
		return 0;
	}
}
int thread_dpa_down(struct task_struct *p, u8 *prio_index)
{
	int ret = 0;
	s8  idx = 0;
	if (unlikely(!p || !prio_index)) {
		bh_printk(XRADIO_DBG_ERROR,
			"%s, task_struct=%p, prio_index=%p\n",
			__func__, p, prio_index);
		return -EINVAL;
	}
	idx = (*prio_index) + 1;
	if (idx < PROC_LOW_IDX) {
		struct sched_param param = {
			.sched_priority = g_dpa[idx].priority
		};
		bh_printk(XRADIO_DBG_NIY, "%s=%d\n", __func__, idx);
		ret = sched_setscheduler(p, g_dpa[idx].policy, &param);
		if (!ret)
			*prio_index = idx;
		else
			bh_printk(XRADIO_DBG_ERROR,
				"%s, sched_setscheduler failed, idx=%d\n",
				__func__, idx);
		return ret;
	} else {
		bh_printk(XRADIO_DBG_NIY, "%s, prio_index=%d\n",
			__func__, idx - 1);
		return 0;
	}
}
static inline int proc_set_priority(struct xradio_common *hw_priv, u8 idx)
{
	struct sched_param param = {
		.sched_priority = g_dpa[idx].priority
	};
	hw_priv->proc.proc_prio = idx;
	return sched_setscheduler(hw_priv->proc.proc_thread,
			g_dpa[idx].policy, &param);
}
int dpa_proc_tx;
int dpa_proc_rx;
u32 proc_dpa_cnt;
u32 proc_up_cnt;
u32 proc_down_cnt;
static inline int proc_dpa_update(struct xradio_common *hw_priv)
{
	int tx_ret = 0;
	int rx_ret = 0;
	int dpa_old = 0;
	int i = 0;

	if (!hw_priv->proc.proc_thread)
		return -ENOENT;
	++proc_dpa_cnt;
	/*update by rx.*/
	dpa_old = dpa_proc_rx;
	dpa_proc_rx = atomic_read(&hw_priv->proc.rx_queued);
	if (dpa_proc_rx >= (ITEM_RESERVED>>2) ||
		dpa_proc_rx >= (dpa_old + 10)) {
		rx_ret = 1;
	} else if ((dpa_proc_rx + 20) < dpa_old ||
		dpa_proc_rx < (ITEM_RESERVED>>5)) {
		rx_ret = -1;
	}

	/* update by tx.*/
	dpa_old = dpa_proc_tx;
	for (dpa_proc_tx = 0, i = 0; i < 4; ++i) {
		dpa_proc_tx += hw_priv->tx_queue[i].num_queued -
			hw_priv->tx_queue[i].num_pending;
	}
	if (dpa_proc_tx > (dpa_old + 10) ||
		dpa_proc_tx > XRWL_HOST_VIF0_11N_THROTTLE) {
		tx_ret = 1;
	} else if ((dpa_proc_tx + 10) < dpa_old ||
		dpa_proc_tx < (XRWL_HOST_VIF0_11N_THROTTLE>>2)) {
		tx_ret = -1;
	}

	if (rx_ret > 0 || tx_ret > 0) {
		++proc_up_cnt;
		if (++hw_priv->proc.prio_cnt > 10) {
			hw_priv->proc.prio_cnt = 0;
			return thread_dpa_up(hw_priv->proc.proc_thread,
				&hw_priv->proc.proc_prio);
		}
	} else if (rx_ret < 0 && tx_ret < 0) {
		++proc_down_cnt;
		if (--hw_priv->proc.prio_cnt < -10) {
			hw_priv->proc.prio_cnt = 0;
			return thread_dpa_down(hw_priv->proc.proc_thread,
				&hw_priv->proc.proc_prio);
		}
	}
	return 0;
}
#endif

static int xradio_proc(void *arg)
{
	struct xradio_common *hw_priv = arg;
#if !BH_PROC_DPA
	struct sched_param param = {
		.sched_priority = 99
	};
#endif
	int ret = 0;
	int term = 0;
	int tx = 0;
	int rx = 0;
#if BH_PROC_DPA
	int dpa_num = 0;
#endif
	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);

#if BH_PROC_DPA
	ret = proc_set_priority(hw_priv, 3);
#else
	ret = sched_setscheduler(hw_priv->proc.proc_thread,
			SCHED_FIFO, &param);
#endif
	if (ret)
		bh_printk(XRADIO_DBG_WARN, "%s sched_setscheduler failed(%d)\n",
			__func__, ret);

	for (;;) {
		PERF_INFO_GETTIME(&proc_start_time);
		ret = wait_event_interruptible(hw_priv->proc.proc_wq, ({
			term = kthread_should_stop();
#if BH_PROC_RX
			rx = atomic_read(&hw_priv->proc.rx_queued);
#else
			rx = 0;
#endif
#if BH_PROC_TX
			tx = atomic_xchg(&hw_priv->proc.proc_tx, 0);
#else
			tx = 0;
#endif
			(term || ((rx || tx) &&
			!hw_priv->bh_error && !hw_priv->proc.proc_state &&
			XRADIO_BH_RESUMED == atomic_read(&hw_priv->bh_suspend))); }));

		/* 0--proc is going to be shut down */
		if (term) {
			bh_printk(XRADIO_DBG_NIY, "%s exit!\n", __func__);
			break;
		} else if (ret < 0) {
			bh_printk(XRADIO_DBG_ERROR, "%s wait_event err=%d!\n",
				__func__, ret);
			continue;  /*continue to wait for exit */
		}
		PERF_INFO_STAMP_UPDATE(&proc_start_time, &proc_wait, 0);

		while (rx || tx) {
			bh_printk(XRADIO_DBG_NIY, "%s rx=%d, tx=%d\n",
				__func__, rx, tx);
#if BH_PROC_RX
			/* 1--handle rx*/
			if (rx) {
				size_t rx_len = 0;
				spin_lock(&hw_priv->proc.lock);
				if (likely(!list_empty(&hw_priv->proc.bh_rx))) {
					struct bh_items *rx_item = NULL;
					struct sk_buff *rx_skb   = NULL;
					u8 flags = 0;
					rx_item = list_first_entry(&hw_priv->proc.bh_rx,
						struct bh_items, head);
					if (rx_item) {
						rx_skb = (struct sk_buff *)rx_item->data;
						flags  = rx_item->flags;
						rx_item->data = NULL;
						rx_len = rx_item->datalen;
						list_move_tail(&rx_item->head,
							&hw_priv->proc.bh_free);
					}
					rx = atomic_sub_return(1, &hw_priv->proc.rx_queued);
					spin_unlock(&hw_priv->proc.lock);
					if (rx_skb) {
						ret = wsm_handle_rx(hw_priv, rx_item->flags, &rx_skb);
						/* Reclaim the SKB buffer */
						if (rx_skb) {
							if (xradio_put_resv_skb(hw_priv, rx_skb, rx_item->flags))
								xradio_put_skb(hw_priv, rx_skb);
							rx_skb = NULL;
						}
						if (ret) {
							bh_printk(XRADIO_DBG_ERROR,
								"wsm_handle_rx err=%d!\n", ret);
							break;
						}
					} else {
						bh_printk(XRADIO_DBG_ERROR,
							"%s rx_item data is NULL\n", __func__);
					}
					hw_priv->proc.rxed_num++;
				} else {
					rx = 0;
					hw_priv->proc.proc_state = 1;  /*need to restart proc*/
					bh_printk(XRADIO_DBG_WARN,
						"rx_queued=%d, but proc.bh_rx is empty!\n",
						atomic_read(&hw_priv->proc.rx_queued));
					spin_unlock(&hw_priv->proc.lock);
				}
				PERF_INFO_STAMP_UPDATE(&proc_start_time, &proc_rx, rx_len);
			}
#endif

#if BH_PROC_TX
			/* 2--handle tx*/
			if (tx) {
				u8 *data = NULL;
				size_t tx_len = 0;
				int burst = 0;
				int vif_selected = 0;
				ret = wsm_get_tx(hw_priv, &data, &tx_len,
					&burst, &vif_selected);
				if (ret < 0) {
					bh_printk(XRADIO_DBG_ERROR,
								"wsm_get_tx err=%d!\n", ret);
					tx = 0;
					break;
				} else if (ret) {
					struct bh_items *item  = NULL;
					spin_lock(&hw_priv->proc.lock);
					item = xradio_get_free_item(hw_priv);
					if (likely(item)) {
						SYS_BUG(item->data);
						item->data = data;
						item->datalen = tx_len;
						if (unlikely(item->datalen != tx_len)) {
							bh_printk(XRADIO_DBG_ERROR,
								"%s datalen=%u, tx_len=%zu.\n",
								__func__, item->datalen, tx_len);
						}
						item->if_id = vif_selected;
						item->flags = 0;
						list_move_tail(&item->head, &hw_priv->proc.bh_tx);
						spin_unlock(&hw_priv->proc.lock);
						if (atomic_add_return(1, &hw_priv->proc.tx_queued) == 1 &&
							hw_priv->bh_thread) {
							xradio_bh_wakeup(hw_priv);
						}
						hw_priv->proc.txed_num++;
						bh_printk(XRADIO_DBG_NIY,
							"%s Tx if=%d, datalen=%zu, queued=%d\n",
							__func__, vif_selected, tx_len,
							atomic_read(&hw_priv->proc.tx_queued));
					} else {
						bh_printk(XRADIO_DBG_ERROR,
							"%s pool is empty\n", __func__);
						spin_unlock(&hw_priv->proc.lock);
						hw_priv->proc.proc_state = 1; /*need to restart proc*/
						break;
					}
				} else {
					tx = 0;
					bh_printk(XRADIO_DBG_NIY, "wsm_get_tx no data!\n");
				}
				PERF_INFO_STAMP_UPDATE(&proc_start_time, &proc_tx, tx_len);
			}
#endif

#if BH_PROC_DPA
			if (++dpa_num > 20) {
				proc_dpa_update(hw_priv);
				dpa_num = 0;
			}
#endif
		}  /* while */

		if (hw_priv->proc.proc_state) {
			/* proc error occurs, to restart driver.*/
			hw_priv->bh_error = 1;
		}

#if 0
		/* for debug */
		if (!atomic_read(&hw_priv->proc.proc_tx)) {
			int num = 0;
			int pending = 0;
			int i = 0;
			for (i = 0; i < 4; ++i) {
				pending += hw_priv->tx_queue[i].num_pending;
				num += hw_priv->tx_queue[i].num_queued -
					hw_priv->tx_queue[i].num_pending;
			}
			if (num && !atomic_read(&hw_priv->proc.proc_tx)) {
				bh_printk(XRADIO_DBG_NIY,
					"%s rx=%d, tx=%d, num=%d, pending=%d, "
					" rx_queued=%d, bufuse=%d\n",
					__func__, rx, tx, num, pending,
					atomic_read(&hw_priv->proc.rx_queued),
					hw_priv->hw_bufs_used);
			}
		}
#endif

	} /* for (;;) */
	return 0;
}

#if BH_PROC_TX
static inline int xradio_bh_get(struct xradio_common *hw_priv, u8 **data,
			size_t *tx_len, int *burst, int *vif_selected)
{
	int ret = 0;
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);
	/* check cmd first */
	spin_lock(&hw_priv->wsm_cmd.lock);
	if (hw_priv->wsm_cmd.ptr) {
		*data = hw_priv->wsm_cmd.ptr;
		*tx_len = hw_priv->wsm_cmd.len;
		*burst = atomic_read(&hw_priv->proc.tx_queued) + 1;
		*vif_selected = -1;
		spin_unlock(&hw_priv->wsm_cmd.lock);
		return 1;
	}
	spin_unlock(&hw_priv->wsm_cmd.lock);

	/* check tx data */
	spin_lock(&hw_priv->proc.lock);
	if (!list_empty(&hw_priv->proc.bh_tx) &&
		!atomic_read(&hw_priv->tx_lock) &&
		hw_priv->hw_bufs_used < hw_priv->wsm_caps.numInpChBufs) {
		struct bh_items *item = list_first_entry(
			&hw_priv->proc.bh_tx, struct bh_items, head);
		if (item && item->data) {
			struct xradio_queue_item *queue_item =
				(struct xradio_queue_item *)item->data;
			queue_item->xmit_timestamp = jiffies;
			queue_item->xmit_to_fw = 1;
			*data = queue_item->skb->data;
			*tx_len = item->datalen;
			*vif_selected = item->if_id;
			*burst = atomic_sub_return(1, &hw_priv->proc.tx_queued) + 1;
			item->data = NULL;
			list_move_tail(&item->head, &hw_priv->proc.bh_free);
			ret = 1;
			bh_printk(XRADIO_DBG_NIY, "%s tx_len=%zu, burst=%d!\n",
				__func__, *tx_len, *burst);
		} else {
			bh_printk(XRADIO_DBG_ERROR, "%s item=%p, data=%p!\n",
				__func__, item, item->data);
			ret = -ENOENT;
		}
	}
	spin_unlock(&hw_priv->proc.lock);
	return ret;
}
#endif

#if PERF_INFO_TEST
struct timespec64 bh_put_time;
#endif

static inline int xradio_bh_put(struct xradio_common *hw_priv,
		struct sk_buff **skb_p, u8 flags)
{
	struct bh_items *item = NULL;
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	PERF_INFO_GETTIME(&bh_put_time);
	spin_lock(&hw_priv->proc.lock);
	if (unlikely(!hw_priv->proc.proc_thread)) {
		spin_unlock(&hw_priv->proc.lock);
		bh_printk(XRADIO_DBG_WARN,
			"%s proc_thread is stopped!\n", __func__);
		dev_kfree_skb(*skb_p);
		*skb_p = NULL;
		return 0;
	}
	item = xradio_get_free_item(hw_priv);
	if (likely(item)) {
		SYS_BUG(item->data);
		item->data = (u8 *)(*skb_p);
		item->datalen = (*skb_p)->len;
		if (unlikely(item->datalen != (*skb_p)->len)) {
			bh_printk(XRADIO_DBG_ERROR,
				"%s datalen=%u, skblen=%u.\n",
				__func__, item->datalen, (*skb_p)->len);
		}
		item->flags = flags;
		if ((flags & ITEM_F_CMDCFM))
			list_move(&item->head, &hw_priv->proc.bh_rx);
		else
			list_move_tail(&item->head, &hw_priv->proc.bh_rx);
		spin_unlock(&hw_priv->proc.lock);
		PERF_INFO_STAMP_UPDATE(&bh_put_time, &get_item, 0);
		if (atomic_add_return(1, &hw_priv->proc.rx_queued) == 1) {
			wake_up(&hw_priv->proc.proc_wq);
		}
		*skb_p = NULL;
		PERF_INFO_STAMP(&bh_put_time, &wake_proc, 0);
	} else {
		bh_printk(XRADIO_DBG_ERROR,
			"%s pool is empty!\n", __func__);
		goto err;
	}
	return 0;

err:
	spin_unlock(&hw_priv->proc.lock);
	return -ENOENT;
}
#endif /* #ifdef BH_PROC_THREAD */

int xradio_register_bh(struct xradio_common *hw_priv)
{
	int err = 0;
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	SYS_BUG(hw_priv->bh_thread);
	atomic_set(&hw_priv->bh_rx, 0);
	atomic_set(&hw_priv->bh_tx, 0);
	atomic_set(&hw_priv->bh_term, 0);
	atomic_set(&hw_priv->bh_suspend, XRADIO_BH_RESUMED);
	hw_priv->buf_id_tx = 0;
	hw_priv->buf_id_rx = 0;
#ifdef BH_USE_SEMAPHORE
	sema_init(&hw_priv->bh_sem, 0);
	atomic_set(&hw_priv->bh_wk, 0);
#else
	init_waitqueue_head(&hw_priv->bh_wq);
#endif
	init_waitqueue_head(&hw_priv->bh_evt_wq);

	hw_priv->bh_thread = kthread_create(&xradio_bh, hw_priv, XRADIO_BH_THREAD);
	if (IS_ERR(hw_priv->bh_thread)) {
		err = PTR_ERR(hw_priv->bh_thread);
		hw_priv->bh_thread = NULL;
	} else {
#ifdef HAS_PUT_TASK_STRUCT
		get_task_struct(hw_priv->bh_thread);
#endif
		wake_up_process(hw_priv->bh_thread);
	}

	return err;
}

void xradio_unregister_bh(struct xradio_common *hw_priv)
{
	struct task_struct *thread = hw_priv->bh_thread;
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	if (SYS_WARN(!thread))
		return;

	hw_priv->bh_thread = NULL;
	kthread_stop(thread);
#ifdef HAS_PUT_TASK_STRUCT
	put_task_struct(thread);
#endif
	bh_printk(XRADIO_DBG_NIY, "Unregister success.\n");
}

void xradio_irq_handler(void *priv)
{
	struct xradio_common *hw_priv = (struct xradio_common *)priv;
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);
	DBG_INT_ADD(irq_count);
	if (/* SYS_WARN */(hw_priv->bh_error))
		return;
#ifdef BH_USE_SEMAPHORE
	atomic_add(1, &hw_priv->bh_rx);
	if (atomic_add_return(1, &hw_priv->bh_wk) == 1) {
		up(&hw_priv->bh_sem);
	}
#else
	if (atomic_add_return(1, &hw_priv->bh_rx) == 1) {
		wake_up(&hw_priv->bh_wq);
	}
#endif

}

void xradio_bh_wakeup(struct xradio_common *hw_priv)
{
	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);
	if (hw_priv->bh_error) {
		bh_printk(XRADIO_DBG_ERROR, "%s bh_error=%d\n",
			__func__, hw_priv->bh_error);
		return;
	}
#ifdef BH_USE_SEMAPHORE
	atomic_add(1, &hw_priv->bh_tx);
	if (atomic_add_return(1, &hw_priv->bh_wk) == 1) {
		up(&hw_priv->bh_sem);
	}
#else
	if (atomic_add_return(1, &hw_priv->bh_tx) == 1) {
		wake_up(&hw_priv->bh_wq);
	}
#endif
}

int xradio_bh_suspend(struct xradio_common *hw_priv)
{

#ifdef MCAST_FWDING
	int i = 0;
	struct xradio_vif *priv = NULL;
#endif

	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);

	if (hw_priv->bh_thread == NULL)
		return 0;

	if (hw_priv->bh_error)
		return -EINVAL;

#ifdef MCAST_FWDING
	xradio_for_each_vif(hw_priv, priv, i) {
		if (!priv)
			continue;
		if ((priv->multicast_filter.enable)
		    && (priv->join_status == XRADIO_JOIN_STATUS_AP)) {
			wsm_release_buffer_to_fw(priv,
						 (hw_priv->wsm_caps.
						  numInpChBufs - 1));
			break;
		}
	}
#endif

	atomic_set(&hw_priv->bh_suspend, XRADIO_BH_SUSPEND);
#ifdef BH_USE_SEMAPHORE
	up(&hw_priv->bh_sem);
#else
	wake_up(&hw_priv->bh_wq);
#endif
	return wait_event_timeout(hw_priv->bh_evt_wq, (hw_priv->bh_error ||
		XRADIO_BH_SUSPENDED == atomic_read(&hw_priv->bh_suspend)),
		    1 * HZ) ? 0 : -ETIMEDOUT;
}

int xradio_bh_resume(struct xradio_common *hw_priv)
{

#ifdef MCAST_FWDING
	int ret;
	int i = 0;
	struct xradio_vif *priv = NULL;
#endif

	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);
	if (hw_priv->bh_error || atomic_read(&hw_priv->bh_term)) {
		return -EINVAL;
	}

	atomic_set(&hw_priv->bh_suspend, XRADIO_BH_RESUME);
#ifdef BH_USE_SEMAPHORE
	up(&hw_priv->bh_sem);
#else
	wake_up(&hw_priv->bh_wq);
#endif

#ifdef MCAST_FWDING
	ret = wait_event_timeout(hw_priv->bh_evt_wq, (hw_priv->bh_error ||
	     XRADIO_BH_RESUMED == atomic_read(&hw_priv->bh_suspend)), 1 * HZ) ?
	     0 : -ETIMEDOUT;

	xradio_for_each_vif(hw_priv, priv, i) {
		if (!priv)
			continue;
		if ((priv->join_status == XRADIO_JOIN_STATUS_AP) &&
			  (priv->multicast_filter.enable)) {
			u8 count = 0;
			SYS_WARN(wsm_request_buffer_request(priv, &count));
			bh_printk(XRADIO_DBG_NIY, "Reclaim Buff %d \n", count);
			break;
		}
	}

	return ret;
#else
	return wait_event_timeout(hw_priv->bh_evt_wq, hw_priv->bh_error ||
		(XRADIO_BH_RESUMED == atomic_read(&hw_priv->bh_suspend)),
		1 * HZ) ? 0 : -ETIMEDOUT;
#endif

}

static inline void wsm_alloc_tx_buffer(struct xradio_common *hw_priv)
{
	++hw_priv->hw_bufs_used;
}

int wsm_release_tx_buffer(struct xradio_common *hw_priv, int count)
{
	int ret = 0;
	int hw_bufs_used = hw_priv->hw_bufs_used;
	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);

	hw_priv->hw_bufs_used -= count;
	if (SYS_WARN(hw_priv->hw_bufs_used < 0)) {
		/* Tx data patch stops when all but one hw buffers are used.
		   So, re-start tx path in case we find hw_bufs_used equals
		   numInputChBufs - 1.
		 */
		bh_printk(XRADIO_DBG_ERROR, "%s, hw_bufs_used=%d, count=%d.\n",
			  __func__, hw_priv->hw_bufs_used, count);
		ret = -1;
	} else if (hw_bufs_used >= (hw_priv->wsm_caps.numInpChBufs - 1))
		ret = 1;
	if (!hw_priv->hw_bufs_used)
		wake_up(&hw_priv->bh_evt_wq);
	return ret;
}

int wsm_release_vif_tx_buffer(struct xradio_common *hw_priv,
							  int if_id, int count)
{
	int ret = 0;
	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);

	hw_priv->hw_bufs_used_vif[if_id] -= count;
	if (!hw_priv->hw_bufs_used_vif[if_id])
		wake_up(&hw_priv->bh_evt_wq);

	if (hw_priv->hw_bufs_used_vif[if_id] < 0) {
		bh_printk(XRADIO_DBG_WARN,
			"%s, if=%d, used=%d, count=%d.\n", __func__, if_id,
			hw_priv->hw_bufs_used_vif[if_id], count);
		ret = -1;
	}
	return ret;
}

#ifdef MCAST_FWDING
int wsm_release_buffer_to_fw(struct xradio_vif *priv, int count)
{
	int i;
	u8 flags;
	struct wsm_hdr *wsm;
	struct xradio_common *hw_priv = priv->hw_priv;
	struct wsm_buf *buf = &hw_priv->wsm_release_buf;
	size_t buf_len = buf->end - buf->begin;

	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);

	if (priv->join_status != XRADIO_JOIN_STATUS_AP || buf_len == 0) {
		return 0;
	}
	bh_printk(XRADIO_DBG_NIY, "Rel buffer to FW %d, %d\n",
		  count, hw_priv->hw_bufs_used);

	for (i = 0; i < count; i++) {
		if ((hw_priv->hw_bufs_used + 1) < hw_priv->wsm_caps.numInpChBufs) {
			/* Fill Buffer Request Msg */
			flags = i ? 0 : 0x1;
			buf->data[0] = flags;

			/* Add sequence number */
			wsm = (struct wsm_hdr *)buf->begin;
			wsm->id &= __cpu_to_le32(~WSM_TX_SEQ(WSM_TX_SEQ_MAX));
			wsm->id |= cpu_to_le32(WSM_TX_SEQ(hw_priv->wsm_tx_seq));
			bh_printk(XRADIO_DBG_NIY, "REL %d, len=%d, buflen=%zu\n",
				  hw_priv->wsm_tx_seq, wsm->len, buf_len);

			wsm_alloc_tx_buffer(hw_priv);
			if (SYS_WARN(xradio_data_write(hw_priv, buf->begin, buf_len))) {
				break;
			}
			hw_priv->buf_released = 1;
			hw_priv->wsm_tx_seq = (hw_priv->wsm_tx_seq + 1) & WSM_TX_SEQ_MAX;
		} else
			break;
	}

	if (i == count) {
		return 0;
	}

	/* Should not be here */
	bh_printk(XRADIO_DBG_ERROR, "Error, Less HW buf %d, %d.\n",
		  hw_priv->hw_bufs_used, hw_priv->wsm_caps.numInpChBufs);
	SYS_WARN(1);
	return -1;
}
#endif

/* reserve a packet for the case dev_alloc_skb failed in bh.*/
int xradio_init_resv_skb(struct xradio_common *hw_priv)
{
	int len = SKB_RESV_MAX + WSM_TX_EXTRA_HEADROOM + \
			   8 + 12;	/* TKIP IV + ICV and MIC */
	len = xr_sdio_blksize_align(len);
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	spin_lock_init(&hw_priv->cache_lock);
	hw_priv->skb_reserved = xr_alloc_skb(len);
	if (hw_priv->skb_reserved) {
		hw_priv->skb_resv_len = len;
		skb_reserve(hw_priv->skb_reserved,
			WSM_TX_EXTRA_HEADROOM + 8 /* TKIP IV */
			- WSM_RX_EXTRA_HEADROOM);
	} else {
		bh_printk(XRADIO_DBG_WARN, "%s xr_alloc_skb failed(%d)\n",
			__func__, len);
	}
	return 0;
}

void xradio_deinit_resv_skb(struct xradio_common *hw_priv)
{
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);
	if (hw_priv->skb_reserved) {
		dev_kfree_skb(hw_priv->skb_reserved);
		hw_priv->skb_reserved = NULL;
		hw_priv->skb_resv_len = 0;
	}
}

int xradio_realloc_resv_skb(struct xradio_common *hw_priv,
							struct sk_buff *skb, u8 flags)
{
	/* spin_lock(&hw_priv->cache_lock); */
	if (!hw_priv->skb_reserved && hw_priv->skb_resv_len) {
		hw_priv->skb_reserved = xr_alloc_skb(hw_priv->skb_resv_len);
		if (!hw_priv->skb_reserved && (flags & ITEM_F_RESERVE)) {
			hw_priv->skb_reserved = skb;
			skb_reserve(hw_priv->skb_reserved,
				WSM_TX_EXTRA_HEADROOM + 8 /* TKIP IV */
				- WSM_RX_EXTRA_HEADROOM);
			/* spin_unlock(&hw_priv->cache_lock); */
			bh_printk(XRADIO_DBG_WARN, "%s xr_alloc_skb failed(%d)\n",
				__func__, hw_priv->skb_resv_len);
			return -1;
		}
	}
	/* spin_unlock(&hw_priv->cache_lock); */
	return 0; /* realloc sbk success, deliver to upper.*/
}

static inline struct sk_buff *xradio_get_resv_skb(struct xradio_common *hw_priv,
												  size_t len)
{	struct sk_buff *skb = NULL;
	/* spin_lock(&hw_priv->cache_lock); */
	if (hw_priv->skb_reserved && len <= hw_priv->skb_resv_len) {
		skb = hw_priv->skb_reserved;
		hw_priv->skb_reserved = NULL;
	}
	/* spin_unlock(&hw_priv->cache_lock); */
	return skb;
}

static inline int xradio_put_resv_skb(struct xradio_common *hw_priv,
									  struct sk_buff *skb, u8 flags)
{
	/* spin_lock(&hw_priv->cache_lock); */
	if (!hw_priv->skb_reserved && hw_priv->skb_resv_len &&
	    (flags & ITEM_F_RESERVE)) {
		hw_priv->skb_reserved = skb;
		/* spin_unlock(&hw_priv->cache_lock); */
		return 0;
	}
	/* spin_unlock(&hw_priv->cache_lock); */
	return 1; /* sbk not put to reserve*/
}

static struct sk_buff *xradio_get_skb(struct xradio_common *hw_priv, size_t len, u8 *flags)
{
	struct sk_buff *skb = NULL;
	size_t alloc_len = (len > SKB_CACHE_LEN) ? len : SKB_CACHE_LEN;
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);

	/* TKIP IV + TKIP ICV and MIC - Piggyback.*/
	alloc_len += WSM_TX_EXTRA_HEADROOM + 8 + 12 - 2;
	if (len > SKB_CACHE_LEN || !hw_priv->skb_cache) {
		skb = xr_alloc_skb_pf(alloc_len);
		/* In AP mode RXed SKB can be looped back as a broadcast.
		 * Here we reserve enough space for headers. */
		if (skb) {
			skb_reserve(skb, WSM_TX_EXTRA_HEADROOM + 8 /* TKIP IV */
					    - WSM_RX_EXTRA_HEADROOM);
		} else {
			skb = xradio_get_resv_skb(hw_priv, alloc_len);
			if (skb) {
				*flags |= ITEM_F_RESERVE;
				bh_printk(XRADIO_DBG_WARN, "%s get skb_reserved(%zu)!\n",
					__func__, alloc_len);
			} else {
				bh_printk(XRADIO_DBG_ERROR, "%s xr_alloc_skb failed(%zu)!\n",
					__func__, alloc_len);
			}
		}
	} else {
		/* don't care cache because min len is SKB_CACHE_LEN*/
		/* spin_lock(&hw_priv->cache_lock); */
		skb = hw_priv->skb_cache;
		hw_priv->skb_cache = NULL;
		/* spin_unlock(&hw_priv->cache_lock); */
	}
	return skb;
}

static void xradio_put_skb(struct xradio_common *hw_priv, struct sk_buff *skb)
{
	bh_printk(XRADIO_DBG_TRC, "%s\n", __func__);
	/* spin_lock(&hw_priv->cache_lock); */
	if (hw_priv->skb_cache)
		dev_kfree_skb(skb);
	else {
		hw_priv->skb_cache = skb;
	}
	/* spin_unlock(&hw_priv->cache_lock); */
}

static int xradio_bh_read_ctrl_reg(struct xradio_common *hw_priv,
				   u16 *ctrl_reg)
{
	int ret = 0;

	ret = xradio_reg_read_16(hw_priv, HIF_CONTROL_REG_ID, ctrl_reg);
	if (ret) {
		*ctrl_reg = 0;
		ret = 0;
		bh_printk(XRADIO_DBG_NIY, "read ctrl failed, SDIO DCE occupied!\n");
		ret = xradio_reg_read_16(hw_priv, HIF_CONTROL_REG_ID, ctrl_reg);
		if (ret) {
			hw_priv->bh_error = 1;
			bh_printk(XRADIO_DBG_ERROR, "Failed to read control register.\n");
		}
	}
	return ret;
}

static inline int xradio_device_sleep(struct xradio_common *hw_priv)
{
	int ret;
	ret = xradio_reg_write_32(hw_priv, HIF_CONTROL_REG_ID, 0);
	if (ret) {
		hw_priv->bh_error = 1;
		bh_printk(XRADIO_DBG_ERROR, "%s:control reg failed.\n", __func__);
	}

	return ret;
}

struct timespec64 wakeup_time;
static int xradio_device_wakeup(struct xradio_common *hw_priv, u16 *ctrl_reg_ptr)
{
	int ret = 0;
	unsigned long time = 0;

	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);

	PERF_INFO_GETTIME(&wakeup_time);
	/* To force the device to be always-on, the host sets WLAN_UP to 1 */
	ret = xradio_reg_write_16(hw_priv, HIF_CONTROL_REG_ID, HIF_CTRL_WUP_BIT);
	if (SYS_WARN(ret))
		return ret;

	ret = xradio_bh_read_ctrl_reg(hw_priv, ctrl_reg_ptr);
	if (SYS_WARN(ret))
		return ret;

	/* If the device returns WLAN_RDY as 1, the device is active and will
	 * remain active. */
	time = jiffies + DEV_WAKEUP_MAX_TIME;
	while (!(*ctrl_reg_ptr & (HIF_CTRL_RDY_BIT|HIF_CTRL_NEXT_LEN_MASK)) &&
		   time_before(jiffies, time) && !ret) {
#ifdef BH_USE_SEMAPHORE
		msleep(1);
#else
		wait_event_timeout(hw_priv->bh_wq,
				atomic_read(&hw_priv->bh_rx), DEV_WAKEUP_WAIT_TIME);
#endif
		ret = xradio_bh_read_ctrl_reg(hw_priv, ctrl_reg_ptr);
	}

	PERF_INFO_STAMP(&wakeup_time, &dev_wake, 0);

	if (likely(*ctrl_reg_ptr & HIF_CTRL_RDY_BIT)) {
		bh_printk(XRADIO_DBG_NIY, "Device awake, t=%ldms.\n",
			(jiffies+DEV_WAKEUP_MAX_TIME-time)*1000/HZ);
		return 1;
	} else if (*ctrl_reg_ptr & HIF_CTRL_NEXT_LEN_MASK) { /*device has data to rx.*/
		bh_printk(XRADIO_DBG_NIY, "To rx data before wakeup, len=%d.\n",
				(*ctrl_reg_ptr & HIF_CTRL_NEXT_LEN_MASK)<<1);
		return (int)(*ctrl_reg_ptr & HIF_CTRL_NEXT_LEN_MASK);
	} else {
		bh_printk(XRADIO_DBG_ERROR, "Device cannot wakeup in %dms.\n",
				DEV_WAKEUP_MAX_TIME*1000/HZ);
		return -1;
	}
}

#ifdef BH_COMINGRX_FORECAST
static bool xradio_comingrx_update(struct xradio_common *hw_priv)
{
	static bool is_full;
	static unsigned long tmo;
	if (hw_priv->hw_bufs_used >= (hw_priv->wsm_caps.numInpChBufs-1)) {
		if (is_full == false) {
			tmo = jiffies + (HZ/166);/*1/166s = 6ms*/
		}
		is_full = true;
	} else {
		tmo = jiffies - 1;
		is_full = false;
	}

	if (time_before(jiffies, tmo))
		return true;
	else
		return false;

}
#endif

/* Must be called from BH thraed. */
void xradio_enable_powersave(struct xradio_vif *priv, bool enable)
{
	priv->powersave_enabled = enable;
	bh_printk(XRADIO_DBG_NIY, "Powerave is %s.\n",
		  enable ? "enabled" : "disabled");
}

#if PERF_INFO_TEST
struct timespec64 tx_start_time1;
struct timespec64 tx_start_time2;
struct timespec64 rx_start_time1;
struct timespec64 rx_start_time2;
struct timespec64 bh_start_time;
struct timespec64 sdio_reg_time;
extern struct timespec64 last_showtime;
#endif

u32  sdio_reg_cnt1;
u32  sdio_reg_cnt2;
u32  sdio_reg_cnt3;
u32  sdio_reg_cnt4;
u32  sdio_reg_cnt5;
u32  sdio_reg_cnt6;
u32  tx_limit_cnt1;
u32  tx_limit_cnt2;
u32  tx_limit_cnt3;
u32  tx_limit_cnt4;
u32  tx_limit_cnt5;
u32  tx_limit_cnt6;

static int xradio_bh(void *arg)
{
	struct xradio_common *hw_priv = arg;
	struct sched_param param = {
		.sched_priority = 1
	};
	int ret = 0;
	struct sk_buff *skb_rx = NULL;
	size_t read_len = 0;
	int rx = 0, tx = 0, term, suspend;
	struct wsm_hdr *wsm;
	size_t wsm_len;
	int wsm_id;
	u8 wsm_seq;
	int rx_resync = 1;
	u16 ctrl_reg = 0;
	int tx_allowed;
	int pending_tx = 0;
	int tx_burst;
	int tx_bursted = 0;
	int rx_burst = 0;
	long status;
	bool coming_rx = false;
#if 0
	u32 dummy;
#endif
	int reg_read = 1;
	int vif_selected;

	bh_printk(XRADIO_DBG_MSG, "%s\n", __func__);
	ret = sched_setscheduler(hw_priv->bh_thread, SCHED_FIFO, &param);
	if (ret)
		bh_printk(XRADIO_DBG_WARN, "%s sched_setscheduler failed(%d)\n",
			__func__, ret);

	PERF_INFO_GETTIME(&last_showtime);
	for (;;) {
		PERF_INFO_GETTIME(&bh_start_time);
		/* Check if devices can sleep, and set time to wait for interrupt. */
		if (!hw_priv->hw_bufs_used && !pending_tx &&
		    hw_priv->powersave_enabled && !hw_priv->device_can_sleep &&
		    !atomic_read(&hw_priv->recent_scan) &&
		    atomic_read(&hw_priv->bh_rx) == 0 &&
		    atomic_read(&hw_priv->bh_tx) == 0) {
			bh_printk(XRADIO_DBG_MSG, "Device idle, can sleep.\n");
			SYS_WARN(xradio_device_sleep(hw_priv));
			hw_priv->device_can_sleep = true;
			status = (HZ>>3);	/*1/8s = 125ms*/
		} else if (hw_priv->hw_bufs_used >=
			(hw_priv->wsm_caps.numInpChBufs - 1)) {
			/* don't wait too long if some frames to confirm
			 * and miss interrupt.*/
			status = (HZ>>4);	/*1/16s=62ms.*/
		} else {
			status = (HZ>>3);	/*1/8s = 125ms*/
		}


#if 0
		/* Dummy Read for SDIO retry mechanism */
		if (atomic_read(&hw_priv->bh_rx) == 0 &&
		    atomic_read(&hw_priv->bh_tx) == 0) {
			xradio_reg_read(hw_priv, HIF_CONFIG_REG_ID, &dummy, sizeof(dummy));
		}
#endif

#if 0
		/* If a packet has already been txed to the device then read the
		 * control register for a probable interrupt miss before going
		 * further to wait for interrupt; if the read length is non-zero
		 * then it means there is some data to be received */
		if (hw_priv->hw_bufs_used) {
			PERF_INFO_GETTIME(&sdio_reg_time);
			atomic_xchg(&hw_priv->bh_rx, 0);
			xradio_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
			++reg_read;
			++sdio_reg_cnt1;
			PERF_INFO_STAMP(&sdio_reg_time, &sdio_reg, 4);
			if (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK) {
				DBG_INT_ADD(fix_miss_cnt);
				rx = 1;
				goto data_proc;
			} else {
				++sdio_reg_cnt5;
			}
		}
#endif

#ifdef BH_COMINGRX_FORECAST
		coming_rx = xradio_comingrx_update(hw_priv);

		if (coming_rx) {
			PERF_INFO_GETTIME(&sdio_reg_time);
			atomic_xchg(&hw_priv->bh_rx, 0);
			xradio_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
			++reg_read;
			++sdio_reg_cnt1;
			PERF_INFO_STAMP(&sdio_reg_time, &sdio_reg, 4);
			if (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK) {
				DBG_INT_ADD(fix_miss_cnt);
				rx = 1;
				goto data_proc;
			} else {
				++sdio_reg_cnt5;
			}
		}
#endif

		PERF_INFO_GETTIME(&sdio_reg_time);
		/* Wait for Events in HZ/8 */
#ifdef BH_USE_SEMAPHORE
		rx = atomic_xchg(&hw_priv->bh_rx, 0);
		tx = atomic_xchg(&hw_priv->bh_tx, 0);
		suspend = pending_tx ? 0 : atomic_read(&hw_priv->bh_suspend);
		term = kthread_should_stop();
		if (!(rx || tx || coming_rx || term || suspend || hw_priv->bh_error)) {
			atomic_set(&hw_priv->bh_wk, 0);
			status = (long)(down_timeout(&hw_priv->bh_sem, status) != -ETIME);
			rx = atomic_xchg(&hw_priv->bh_rx, 0);
			tx = atomic_xchg(&hw_priv->bh_tx, 0);
			suspend = pending_tx ? 0 : atomic_read(&hw_priv->bh_suspend);
			term = kthread_should_stop();
		}
#else
		status = wait_event_interruptible_timeout(hw_priv->bh_wq, ({
			 rx = atomic_xchg(&hw_priv->bh_rx, 0);
			 tx = atomic_xchg(&hw_priv->bh_tx, 0);
			 term = kthread_should_stop();
			 suspend = pending_tx ? 0 : atomic_read(&hw_priv->bh_suspend);
			 (rx || tx || coming_rx || term || suspend || hw_priv->bh_error); }),
			 status);
#endif
		PERF_INFO_STAMP(&sdio_reg_time, &bh_wait, 0);

		/* 0--bh is going to be shut down */
		if (term) {
			bh_printk(XRADIO_DBG_MSG, "xradio_bh exit!\n");
			break;
		}
		/* 1--An fatal error occurs */
		if (status < 0 || hw_priv->bh_error) {
			bh_printk(XRADIO_DBG_ERROR, "bh_error=%d, status=%ld\n",
				  hw_priv->bh_error, status);
			hw_priv->bh_error = __LINE__;
			break;
		}

		/* 2--Wait for interrupt time out */
		if (!status) {
			DBG_INT_ADD(bh_idle);
			/* Check if miss interrupt. */
			PERF_INFO_GETTIME(&sdio_reg_time);
			xradio_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
			PERF_INFO_STAMP(&sdio_reg_time, &sdio_reg, 4);
			++reg_read;
			++sdio_reg_cnt2;
			if (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK) {
				bh_printk(XRADIO_DBG_WARN, "miss interrupt!\n");
				DBG_INT_ADD(int_miss_cnt);
				rx = 1;
				goto data_proc;
			} else {
				++sdio_reg_cnt5;
			}

			/* There are some frames to be confirmed. */
			if (hw_priv->hw_bufs_used) {
				long timeout = 0;
				bool pending = 0;
				bh_printk(XRADIO_DBG_NIY, "Need confirm:%d!\n",
					  hw_priv->hw_bufs_used);
				/* Check if frame transmission is timed out. */
				pending = xradio_query_txpkt_timeout(hw_priv, XRWL_ALL_IFS,
					       hw_priv->pending_frame_id, &timeout);
				/* There are some frames confirm time out. */
				if (pending && timeout < 0) {
					bh_printk(XRADIO_DBG_ERROR,
						  "query_txpkt_timeout:%ld!\n", timeout);
					hw_priv->bh_error = __LINE__;
					break;
				}
				rx = 1;	/* Go to check rx again. */
			} else if (!pending_tx) {
				if (hw_priv->powersave_enabled &&
					!hw_priv->device_can_sleep &&
					!atomic_read(&hw_priv->recent_scan)) {
					/* Device is idle, we can go to sleep. */
					bh_printk(XRADIO_DBG_MSG,
						  "Device idle(timeout), can sleep.\n");
					SYS_WARN(xradio_device_sleep(hw_priv));
					hw_priv->device_can_sleep = true;
				}
				PERF_INFO_STAMP(&bh_start_time, &bh_others, 0);
				continue;
			}
		/* 3--Host suspend request. */
		} else if (suspend) {
			bh_printk(XRADIO_DBG_NIY, "Host suspend request.\n");
			/* Check powersave setting again. */
			if (hw_priv->powersave_enabled) {
				bh_printk(XRADIO_DBG_MSG,
					 "Device idle(host suspend), can sleep.\n");
				SYS_WARN(xradio_device_sleep(hw_priv));
				hw_priv->device_can_sleep = true;
			}

			/* bh thread go to suspend. */
			atomic_set(&hw_priv->bh_suspend, XRADIO_BH_SUSPENDED);
			wake_up(&hw_priv->bh_evt_wq);
#ifdef BH_USE_SEMAPHORE
			do {
				status = down_timeout(&hw_priv->bh_sem, HZ/10);
				term = kthread_should_stop();
			} while (XRADIO_BH_RESUME != atomic_read(&hw_priv->bh_suspend) &&
				     !term && !hw_priv->bh_error);
			if (XRADIO_BH_RESUME != atomic_read(&hw_priv->bh_suspend))
				status = -1;
			else
				status = 0;
#else
			status = wait_event_interruptible(hw_priv->bh_wq, ({
				term = kthread_should_stop();
				(XRADIO_BH_RESUME == atomic_read(&hw_priv->bh_suspend) ||
				term || hw_priv->bh_error); }));
#endif
			if (hw_priv->bh_error) {
				bh_printk(XRADIO_DBG_ERROR, "bh error during bh suspend.\n");
				break;
			} else if (term) {
				bh_printk(XRADIO_DBG_WARN, "bh exit during bh suspend.\n");
				break;
			} else if (status < 0) {
				bh_printk(XRADIO_DBG_ERROR,
					  "Failed to wait for resume: %ld.\n", status);
				hw_priv->bh_error = __LINE__;
				break;
			}
			bh_printk(XRADIO_DBG_NIY, "Host resume.\n");
			atomic_set(&hw_priv->bh_suspend, XRADIO_BH_RESUMED);
			wake_up(&hw_priv->bh_evt_wq);
			atomic_add(1, &hw_priv->bh_rx);
			continue;
		}
		/* query stuck frames in firmware. */
		if (atomic_xchg(&hw_priv->query_cnt, 0)) {
			if (schedule_work(&hw_priv->query_work) <= 0)
				atomic_add(1, &hw_priv->query_cnt);
		}

#if 0
		/* If a packet has already been txed to the device then read the
		 * control register for a probable interrupt miss before going
		 * further to wait for interrupt; if the read length is non-zero
		 * then it means there is some data to be received */
		if ((hw_priv->wsm_caps.numInpChBufs -
			hw_priv->hw_bufs_used) <= 1 && !reg_read) {
			PERF_INFO_GETTIME(&sdio_reg_time);
			atomic_xchg(&hw_priv->bh_rx, 0);
			xradio_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
			++sdio_reg_cnt1;
			PERF_INFO_STAMP(&sdio_reg_time, &sdio_reg, 4);
			if (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK) {
				DBG_INT_ADD(fix_miss_cnt);
				rx = 1;
				goto data_proc;
			} else {
				++sdio_reg_cnt5;
			}
		}
#endif

		/* 4--Rx & Tx process. */
data_proc:
		term = kthread_should_stop();
		if (hw_priv->bh_error || term)
			break;
		/*pre-txrx*/
		tx_bursted = 0;

		rx += atomic_xchg(&hw_priv->bh_rx, 0);
		if (rx) {
			size_t alloc_len;
			u8 *data;
			u8 flags;

			/* Check ctrl_reg again. */
			if (!(ctrl_reg & HIF_CTRL_NEXT_LEN_MASK)) {
				PERF_INFO_GETTIME(&sdio_reg_time);
				if (SYS_WARN(xradio_bh_read_ctrl_reg(hw_priv, &ctrl_reg))) {
					hw_priv->bh_error = __LINE__;
					break;
				}
				++reg_read;
				++sdio_reg_cnt3;
				PERF_INFO_STAMP(&sdio_reg_time, &sdio_reg, 4);
			}
			PERF_INFO_STAMP(&bh_start_time, &bh_others, 0);

			/* read_len=ctrl_reg*2.*/
			read_len = (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK)<<1;
			if (!read_len) {
				++sdio_reg_cnt6;
				rx = 0;
				goto tx;
			}

rx:
			reg_read = 0;
			flags = 0;
			PERF_INFO_GETTIME(&rx_start_time1);
			if (SYS_WARN((read_len < sizeof(struct wsm_hdr)) ||
				     (read_len > EFFECTIVE_BUF_SIZE))) {
				bh_printk(XRADIO_DBG_ERROR, "Invalid read len: %zu", read_len);
				hw_priv->bh_error = __LINE__;
				break;
			}
#if BH_PROC_RX
			if (unlikely(atomic_read(&hw_priv->proc.rx_queued) >=
				((ITEM_RESERVED*PROC_POOL_NUM) - XRWL_MAX_QUEUE_SZ - 1))) {
				bh_printk(XRADIO_DBG_WARN,
					"Too many rx packets, proc cannot handle in time!\n");
				msleep(10);
				goto tx; /* too many rx packets to be handled, do tx first*/
			}
#endif

			/* Add SIZE of PIGGYBACK reg (CONTROL Reg)
			 * to the NEXT Message length + 2 Bytes for SKB */
			read_len = read_len + 2;
			alloc_len = hw_priv->sbus_ops->align_size(hw_priv->sbus_priv,
				      read_len);
			/* Check if not exceeding XRADIO capabilities */
			if (WARN_ON_ONCE(alloc_len > EFFECTIVE_BUF_SIZE)) {
				bh_printk(XRADIO_DBG_ERROR,
					"Read aligned len: %zu\n", alloc_len);
			} else {
				bh_printk(XRADIO_DBG_MSG,
					"Rx len=%zu, aligned len=%zu\n",
					read_len, alloc_len);
			}

			/* Get skb buffer. */
			skb_rx = xradio_get_skb(hw_priv, alloc_len, &flags);
			if (SYS_WARN(!skb_rx)) {
				bh_printk(XRADIO_DBG_ERROR, "xradio_get_skb failed.\n");
				hw_priv->bh_error = __LINE__;
				break;
			}
			skb_trim(skb_rx, 0);
			skb_put(skb_rx, read_len);
			data = skb_rx->data;
			if (SYS_WARN(!data)) {
				bh_printk(XRADIO_DBG_ERROR, "skb data is NULL.\n");
				hw_priv->bh_error = __LINE__;
				break;
			}
			PERF_INFO_STAMP(&rx_start_time1, &prepare_rx, alloc_len);

			/* Read data from device. */
			PERF_INFO_GETTIME(&rx_start_time2);
			if (SYS_WARN(xradio_data_read(hw_priv, data, alloc_len))) {
				hw_priv->bh_error = __LINE__;
				break;
			}
			DBG_INT_ADD(rx_total_cnt);

			PERF_INFO_STAMP_UPDATE(&rx_start_time2, &sdio_read, alloc_len);

			/* Piggyback */
			ctrl_reg = __le16_to_cpu(((__le16 *)data)[(alloc_len >> 1) - 1]);

			/* check wsm length. */
			wsm = (struct wsm_hdr *)data;
			wsm_len = __le32_to_cpu(wsm->len);
			if (SYS_WARN(wsm_len > read_len)) {
				bh_printk(XRADIO_DBG_ERROR, "wsm_id=0x%04x, wsm_len=%zu.\n",
						(__le32_to_cpu(wsm->id) & 0xFFF), wsm_len);
				hw_priv->bh_error = __LINE__;
				break;
			}

			/* dump rx data. */
#if defined(CONFIG_XRADIO_DEBUG)
			if (unlikely(hw_priv->wsm_enable_wsm_dumps)) {
				u16 msgid, ifid;
				u16 *p = (u16 *) data;
				msgid = (*(p + 1)) & WSM_MSG_ID_MASK;
				ifid = (*(p + 1)) >> 6;
				ifid &= 0xF;
				bh_printk(XRADIO_DBG_ALWY,
					  "[DUMP] msgid 0x%.4X ifid %d len %d\n",
					  msgid, ifid, *p);
				print_hex_dump_bytes("<-- ", DUMP_PREFIX_NONE, data,
				   min(wsm_len, (size_t)hw_priv->wsm_dump_max_size));
			}
#endif /* CONFIG_XRADIO_DEBUG */

			/* extract wsm id and seq. */
			wsm_id = __le32_to_cpu(wsm->id) & 0xFFF;
			wsm_seq = (__le32_to_cpu(wsm->id) >> 13) & 7;
			/* for multi-rx indication, there two case.*/
			if (ROUND4(wsm_len) < read_len - 2)
				skb_trim(skb_rx, read_len - 2);
			else
				skb_trim(skb_rx, wsm_len);

			/* process exceptions. */
			if (unlikely(wsm_id == 0x0800)) {
				bh_printk(XRADIO_DBG_ERROR, "firmware exception!\n");
				wsm_handle_exception(hw_priv, &data[sizeof(*wsm)],
						     wsm_len - sizeof(*wsm));
				hw_priv->bh_error = __LINE__;
				break;
			} else if (likely(!rx_resync)) {
				if (SYS_WARN(wsm_seq != hw_priv->wsm_rx_seq)) {
					bh_printk(XRADIO_DBG_ERROR, "wsm_seq=%d.\n", wsm_seq);
					hw_priv->bh_error = __LINE__;
					break;
				}
			}
			hw_priv->wsm_rx_seq = (wsm_seq + 1) & 7;
			rx_resync = 0;
#if (DGB_XRADIO_HWT)
			rx_resync = 1;	/*0 -> 1, HWT test, should not check this.*/
#endif

			/* Process tx frames confirm. */
			if (wsm_id & 0x0400) {
				int rc = 0;
				int if_id = 0;
				u32 *cfm = (u32 *)(wsm + 1);
				wsm_id &= ~WSM_TX_LINK_ID(WSM_TX_LINK_ID_MAX);
				if (wsm_id == 0x041E) {
					int cfm_cnt = *cfm;
					struct wsm_tx_confirm *tx_cfm =
						(struct wsm_tx_confirm *)(cfm + 1);
					bh_printk(XRADIO_DBG_NIY, "multi-cfm %d.\n", cfm_cnt);

					rc = wsm_release_tx_buffer(hw_priv, cfm_cnt);
					do {
						if_id = xradio_queue_get_if_id(tx_cfm->packetID);
						wsm_release_vif_tx_buffer(hw_priv, if_id, 1);
						tx_cfm = (struct wsm_tx_confirm *)((u8 *)tx_cfm +
							offsetof(struct wsm_tx_confirm, link_id));
					} while (--cfm_cnt);
				} else {
					rc = wsm_release_tx_buffer(hw_priv, 1);
					if (wsm_id == 0x0404) {
						if_id = xradio_queue_get_if_id(*cfm);
						wsm_release_vif_tx_buffer(hw_priv, if_id, 1);
					} else {
#if BH_PROC_RX
						flags |= ITEM_F_CMDCFM;
#endif
					}
					bh_printk(XRADIO_DBG_NIY, "cfm id=0x%04x.\n", wsm_id);
				}
				if (SYS_WARN(rc < 0)) {
					bh_printk(XRADIO_DBG_ERROR, "tx buffer < 0.\n");
					hw_priv->bh_error = __LINE__;
					break;
				} else if (rc > 0) {
					tx = 1;
					xradio_proc_wakeup(hw_priv);
				}
			}

			/* WSM processing frames. */
#if BH_PROC_RX
			if (SYS_WARN(xradio_bh_put(hw_priv, &skb_rx, flags))) {
				bh_printk(XRADIO_DBG_ERROR, "xradio_bh_put failed.\n");
				hw_priv->bh_error = __LINE__;
				break;
			}
#else
			if (SYS_WARN(wsm_handle_rx(hw_priv, flags, &skb_rx))) {
				bh_printk(XRADIO_DBG_ERROR, "wsm_handle_rx failed.\n");
				hw_priv->bh_error = __LINE__;
				break;
			}
			/* Reclaim the SKB buffer */
			if (skb_rx) {
				if (xradio_put_resv_skb(hw_priv, skb_rx, flags))
					xradio_put_skb(hw_priv, skb_rx);
				skb_rx = NULL;
			}
#endif
			PERF_INFO_STAMP(&rx_start_time2, &handle_rx, wsm_len);
			PERF_INFO_STAMP(&rx_start_time1, &data_rx, wsm_len);

			/* Check if rx burst */
			read_len = (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK)<<1;
			if (!read_len) {
				rx = 0;
				rx_burst = 0;
				goto tx;
			} else if (rx_burst) {
				xradio_debug_rx_burst(hw_priv);
				--rx_burst;
				goto rx;
			}
		} else {
			PERF_INFO_STAMP(&bh_start_time, &bh_others, 0);
		}

tx:
		SYS_BUG(hw_priv->hw_bufs_used > hw_priv->wsm_caps.numInpChBufs);
		tx += pending_tx + atomic_xchg(&hw_priv->bh_tx, 0);
#if BH_PROC_TX
		tx += atomic_read(&hw_priv->proc.tx_queued);
#endif
		pending_tx = 0;
		tx_burst = hw_priv->wsm_caps.numInpChBufs - hw_priv->hw_bufs_used;
		tx_allowed = tx_burst > 0;
		if (tx && tx_allowed) {
			int ret;
			u8 *data;
			size_t tx_len;
#if 0
			int  num = 0, i;
#endif

			PERF_INFO_GETTIME(&tx_start_time1);
			/* Wake up the devices */
			if (hw_priv->device_can_sleep) {
				ret = xradio_device_wakeup(hw_priv, &ctrl_reg);
				if (SYS_WARN(ret < 0)) {
					hw_priv->bh_error = __LINE__;
					break;
				} else if (ret == 1) {
					hw_priv->device_can_sleep = false;
				} else if (ret > 1) {
					rx = 1;
					ctrl_reg = (ret & HIF_CTRL_NEXT_LEN_MASK);
					goto data_proc;
				} else {	/* Wait for "awake" interrupt */
					pending_tx = tx;
					continue;
				}
			}
			/* Increase Tx buffer */
			wsm_alloc_tx_buffer(hw_priv);

#if (DGB_XRADIO_HWT)
			/*hardware test.*/
			ret = get_hwt_hif_tx(hw_priv, &data, &tx_len,
					     &tx_burst, &vif_selected);
			if (ret <= 0)
#endif /*DGB_XRADIO_HWT*/

#if BH_PROC_TX
				/* Get data to send and send it. */
				ret = xradio_bh_get(hw_priv, &data, &tx_len, &tx_burst,
						 &vif_selected);
#else
				/* Get data to send and send it. */
				ret = wsm_get_tx(hw_priv, &data, &tx_len, &tx_burst,
						 &vif_selected);
#endif
			if (ret <= 0) {
				if (hw_priv->hw_bufs_used >= hw_priv->wsm_caps.numInpChBufs)
					++tx_limit_cnt3;
#if BH_PROC_TX
				if (list_empty(&hw_priv->proc.bh_tx))
					++tx_limit_cnt4;
#endif
				wsm_release_tx_buffer(hw_priv, 1);
				if (SYS_WARN(ret < 0)) {
					bh_printk(XRADIO_DBG_ERROR, "get tx packet=%d.\n", ret);
					hw_priv->bh_error = __LINE__;
					break;
				}
				tx = 0;
				DBG_INT_ADD(tx_limit);
				PERF_INFO_STAMP(&tx_start_time1, &prepare_tx, 0);
			} else {
				wsm = (struct wsm_hdr *)data;
				SYS_BUG(tx_len < sizeof(*wsm));
				if (SYS_BUG(__le32_to_cpu(wsm->len) != tx_len)) {
					bh_printk(XRADIO_DBG_ERROR, "%s wsmlen=%u, tx_len=%zu.\n",
						__func__, __le32_to_cpu(wsm->len), tx_len);
				}

				/* Continue to send next data if have any. */
				atomic_add(1, &hw_priv->bh_tx);

				if (tx_len <= 8)
					tx_len = 16;
				/* Align tx length and check it. */
				/* HACK!!! Platform limitation.
				 * It is also supported by upper layer:
				 * there is always enough space at the end of the buffer. */
				tx_len = hw_priv->sbus_ops->align_size(hw_priv->sbus_priv,
								       tx_len);
				/* Check if not exceeding XRADIO capabilities */
				if (tx_len > EFFECTIVE_BUF_SIZE) {
					bh_printk(XRADIO_DBG_WARN,
						  "Write aligned len: %zu\n", tx_len);
				} else {
					bh_printk(XRADIO_DBG_MSG,
						"Tx len=%d, aligned len=%zu\n",
						wsm->len, tx_len);
				}

				/* Make sequence number. */
				wsm->id &= __cpu_to_le32(~WSM_TX_SEQ(WSM_TX_SEQ_MAX));
				wsm->id |= cpu_to_le32(WSM_TX_SEQ(hw_priv->wsm_tx_seq));

				if ((wsm->id & WSM_MSG_ID_MASK) != 0x0004)
					hw_priv->wsm_cmd.seq = cpu_to_le32(WSM_TX_SEQ(hw_priv->wsm_tx_seq));

				PERF_INFO_STAMP(&tx_start_time1, &prepare_tx, tx_len);
				PERF_INFO_GETTIME(&tx_start_time2);
				/* Send the data to devices. */
				if (SYS_WARN(xradio_data_write(hw_priv, data, tx_len))) {
					wsm_release_tx_buffer(hw_priv, 1);
					bh_printk(XRADIO_DBG_ERROR, "xradio_data_write failed\n");
					hw_priv->bh_error = __LINE__;
					break;
				}
				DBG_INT_ADD(tx_total_cnt);
				PERF_INFO_STAMP(&tx_start_time2, &sdio_write, tx_len);

#if defined(CONFIG_XRADIO_DEBUG)
				if (unlikely(hw_priv->wsm_enable_wsm_dumps)) {
					u16 msgid, ifid;
					u16 *p = (u16 *) data;
					msgid = (*(p + 1)) & 0x3F;
					ifid = (*(p + 1)) >> 6;
					ifid &= 0xF;
					if (msgid == 0x0006) {
						bh_printk(XRADIO_DBG_ALWY,
							  "[DUMP] >>> msgid 0x%.4X ifid %d" \
							  "len %d MIB 0x%.4X\n",
							  msgid, ifid, *p, *(p + 2));
					} else {
						bh_printk(XRADIO_DBG_ALWY,
							  "[DUMP] >>> msgid 0x%.4X ifid %d " \
							  "len %d\n", msgid, ifid, *p);
					}
					print_hex_dump_bytes("--> ", DUMP_PREFIX_NONE, data,
							     min(__le32_to_cpu(wsm->len),
							     hw_priv->wsm_dump_max_size));
				}
#endif /* CONFIG_XRADIO_DEBUG */

				/* Process after data have sent. */
				if (vif_selected != -1) {
					hw_priv->hw_bufs_used_vif[vif_selected]++;
				}
				wsm_txed(hw_priv, data);
				hw_priv->wsm_tx_seq = (hw_priv->wsm_tx_seq + 1) & WSM_TX_SEQ_MAX;

				PERF_INFO_STAMP(&tx_start_time1, &data_tx, wsm->len);

				/* Check for burst. */
#if !BH_PROC_TX
				/*if not proc tx, just look to burst limit.*/
				tx_burst = 2;
#endif
				if (tx_burst > 1 && tx_bursted < tx_burst_limit &&
					(hw_priv->wsm_caps.numInpChBufs -
					hw_priv->hw_bufs_used) > 1) {
					xradio_debug_tx_burst(hw_priv);
					if (rx_burst < tx_burst_limit)
						++rx_burst;
					++tx_bursted;
					goto tx;
				} else {
					if (tx_bursted >= tx_burst_limit)
						++tx_limit_cnt5;
					if (tx_burst <= 1)
						++tx_limit_cnt6;
				}
			}
		} else {
			/*no tx or not allow to tx, pending it.*/
			pending_tx = tx;
			if (!tx)
				++tx_limit_cnt1;
			if (!tx_allowed)
				++tx_limit_cnt2;
		}

		PERF_INFO_GETTIME(&bh_start_time);
		/*Check if there are frames to be rx. */
		if (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK) {
			DBG_INT_ADD(next_rx_cnt);
			rx = 1;
			goto data_proc;
		}
		/*if no rx, we check tx again.*/
		if (tx + atomic_xchg(&hw_priv->bh_tx, 0)) {
			if (hw_priv->hw_bufs_used < (hw_priv->wsm_caps.numInpChBufs - 1)) {
				tx = 1;
				goto data_proc;
			} else { /*if no tx buffer, we check rx reg.*/
				PERF_INFO_GETTIME(&sdio_reg_time);
				atomic_xchg(&hw_priv->bh_rx, 0);
				xradio_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
				++reg_read;
				++sdio_reg_cnt1;
				PERF_INFO_STAMP(&sdio_reg_time, &sdio_reg, 4);
				if (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK) {
					DBG_INT_ADD(fix_miss_cnt);
					rx = 1;
					goto data_proc;
				} else {
					++sdio_reg_cnt5;
				}
			}
			if (hw_priv->hw_bufs_used < hw_priv->wsm_caps.numInpChBufs) {
				tx = 1;
				goto data_proc;
			}
		}


#if 0
		/*One more to check rx if reg has not be read. */
		if (!reg_read && hw_priv->hw_bufs_used >=
			(hw_priv->wsm_caps.numInpChBufs - 1)) {
			atomic_xchg(&hw_priv->bh_rx, 0);
			PERF_INFO_GETTIME(&sdio_reg_time);
			xradio_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
			++reg_read;
			++sdio_reg_cnt4;
			PERF_INFO_STAMP(&sdio_reg_time, &sdio_reg, 4);
			if (ctrl_reg & HIF_CTRL_NEXT_LEN_MASK) {
				DBG_INT_ADD(fix_miss_cnt);
				rx = 1;
				goto data_proc;
			} else {
				++sdio_reg_cnt5;
				rx = 0;
			}
		}
#endif
		DBG_INT_ADD(tx_rx_idle);
		PERF_INFO_STAMP(&bh_start_time, &bh_others, 0);

#if 0
		if (hw_priv->wsm_caps.numInpChBufs - hw_priv->hw_bufs_used > 1 &&
		    atomic_read(&hw_priv->bh_tx) == 0 && pending_tx == 0 &&
			!tx && atomic_read(&hw_priv->tx_lock) == 0) {
			int i = 0;
			for (i = 0; i < 4; ++i) {
				if (hw_priv->tx_queue[i].num_queued - hw_priv->tx_queue[i].num_pending) {
					bh_printk(XRADIO_DBG_NIY, "queued=%d, pending=%d, buf=%d.\n",
					hw_priv->tx_queue[i].num_queued,
					hw_priv->tx_queue[i].num_pending,
					hw_priv->wsm_caps.numInpChBufs - hw_priv->hw_bufs_used);
					tx = 1;
					xradio_proc_wakeup(hw_priv);
					goto data_proc;
				}
			}
		}
#endif
	}			/* for (;;) */

	/* Free the SKB buffer when exit. */
	if (skb_rx) {
		dev_kfree_skb(skb_rx);
		skb_rx = NULL;
	}

	/* If BH Error, handle it. */
	if (!term) {
		bh_printk(XRADIO_DBG_ERROR, "Fatal error, exitting code=%d.\n",
			  hw_priv->bh_error);

#ifdef SUPPORT_FW_DBG_INF
		xradio_fw_dbg_dump_in_direct_mode(hw_priv);
#endif

#ifdef HW_ERROR_WIFI_RESET
		/* notify upper layer to restart wifi.
		 * don't do it in debug version. */
#ifdef CONFIG_XRADIO_ETF
		/* we should restart manually in etf mode.*/
		if (!etf_is_connect() &&
			XRADIO_BH_RESUMED == atomic_read(&hw_priv->bh_suspend)) {
			wsm_upper_restart(hw_priv);
		}
#else
		if (XRADIO_BH_RESUMED == atomic_read(&hw_priv->bh_suspend))
			wsm_upper_restart(hw_priv);
#endif
#endif
		/* TODO: schedule_work(recovery) */
#ifndef HAS_PUT_TASK_STRUCT
		/* The only reason of having this stupid code here is
		 * that __put_task_struct is not exported by kernel. */
		for (;;) {
#ifdef BH_USE_SEMAPHORE
			status = down_timeout(&hw_priv->bh_sem, HZ/10);
			term = kthread_should_stop();
			status = 0;
#else
			int status = wait_event_interruptible(hw_priv->bh_wq, ({
				     term = kthread_should_stop();
				     (term); }));
#endif
			if (status || term)
				break;
		}
#endif
	}
	atomic_add(1, &hw_priv->bh_term);	/*debug info, show bh status.*/
	return 0;
}
