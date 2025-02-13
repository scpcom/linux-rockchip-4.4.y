/*
 * Copyright (C) 2015 Allwinnertech, z.q <zengqi@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include"disp_eink.h"

#ifdef SUPPORT_EINK
#include "include.h"
#include "disp_private.h"

/* #define BUFFER_SAVE_BACK */

#ifdef BUFFER_SAVE_BACK
#if 0
static s32 __clean_ring_queue(struct eink_buffer_manager *buffer_mgr)
{
	int i;
	unsigned int buf_size;

	buf_size = buffer_mgr->buf_size;

	for (i = 0; i < IMAGE_BUF_NUM; i++)
		disp_free((void *)buffer_mgr->image_slot[i].vaddr,
			  (void *)buffer_mgr->image_slot[i].paddr, buf_size);

	return 0;
}
#endif
static int __save_buf2storg(__u8 *buf, char *file_name, __u32 length,
			    loff_t pos)
{
	struct file *fp = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	mm_segment_t old_fs;
#endif
	ssize_t ret = 0;

	if ((buf == NULL) || (file_name == NULL)) {
		__debug(KERN_ALERT "%s: buf or file_name is null\n", __func__);
		return -1;
	}

	fp = filp_open(file_name, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(fp)) {
		__debug(KERN_ALERT "%s: fail to open file(%s), ret=%d\n",
			__func__, file_name, (u32) fp);
		return -1;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	ret = vfs_write(fp, buf, length, &pos);
	__debug(KERN_ALERT "%s: save %s done, len=%d, pos=%lld, ret=%d\n",
			__func__, file_name, length, pos, ret);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	set_fs(old_fs);
#endif
	filp_close(fp, NULL);

	return ret;

}
int __put_gary2buf(__u8 *buf, char *file_name, __u32 length, loff_t pos)
{
	struct file *fp = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	mm_segment_t fs;
#endif
	__s32 read_len = 0;
	ssize_t ret = 0;

	if ((buf == NULL) || (file_name == NULL)) {
		__debug(KERN_ALERT "%s: buf or file_name is null\n", __func__);
		return -1;
	}

	fp = filp_open(file_name, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		 pr_err("%s: fail to open file(%s), ret=%d\n",
						__func__, file_name, (u32)fp);
		return -1;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	read_len = vfs_read(fp, buf, length, &pos);
	if (read_len != length) {
		pr_err("maybe miss some data(read=%d byte, file=%d byte)\n",
				read_len, length);
		ret = -EAGAIN;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
		set_fs(fs);
#endif
		filp_close(fp, NULL);

	return ret;

}
#endif

s32 __clear_ring_queue_image(struct eink_buffer_manager *buffer_mgr)
{
	int ret = 0, i = 0;

	mutex_lock(&buffer_mgr->mlock);

	for (i = 0; i < IMAGE_BUF_NUM; i++) {
		/* init mode need buf data 0xff */
		memset((void *)buffer_mgr->image_slot[i].vaddr,
		       0xff, buffer_mgr->buf_size);
	}

	mutex_unlock(&buffer_mgr->mlock);

	return ret;
}

bool __is_ring_queue_full(struct eink_buffer_manager *buffer_mgr)
{
	bool ret;
	unsigned int in_index, out_index;

	mutex_lock(&buffer_mgr->mlock);

	in_index = buffer_mgr->in_index;
	out_index = buffer_mgr->out_index;
	ret = ((in_index + 1) % IMAGE_BUF_NUM == out_index) ? true : false;

	mutex_unlock(&buffer_mgr->mlock);

	return ret;
}

bool __is_ring_queue_empty(struct eink_buffer_manager *buffer_mgr)
{
	bool ret;
	unsigned int in_index, out_index;

	mutex_lock(&buffer_mgr->mlock);

	in_index = buffer_mgr->in_index;
	out_index = buffer_mgr->out_index;
	ret = (in_index == (out_index + 1) % IMAGE_BUF_NUM) ? true : false;

	mutex_unlock(&buffer_mgr->mlock);

	return ret;
}

struct eink_8bpp_image *__get_current_image(struct eink_buffer_manager
					    *buffer_mgr)
{
	struct eink_8bpp_image *ret;
	unsigned int out_index;

	mutex_lock(&buffer_mgr->mlock);

	out_index = buffer_mgr->out_index;
	if (out_index < IMAGE_BUF_NUM) {
		ret = &buffer_mgr->image_slot[(out_index + 1) % IMAGE_BUF_NUM];
		goto out;
	} else {
		__wrn("%s: in_index larger than the max value!\n", __func__);
		ret = NULL;
		goto out;
	}

out:
	mutex_unlock(&buffer_mgr->mlock);

	return ret;
}

struct eink_8bpp_image *__get_last_image(struct eink_buffer_manager *buffer_mgr)
{
	struct eink_8bpp_image *ret;
	unsigned int out_index;

	mutex_lock(&buffer_mgr->mlock);

	out_index = buffer_mgr->out_index;
	if (out_index < IMAGE_BUF_NUM) {
		ret = &buffer_mgr->image_slot[out_index];
		goto out;
	} else {
		__wrn("%s: in_index larger than the max value!\n", __func__);
		ret = NULL;
		goto out;
	}

out:
	mutex_unlock(&buffer_mgr->mlock);

	return ret;
}

#if !defined(SUPPORT_WB)
static int __conver_32bit_bmp_to_8bit(u8 *src_image_data, u32 width,
				      u32 height, u8 *dest_image_data)
{
	struct st_argb *tmp_src_data = NULL;
	u8 *tmp_dest_data = NULL;
	u32 wi = 0, hi = 0, co = 0;

	if ((src_image_data == NULL) || (dest_image_data == NULL)) {
		__wrn("%s: input param is null\n", __func__);
		return -1;
	}

	tmp_dest_data = (u8 *) dest_image_data;
	hi = height;
	while (hi > 0) {
		tmp_src_data =
		    (struct st_argb *)src_image_data + (height - hi) * width;
		for (wi = 0; wi < width; wi++) {
			*tmp_dest_data = (306 * tmp_src_data->red +
					  601 * tmp_src_data->green +
					  117 * tmp_src_data->blue +
					  0x200) >> 10;
			tmp_dest_data++;
			tmp_src_data++;
			co++;
		}
		hi--;
	}
	__debug("%s: size = %u.\n", __func__, co);

	return 0;
}
#endif

s32 __queue_image(struct eink_buffer_manager *buffer_mgr,
		  struct disp_layer_config_inner *config,
		  unsigned int layer_num,
		  u32 mode, struct area_info update_area)
{
	bool queue_is_full;
	int ret = 0;
	unsigned int in_index, out_index;
	unsigned int buf_size;
#ifndef SUPPORT_WB
	u8 *src_image;
#endif
#ifdef SUPPORT_WB
	struct format_manager *cmgr;
	struct image_format src, dest;
#endif

	mutex_lock(&buffer_mgr->mlock);

	in_index = buffer_mgr->in_index;
	out_index = buffer_mgr->out_index;
	buf_size = buffer_mgr->buf_size;

	if (in_index >= IMAGE_BUF_NUM) {
		__wrn("in_index larger than the max value!\n");
		ret = -EINVAL;
		goto out;
	}

	queue_is_full =
	    ((in_index + 1) % IMAGE_BUF_NUM == out_index) ? true : false;
	/* when last image is missed ,it need cover last image */

	if (queue_is_full) {
		if (in_index)
			in_index--;
		else
			in_index = IMAGE_BUF_NUM - 1;
	}
#ifdef SUPPORT_WB
	cmgr = disp_get_format_manager(0);
	memset((void *)&src, 0, sizeof(struct image_format));
	src.format = DISP_FORMAT_ARGB_8888;
	src.width = buffer_mgr->width;
	src.height = buffer_mgr->height;

	memset((void *)&dest, 0, sizeof(struct image_format));
	dest.format = DISP_FORMAT_8BIT_GRAY;
	dest.addr1 = (unsigned long)buffer_mgr->image_slot[in_index].paddr;
	dest.width = buffer_mgr->width;
	dest.height = buffer_mgr->height;

	/*start convert*/
	if (config != NULL)/*config regs*/
		ret = cmgr->start_convert(0, config, layer_num, &dest);
#ifdef BUFFER_SAVE_BACK
	__save_buf2storg((void *)buffer_mgr->image_slot[in_index].vaddr,
			 "./dst_eink_image.bin",
			 buffer_mgr->width * buffer_mgr->height, 0);
#endif
	if (ret)
		goto out;
#else
	if (src_image != NULL)
		__conver_32bit_bmp_to_8bit(src_image, buffer_mgr->width,
					   buffer_mgr->height,
					   (void *)buffer_mgr->
					   image_slot[in_index].vaddr);

	__put_gary2buf((void *)buffer_mgr->image_slot[in_index].vaddr,
			"./system/eink_image.bin",
			buffer_mgr->width * buffer_mgr->height, 0);
#ifdef BUFFER_SAVE_BACK
	__save_buf2storg((void *)buffer_mgr->image_slot[in_index].vaddr,
			 "./eink_image.bin",
			 buffer_mgr->width * buffer_mgr->height, 0);
#endif

#endif				/* endif SUPPORT_WB */
	__debug
	    ("%s: index =%d, vaddr=%p, paddr=%p, xt=%d, yt=%d, xb=%d, yb=%d\n",
	     __func__, in_index, buffer_mgr->image_slot[in_index].vaddr,
	     buffer_mgr->image_slot[in_index].paddr, update_area.x_top,
	     update_area.y_top, update_area.x_bottom, update_area.y_bottom);
	/*
	 * update new 8bpp image information
	 */
	buffer_mgr->image_slot[in_index].state = USED;
	buffer_mgr->image_slot[in_index].update_mode = mode;
	buffer_mgr->image_slot[in_index].window_calc_enable = false;

	if (mode & EINK_RECT_MODE) {
		buffer_mgr->image_slot[in_index].flash_mode = LOCAL;

		if ((update_area.x_bottom == 0) && (update_area.x_top == 0) &&
		    (update_area.y_bottom == 0) && (update_area.y_top == 0)) {
			buffer_mgr->image_slot[in_index].window_calc_enable =
			    true;
			memset(&buffer_mgr->image_slot[in_index].update_area, 0,
			       sizeof(struct area_info));
		} else {
			memcpy(&buffer_mgr->image_slot[in_index].update_area,
			       &update_area, sizeof(struct area_info));
		}
	} else {
		if (mode == EINK_INIT_MODE)
			buffer_mgr->image_slot[in_index].flash_mode = INIT;
		else
			buffer_mgr->image_slot[in_index].flash_mode = GLOBAL;

		/* set update area full screen. */
		buffer_mgr->image_slot[in_index].update_area.x_top = 0;
		buffer_mgr->image_slot[in_index].update_area.y_top = 0;
		buffer_mgr->image_slot[in_index].update_area.x_bottom =
		    buffer_mgr->image_slot[in_index].size.width - 1;
		buffer_mgr->image_slot[in_index].update_area.y_bottom =
		    buffer_mgr->image_slot[in_index].size.height - 1;
	}

	/*
	 * if queue is full,then cover the newest image,
	 * and in_index keep the same value.
	 */
	if (!queue_is_full)
		buffer_mgr->in_index =
		    (buffer_mgr->in_index + 1) % IMAGE_BUF_NUM;

out:
	__debug("q:in_index:%d, out_index:%d,paddr:0x%p, wcalc_en=%d\n",
		buffer_mgr->in_index, buffer_mgr->out_index,
		buffer_mgr->image_slot[in_index].paddr,
		buffer_mgr->image_slot[in_index].window_calc_enable);

	mutex_unlock(&buffer_mgr->mlock);

	return ret;
}

s32 __dequeue_image(struct eink_buffer_manager *buffer_mgr)
{
	bool queue_is_empty;
	int ret = 0;
	unsigned int in_index, out_index;

	mutex_lock(&buffer_mgr->mlock);
	in_index = buffer_mgr->in_index;
	out_index = buffer_mgr->out_index;

	if (out_index >= IMAGE_BUF_NUM) {
		__wrn("%s: out_index larger than the max value!\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	queue_is_empty =
	    (in_index == (out_index + 1) % IMAGE_BUF_NUM) ? true : false;
	if (queue_is_empty) {
		ret = -EBUSY;
		__debug("queue is empty!\n");
		goto out;
	}

	/*set the 8pp image information to initial state. */
	buffer_mgr->image_slot[out_index].state = FREE;
	buffer_mgr->image_slot[out_index].flash_mode = GLOBAL;
	buffer_mgr->image_slot[out_index].update_mode = EINK_INIT_MODE;
	buffer_mgr->image_slot[out_index].window_calc_enable = true;
	buffer_mgr->out_index = (buffer_mgr->out_index + 1) % IMAGE_BUF_NUM;

	__debug("dq in_index:%d, out_index:%d\n", buffer_mgr->in_index,
		buffer_mgr->out_index);
out:
	mutex_unlock(&buffer_mgr->mlock);

	return ret;
}

s32 ring_buffer_manager_init(struct disp_eink_manager *eink_manager)
{
	int i;
	int ret = 0;
	struct eink_buffer_manager *buffer_mgr = NULL;

	buffer_mgr =
		(struct eink_buffer_manager *)disp_sys_malloc(
				sizeof(struct eink_buffer_manager));
	if (!buffer_mgr) {
		__wrn("malloc eink buffer manager memory fail!\n");
		ret = -ENOMEM;
		goto buffer_mgr_err;
	}

	/*
	 * init in_index is 1,ring buffer is empty.
	 * out_index is 0,point to last image.
	 */
	memset((void *)(buffer_mgr), 0, sizeof(struct eink_buffer_manager));
	buffer_mgr->width = eink_manager->private_data->param.timing.width;
	buffer_mgr->height = eink_manager->private_data->param.timing.height;
	buffer_mgr->buf_size = buffer_mgr->width * buffer_mgr->height;
	buffer_mgr->in_index = 1;
	buffer_mgr->out_index = 0;
	mutex_init(&buffer_mgr->mlock);

	for (i = 0; i < IMAGE_BUF_NUM; i++) {
		buffer_mgr->image_slot[i].size.width = buffer_mgr->width;
		buffer_mgr->image_slot[i].size.height = buffer_mgr->height;
		/* fix it to match different drawer. */
		buffer_mgr->image_slot[i].size.align = 4;
		buffer_mgr->image_slot[i].update_mode = EINK_INIT_MODE;
		buffer_mgr->image_slot[i].vaddr =
		    disp_malloc(buffer_mgr->buf_size,
				&buffer_mgr->image_slot[i].paddr);
		if (!buffer_mgr->image_slot[i].vaddr) {
			__wrn("malloc image buffer memory fail!\n");
			ret = -ENOMEM;
			goto image_buffer_err;
		}
		__debug("image paddr%d=0x%p", i,
			buffer_mgr->image_slot[i].paddr);

		/* init mode need buf data 0xff */
		memset((void *)buffer_mgr->image_slot[i].vaddr, 0xff,
		       buffer_mgr->buf_size);
	}

	buffer_mgr->is_full = __is_ring_queue_full;
	buffer_mgr->is_empty = __is_ring_queue_empty;
	buffer_mgr->get_last_image = __get_last_image;
	buffer_mgr->get_current_image = __get_current_image;
	buffer_mgr->queue_image = __queue_image;
	buffer_mgr->dequeue_image = __dequeue_image;
	buffer_mgr->clear_image = __clear_ring_queue_image;
	eink_manager->buffer_mgr = buffer_mgr;

	return ret;

image_buffer_err:
	for (i = 0; i < IMAGE_BUF_NUM; i++) {
		if (buffer_mgr->image_slot[i].vaddr)
			disp_free(buffer_mgr->image_slot[i].vaddr,
				  (void *)buffer_mgr->image_slot[i].paddr,
				  buffer_mgr->buf_size);
	}

buffer_mgr_err:
	kfree(buffer_mgr);

	return ret;
}

s32 ring_buffer_manager_exit(struct disp_eink_manager *eink_manager)
{
	int i;
	struct eink_buffer_manager *buffer_mgr = NULL;

	buffer_mgr = eink_manager->buffer_mgr;
	for (i = 0; i < IMAGE_BUF_NUM; i++) {
		if (buffer_mgr->image_slot[i].vaddr)
			disp_free(buffer_mgr->image_slot[i].vaddr,
				  (void *)buffer_mgr->image_slot[i].paddr,
				  buffer_mgr->buf_size);
	}

	kfree(buffer_mgr);
	eink_manager->buffer_mgr = NULL;

	return 0;
}
#endif

