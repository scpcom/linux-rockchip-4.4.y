/*
 *
 *  Copyright (C) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _PFE_HIF_LIB_H_
#define _PFE_HIF_LIB_H_

#include <linux/version.h>

#include "pfe_hif.h"

#ifdef HIF_LIB_DEBUG
#define dbg_print_info( fmt, args...) \
	printk(KERN_INFO fmt, ##args)
#else
#define dbg_print_info( fmt, args...) 
#endif

#define HIF_CL_REQ_TIMEOUT	10

#if defined(CONFIG_COMCERTO_ZONE_DMA_NCNB)
#define GFP_DMA_PFE (GFP_DMA_NCNB | __GFP_NOWARN)
#else
#define GFP_DMA_PFE 0
#endif

enum {
	REQUEST_CL_REGISTER = 0,
	REQUEST_CL_UNREGISTER,
	HIF_REQUEST_MAX
};

enum {
	EVENT_HIGH_RX_WM = 0,     /* Event to indicate that client rx queue is reached water mark level */
	EVENT_RX_PKT_IND,	/* Event to indicate that, packet recieved for client */
	EVENT_TXDONE_IND,	/* Event to indicate that, packet tx done for client */
	HIF_EVENT_MAX
};

/*structure to store client queue info */

/*structure to store client queue info */
struct hif_client_rx_queue {
	struct rx_queue_desc *base;
	u32	size;
	u32	read_idx;
	u32	write_idx;
};

struct hif_client_tx_queue {
	struct tx_queue_desc *base;
	u32	size;
	u32	read_idx;
	u32	write_idx;
	u32	tx_pending;
	unsigned long jiffies_last_packet;
	u32	nocpy_flag;
	u32	prev_tmu_tx_pkts;
	u32	done_tmu_tx_pkts;
	struct hif_tso_hdr_nocpy *cur_tso_hdr;
	u32	cur_tso_hdr_p;
	int 	tso_buf_cnt;
};

struct hif_client_s
{
	int	id;
	int	tx_qn;
	int	rx_qn;
	void	*rx_qbase;
	void	*tx_qbase;
	/* FIXME tx/rx_qsize fields  can be removed after per queue depth is supported*/
	int	tx_qsize;
	int	rx_qsize;
	int	cpu_id;
	int	user_cpu_id;

//	spinlock_t 	rx_lock;
	struct hif_client_tx_queue tx_q[HIF_CLIENT_QUEUES_MAX];
	struct hif_client_rx_queue rx_q[HIF_CLIENT_QUEUES_MAX];
	int (*event_handler)(void *priv, int event, int data);
	unsigned long queue_mask[HIF_EVENT_MAX];
	struct pfe *pfe;
	void *priv;
};


/* Client specific shared memory 
 * It contains number of Rx/Tx queues, base addresses and queue sizes */
struct hif_client_shm {
	u32 ctrl; /*0-7: number of Rx queues, 8-15: number of tx queues */
	u32 rx_qbase; /*Rx queue base address */
	u32 rx_qsize; /*each Rx queue size, all Rx queues are of same size */
	u32 tx_qbase; /* Tx queue base address */
	u32 tx_qsize; /*each Tx queue size, all Tx queues are of same size */
};

/*Client shared memory ctrl bit description */
#define CLIENT_CTRL_RX_Q_CNT_OFST	0
#define CLIENT_CTRL_TX_Q_CNT_OFST	8
#define CLIENT_CTRL_RX_Q_CNT(ctrl)	(((ctrl) >> CLIENT_CTRL_RX_Q_CNT_OFST) & 0xFF)
#define CLIENT_CTRL_TX_Q_CNT(ctrl)	(((ctrl) >> CLIENT_CTRL_TX_Q_CNT_OFST) & 0xFF)



/*Shared memory used to communicate between HIF driver and host/client drivers
 * Before starting the hif driver rx_buf_pool ans rx_buf_pool_cnt should be 
 * initialized with host buffers and buffers count in the pool. 
 * rx_buf_pool_cnt should be >= HIF_RX_DESC_NT.
 * 
 */
struct hif_shm {
	u32 rx_buf_pool_cnt; /*Number of rx buffers available*/
	void *rx_buf_pool[HIF_RX_DESC_NT];/*Rx buffers required to initialize HIF rx descriptors */
	unsigned long gClient_status[2]; /*Global client status bit mask */
	u32 hif_qfull; /*TODO Client-id that caused for the TMU3 queue stop */
	u32 hif_qresume; /*TODO */
	struct hif_client_shm client[HIF_CLIENTS_MAX]; /* Client specific shared memory */
};


#define CL_DESC_OWN			(1 << 31) /* This sets owner ship to HIF driver */
#define CL_DESC_LAST		(1 << 30) /* This indicates last packet for multi buffers handling */
#define CL_DESC_FIRST		(1 << 29) /* This indicates first packet for multi buffers handling */
#define CL_DESC_BUF_LEN(x)		((x) & 0xFFFF)
#define CL_DESC_FLAGS(x)		(((x) & 0xF) << 16)
#define CL_DESC_GET_FLAGS(x)		(((x) >> 16) & 0xF)

struct rx_queue_desc {
	void *data;
	u32	ctrl; /*0-15bit len, 16-20bit flags, 31bit owner*/
	u32	client_ctrl;
};

struct tx_queue_desc {
	void *data;
	u32	ctrl; /*0-15bit len, 16-20bit flags, 31bit owner*/
};

/* HIF Rx is not working properly for 2-byte aligned buffers and   
 * ip_header should be 4byte aligned for better iperformance.
 * "ip_header = 64 + 6(hif_header) + 14 (MAC Header)" will be 4byte aligned. 
 */
#define PFE_PKT_HEADER_SZ	sizeof(struct hif_hdr)
#define PFE_BUF_SIZE		2048	/* must be big enough for headroom, pkt size and skb shared info */
#define PFE_PKT_HEADROOM	128
#define SKB_SHARED_INFO_SIZE	256    /* At least sizeof(struct skb_shared_info) bytes */

//#define PFE_PKT_SIZE		1544	/* maximum ethernet packet size */
#define PFE_PKT_SIZE		(PFE_BUF_SIZE - PFE_PKT_HEADROOM - SKB_SHARED_INFO_SIZE)	/* maximum ethernet packet size after reassembly offload*/
#define MAX_L2_HDR_SIZE		14	/* Not correct for VLAN/PPPoE */
#define MAX_L3_HDR_SIZE		20	/* Not correct for IPv6 */
#define MAX_L4_HDR_SIZE		60	/* TCP with maximum options */
#define MAX_HDR_SIZE		(MAX_L2_HDR_SIZE + MAX_L3_HDR_SIZE + MAX_L4_HDR_SIZE)
#define MAX_WIFI_HDR_SIZE 	(MAX_L2_HDR_SIZE + MAX_L3_HDR_SIZE + 6)
#define MAX_PFE_PKT_SIZE	16380UL		/* Used in page mode to clamp packet size to the maximum supported by the hif hw interface (<16KiB) */

extern unsigned int pfe_pkt_size;
extern unsigned int pfe_pkt_headroom;
extern unsigned int page_mode;
extern unsigned int lro_mode;
extern unsigned int tx_qos;
extern unsigned int emac_txq_cnt;

int pfe_hif_lib_init(struct pfe *pfe);
void pfe_hif_lib_exit(struct pfe *pfe);
int hif_lib_client_register(struct hif_client_s *client);
int hif_lib_client_unregister(struct  hif_client_s *client);
void __hif_lib_xmit_tso_hdr(struct hif_client_s *client, unsigned int qno, u32 client_ctrl, unsigned int ip_off, unsigned int ip_id, unsigned int ip_len, unsigned int tcp_off, unsigned int tcp_seq);
void __hif_lib_xmit_pkt(struct hif_client_s *client, unsigned int qno, void *data, unsigned int len, u32 client_ctrl, unsigned int flags, void *client_data);
int hif_lib_xmit_pkt(struct hif_client_s *client, unsigned int qno, void *data, unsigned int len, u32 client_ctrl, void *client_data);
void hif_lib_indicate_client(int cl_id, int event, int data);
int hif_lib_event_handler_start( struct hif_client_s *client, int event, int data );
int hif_lib_tmu_queue_start( struct hif_client_s *client, int qno );
int hif_lib_tmu_queue_stop( struct hif_client_s *client, int qno );
void *hif_lib_tx_get_next_complete(struct hif_client_s *client, int qno, unsigned int *flags, int count);
void *hif_lib_receive_pkt(struct hif_client_s *client, int qno, int *len, int *ofst, unsigned int *rx_ctrl, unsigned int *desc_ctrl, void **priv_data);
void hif_lib_update_credit(struct hif_client_s *client, unsigned int qno);
void __hif_lib_update_credit(struct hif_client_s *client, unsigned int queue);
void hif_lib_set_rx_cpu_affinity(struct hif_client_s *client, int cpu_id);
void hif_lib_set_tx_queue_nocpy(struct hif_client_s *client, int qno, int enable);
static inline int hif_lib_tx_avail(struct hif_client_s *client, unsigned int qno)
{
	struct hif_client_tx_queue *queue = &client->tx_q[qno];

	return (queue->size - queue->tx_pending);
}

static inline int hif_lib_get_tx_wrIndex(struct hif_client_s *client, unsigned int qno)
{
	struct hif_client_tx_queue *queue = &client->tx_q[qno];

	return queue->write_idx;
}


static inline int hif_lib_tx_pending(struct hif_client_s *client, unsigned int qno)
{
	struct hif_client_tx_queue *queue = &client->tx_q[qno];

	return queue->tx_pending;
}

#define hif_lib_tx_credit_avail(pfe, id, qno) pfe->tmu_credit.tx_credit[id][qno]
#define hif_lib_tx_credit_max(pfe, id, qno) pfe->tmu_credit.tx_credit_max[id][qno]
#define hif_lib_tx_credit_use(pfe, id, qno, credit) do {if (tx_qos) {pfe->tmu_credit.tx_credit[id][qno]-= credit; pfe->tmu_credit.tx_packets[id][qno]+=credit;}} while (0)

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
#define build_skb(buf_addr, b) alloc_skb_header(PFE_BUF_SIZE, buf_addr, GFP_ATOMIC)
#endif

#endif /* _PFE_HIF_LIB_H_ */
