/*
 * Copyright (c) 2012-2018 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

#ifndef REMOVE_PKT_LOG
#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif
#ifndef __KERNEL__
#define __KERNEL__
#endif
/*
 * Linux specific implementation of Pktlogs for 802.11ac
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <pktlog_ac_i.h>
#include <pktlog_ac_fmt.h>
#include <pktlog_ac.h>
#include "i_vos_diag_core_log.h"
#include "vos_diag_core_log.h"
#include "aniGlobal.h"

#define PKTLOG_TAG		"ATH_PKTLOG"
#define PKTLOG_DEVNAME_SIZE	32
#define MAX_WLANDEV		1

#ifdef MULTI_IF_NAME
#define PKTLOG_PROC_DIR		"ath_pktlog" MULTI_IF_NAME
#else
#define PKTLOG_PROC_DIR		"ath_pktlog"
#endif

/* Permissions for creating proc entries */
#define PKTLOG_PROC_PERM	0444
#define PKTLOG_PROCSYS_DIR_PERM	0555
#define PKTLOG_PROCSYS_PERM	0644

#ifndef __MOD_INC_USE_COUNT
#define PKTLOG_MOD_INC_USE_COUNT				\
	if (!try_module_get(THIS_MODULE)) {			\
		printk(KERN_WARNING "try_module_get failed\n");	\
	}

#define PKTLOG_MOD_DEC_USE_COUNT	module_put(THIS_MODULE)
#else
#define PKTLOG_MOD_INC_USE_COUNT	MOD_INC_USE_COUNT
#define PKTLOG_MOD_DEC_USE_COUNT	MOD_DEC_USE_COUNT
#endif

static struct ath_pktlog_info *g_pktlog_info;

static struct proc_dir_entry *g_pktlog_pde;

static DEFINE_MUTEX(proc_mutex);

static int pktlog_attach(struct ol_softc *sc);
static void pktlog_detach(struct ol_softc *sc);
static int pktlog_open(struct inode *i, struct file *f);
static int pktlog_release(struct inode *i, struct file *f);
static ssize_t pktlog_read(struct file *file, char *buf, size_t nbytes,
			   loff_t * ppos);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
static struct proc_ops pktlog_fops = {
	proc_open:pktlog_open,
	proc_release:pktlog_release,
	proc_read:pktlog_read,
};
#else
static struct file_operations pktlog_fops = {
	open:pktlog_open,
	release:pktlog_release,
	read:pktlog_read,
};
#endif

/*
 * Linux implementation of helper functions
 */

static struct ol_pktlog_dev_t *get_pl_handle(struct ol_softc *scn)
{
	if (!scn || !scn->pdev_txrx_handle)
		return NULL;
	return scn->pdev_txrx_handle->pl_dev;
}

void ol_pl_set_name(ol_softc_handle scn, net_device_handle dev)
{
	if (scn && scn->pdev_txrx_handle->pl_dev && dev)
		scn->pdev_txrx_handle->pl_dev->name = dev->name;
}

void pktlog_disable_adapter_logging(struct ol_softc *scn)
{
	struct ol_pktlog_dev_t *pl_dev = get_pl_handle(scn);
	if (pl_dev) pl_dev->pl_info->log_state = 0;
}

int pktlog_alloc_buf(struct ol_softc *scn)
{
	u_int32_t page_cnt;
	unsigned long vaddr;
	struct page *vpg;
	struct ath_pktlog_info *pl_info;
	struct ath_pktlog_buf *buffer;

	if (!scn || !scn->pdev_txrx_handle->pl_dev) {
		printk(PKTLOG_TAG
		       "%s: Unable to allocate buffer "
		       "scn or scn->pdev_txrx_handle->pl_dev is null\n",
		       __func__);
		return -EINVAL;
	}

	pl_info = scn->pdev_txrx_handle->pl_dev->pl_info;

	page_cnt = (sizeof(*(pl_info->buf)) + pl_info->buf_size) / PAGE_SIZE;

	adf_os_spin_lock_bh(&pl_info->log_lock);
	if(pl_info->buf != NULL) {
		printk("Buffer is already in use\n");
		adf_os_spin_unlock_bh(&pl_info->log_lock);
		return -EINVAL;
	}
	adf_os_spin_unlock_bh(&pl_info->log_lock);

	if ((buffer = vmalloc((page_cnt + 2) * PAGE_SIZE)) == NULL) {
		printk(PKTLOG_TAG
		       "%s: Unable to allocate buffer "
		       "(%d pages)\n", __func__, page_cnt);
		return -ENOMEM;
	}


	buffer = (struct ath_pktlog_buf *)
			(((unsigned long) (buffer) + PAGE_SIZE - 1)
			& PAGE_MASK);

	for (vaddr = (unsigned long) (buffer);
	     vaddr < ((unsigned long) (buffer) + (page_cnt * PAGE_SIZE));
	     vaddr += PAGE_SIZE) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25))
		vpg = vmalloc_to_page((const void *) vaddr);
#else
		vpg = virt_to_page(pktlog_virt_to_logical((void *) vaddr));
#endif
		SetPageReserved(vpg);
	}

	adf_os_spin_lock_bh(&pl_info->log_lock);
	if(pl_info->buf != NULL)
		pktlog_release_buf(scn);

	pl_info->buf =  buffer;
	adf_os_spin_unlock_bh(&pl_info->log_lock);
	return 0;
}

void pktlog_release_buf(struct ol_softc *scn)
{
	unsigned long page_cnt;
	unsigned long vaddr;
	struct page *vpg;
	struct ath_pktlog_info *pl_info;

	if (!scn || !scn->pdev_txrx_handle->pl_dev) {
		printk(PKTLOG_TAG
		       "%s: Unable to allocate buffer"
		       "scn or scn->pdev_txrx_handle->pl_dev is null\n",
		       __func__);
		return;
	}

	pl_info = scn->pdev_txrx_handle->pl_dev->pl_info;

	page_cnt = ((sizeof(*(pl_info->buf)) + pl_info->buf_size) /
		   PAGE_SIZE) + 1;

	for (vaddr = (unsigned long) (pl_info->buf);
	     vaddr < (unsigned long) (pl_info->buf) + (page_cnt * PAGE_SIZE);
	     vaddr += PAGE_SIZE) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25))
		vpg = vmalloc_to_page((const void *) vaddr);
#else
		vpg = virt_to_page(pktlog_virt_to_logical((void *) vaddr));
#endif
		ClearPageReserved(vpg);
	}

	vfree(pl_info->buf);
	pl_info->buf = NULL;
}

void
pktlog_cleanup(struct ath_pktlog_info *pl_info)
{
	pl_info->log_state = 0;
	PKTLOG_LOCK_DESTROY(pl_info);
	mutex_destroy(&pl_info->pktlog_mutex);
}

/* sysctl procfs handler to enable pktlog */
static int
ATH_SYSCTL_DECL(ath_sysctl_pktlog_enable, ctl, write, filp, buffer, lenp,
		ppos)
{
	int ret, enable;
	ol_ath_generic_softc_handle scn;
	struct ol_pktlog_dev_t *pl_dev;

	mutex_lock(&proc_mutex);
	scn = (ol_ath_generic_softc_handle) ctl->extra1;

	if (!scn) {
		mutex_unlock(&proc_mutex);
		printk("%s: Invalid scn context\n", __func__);
		ASSERT(0);
		return -EINVAL;
	}

	pl_dev = get_pl_handle((struct ol_softc *)scn);

	if (!pl_dev) {
		mutex_unlock(&proc_mutex);
		printk("%s: Invalid pktlog context\n", __func__);
		ASSERT(0);
		return -ENODEV;
	}

	ctl->data = &enable;
	ctl->maxlen = sizeof(enable);

	if (write) {
		ret = ATH_SYSCTL_PROC_DOINTVEC(ctl, write, filp, buffer,
					       lenp, ppos);
		if (ret == 0)
			ret = pl_dev->pl_funcs->pktlog_enable(
						(struct ol_softc *)scn,
						enable);
		else
			printk(PKTLOG_TAG "%s:proc_dointvec failed\n",
			       __func__);
	} else {
		ret = ATH_SYSCTL_PROC_DOINTVEC(ctl, write, filp, buffer,
					       lenp, ppos);
		if (ret)
			printk(PKTLOG_TAG "%s:proc_dointvec failed\n",
			       __func__);
	}

	ctl->data = NULL;
	ctl->maxlen = 0;

	mutex_unlock(&proc_mutex);
	return ret;
}

static int get_pktlog_bufsize(struct ol_pktlog_dev_t *pl_dev)
{
	return pl_dev->pl_info->buf_size;
}

/* sysctl procfs handler to set/get pktlog size */
static int
ATH_SYSCTL_DECL(ath_sysctl_pktlog_size, ctl, write, filp, buffer, lenp,
		ppos)
{
	int ret, size;
	ol_ath_generic_softc_handle scn;
	struct ol_pktlog_dev_t *pl_dev;

	mutex_lock(&proc_mutex);
	scn = (ol_ath_generic_softc_handle) ctl->extra1;

	if (!scn) {
		mutex_unlock(&proc_mutex);
		printk("%s: Invalid scn context\n", __func__);
		ASSERT(0);
		return -EINVAL;
	}

	pl_dev = get_pl_handle((struct ol_softc *)scn);

	if (!pl_dev) {
		mutex_unlock(&proc_mutex);
		printk("%s: Invalid pktlog handle\n", __func__);
		ASSERT(0);
		return -ENODEV;
	}

	ctl->data = &size;
	ctl->maxlen = sizeof(size);

	if (write) {
		ret = ATH_SYSCTL_PROC_DOINTVEC(ctl, write, filp, buffer,
					       lenp, ppos);
		if (ret == 0)
			ret = pl_dev->pl_funcs->pktlog_setsize(
						(struct ol_softc *)scn,
						size);
	} else {
		size = get_pktlog_bufsize(pl_dev);
		ret = ATH_SYSCTL_PROC_DOINTVEC(ctl, write, filp, buffer,
					       lenp, ppos);
	}

	ctl->data = NULL;
	ctl->maxlen = 0;

	mutex_unlock(&proc_mutex);
	return ret;
}

/* Register sysctl table */
static int pktlog_sysctl_register(struct ol_softc *scn)
{
	struct ol_pktlog_dev_t *pl_dev = get_pl_handle(scn);
	struct ath_pktlog_info_lnx *pl_info_lnx;
	char *proc_name;

	if (pl_dev) {
		pl_info_lnx = PL_INFO_LNX(pl_dev->pl_info);
		proc_name = pl_dev->name;
	} else {
		pl_info_lnx = PL_INFO_LNX(g_pktlog_info);
		proc_name = PKTLOG_PROC_SYSTEM;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,31))
#define set_ctl_name(a, b)	/* nothing */
#else
#define set_ctl_name(a, b)	pl_info_lnx->sysctls[a].ctl_name = b
#endif

	/*
	 * Setup the sysctl table for creating the following sysctl entries:
	 * /proc/sys/PKTLOG_PROC_DIR/<adapter>/enable for enabling/disabling
	 * pktlog
	 * /proc/sys/PKTLOG_PROC_DIR/<adapter>/size for changing the buffer size
	 */
	memset(pl_info_lnx->sysctls, 0, sizeof(pl_info_lnx->sysctls));
	set_ctl_name(0, CTL_AUTO);
	pl_info_lnx->sysctls[0].procname = PKTLOG_PROC_DIR;
	pl_info_lnx->sysctls[0].mode = PKTLOG_PROCSYS_DIR_PERM;
	pl_info_lnx->sysctls[0].child = &pl_info_lnx->sysctls[2];
	/* [1] is NULL terminator */
	set_ctl_name(2, CTL_AUTO);
	pl_info_lnx->sysctls[2].procname = proc_name;
	pl_info_lnx->sysctls[2].mode = PKTLOG_PROCSYS_DIR_PERM;
	pl_info_lnx->sysctls[2].child = &pl_info_lnx->sysctls[4];
	/* [3] is NULL terminator */
	set_ctl_name(4, CTL_AUTO);
	pl_info_lnx->sysctls[4].procname = "enable";
	pl_info_lnx->sysctls[4].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[4].proc_handler = ath_sysctl_pktlog_enable;
	pl_info_lnx->sysctls[4].extra1 = scn;

	set_ctl_name(5, CTL_AUTO);
	pl_info_lnx->sysctls[5].procname = "size";
	pl_info_lnx->sysctls[5].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[5].proc_handler = ath_sysctl_pktlog_size;
	pl_info_lnx->sysctls[5].extra1 = scn;

	set_ctl_name(6, CTL_AUTO);
	pl_info_lnx->sysctls[6].procname = "options";
	pl_info_lnx->sysctls[6].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[6].proc_handler = proc_dointvec;
	pl_info_lnx->sysctls[6].data = &pl_info_lnx->info.options;
	pl_info_lnx->sysctls[6].maxlen = sizeof(pl_info_lnx->info.options);

	set_ctl_name(7, CTL_AUTO);
	pl_info_lnx->sysctls[7].procname = "sack_thr";
	pl_info_lnx->sysctls[7].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[7].proc_handler = proc_dointvec;
	pl_info_lnx->sysctls[7].data = &pl_info_lnx->info.sack_thr;
	pl_info_lnx->sysctls[7].maxlen = sizeof(pl_info_lnx->info.sack_thr);

	set_ctl_name(8, CTL_AUTO);
	pl_info_lnx->sysctls[8].procname = "tail_length";
	pl_info_lnx->sysctls[8].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[8].proc_handler = proc_dointvec;
	pl_info_lnx->sysctls[8].data = &pl_info_lnx->info.tail_length;
	pl_info_lnx->sysctls[8].maxlen = sizeof(pl_info_lnx->info.tail_length);

	set_ctl_name(9, CTL_AUTO);
	pl_info_lnx->sysctls[9].procname = "thruput_thresh";
	pl_info_lnx->sysctls[9].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[9].proc_handler = proc_dointvec;
	pl_info_lnx->sysctls[9].data = &pl_info_lnx->info.thruput_thresh;
	pl_info_lnx->sysctls[9].maxlen =
				sizeof(pl_info_lnx->info.thruput_thresh);

	set_ctl_name(10, CTL_AUTO);
	pl_info_lnx->sysctls[10].procname = "phyerr_thresh";
	pl_info_lnx->sysctls[10].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[10].proc_handler = proc_dointvec;
	pl_info_lnx->sysctls[10].data = &pl_info_lnx->info.phyerr_thresh;
	pl_info_lnx->sysctls[10].maxlen =
				sizeof(pl_info_lnx->info.phyerr_thresh);

	set_ctl_name(11, CTL_AUTO);
	pl_info_lnx->sysctls[11].procname = "per_thresh";
	pl_info_lnx->sysctls[11].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[11].proc_handler = proc_dointvec;
	pl_info_lnx->sysctls[11].data = &pl_info_lnx->info.per_thresh;
	pl_info_lnx->sysctls[11].maxlen = sizeof(pl_info_lnx->info.per_thresh);

	set_ctl_name(12, CTL_AUTO);
	pl_info_lnx->sysctls[12].procname = "trigger_interval";
	pl_info_lnx->sysctls[12].mode = PKTLOG_PROCSYS_PERM;
	pl_info_lnx->sysctls[12].proc_handler = proc_dointvec;
	pl_info_lnx->sysctls[12].data = &pl_info_lnx->info.trigger_interval;
	pl_info_lnx->sysctls[12].maxlen =
				sizeof(pl_info_lnx->info.trigger_interval);
	/* [13] is NULL terminator */

	/* and register everything */
	/* register_sysctl_table changed from 2.6.21 onwards */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,20))
	pl_info_lnx->sysctl_header =
			register_sysctl_table(pl_info_lnx->sysctls);
#else
	pl_info_lnx->sysctl_header =
			register_sysctl_table(pl_info_lnx->sysctls, 1);
#endif
	if (!pl_info_lnx->sysctl_header) {
		printk("%s: failed to register sysctls!\n", proc_name);
		return -1;
	}

	return 0;
}

/*
 * Initialize logging for system or adapter
 * Parameter scn should be NULL for system wide logging
 */
static int pktlog_attach(struct ol_softc *scn)
{
	struct ol_pktlog_dev_t *pl_dev;
	struct ath_pktlog_info_lnx *pl_info_lnx;
	char *proc_name;
	struct proc_dir_entry *proc_entry;

	pl_dev = get_pl_handle(scn);

	if (pl_dev != NULL) {
		pl_info_lnx = vos_mem_malloc(sizeof(*pl_info_lnx));
		if (pl_info_lnx == NULL) {
			printk(PKTLOG_TAG "%s:allocation failed for pl_info\n",
			       __func__);
			return -ENOMEM;
		}
		pl_dev->pl_info = &pl_info_lnx->info;
		pl_dev->name = WLANDEV_BASENAME;
		proc_name = pl_dev->name;
		if (!pl_dev->pl_funcs)
			pl_dev->pl_funcs = &ol_pl_funcs;

		/*
		 * Valid for both direct attach and offload architecture
		 */
		pl_dev->pl_funcs->pktlog_init(scn);
	}
	else {
		return -1;
	}

	/*
	 * initialize log info
	 * might be good to move to pktlog_init
	 */
	/* pl_dev->tgt_pktlog_enabled = false; */
	pl_info_lnx->proc_entry = NULL;
	pl_info_lnx->sysctl_header = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	proc_entry = proc_create_data(proc_name, PKTLOG_PROC_PERM,
				      g_pktlog_pde, &pktlog_fops,
				      &pl_info_lnx->info);

	if (proc_entry == NULL) {
		printk(PKTLOG_TAG "%s: create_proc_entry failed for %s\n",
		       __func__, proc_name);
		goto attach_fail1;
	}
#else
	proc_entry = create_proc_entry(proc_name, PKTLOG_PROC_PERM,
				       g_pktlog_pde);

	if (proc_entry == NULL) {
		printk(PKTLOG_TAG "%s: create_proc_entry failed for %s\n",
		       __func__, proc_name);
		goto attach_fail1;
	}

	proc_entry->data = &pl_info_lnx->info;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	proc_entry->owner = THIS_MODULE;
#endif
	proc_entry->proc_fops = &pktlog_fops;
#endif

	pl_info_lnx->proc_entry = proc_entry;

	if (pktlog_sysctl_register(scn)) {
		printk(PKTLOG_TAG "%s: sysctl register failed for %s\n",
		       __func__, proc_name);
		goto attach_fail2;
	}
	return 0;

attach_fail2:
	remove_proc_entry(proc_name, g_pktlog_pde);

attach_fail1:
	if (pl_dev)
		vos_mem_free(pl_dev->pl_info);
	return -1;
}

static void pktlog_sysctl_unregister(struct ol_pktlog_dev_t *pl_dev)
{
	struct ath_pktlog_info_lnx *pl_info_lnx;

	if (!pl_dev) {
		printk("%s: Invalid pktlog context\n", __func__);
		ASSERT(0);
		return;
	}

	pl_info_lnx = (pl_dev) ? PL_INFO_LNX(pl_dev->pl_info) :
				 PL_INFO_LNX(g_pktlog_info);

	if (pl_info_lnx->sysctl_header) {
		unregister_sysctl_table(pl_info_lnx->sysctl_header);
		pl_info_lnx->sysctl_header = NULL;
	}
}

static void pktlog_detach(struct ol_softc *scn)
{
	struct ol_pktlog_dev_t *pl_dev = (struct ol_pktlog_dev_t *)
					 get_pl_handle(scn);
	struct ath_pktlog_info *pl_info;

	if (!pl_dev) {
		printk("%s: Invalid pktlog context\n", __func__);
		ASSERT(0);
		return;
	}

	pl_info = pl_dev->pl_info;
	remove_proc_entry(WLANDEV_BASENAME, g_pktlog_pde);
	pktlog_sysctl_unregister(pl_dev);

	adf_os_spin_lock_bh(&pl_info->log_lock);
	if (pl_info->buf)
		pktlog_release_buf(scn);
	adf_os_spin_unlock_bh(&pl_info->log_lock);
	pktlog_cleanup(pl_info);

	if (pl_dev) {
		vos_mem_free(pl_info);
		pl_dev->pl_info = NULL;
	}
}

static int pktlog_open(struct inode *i, struct file *f)
{
	PKTLOG_MOD_INC_USE_COUNT;
	return 0;
}

static int pktlog_release(struct inode *i, struct file *f)
{
	PKTLOG_MOD_DEC_USE_COUNT;
	return 0;
}

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

/**
 * pktlog_read_proc_entry() - This function is used to read data from the
 * proc entry into the readers buffer
 * @buf:           Readers buffer
 * @nbytes:        Number of bytes to read
 * @ppos:          Offset within the drivers buffer
 * @pl_info:       Packet log information pointer
 * @read_complete: Boolean value indication whether read is complete
 *
 * This function is used to read data from the proc entry into the readers
 * buffer. Its functionality is similar to 'pktlog_read' which does
 * copy to user to the user space buffer
 *
 * Return: Number of bytes read from the buffer
 *
 */
ssize_t
pktlog_read_proc_entry(char *buf, size_t nbytes, loff_t *ppos,
		       struct ath_pktlog_info *pl_info,
		       bool *read_complete)
{
	size_t bufhdr_size;
	size_t count = 0, ret_val = 0;
	int rem_len;
	int start_offset, end_offset;
	int fold_offset, ppos_data, cur_rd_offset, cur_wr_offset;
	struct ath_pktlog_buf *log_buf;

	adf_os_spin_lock_bh(&pl_info->log_lock);
	log_buf = pl_info->buf;

	*read_complete = false;

	if (log_buf == NULL) {
		*read_complete = true;
		adf_os_spin_unlock_bh(&pl_info->log_lock);
		return 0;
	}

	if (*ppos == 0 && pl_info->log_state) {
		pl_info->saved_state = pl_info->log_state;
		pl_info->log_state = 0;
	}

	bufhdr_size = sizeof(log_buf->bufhdr);

	/* copy valid log entries from circular buffer into user space */
	rem_len = nbytes;
	count = 0;

	if (*ppos < bufhdr_size) {
		count = MIN((bufhdr_size - *ppos), rem_len);
		vos_mem_copy(buf, ((char *)&log_buf->bufhdr) + *ppos,
			     count);
		rem_len -= count;
		ret_val += count;
	}

	start_offset = log_buf->rd_offset;
	cur_wr_offset = log_buf->wr_offset;

	if ((rem_len == 0) || (start_offset < 0))
		goto rd_done;

	fold_offset = -1;
	cur_rd_offset = start_offset;

	/* Find the last offset and fold-offset if the buffer is folded */
	do {
		struct ath_pktlog_hdr *log_hdr;
		int log_data_offset;

		log_hdr = (struct ath_pktlog_hdr *) (log_buf->log_data +
			  cur_rd_offset);

		log_data_offset = cur_rd_offset + sizeof(struct ath_pktlog_hdr);

		if ((fold_offset == -1)
		    && ((pl_info->buf_size - log_data_offset)
		    <= log_hdr->size))
			fold_offset = log_data_offset - 1;

		PKTLOG_MOV_RD_IDX(cur_rd_offset, log_buf, pl_info->buf_size);

		if ((fold_offset == -1) && (cur_rd_offset == 0)
		    && (cur_rd_offset != cur_wr_offset))
			fold_offset = log_data_offset + log_hdr->size - 1;

		end_offset = log_data_offset + log_hdr->size - 1;
	} while (cur_rd_offset != cur_wr_offset);

	ppos_data = *ppos + ret_val - bufhdr_size + start_offset;

	if (fold_offset == -1) {
		if (ppos_data > end_offset)
			goto rd_done;

		count = MIN(rem_len, (end_offset - ppos_data + 1));
		vos_mem_copy(buf + ret_val,
			     log_buf->log_data + ppos_data,
			     count);
		ret_val += count;
		rem_len -= count;
	} else {
		if (ppos_data <= fold_offset) {
			count = MIN(rem_len, (fold_offset - ppos_data + 1));
			vos_mem_copy(buf + ret_val,
				     log_buf->log_data + ppos_data,
				     count);
			ret_val += count;
			rem_len -= count;
		}

		if (rem_len == 0)
			goto rd_done;

		ppos_data =
			*ppos + ret_val - (bufhdr_size +
			(fold_offset - start_offset + 1));

		if (ppos_data <= end_offset) {
			count = MIN(rem_len, (end_offset - ppos_data + 1));
			vos_mem_copy(buf + ret_val,
				     log_buf->log_data + ppos_data,
				     count);
			ret_val += count;
			rem_len -= count;
		}
	}

rd_done:
	if ((ret_val < nbytes) && pl_info->saved_state) {
		pl_info->log_state = pl_info->saved_state;
		pl_info->saved_state = 0;
	}
	*ppos += ret_val;

	if (ret_val == 0) {
		/* Write pointer might have been updated during the read.
		 * So, if some data is written into, lets not reset the pointers.
		 * We can continue to read from the offset position
		 */
		if (cur_wr_offset != log_buf->wr_offset) {
			*read_complete = false;
		} else {
			pl_info->buf->rd_offset = -1;
			pl_info->buf->wr_offset = 0;
			pl_info->buf->bytes_written = 0;
			pl_info->buf->offset = PKTLOG_READ_OFFSET;
			*read_complete = true;
		}
	}
	adf_os_spin_unlock_bh(&pl_info->log_lock);
	return ret_val;
}

static ssize_t
__pktlog_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	size_t bufhdr_size;
	size_t count = 0, ret_val = 0;
	int rem_len;
	int start_offset, end_offset;
	int fold_offset, ppos_data, cur_rd_offset;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	struct ath_pktlog_info *pl_info = (struct ath_pktlog_info *)
					  PDE_DATA(file->f_path.dentry->d_inode);
#else
	struct proc_dir_entry *proc_entry = PDE(file->f_dentry->d_inode);
	struct ath_pktlog_info *pl_info = (struct ath_pktlog_info *)
					  proc_entry->data;
#endif
	struct ath_pktlog_buf *log_buf;

	adf_os_spin_lock_bh(&pl_info->log_lock);
	log_buf = pl_info->buf;

	if (log_buf == NULL) {
		adf_os_spin_unlock_bh(&pl_info->log_lock);
		return 0;
	}

	if (*ppos == 0 && pl_info->log_state) {
		pl_info->saved_state = pl_info->log_state;
		pl_info->log_state = 0;
	}

	bufhdr_size = sizeof(log_buf->bufhdr);

	/* copy valid log entries from circular buffer into user space */
	rem_len = nbytes;
	count = 0;

	if (*ppos < bufhdr_size) {
		count = MIN((bufhdr_size - *ppos), rem_len);
		adf_os_spin_unlock_bh(&pl_info->log_lock);
		if (copy_to_user(buf, ((char *)&log_buf->bufhdr) + *ppos,
				 count))
			return -EFAULT;
		rem_len -= count;
		ret_val += count;
		adf_os_spin_lock_bh(&pl_info->log_lock);
	}

	start_offset = log_buf->rd_offset;

	if ((rem_len == 0) || (start_offset < 0))
		goto rd_done;

	fold_offset = -1;
	cur_rd_offset = start_offset;

	/* Find the last offset and fold-offset if the buffer is folded */
	do {
		struct ath_pktlog_hdr *log_hdr;
		int log_data_offset;

		log_hdr = (struct ath_pktlog_hdr *) (log_buf->log_data +
			  cur_rd_offset);

		log_data_offset = cur_rd_offset + sizeof(struct ath_pktlog_hdr);

		if ((fold_offset == -1)
		    && ((pl_info->buf_size - log_data_offset)
		    <= log_hdr->size))
			fold_offset = log_data_offset - 1;

		PKTLOG_MOV_RD_IDX(cur_rd_offset, log_buf, pl_info->buf_size);

		if ((fold_offset == -1) && (cur_rd_offset == 0)
		    && (cur_rd_offset != log_buf->wr_offset))
			fold_offset = log_data_offset + log_hdr->size - 1;

		end_offset = log_data_offset + log_hdr->size - 1;
	} while (cur_rd_offset != log_buf->wr_offset);

	ppos_data = *ppos + ret_val - bufhdr_size + start_offset;

	if (fold_offset == -1) {
		if (ppos_data > end_offset)
			goto rd_done;

		count = MIN(rem_len, (end_offset - ppos_data + 1));
		adf_os_spin_unlock_bh(&pl_info->log_lock);
		if (copy_to_user(buf + ret_val,
				 log_buf->log_data + ppos_data,
				 count))
			return -EFAULT;
		ret_val += count;
		rem_len -= count;
		adf_os_spin_lock_bh(&pl_info->log_lock);
	} else {
		if (ppos_data <= fold_offset) {
			count = MIN(rem_len, (fold_offset - ppos_data + 1));
			adf_os_spin_unlock_bh(&pl_info->log_lock);
			if (copy_to_user(buf + ret_val,
					 log_buf->log_data + ppos_data,
					 count))
				return -EFAULT;
			ret_val += count;
			rem_len -= count;
			adf_os_spin_lock_bh(&pl_info->log_lock);
		}

		if (rem_len == 0)
			goto rd_done;

		ppos_data =
			*ppos + ret_val - (bufhdr_size +
			(fold_offset - start_offset + 1));

		if (ppos_data <= end_offset) {
			count = MIN(rem_len, (end_offset - ppos_data + 1));
			adf_os_spin_unlock_bh(&pl_info->log_lock);
			if (copy_to_user(buf + ret_val,
					 log_buf->log_data + ppos_data,
					 count))
				return -EFAULT;
			ret_val += count;
			rem_len -= count;
			adf_os_spin_lock_bh(&pl_info->log_lock);
		}
	}

rd_done:
	if ((ret_val < nbytes) && pl_info->saved_state) {
		pl_info->log_state = pl_info->saved_state;
		pl_info->saved_state = 0;
	}
	*ppos += ret_val;

	adf_os_spin_unlock_bh(&pl_info->log_lock);
	return ret_val;
}

static ssize_t
pktlog_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	size_t ret_val = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	struct ath_pktlog_info *pl_info = (struct ath_pktlog_info *)
					  PDE_DATA(file->f_path.dentry->d_inode);
#else
	struct proc_dir_entry *proc_entry = PDE(file->f_dentry->d_inode);
	struct ath_pktlog_info *pl_info = (struct ath_pktlog_info *)
					  proc_entry->data;
#endif
	mutex_lock(&pl_info->pktlog_mutex);
	ret_val = __pktlog_read(file, buf, nbytes, ppos);
	mutex_unlock(&pl_info->pktlog_mutex);
	return ret_val;
}

#ifndef VMALLOC_VMADDR
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31))
/* Convert a kernel virtual address to a kernel logical address */
static volatile void *pktlog_virt_to_logical(volatile void *addr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep, pte;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15) || \
	(defined(__i386__) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)))
	pud_t *pud;
#endif
	unsigned long vaddr, ret = 0UL;

	vaddr = VMALLOC_VMADDR((unsigned long) addr);

	pgd = pgd_offset_k(vaddr);

	if (!pgd_none(*pgd)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15) || \
	(defined(__i386__) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)))
		pud = pud_offset(pgd, vaddr);
		pmd = pmd_offset(pud, vaddr);
#else
		pmd = pmd_offset(pgd, vaddr);
#endif

		if (!pmd_none(*pmd)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
			ptep = pte_offset_map(pmd, vaddr);
#else
			ptep = pte_offset(pmd, vaddr);
#endif
			pte = *ptep;

			if (pte_present(pte)) {
				ret = (unsigned long)
				      page_address(pte_page(pte));
				ret |= (vaddr & (PAGE_SIZE - 1));
			}
		}
	}
	return (volatile void *)ret;
}
#endif

int pktlogmod_init(void *context)
{
	int ret;

	/* create the proc directory entry */
	g_pktlog_pde = proc_mkdir(PKTLOG_PROC_DIR, NULL);

	if (g_pktlog_pde == NULL) {
		printk(PKTLOG_TAG "%s: proc_mkdir failed\n", __func__);
		return -1;
	}

	/* Attach packet log */
	if ((ret = pktlog_attach((struct ol_softc *)context)))
		goto attach_fail;

	return ret;

attach_fail:
	remove_proc_entry(PKTLOG_PROC_DIR, NULL);
	g_pktlog_pde = NULL;
	return ret;
}

void pktlogmod_exit(void *context)
{
	struct ol_softc *scn = (struct ol_softc *)context;
	struct ol_pktlog_dev_t *pl_dev = get_pl_handle(scn);

	if (!pl_dev || g_pktlog_pde == NULL) {
		printk("%s: pldev or g_pktlog_pde is NULL\n", __func__);
		return;
	}

	/*
	 * pktlog already be detached
	 * avoid to detach and remove proc entry again
	 */
	if (!pl_dev->pl_info) {
		printk("%s: pldev pl_info is NULL\n", __func__);
		return;
	}

	/*
	 *  Disable firmware side pktlog function
	 */
	if (pl_dev->tgt_pktlog_enabled) {
		if (pl_dev->pl_funcs->pktlog_enable(scn, 0)) {
			printk("%s: cannot disable pktlog in the target\n",
			       __func__);
		}
	}

	pktlog_detach(scn);
	/*
	 *  pdev kill needs to be implemented
	 */
	remove_proc_entry(PKTLOG_PROC_DIR, NULL);
}
#endif
