/*
 * Copyright (C) 2015 Allwinnertech,  <zhengxiaobin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/videodev2.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
#include <linux/freezer.h>
#endif

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sunxi-gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#if defined(CONFIG_EXTCON)
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#endif
/*#include <linux/sys_config.h>*/
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/reset.h>
/*#include <linux/clk-private.h>*/

#ifdef CONFIG_DEVFREQ_DRAM_FREQ_WITH_SOFT_NOTIFY
#include <linux/sunxi_dramfreq.h>
#endif

#include "tvd.h"

#define TVD_MODULE_NAME "sunxi_tvd"
#define MIN_WIDTH (32)
#define MIN_HEIGHT (32)
#define MAX_WIDTH (4096)
#define MAX_HEIGHT (4096)
#define MAX_BUFFER (32 * 1024 * 1024)
#define NUM_INPUTS 1

#define TVD_MAX_POWER_NUM 2
#define TVD_MAX_GPIO_NUM 2
#define TVD_MAJOR_VERSION 1
#define TVD_MINOR_VERSION 0
#define TVD_RELEASE 0
#define TVD_VERSION                                                            \
	KERNEL_VERSION(TVD_MAJOR_VERSION, TVD_MINOR_VERSION, TVD_RELEASE)

/*v4l2_format's raw_data index*/
#define RAW_DATA_INTERFACE   1
#define RAW_DATA_SYSTEM      2
#define RAW_DATA_FORMAT      3
#define RAW_DATA_PIXELFORMAT 4
#define RAW_DATA_ROW         5
#define RAW_DATA_COLUMN      6
#define RAW_DATA_CH0_INDEX   7
#define RAW_DATA_CH1_INDEX   8
#define RAW_DATA_CH2_INDEX   9
#define RAW_DATA_CH3_INDEX   10
#define RAW_DATA_CH0_STATUS  11
#define RAW_DATA_CH1_STATUS  12
#define RAW_DATA_CH2_STATUS  13
#define RAW_DATA_CH3_STATUS  14

static unsigned int tvd_dbg_en;
static unsigned int tvd_dbg_sel;
#define TVD_DBG_DUMP_LEN 0x200000
#define TVD_NAME_LEN 32
static char tvd_dump_file_name[TVD_NAME_LEN];
/*todo:alloc dynamic*/
static struct tvd_status tvd_status[TVD_MAX];
static struct tvd_dev *tvd[TVD_MAX];
static void __iomem *tvd_addr[TVD_MAX];
static void __iomem *tvd_top;

static struct clk *tvd_clk_top;
static struct clk *tvd_mbus;
static struct reset_control *rst_tvd_top;

static struct clk *tvd_clk[TVD_MAX];
static struct clk *tvd_bus_clk[TVD_MAX];
static struct reset_control *rst_tvd[TVD_MAX];

static int tvd_irq[TVD_MAX];
static unsigned int tvd_hot_plug;
static unsigned int tvd_row;
static unsigned int tvd_column;
static unsigned int tvd_total_num;

/* use for reversr special interfaces */
static int tvd_count;
static int fliter_count;
static struct mutex fliter_lock;
static struct mutex power_lock;
static char tvd_power[TVD_MAX_POWER_NUM][32];
static struct gpio_config tvd_gpio_config[TVD_MAX_GPIO_NUM];
static struct regulator *regu[TVD_MAX_POWER_NUM];
static atomic_t tvd_used_power_num = ATOMIC_INIT(0);
static atomic_t tvd_used_gpio_num = ATOMIC_INIT(0);
static atomic_t gpio_power_enable_count = ATOMIC_INIT(0);

static irqreturn_t tvd_isr(int irq, void *priv);
static irqreturn_t tvd_isr_special(int irq, void *priv);
static int __tvd_fetch_sysconfig(int sel, char *sub_name, int value[]);
static int __tvd_auto_plug_enable(struct tvd_dev *dev);
static int __tvd_auto_plug_disable(struct tvd_dev *dev);

static ssize_t tvd_dbg_en_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{

	tvd_here;
	return sprintf(buf, "%u\n", tvd_dbg_en);
}

static ssize_t tvd_dbg_en_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int err;
	unsigned long val;

	tvd_here;

	err = kstrtoul(buf, 10, &val);
	if (err) {
		tvd_dbg("Invalid size\n");
		return err;
	}

	if (val < 0 || val > 1) {
		tvd_dbg("Invalid value, 0~1 is expected!\n");
	} else {
		tvd_dbg_en = val;

		tvd_dbg("tvd_dbg_en = %ld\n", val);
	}

	return count;
}

static ssize_t tvd_dbg_lv_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{

	tvd_here;
	return sprintf(buf, "%u\n", tvd_dbg_sel);
}

static ssize_t tvd_dbg_lv_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int err;
	unsigned long val;

	tvd_here;

	err = kstrtoul(buf, 10, &val);
	if (err) {
		tvd_dbg("Invalid size\n");
		return err;
	}

	if (val < 0 || val > 4) {
		tvd_dbg("Invalid value, 0~3 is expected!\n");
	} else {
		tvd_dbg_sel = val;
		tvd_dbg("tvd_dbg_sel = %d\n", tvd_dbg_sel);
		tvd_isr(46, tvd[tvd_dbg_sel]);
	}

	return count;
}

static ssize_t tvd_dbg_dump_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct tvd_dev *tvd_dev = (struct tvd_dev *)dev_get_drvdata(dev);
	struct file *pfile;
	mm_segment_t old_fs;
	ssize_t bw;
	dma_addr_t buf_dma_addr;
	void *buf_addr;

	tvd_here;

	buf_addr = dma_alloc_coherent(dev, TVD_DBG_DUMP_LEN,
				      (dma_addr_t *)&buf_dma_addr, GFP_KERNEL);
	if (!buf_addr) {
		tvd_wrn("%s(), dma_alloc_coherent fail, size=0x%x\n", __func__,
			TVD_DBG_DUMP_LEN);

		return 0;
	}

	/* start debug mode */
	if (tvd_dbgmode_dump_data(tvd_dev->sel, 0, buf_dma_addr,
				  TVD_DBG_DUMP_LEN / 2)) {
		tvd_wrn("%s(), debug mode start fail\n", __func__);
		goto exit;
	}

	pfile = filp_open(tvd_dump_file_name, O_RDWR | O_CREAT | O_EXCL, 0755);
	if (IS_ERR(pfile)) {
		tvd_wrn("%s, open %s err\n", __func__, tvd_dump_file_name);
		goto exit;
	}
	tvd_wrn("%s, open %s ok\n", __func__, tvd_dump_file_name);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	bw = pfile->f_op->write(pfile, (const char *)buf_addr, TVD_DBG_DUMP_LEN,
				&pfile->f_pos);

	if (unlikely(bw != TVD_DBG_DUMP_LEN))
		tvd_wrn("%s, write %s err at byte offset %llu\n", __func__,
			tvd_dump_file_name, pfile->f_pos);
	set_fs(old_fs);
	filp_close(pfile, NULL);
	pfile = NULL;

exit:
	dma_free_coherent(dev, TVD_DBG_DUMP_LEN, buf_addr, buf_dma_addr);

	return 0;
}

static ssize_t tvd_dbg_dump_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{

	tvd_here;
	memset(tvd_dump_file_name, '\0', TVD_NAME_LEN);
	count = (count > TVD_NAME_LEN) ? TVD_NAME_LEN : count;
	memcpy(tvd_dump_file_name, buf, count - 1);

	tvd_dbg("%s(), get dump file name %s\n", __func__, tvd_dump_file_name);

	return count;
}

static DEVICE_ATTR(tvd_dbg_en, S_IRUGO | S_IWUSR | S_IWGRP, tvd_dbg_en_show,
		   tvd_dbg_en_store);

static DEVICE_ATTR(tvd_dbg_lv, S_IRUGO | S_IWUSR | S_IWGRP, tvd_dbg_lv_show,
		   tvd_dbg_lv_store);
static DEVICE_ATTR(tvd_dump, S_IRUGO | S_IWUSR | S_IWGRP, tvd_dbg_dump_show,
		   tvd_dbg_dump_store);

static struct attribute *tvd0_attributes[] = {&dev_attr_tvd_dbg_en.attr,
					      &dev_attr_tvd_dbg_lv.attr,
					      &dev_attr_tvd_dump.attr, NULL};

static struct attribute *tvd1_attributes[] = {&dev_attr_tvd_dbg_en.attr,
					      &dev_attr_tvd_dbg_lv.attr,
					      &dev_attr_tvd_dump.attr, NULL};

static struct attribute *tvd2_attributes[] = {&dev_attr_tvd_dbg_en.attr,
					      &dev_attr_tvd_dbg_lv.attr,
					      &dev_attr_tvd_dump.attr, NULL};

static struct attribute *tvd3_attributes[] = {&dev_attr_tvd_dbg_en.attr,
					      &dev_attr_tvd_dbg_lv.attr,
					      &dev_attr_tvd_dump.attr, NULL};

static struct attribute *tvd_multi_ch_attributes[] = {
				&dev_attr_tvd_dbg_en.attr,
				&dev_attr_tvd_dbg_lv.attr,
				&dev_attr_tvd_dump.attr, NULL};

static struct attribute_group tvd_attribute_group[] = {
	{.name = "tvd0_attr", .attrs = tvd0_attributes},
	{.name = "tvd1_attr", .attrs = tvd1_attributes},
	{.name = "tvd2_attr", .attrs = tvd2_attributes},
	{.name = "tvd3_attr", .attrs = tvd3_attributes},
	{.name = "tvd_multi_ch_attr", .attrs = tvd_multi_ch_attributes},
};

static struct tvd_fmt formats[] = {
	{
		.name = "planar UVUV",
		.fourcc = V4L2_PIX_FMT_NV12,
		.output_fmt = TVD_PL_YUV420,
		.depth = 12,
	},
	{
		.name = "planar VUVU",
		.fourcc = V4L2_PIX_FMT_NV21,
		.output_fmt = TVD_PL_YUV420,
		.depth = 12,
	},
	{
		.name = "planar UVUV",
		.fourcc = V4L2_PIX_FMT_NV16,
		.output_fmt = TVD_PL_YUV422,
		.depth = 16,
	},
	{
		.name = "planar VUVU",
		.fourcc = V4L2_PIX_FMT_NV61,
		.output_fmt = TVD_PL_YUV422,
		.depth = 16,
	},
	/* this format is not standard, just for allwinner. */
	{
		.name = "planar PACK",
		.fourcc = 0,
		.output_fmt = TVD_MB_YUV420,
		.depth = 12,
	},
};

static inline int tvd_is_generating(struct tvd_dev *dev)
{
	return test_bit(0, &dev->generating);
}

static inline void tvd_start_generating(struct tvd_dev *dev)
{
	tvd_here;
	set_bit(0, &dev->generating);
	return;
}

static inline void tvd_stop_generating(struct tvd_dev *dev)
{
	tvd_here;
	clear_bit(0, &dev->generating);
	return;
}

static int tvd_is_opened(struct tvd_dev *dev)
{
	int ret;

	tvd_here;
	mutex_lock(&dev->opened_lock);
	ret = test_bit(0, &dev->opened);
	mutex_unlock(&dev->opened_lock);
	return ret;
}

static void tvd_start_opened(struct tvd_dev *dev)
{

	tvd_here;
	mutex_lock(&dev->opened_lock);
	set_bit(0, &dev->opened);
	mutex_unlock(&dev->opened_lock);
}

static void tvd_stop_opened(struct tvd_dev *dev)
{

	tvd_here;
	mutex_lock(&dev->opened_lock);
	clear_bit(0, &dev->opened);
	mutex_unlock(&dev->opened_lock);
}

static int __tvd_clk_init(struct tvd_dev *dev)
{
	int div = 0, ret = 0, i = 0;
	unsigned long p = 216000000;

	tvd_here;

	tvd_dbg("%s: dev->interface = %d, dev->system = %d\n", __func__,
		dev->interface, dev->system);

	dev->parent = clk_get_parent(dev->clk);
	if (IS_ERR_OR_NULL(dev->parent) || IS_ERR_OR_NULL(dev->clk))
		return -EINVAL;

	/* parent is 297M */
	ret = clk_set_rate(dev->parent, p);
	if (ret) {
		ret = -EINVAL;
		goto out;
	}

	if (dev->interface == CVBS_INTERFACE || (dev->sel == 3)) {
		/* cvbs interface */
			div = 8;

	} else if (dev->interface == YPBPRI_INTERFACE) {
		/* ypbprI interface */
		div = 8;
	} else if (dev->interface == YPBPRP_INTERFACE) {
		/* ypbprP interface */
		div = 4;
	} else {
		tvd_wrn("%s: interface is err!\n", __func__);
		return -EINVAL;
	}

	tvd_dbg("div = %d\n", div);

	p /= div;
	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; i++) {
			if (!tvd_status[i].tvd_used)
				continue;
			ret = clk_set_rate(tvd_clk[i], p);
			if (ret) {
				ret = -EINVAL;
				goto out;
			}
			tvd_dbg("tvd%d: parent = %lu, clk = %lu\n", i,
				clk_get_rate(dev->parent),
				clk_get_rate(tvd_clk[i]));
		}
	} else {
		ret = clk_set_rate(dev->clk, p);
		if (ret) {
			ret = -EINVAL;
			goto out;
		}
		tvd_dbg("tvd%d: parent = %lu, clk = %lu\n", dev->sel,
			clk_get_rate(dev->parent), clk_get_rate(dev->clk));
	}

out:
	return ret;
}

static int __tvd_clk_enable(struct tvd_dev *dev)
{
	int ret = 0, i = 0;

	tvd_here;

	ret = reset_control_deassert(dev->clk_top_rst);
	if (ret) {
		tvd_wrn("reset clk_top_rst fail");
		return ret;
	}

	ret = clk_prepare_enable(dev->clk_mbus);
	if (ret) {
		tvd_wrn("%s: tvd clk mbus enable err!", __func__);
		return ret;
	}

	ret = clk_prepare_enable(dev->clk_top);
	if (ret) {
		tvd_wrn("%s: tvd top clk enable err!", __func__);
		clk_disable(dev->clk_top);
		return ret;
	}

	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; i++) {
			if (!tvd_status[i].tvd_used)
				continue;

			if (!__clk_get_enable_count(tvd_clk[i])) {
				ret = reset_control_deassert(rst_tvd[i]);
				if (ret)
					tvd_wrn("reset tvd reset fail");

				ret = clk_prepare_enable(tvd_bus_clk[i]);
				if (ret)
					tvd_wrn("enable tvd bus clk fail");

				ret = clk_prepare_enable(tvd_clk[i]);
				if (ret) {
					tvd_wrn("%s: tvd clk %d enable err!",
					       __func__, i);
					clk_disable(tvd_clk[i]);
					break;
				}
			}
			tvd_status[i].tvd_clk_enabled = 1;
		}
	} else {
		ret = reset_control_deassert(dev->rst_tvd);
		if (ret)
			tvd_wrn("reset tvd reset fail");

		ret = clk_prepare_enable(dev->clk_bus);
		if (ret)
			tvd_wrn("enable tvd bus clk fail");

		ret = clk_prepare_enable(dev->clk);
		if (ret) {
			tvd_wrn("%s: tvd clk %d enable err!", __func__, i);
			clk_disable(dev->clk);
		}
	}

	return ret;
}

static int __tvd_clk_disable(struct tvd_dev *dev)
{
	int ret = 0, i = 0;

	tvd_here;

	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (!tvd_status[i].tvd_used)
				continue;
			tvd_dbg("Disable tvd%d clk\n", i);
			while (__clk_get_enable_count(tvd_clk[i]))
				clk_disable(tvd_clk[i]);
			tvd_status[i].tvd_clk_enabled = 0;
			reset_control_assert(rst_tvd[i]);
		}
	} else {
		/*while (dev->clk->enable_count)*/
			clk_disable(dev->clk);
			clk_disable(dev->clk_bus);
			reset_control_assert(dev->rst_tvd);
	}
	clk_disable(dev->clk_top);
	reset_control_assert(dev->clk_top_rst);
	clk_disable(dev->clk_mbus);

	return ret;
}

static int __tvd_init(struct tvd_dev *dev)
{
	int i = 0;

	tvd_here;

	tvd_top_set_reg_base((unsigned long)dev->regs_top);
	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; ++i)
			tvd_set_reg_base(i, (unsigned long)tvd_addr[i]);
	} else
		tvd_set_reg_base(dev->sel, (unsigned long)dev->regs_tvd);
	return 0;
}

static int __tvd_config(struct tvd_dev *dev)
{
	int i = 0;

	tvd_here;

	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (!tvd_status[i].tvd_used)
				continue;
			tvd_init(i, dev->interface);
			tvd_config(i, dev->interface, dev->system);
			tvd_set_wb_width(i, dev->width / dev->row);
			tvd_set_wb_width_jump(i, dev->width);
			if (dev->interface == YPBPRP_INTERFACE)
				tvd_set_wb_height(i,
						  dev->height /
						  dev->column); /*P,no div*/
			else
				tvd_set_wb_height(
				    i, dev->height / (2 * dev->column));
			/* pl_yuv420, mb_yuv420, pl_yuv422 */
			tvd_set_wb_fmt(i, dev->fmt->output_fmt);
			switch (dev->fmt->fourcc) {

			case V4L2_PIX_FMT_NV12:
			case V4L2_PIX_FMT_NV16:
				tvd_set_wb_uv_swap(i, 0);
				break;

			case V4L2_PIX_FMT_NV21:
			case V4L2_PIX_FMT_NV61:
				tvd_set_wb_uv_swap(i, 1);
				break;
			}
		}
	} else {
		tvd_init(dev->sel, dev->interface);
		tvd_config(dev->sel,
		(dev->sel == 3) ? 0 : dev->interface, dev->system);
		tvd_set_wb_width(dev->sel, dev->width);
		tvd_set_wb_width_jump(dev->sel, dev->width);
		if (dev->interface == YPBPRP_INTERFACE && (dev->sel != 3))
			tvd_set_wb_height(dev->sel, dev->height); /*P,no div*/
		else
			tvd_set_wb_height(dev->sel, dev->height / 2);

		/* pl_yuv420, mb_yuv420, pl_yuv422 */
		tvd_set_wb_fmt(dev->sel, dev->fmt->output_fmt);
		switch (dev->fmt->fourcc) {

		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV16:
			tvd_set_wb_uv_swap(dev->sel, 0);
			break;

		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV61:
			tvd_set_wb_uv_swap(dev->sel, 1);
			break;
		}
	}


	return 0;
}

static int __tvd_3d_comp_mem_request(struct tvd_dev *dev, int size)
{
	unsigned long phyaddr;

	tvd_here;

	dev->fliter.size = PAGE_ALIGN(size);
	dev->fliter.vir_address = dma_alloc_coherent(
	    dev->v4l2_dev.dev, size, (dma_addr_t *)&phyaddr, GFP_KERNEL);
	dev->fliter.phy_address = (void *)phyaddr;
	if (IS_ERR_OR_NULL(dev->fliter.vir_address)) {
		tvd_wrn("%s: 3d fliter buf_alloc failed!\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static void __tvd_3d_comp_mem_free(struct tvd_dev *dev)
{
	u32 actual_bytes;

	tvd_here;
	actual_bytes = PAGE_ALIGN(dev->fliter.size);
	if (dev->fliter.phy_address && dev->fliter.vir_address)
		dma_free_coherent(dev->v4l2_dev.dev, actual_bytes,
				  dev->fliter.vir_address,
				  (dma_addr_t)dev->fliter.phy_address);
}

/*
 * set width,set height, set jump, set wb addr, set 3d_comb
*/
static void __tvd_set_addr(struct tvd_dev *dev, struct tvd_buffer *buffer)
{
	struct tvd_buffer *buf = buffer;
	dma_addr_t addr_org;
	struct vb2_buffer *vb_buf = &buf->vb;
	unsigned int c_offset = 0, i = 0;

	if (vb_buf == NULL || vb_buf->planes[0].mem_priv == NULL) {
		tvd_wrn("%s: vb_buf->priv is NULL!\n", __func__);
		return;
	}

	/*get one frame buffer buffer physical address from dma*/
	addr_org = vb2_dma_contig_plane_dma_addr(vb_buf, 0);

	switch (dev->fmt->output_fmt) {

	case TVD_PL_YUV422:
	case TVD_PL_YUV420:
		c_offset = dev->width * dev->height;
		break;

	case TVD_MB_YUV420:
		c_offset = 0;
		break;
	default:
		break;
	}

	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (!tvd_status[i].tvd_used)
				continue;
			tvd_set_wb_addr(
			    i, addr_org + dev->channel_offset_y[i],
			    addr_org + c_offset + dev->channel_offset_c[i]);
		}
	} else
		tvd_set_wb_addr(dev->sel, addr_org, addr_org + c_offset);

	/* set y_addr,c_addr */
	/*tvd_dbg("%s: format:%d, addr_org = 0x%x, addr_org + c_offset = 0x%x\n",*/
		 /*__func__, dev->format, addr_org, addr_org + c_offset);*/
}

/**
 * @name       :tvd_valid_frame_setting
 * @brief      :for multichannel mode,make sure all channel is locked
 * @param[IN]  :sel:index of tvd module
 * @return     :
 */
s32 tvd_valid_frame_setting(u32 sel, struct tvd_dev *dev)
{
	__u32 i = 0;
	const __s32 blue_start = -30;
	const __s32 blue_middle = 0;
	const __s32 blue_end = 10;
	static __s32 frame_cntr;
	__u32 lock_any_current = 0;
	static __u32 lock_any_latest;


	for (i = 0; i < tvd_total_num; i++) {
		if (!tvd_status[i].tvd_used)
			continue;
		lock_any_current = tvd_get_lock(i);
		if (lock_any_current) {
			lock_any_current = 1;
			break;
		}
	}

	if (lock_any_current == 0 && lock_any_latest == 1 &&
	    blue_end == frame_cntr) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (!tvd_status[i].tvd_used)
				continue;
			tvd_blue_display_mode(i, 1);
		}
		frame_cntr = blue_start;
	}
	if (lock_any_current == 0 && lock_any_latest == 0 &&
	    blue_middle == frame_cntr) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (!tvd_status[i].tvd_used)
				continue;
			tvd_blue_display_mode(i, 2);
		}
	} else if (lock_any_current == 1 && lock_any_latest == 0 &&
		   blue_middle == frame_cntr) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (!tvd_status[i].tvd_used)
				continue;
			tvd_reset(i);
		}
		frame_cntr = blue_middle + 1;
	}
	lock_any_latest = lock_any_current;

	if (frame_cntr != blue_middle && frame_cntr != blue_end)
		frame_cntr++;

	return lock_any_current;
}
/*
 *  the interrupt routine
*/
static irqreturn_t tvd_isr(int irq, void *priv)
{
	struct tvd_buffer *buf;
	unsigned long flags;
	u32 irq_status = 0;
	struct tvd_dev *dev = (struct tvd_dev *)priv;
	struct tvd_dmaqueue *dma_q = &dev->vidq;
	u32 err = (1 << TVD_IRQ_FIFO_C_O) | (1 << TVD_IRQ_FIFO_Y_O) |
		  (1 << TVD_IRQ_FIFO_C_U) | (1 << TVD_IRQ_FIFO_Y_U) |
		  (1 << TVD_IRQ_WB_ADDR_CHANGE_ERR);
	static int flag;
	u32 tvf = 0;

	if (flag == 0) {
		tvd_wrn("In tvd_isr\n");
		flag = 1;
	}
	/*todo:handle this fucking specifial*/
	if (dev->special_active == 1) {
		return tvd_isr_special(irq, priv);
	}

	if (tvd_is_generating(dev) == 0) {
		tvd_irq_status_clear(dev->sel, TVD_IRQ_FRAME_END);
		return IRQ_HANDLED;
	}
	tvd_dma_irq_status_get(dev->sel, &irq_status);
	if ((irq_status & err) != 0)
		tvd_dma_irq_status_clear_err_flag(dev->sel, err);

	spin_lock_irqsave(&dev->slock, flags);

	if (dev->mulit_channel_mode) {
		tvf = tvd_valid_frame_setting(dev->sel, dev);
		if (!tvf)
			goto unlock;
	}

	if (0 == dev->first_flag) {
		/* if is first frame, flag set 1 */
		dev->first_flag = 1;
		goto set_next_addr;
	}
	if (list_empty(&dma_q->active) ||
	    dma_q->active.next->next == (&dma_q->active)) {
		tvd_dbg("No active queue to serve\n");
		goto unlock;
	}

	buf = list_entry(dma_q->active.next, struct tvd_buffer, list);
	/* Nobody is waiting for this buffer*/
	if (!waitqueue_active(&buf->vb.vb2_queue->done_wq)) {
		tvd_dbg(
		    "%s: Nobody is waiting on this video buffer,buf = 0x%p\n",
		    __func__, buf);
	}
	list_del(&buf->list);

	dev->ms += jiffies_to_msecs(jiffies - dev->jiffies);
	dev->jiffies = jiffies;
	/*setting vb2 buffer state to VB2_BUF_STATE_DONE*/
	vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);

	if (list_empty(&dma_q->active)) {
		tvd_dbg("%s: No more free frame\n", __func__);
		goto unlock;
	}

	/* hardware need one frame */
	if ((&dma_q->active) == dma_q->active.next->next) {
		tvd_dbg("No more free frame on next time\n");
		goto unlock;
	}

set_next_addr:
	buf = list_entry(dma_q->active.next->next, struct tvd_buffer, list);
	__tvd_set_addr(dev, buf);

unlock:
	spin_unlock(&dev->slock);
	tvd_irq_status_clear(dev->sel, TVD_IRQ_FRAME_END);

	return IRQ_HANDLED;
}

/*
 * Videobuf operations
*/
static int queue_setup(struct vb2_queue *vq,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], struct device *alloc_devs[])
{
	struct tvd_dev *dev = vb2_get_drv_priv(vq);
	unsigned int size;

	tvd_here;

	switch (dev->fmt->output_fmt) {
	case TVD_MB_YUV420:
	case TVD_PL_YUV420:
		size = dev->width * dev->height * 3 / 2;
		break;
	case TVD_PL_YUV422:
	default:
		size = dev->width * dev->height * 2;
		break;
	}

	if (size == 0)
		return -EINVAL;

	if (*nbuffers < 3) {
		*nbuffers = 3;
		tvd_wrn("buffer conunt invalid, min is 3!\n");
	} else if (*nbuffers > 10) {
		*nbuffers = 10;
		tvd_wrn("buffer conunt invalid, max 10!\n");
	}

	dev->frame_size = size;
	sizes[0] = size;
	*nplanes = 1;
	alloc_devs[0] = &dev->pdev->dev;
	tvd_dbg("%s, buffer count=%d, size=%d\n", __func__, *nbuffers, size);

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct tvd_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct tvd_buffer *buf = container_of(vb, struct tvd_buffer, vb);
	unsigned long size;

	if (dev->width < MIN_WIDTH || dev->width > MAX_WIDTH ||
	    dev->height < MIN_HEIGHT || dev->height > MAX_HEIGHT) {
		return -EINVAL;
	}

	size = dev->frame_size;

	if (vb2_plane_size(vb, 0) < size) {
		tvd_wrn("%s data will not fit into plane (%lu < %lu)\n",
		       __func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(&buf->vb, 0, size);

	vb->planes[0].m.offset = vb2_dma_contig_plane_dma_addr(vb, 0);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct tvd_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct tvd_buffer *buf = container_of(vb, struct tvd_buffer, vb);
	struct tvd_dmaqueue *vidq = &dev->vidq;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->slock, flags);
	list_add_tail(&buf->list, &vidq->active);
	spin_unlock_irqrestore(&dev->slock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct tvd_dev *dev = vb2_get_drv_priv(vq);

	tvd_here;
	tvd_start_generating(dev);
	return 0;
}

/* abort streaming and wait for last buffer */
static void stop_streaming(struct vb2_queue *vq)
{
	struct tvd_dev *dev = vb2_get_drv_priv(vq);
	struct tvd_dmaqueue *dma_q = &dev->vidq;
	unsigned long flags = 0;

	tvd_here;
	tvd_stop_generating(dev);

	/* Release all active buffers */
	spin_lock_irqsave(&dev->slock, flags);
	while (!list_empty(&dma_q->active)) {
		struct tvd_buffer *buf;
		buf = list_entry(dma_q->active.next, struct tvd_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
}

static void tvd_lock(struct vb2_queue *vq)
{
	struct tvd_dev *dev = vb2_get_drv_priv(vq);
	mutex_lock(&dev->buf_lock);
}

static void tvd_unlock(struct vb2_queue *vq)
{
	struct tvd_dev *dev = vb2_get_drv_priv(vq);
	mutex_unlock(&dev->buf_lock);
}

static const struct vb2_ops tvd_video_qops = {
	.queue_setup = queue_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
	.wait_prepare = tvd_unlock,
	.wait_finish = tvd_lock,
};

/*
 * IOCTL vidioc handling
 */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct tvd_dev *dev = video_drvdata(file);

	tvd_here;

	strcpy(cap->driver, "sunxi-tvd");
	strcpy(cap->card, "sunxi-tvd");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));

	cap->version = TVD_VERSION;
	cap->capabilities =
	    V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;

	cap->device_caps |= V4L2_CAP_VIDEO_CAPTURE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct tvd_fmt *fmt;

	tvd_here;

	if (f->index > ARRAY_SIZE(formats) - 1)
		return -EINVAL;

	fmt = &formats[f->index];

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

/**
 * @name       __get_status
 * @brief      get specified tvd device's status
 * @param[IN]  dev
 * @param[OUT] locked: 1:signal locked, 0:signal not locked
 * @param[OUT] system: 1:pal, 0:ntsc
 * @return     none
 */
static void __get_status(struct tvd_dev *dev, unsigned int *locked,
			 unsigned int *system)
{
	int i = 0;
	unsigned int temp_locked = 0;
	unsigned int temp_system = 0;

	tvd_here;

	if (dev->interface > 0) {
		/* ypbpr signal, search i/p */
		dev->interface = 1;
		for (i = 0; i < 2; i++) {
			__tvd_clk_init(dev);
			mdelay(200);

			tvd_get_status(dev->sel, locked, system);
			if (*locked)
				break;

			if (dev->interface < 2)
				dev->interface++;
		}
	} else if (dev->interface == 0) {
		/* cvbs signal */
		mdelay(200);
		if (dev->mulit_channel_mode) {
			for (i = 0; i < tvd_total_num; ++i) {
				if (tvd_status[i].tvd_used) {
					tvd_get_status(i, locked, system);
					tvd_dbg("tvd%d locked:%d system:%d\n",
						i, *locked, *system);
					tvd_status[i].locked = *locked;
					tvd_status[i].tvd_system = *system;
					if (*locked) {
						temp_locked = *locked;
						temp_system = *system;
					}
				}
			}
			*locked =
			    temp_locked; /*if one of ch locked then is locked*/
			/*if one of ch not insert then follow other chns */
			*system = temp_system;
		} else {
			tvd_get_status(dev->sel, locked, system);
			tvd_dbg("tvd%d locked:%d system:%d\n",
						dev->sel, *locked, *system);
		}
	}
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct tvd_dev *dev = video_drvdata(file);
	u32 locked = 0, system = 2, i = 0;

	tvd_here;


	__get_status(dev, &locked, &system);

	if (dev->mulit_channel_mode) {
		f->fmt.raw_data[RAW_DATA_INTERFACE] = dev->interface;
		f->fmt.raw_data[RAW_DATA_SYSTEM] = system;
		f->fmt.raw_data[RAW_DATA_FORMAT] = dev->format;
		f->fmt.raw_data[RAW_DATA_ROW] = dev->row;
		f->fmt.raw_data[RAW_DATA_COLUMN] = dev->column;
		f->fmt.raw_data[RAW_DATA_CH0_INDEX] = dev->channel_index[0];
		f->fmt.raw_data[RAW_DATA_CH1_INDEX] = dev->channel_index[1];
		f->fmt.raw_data[RAW_DATA_CH2_INDEX] = dev->channel_index[2];
		f->fmt.raw_data[RAW_DATA_CH3_INDEX] = dev->channel_index[3];
		for (i = 0; i < tvd_total_num; ++i) {
			if (dev->channel_index[i]) {
				f->fmt.raw_data[RAW_DATA_CH0_STATUS + i] =
				    tvd_status[i].locked;
			}
		}
	} else {
		f->fmt.pix.width = dev->width;
		f->fmt.pix.height = dev->height;
		f->fmt.pix.priv = dev->interface;
		if (!locked) {
			tvd_dbg("%s: signal is not locked.\n", __func__);
			return -EAGAIN;
		} else {
			/* system: 1->pal, 0->ntsc */
			f->fmt.raw_data[RAW_DATA_INTERFACE] = dev->interface;
			f->fmt.raw_data[RAW_DATA_SYSTEM] = system;
			f->fmt.raw_data[RAW_DATA_FORMAT] = dev->format;
			f->fmt.raw_data[RAW_DATA_ROW] = dev->row;
			f->fmt.raw_data[RAW_DATA_COLUMN] = dev->column;
			f->fmt.raw_data[RAW_DATA_CH0_INDEX] = dev->channel_index[0];
			f->fmt.raw_data[RAW_DATA_CH1_INDEX] = dev->channel_index[1];
			f->fmt.raw_data[RAW_DATA_CH2_INDEX] = dev->channel_index[2];
			f->fmt.raw_data[RAW_DATA_CH3_INDEX] = dev->channel_index[3];
			if (system == PAL) {
				f->fmt.pix.width = 720;
				f->fmt.pix.height = 576;
			} else if (system == NTSC) {
				f->fmt.pix.width = 720;
				f->fmt.pix.height = 480;
			} else {
				tvd_wrn("system is not sure.\n");
			}
		}
	}

	tvd_dbg("system = %d, w = %d, h = %d\n", system, f->fmt.pix.width,
		 f->fmt.pix.height);
	return 0;
}

static u32 get_pixelformat_fro_raw_data(struct v4l2_format *f)
{
	u32 pixelformat = 0;

	tvd_here;
	if (f == NULL)
		return V4L2_PIX_FMT_NV21;

	switch (f->fmt.raw_data[RAW_DATA_PIXELFORMAT]) {
	case 0:
		pixelformat = V4L2_PIX_FMT_NV12;
		break;
	case 1:
		pixelformat = V4L2_PIX_FMT_NV21;
		break;
	case 2:
		pixelformat = V4L2_PIX_FMT_NV16;
		break;
	case 3:
		pixelformat = V4L2_PIX_FMT_NV61;
		break;
	case 4:
		pixelformat = 0;
		break;
	default:
		pixelformat = V4L2_PIX_FMT_NV21;
		break;
	}
	return pixelformat;
}

static struct tvd_fmt *__get_format(struct tvd_dev *dev, struct v4l2_format *f)
{
	struct tvd_fmt *fmt;
	__u32 i;
	__u32 pixelformat = 0;

	tvd_here;

	pixelformat = (dev->mulit_channel_mode)
			  ? get_pixelformat_fro_raw_data(f)
			  : f->fmt.pix.pixelformat;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		fmt = &formats[i];
		if (fmt->fourcc == pixelformat) {
			tvd_dbg("fourcc = %d\n", fmt->fourcc);
			break;
		}
	}
	if (i == ARRAY_SIZE(formats))
		return NULL;

	return &formats[i];
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	u32 locked = 0, system = 2;
	struct tvd_dev *dev = video_drvdata(file);
	__get_status(dev, &locked, &system);
	if (dev->mulit_channel_mode) {
		if (system) {
			f->fmt.pix.width = 720*2;
			f->fmt.pix.height = 576*2;
		} else {
			f->fmt.pix.width = 720*2;
			f->fmt.pix.height = 480*2;
		}
	} else {
		if (system) {
			f->fmt.pix.width = 720;
			f->fmt.pix.height = 576;
		} else {
			f->fmt.pix.width = 720;
			f->fmt.pix.height = 480;
		}
	}

	return 0;
}

/**
 * @name       check_user_setting_rule
 * @brief      funciton description
 * @param[IN]   dev: tvd_dev var
 * @return     0: match rule; -1: violation; -2: null arg
 */
static int check_user_setting_rule(const struct tvd_dev *dev)
{
	u32 tvd_num_to_used = 0, i = 0, ch_num_to_used = 0, j = 0;

	tvd_here;
	if (dev == NULL) {
		tvd_wrn("%s:dev is null\n", __func__);
		return -2;
	}

	tvd_num_to_used = dev->row * dev->column;
	if (tvd_num_to_used < 1 || tvd_num_to_used > tvd_total_num) {
		tvd_wrn("%s:row:%d*column:%d=%d must between 1 and %d\n",
			__func__, dev->row, dev->column, tvd_num_to_used,
			tvd_total_num);
		return -1;
	}

	for (i = 0; i < tvd_total_num; ++i) {
		if (dev->channel_index[i])
			++ch_num_to_used;
		if (dev->channel_index[i] > tvd_num_to_used) {
			tvd_wrn("%s:ch%d's index greater then tvd_num_to_used\n",
			       __func__, i);
			return -1;
		}
		for (j = 0; j < tvd_total_num; ++j) {
			if (i == j || dev->channel_index[i] == 0)
				continue;
			if (dev->channel_index[i] == dev->channel_index[j]) {
				tvd_wrn("channel index can not be same!\n");
				return -1;
			}
		}
	}

	if (tvd_num_to_used != ch_num_to_used) {
		tvd_wrn("%s:tvd_num_to_used != ch_num_to_used\n", __func__);
		return -1;
	}

	return 0;
}

/**
 * @name       update_tvd_parm_from_raw_data
 * @brief      update tvd_dev var from v4l2_format
 * @param[OUT]  dev: tvd_dev var
 * @param[IN]  f: v4l2_format
 * @return	0: success, negetive: fail, 1: need to reinit clk and tvd
 */
static int update_tvd_parm_from_raw_data(struct tvd_dev *dev,
					 const struct v4l2_format *f)
{
	int ret = 0, i = 0;
	u32 height = 0, width = 0;
	u32 reinit_flag = 0;

	tvd_here;


	if (dev == NULL || f == NULL)
		return -EINVAL;

	if (dev->row != f->fmt.raw_data[RAW_DATA_ROW] ||
	    dev->column != f->fmt.raw_data[RAW_DATA_COLUMN] ||
	    dev->system != f->fmt.raw_data[RAW_DATA_SYSTEM] ||
	    dev->channel_index[0] != f->fmt.raw_data[RAW_DATA_CH0_INDEX] ||
	    dev->channel_index[1] != f->fmt.raw_data[RAW_DATA_CH1_INDEX] ||
	    dev->channel_index[2] != f->fmt.raw_data[RAW_DATA_CH2_INDEX] ||
	    dev->channel_index[3] != f->fmt.raw_data[RAW_DATA_CH3_INDEX]) {
		reinit_flag = 1;
	}

	dev->system             = f->fmt.raw_data[RAW_DATA_SYSTEM];
	dev->format             = f->fmt.raw_data[RAW_DATA_FORMAT];
	dev->interface          = f->fmt.raw_data[RAW_DATA_INTERFACE];
	dev->row                = f->fmt.raw_data[RAW_DATA_ROW];
	dev->column             = f->fmt.raw_data[RAW_DATA_COLUMN];
	dev->channel_index[0]   = f->fmt.raw_data[RAW_DATA_CH0_INDEX];
	dev->channel_index[1]   = f->fmt.raw_data[RAW_DATA_CH1_INDEX];
	dev->channel_index[2]   = f->fmt.raw_data[RAW_DATA_CH2_INDEX];
	dev->channel_index[3]   = f->fmt.raw_data[RAW_DATA_CH3_INDEX];

	tvd_dbg("system:%d rowxcolumn:%dx%d\n", dev->system, dev->row,
		dev->column);

	if (check_user_setting_rule(dev)) {
		tvd_wrn("%s:violation rule\n", __func__);
		ret = -1;
		goto OUT;
	}

	/*update info*/
	for (i = 0; i < tvd_total_num; ++i) {
		if (dev->channel_index[i]) {
			tvd_status[i].tvd_used = 1;
			tvd_status[i].tvd_opened = 1;
			dev->id = i;
			dev->sel = i;
		} else {
			tvd_status[i].tvd_used = 0;
			if (tvd_status[i].tvd_clk_enabled) {
				tvd_dbg("Disable tvd%d clk\n", i);
				while (__clk_get_enable_count(tvd_clk[i]))
					clk_disable(tvd_clk[i]);
				tvd_status[i].tvd_clk_enabled = 0;
				tvd_dbg("Disable tvd%d channel\n", i);
				tvd_enable_chanel(i, 0);
				tvd_adc_config(i, 0);
				tvd_status[i].tvd_opened = 0;
			}
		}
	}

	dev->irq = tvd_irq[dev->sel];
	dev->clk = tvd_clk[dev->sel];

	switch (dev->format) {
	case TVD_PL_YUV422:
	case TVD_PL_YUV420:
		dev->width = dev->row*720;
		width = 720;
		break;
	case TVD_MB_YUV420:
		dev->width = dev->row*704;
		width = 704;
		break;
	default:
		ret = -1;
		tvd_wrn("ERROR,dev->format %d\n", dev->format);
		dev->width = dev->row*720;
	}

	if ((dev->format == TVD_MB_YUV420) && (dev->column > 1)) {
		/*cut 32 pixels，448%2%32=0*/
		height = dev->system ? 576 : 448;
	} else {
		height = dev->system ? 576 : 480;
	}
	dev->height = height * dev->column;


	for (i = 0; i < tvd_total_num; ++i) {
		if (!tvd_status[i].tvd_used)
			continue;
		if (dev->format == TVD_PL_YUV420) {
			dev->channel_offset_y[i] =
			    ((dev->channel_index[i] - 1) % dev->row) * width +
			    ((dev->channel_index[i] - 1) / dev->row) *
				dev->row * width * height;

			dev->channel_offset_c[i] =
			    ((dev->channel_index[i] - 1) % dev->row) * width +
			    ((dev->channel_index[i] - 1) / dev->row) *
				dev->row * width * height / 2;
		} else if (dev->format == TVD_PL_YUV422) {
			dev->channel_offset_y[i] =
			    ((dev->channel_index[i] - 1) % dev->row) * width +
			    ((dev->channel_index[i] - 1) / dev->row) *
				dev->row * width * height;

			dev->channel_offset_c[i] = dev->channel_offset_y[i];
		} else if (dev->format == TVD_MB_YUV420) {
			dev->channel_offset_y[i] =
			    ((dev->channel_index[i] - 1) % dev->row) * width *
				32 +
			    ((dev->channel_index[i] - 1) / dev->row) *
				dev->row * width * height;
			dev->channel_offset_c[i] =
			    ((dev->channel_index[i] - 1) % dev->row) * width *
				32 +
			    ((dev->channel_index[i] - 1) / dev->row) *
				dev->row * width * height / 2;
		}
		tvd_dbg("%d:c->%d,y->%d\n", i, dev->channel_offset_c[i],
			dev->channel_offset_y[i]);
	}

	if (ret == 0)
		ret = reinit_flag;
OUT:
	return ret;
}

static int tvd_cagc_and_3d_config(struct tvd_dev *dev)
{
	int used = 0, ret = 0, i = 0;
	int value = 0, mode = 0;
	u32 tvd_total_num_tmp = tvd_total_num;

	tvd_here;

	for (; i < tvd_total_num_tmp; ++i) {
		/* agc function */
		if (dev->mulit_channel_mode && !tvd_status[i].tvd_used)
			continue;

		if (!dev->mulit_channel_mode && i != dev->sel)
			continue;

		ret = __tvd_fetch_sysconfig(i, (char *)"agc_auto_enable",
					    &mode);
		if (ret)
			goto CAGC;

		if (mode == 0) {
			/* manual mode */
			ret = __tvd_fetch_sysconfig(
			    i, (char *)"agc_manual_value", &value);
			if (ret)
				goto CAGC;
			tvd_agc_manual_config(i, (u32)value);
			tvd_wrn("tvd%d agc manual:0x%x\n", i, value);
		} else {
			/* auto mode */
			tvd_agc_auto_config(i);
			tvd_wrn("tvd%d agc auto mode\n", i);
		}

CAGC:
		ret = __tvd_fetch_sysconfig(i, (char *)"cagc_enable",
					    &value);
		if (ret)
			continue;
		tvd_cagc_config(i, (u32)value);
		tvd_wrn("tvd%d CAGC enable:0x%x\n", i, value);

	}

	if (dev->mulit_channel_mode)
		goto OUT;
	ret = __tvd_fetch_sysconfig(dev->sel, (char *)"fliter_used", &used);
	if (ret)
		dev->fliter.used = 0;
	else
		dev->fliter.used = used;

	if (dev->fliter.used) {
		mutex_lock(&fliter_lock);
		if (fliter_count < FLITER_NUM) {
			if (__tvd_3d_comp_mem_request(
						      dev, (int)TVD_3D_COMP_BUFFER_SIZE)) {
				/* no mem support for 3d fliter */
				dev->fliter.used = 0;
				mutex_unlock(&fliter_lock);
				tvd_dbg(
					"no mem support for 3d fliter\n");
				goto OUT;
			}
			fliter_count++;
			tvd_3d_mode(dev->sel, 1, (u32)dev->fliter.phy_address);
			tvd_wrn("tvd%d 3d enable :0x%x\n", dev->sel,
				(u32)dev->fliter.phy_address);
		}
		mutex_unlock(&fliter_lock);
	}
OUT:
	tvd_dbg("%s:Enable _3D_FLITER:%d,used:%d\n", __func__, fliter_count,
		dev->fliter.used);
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct tvd_dev *dev;
	s32 ret = 0, i = 0, temp_sel = 0, temp_system = 0, reinit_flag = 0;
	u32 locked = 0, system = 2;

	tvd_here;

	dev = video_drvdata(file);

	if (tvd_is_generating(dev)) {
		tvd_wrn("%s device busy\n", __func__);
		return -EBUSY;
	}

	temp_sel = dev->sel;

	if (dev->mulit_channel_mode) {
		reinit_flag = update_tvd_parm_from_raw_data(dev, f);
		if (reinit_flag < 0) {
			ret = reinit_flag;
			goto OUT;
		}
	} else {
		dev->width = f->fmt.pix.width;
		dev->height = f->fmt.pix.height;
		/* tvd ypbpr now only support 720*480 & 720*576 */
		temp_system = dev->system;
		dev->system = (dev->height == 576) ? PAL : NTSC;
		reinit_flag = (temp_system == dev->system) ? 0 : 1;
	}

	dev->fmt = __get_format(dev, f);
	if (!dev->fmt) {
		tvd_wrn("Fourcc format (0x%08x) invalid.\n",
		       f->fmt.pix.pixelformat);
		return -EINVAL;
	}
	dev->fmt->field = V4L2_FIELD_NONE;

	if (temp_sel != dev->sel) {
		tvd_dbg("Free tvd%d irq:%d\n", temp_sel, tvd_irq[temp_sel]);
		free_irq(tvd_irq[temp_sel], dev);
		/* register irq again*/
		tvd_dbg("request tvd%d's irq:%d\n", dev->sel, dev->irq);
		ret = request_irq(dev->irq, tvd_isr, IRQF_SHARED, dev->name,
				  dev);
		if (ret != 0)
			goto OUT;
	}

	if (dev->mulit_channel_mode) {
		__get_status(dev, &locked, &system);
		if (!locked) {
			tvd_wrn("%s: signal is not locked.\n", __func__);
			ret = -EAGAIN;
			goto OUT;
		}
		for (i = 0; i < tvd_total_num; ++i) {
			if (dev->channel_index[i]) {
				f->fmt.raw_data[RAW_DATA_CH0_STATUS + i] =
				    tvd_status[i].locked;
			}
		}
		if (dev->system != system) {
			dev->system = system;
			reinit_flag = 1;
		}
	}

	/*param has been updated by user*/
	if (reinit_flag == 1) {
		tvd_dbg("%s:need to reinit clk and tvd\n", __func__);
		if (__tvd_clk_init(dev))
			tvd_wrn("%s: clock init fail!\n", __func__);
		/*
		ret = __tvd_clk_enable(dev);
		if (ret != 0)
			goto OUT;
		*/

		if (dev->mulit_channel_mode) {
			for (i = 0; i < tvd_total_num; ++i) {
				if (!tvd_status[i].tvd_used)
					continue;
				tvd_init(i, dev->interface);
			}
		} else
			tvd_init(dev->sel, dev->interface);

	}

	/*update status*/
	tvd_wrn("\ninterface=%d\nsystem=%s\nformat=%d\noutput_fmt=%s\n",
		dev->interface, (!dev->system) ? "NTSC" : "PAL", dev->format,
		(!dev->fmt->output_fmt) ? "YUV420" : "YUV422");
	tvd_wrn("\nrow=%d\ncolumn=%d\nch[0]=%d\nch[1]=%d\nch[2]=%d\nch[3]=%d\n",
		dev->row, dev->column, dev->channel_index[0],
		dev->channel_index[1], dev->channel_index[2],
		dev->channel_index[3]);
	tvd_wrn("\nwidth=%d\nheight=%d\ndev->sel=%d\n", dev->width, dev->height,
		dev->sel);

	__tvd_config(dev);

	tvd_cagc_and_3d_config(dev);
OUT:
	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct tvd_dev *dev = video_drvdata(file);

	tvd_here;
	return vb2_reqbufs(&dev->vb_vidq, p);
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct tvd_dev *dev = video_drvdata(file);

	return vb2_querybuf(&dev->vb_vidq, p);
}


#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct tvd_dev *dev = video_drvdata(file);

	return videobuf_cgmbuf(&dev->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct tvd_dev *dev = video_drvdata(file);
	struct tvd_dmaqueue *dma_q = &dev->vidq;
	struct tvd_buffer *buf;
	int ret = 0;

	tvd_here;

	mutex_lock(&dev->stream_lock);
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = -EINVAL;
		goto streamon_unlock;
	}

	if (tvd_is_generating(dev)) {
		tvd_wrn("stream has been already on\n");
		ret = -1;
		goto streamon_unlock;
	}

	/* Resets frame counters */
	dev->ms = 0;
	dev->jiffies = jiffies;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	ret = vb2_streamon(&dev->vb_vidq, i);
	if (ret)
		goto streamon_unlock;

	buf = list_entry(dma_q->active.next, struct tvd_buffer, list);

	__tvd_set_addr(dev, buf);

	tvd_irq_status_clear(dev->sel, TVD_IRQ_FRAME_END);
	tvd_irq_enable(dev->sel, TVD_IRQ_FRAME_END);
	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (!tvd_status[i].tvd_used)
				continue;
			tvd_capture_on(i);
		}
	} else
		tvd_capture_on(dev->sel);
	tvd_start_generating(dev);

	tvd_wrn("Out vidioc_streamon:%d\n", dev->sel);

streamon_unlock:
	mutex_unlock(&dev->stream_lock);

	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct tvd_dev *dev = video_drvdata(file);
	struct tvd_dmaqueue *dma_q = &dev->vidq;
	int ret = 0;

	mutex_lock(&dev->stream_lock);

	tvd_here;
	if (!tvd_is_generating(dev)) {
		tvd_wrn("%s: stream has been already off\n", __func__);
		ret = 0;
		goto streamoff_unlock;
	}

	tvd_stop_generating(dev);

	/* Resets frame counters */
	dev->ms = 0;
	dev->jiffies = jiffies;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	/* disable hardware */
	tvd_irq_disable(dev->sel, TVD_IRQ_FRAME_END);
	tvd_irq_status_clear(dev->sel, TVD_IRQ_FRAME_END);
	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (tvd_status[i].tvd_used)
				tvd_capture_off(i);
		}
	} else
		tvd_capture_off(dev->sel);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = -EINVAL;
		goto streamoff_unlock;
	}

	ret = vb2_streamoff(&dev->vb_vidq, i);
	if (ret != 0) {
		tvd_wrn("%s: videobu_streamoff error!\n", __func__);
		goto streamoff_unlock;
	}

streamoff_unlock:
	mutex_unlock(&dev->stream_lock);

	return ret;
}

static int vidioc_enum_input(struct file *file, void *priv,
			     struct v4l2_input *inp)
{
	tvd_here;
	if (inp->index > NUM_INPUTS - 1) {
		tvd_wrn("%s: input index invalid!\n", __func__);
		return -EINVAL;
	}

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct tvd_dev *dev = video_drvdata(file);

	tvd_here;

	tvd_dbg("%s:\n", __func__);
	*i = dev->input;
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct tvd_dev *dev = video_drvdata(file);

	tvd_dbg("%s: input_num = %d\n", __func__, i);

	dev->input = i;
	tvd_input_sel(dev->input);
	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parms)
{
	struct tvd_dev *dev = video_drvdata(file);

	tvd_here;
	if (parms->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		parms->parm.capture.timeperframe.numerator = dev->fps.numerator;
		parms->parm.capture.timeperframe.denominator =
		    dev->fps.denominator;
	}

	return 0;
}

static int vidioc_s_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parms)
{
	struct tvd_dev *dev = video_drvdata(file);

	tvd_here;

	if (parms->parm.capture.capturemode != V4L2_MODE_VIDEO &&
	    parms->parm.capture.capturemode != V4L2_MODE_IMAGE &&
	    parms->parm.capture.capturemode != V4L2_MODE_PREVIEW)
		parms->parm.capture.capturemode = V4L2_MODE_PREVIEW;

	dev->capture_mode = parms->parm.capture.capturemode;

	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	int i;
	static const struct v4l2_frmsize_discrete sizes[] = {
	    {
		.width = 720, .height = 480,
	    },
	    {
		.width = 720, .height = 576,
	    },
	};

	tvd_here;

	/* there are two kinds of framesize*/
	if (fsize->index > 1)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fsize->pixel_format)
			break;
	if (i == ARRAY_SIZE(formats)) {
		tvd_wrn("format not found\n");
		return -EINVAL;
	}
	fsize->discrete.width = sizes[fsize->index].width;
	fsize->discrete.height = sizes[fsize->index].height;

	return 0;
}

static ssize_t tvd_read(struct file *file, char __user *data, size_t count,
			loff_t *ppos)
{
	struct tvd_dev *dev = video_drvdata(file);

	tvd_here;

	if (tvd_is_generating(dev)) {
		return vb2_read(&dev->vb_vidq, data, count, ppos,
				file->f_flags & O_NONBLOCK);
	} else {
		tvd_wrn("%s: tvd is not generating!\n", __func__);
		return -EINVAL;
	}
}

static unsigned int tvd_poll(struct file *file, struct poll_table_struct *wait)
{
	struct tvd_dev *dev = video_drvdata(file);
	struct vb2_queue *q = &dev->vb_vidq;

	if (tvd_is_generating(dev)) {
		return vb2_poll(q, file, wait);
	} else {
		tvd_wrn("%s: tvd is not generating!\n", __func__);
		return -EINVAL;
	}
}

static int __tvd_power_enable(int i, bool is_true)
{
	int ret = 0;

	tvd_here;
	regu[i] =  regulator_get(NULL, &tvd_power[i][0]);
	if (IS_ERR_OR_NULL(regu[i])) {
		tvd_wrn("regulator is err.\n");
		return -EBUSY;
	}

	if (is_true) {
		regulator_set_voltage(regu[i], 3300000, 3300000);
		ret = regulator_enable(regu[i]);
	} else
		ret = regulator_disable(regu[i]);

	regulator_put(regu[i]);
	regu[i] = NULL;
	return ret;
}

static int __tvd_gpio_request(struct gpio_config *pin_cfg)
{
	int ret = -1;

	if (!pin_cfg || !gpio_is_valid(pin_cfg->gpio)) {
		tvd_wrn("Null para!\n");
		goto OUT;
	}

	ret = gpio_direction_output(pin_cfg->gpio, pin_cfg->data);
	if (ret) {
		tvd_wrn("gpio_direction_output %d %d fail:%d\n", pin_cfg->gpio,
			pin_cfg->data, ret);
		goto OUT;
	}

OUT:
	return ret;
}

static int tvd_open(struct file *file)
{
	struct tvd_dev *dev = video_drvdata(file);
	int ret = -1;
	int i = 0;

	tvd_here;
	tvd_dbg("open = %s", file->f_path.dentry->d_iname);
	dev->system = 0;
	if (!strncmp(file->f_path.dentry->d_iname, "video8", strlen("video8"))) {
		dev->row = tvd_row;
		dev->column = tvd_column;
		dev->mulit_channel_mode = 1;
		tvd_dbg("open multi_chanel!,dev->row = %d,dev->column = %d\n", dev->row, dev->column);
	} else {
		dev->row = 1;
		dev->column = 1;
		dev->mulit_channel_mode = 0;
		tvd_dbg("single_chanel!\n");
	}
	if (tvd_is_opened(dev)) {
		tvd_wrn("%s: device open busy\n", __func__);
		return -EBUSY;
	}

	if (dev->mulit_channel_mode == 0) {
		if (tvd_status[dev->sel].tvd_opened) {
			tvd_wrn("%s: tvd%d was opened in multich mode\n",
			       __func__, dev->sel);
			return -EBUSY;
		}
		tvd_status[dev->sel].tvd_opened = 1;
	} else {
		for (i = 0; i < tvd_total_num; ++i) {
			if (tvd_status[i].tvd_used &&
			    tvd_status[i].tvd_opened) {
				tvd_wrn("%s: tvd%d was opened in single mode\n",
				       __func__, i);
				return -EBUSY;
			}
			if (tvd_status[i].tvd_used)
				tvd_status[i].tvd_opened = 1;
		}
	}

	if (tvd_hot_plug)
		__tvd_auto_plug_disable(dev);

	/*dev->system = NTSC;*/

	/* gpio power, open only once */
	mutex_lock(&power_lock);
	if (!atomic_read(&gpio_power_enable_count)) {
		for (i = 0; i < atomic_read(&tvd_used_gpio_num); i++)
			ret = __tvd_gpio_request(&tvd_gpio_config[i]);
	}
	atomic_inc(&gpio_power_enable_count);

	/* pmu power */
	for (i = 0; i < atomic_read(&tvd_used_power_num); i++) {
		ret = __tvd_power_enable(i, true);
		if (ret)
			tvd_wrn("power(%s) enable failed.\n", &tvd_power[i][0]);
	}
	mutex_unlock(&power_lock);

	__tvd_init(dev); /*init base addr*/

	/* register irq */
	tvd_dbg("request_irq:%d\n", dev->irq);
	ret = request_irq(dev->irq, tvd_isr, IRQF_SHARED, dev->v4l2_dev.name, dev);
	if (__tvd_clk_init(dev))
		tvd_wrn("%s: clock init fail!\n", __func__);
	ret = __tvd_clk_enable(dev);


	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (!tvd_status[i].tvd_used)
				continue;
			tvd_init(i, dev->interface);
		}
	} else
		tvd_init(dev->sel, dev->interface);


	tvd_start_opened(dev);

	return 0;
}

/*TODO:close all resource*/
static int tvd_close(struct file *file)
{
	struct tvd_dev *dev = video_drvdata(file);
	int ret = 0;
	int i = 0;

	tvd_here;

	tvd_stop_generating(dev);
	tvd_deinit(dev->sel, dev->interface);

	mutex_lock(&fliter_lock);
	tvd_dbg("%s:Enable _3D_FLITER:%d,used:%d\n", __func__, fliter_count,
		dev->fliter.used);
	if (fliter_count > 0 && dev->fliter.used) {
		__tvd_3d_comp_mem_free(dev);
		fliter_count--;
		tvd_3d_mode(dev->sel, 0, 0);
	}
	mutex_unlock(&fliter_lock);

	__tvd_clk_disable(dev);

	if (dev->mulit_channel_mode) {
		for (i = 0; i < tvd_total_num; ++i) {
			if (tvd_status[i].tvd_used) {
				tvd_dbg("Disable tvd%d\n", i);
				tvd_status[i].tvd_opened = 0;
			}
		}
	} else {
		tvd_status[dev->sel].tvd_opened = 0;
	}

	vb2_queue_release(&dev->vb_vidq);
	tvd_stop_opened(dev);

	tvd_dbg("Free tvd%d irq:%d\n", dev->sel, dev->irq);
	free_irq(dev->irq, dev);


	/* close pmu power */
	mutex_lock(&power_lock);
	for (i = 0; i < atomic_read(&tvd_used_power_num); i++) {
		ret = __tvd_power_enable(i, false);
		if (ret)
			tvd_wrn("power(%s) disable failed.\n", &tvd_power[i][0]);
	}

	if (atomic_dec_and_test(&gpio_power_enable_count)) {
		for (i = 0; i < atomic_read(&tvd_used_gpio_num); i++)
			gpio_free(tvd_gpio_config[i].gpio);
	}
	mutex_unlock(&power_lock);

	if (tvd_hot_plug)
		__tvd_auto_plug_enable(dev);

	tvd_dbg("tvd_close end\n");

	return ret;
}

static int tvd_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct tvd_dev *dev = video_drvdata(file);
	int ret;
	tvd_dbg("%s: mmap called, vma=0x%08lx\n", __func__,
		 (unsigned long)vma);
	ret = vb2_mmap(&dev->vb_vidq, vma);
	tvd_dbg("%s: vma start=0x%08lx, size=%ld, ret=%d\n", __func__,
		 (unsigned long)vma->vm_start,
		 (unsigned long)vma->vm_end - (unsigned long)vma->vm_start,
		 ret);

	return ret;
}

static int tvd_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct tvd_dev *dev =
	    container_of(ctrl->handler, struct tvd_dev, ctrl_handler);
	struct v4l2_control c;

	tvd_here;

	memset((void *)&c, 0, sizeof(struct v4l2_control));
	c.id = ctrl->id;
	switch (c.id) {
	case V4L2_CID_BRIGHTNESS:
		c.value = tvd_get_luma(dev->sel);
		break;
	case V4L2_CID_CONTRAST:
		c.value = tvd_get_contrast(dev->sel);
		break;
	case V4L2_CID_SATURATION:
		c.value = tvd_get_saturation(dev->sel);
		break;
	}
	ctrl->val = c.value;

	return ret;
}

static int tvd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tvd_dev *dev =
	    container_of(ctrl->handler, struct tvd_dev, ctrl_handler);
	int ret = 0, i = 0;
	struct v4l2_control c;

	tvd_dbg("%s: %s set value: 0x%x\n", __func__, ctrl->name, ctrl->val);
	c.id = ctrl->id;
	c.value = ctrl->val;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		tvd_dbg("%s: V4L2_CID_BRIGHTNESS sel=%d, val=%d,\n", __func__,
			 dev->sel, ctrl->val);
		if (dev->mulit_channel_mode) {
			for (i = 0; i < tvd_total_num; ++i) {
				if (tvd_status[i].tvd_used)
					tvd_set_luma(i, ctrl->val);
			}

		} else
			tvd_set_luma(dev->sel, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		tvd_dbg("%s: V4L2_CID_CONTRAST sel=%d, val=%d,\n", __func__,
			 dev->sel, ctrl->val);
		if (dev->mulit_channel_mode) {
			for (i = 0; i < tvd_total_num; ++i) {
				if (tvd_status[i].tvd_used)
					tvd_set_contrast(i, ctrl->val);
			}

		} else
			tvd_set_contrast(dev->sel, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		tvd_dbg("%s: V4L2_CID_SATURATION sel=%d, val=%d,\n", __func__,
			 dev->sel, ctrl->val);
		if (dev->mulit_channel_mode) {
			for (i = 0; i < tvd_total_num; ++i) {
				if (tvd_status[i].tvd_used)
					tvd_set_saturation(i, ctrl->val);
			}

		} else
			tvd_set_saturation(dev->sel, ctrl->val);
		break;
	}

	return ret;
}

static void __tvd_set_addr_special(struct tvd_dev *dev,
				   struct tvd_buffer *buffer)
{
	unsigned long addr_org;
	unsigned int c_offset = 0;

	if (buffer == NULL || buffer->paddr == NULL) {
		tvd_wrn("%s: vb_buf->priv is NULL!\n", __func__);
		return;
	}
	addr_org = (unsigned long)buffer->paddr;
	switch (dev->fmt->output_fmt) {
	case TVD_PL_YUV422:
	case TVD_PL_YUV420:
		c_offset = dev->width * dev->height;
		break;
	case TVD_MB_YUV420:
		c_offset = 0;
		break;
	default:
		break;
	}
	tvd_set_wb_addr(dev->sel, addr_org, addr_org + c_offset);
	tvd_dbg("%s: format:%d, addr_org = 0x%p, addr_org + c_offset = 0x%p\n",
		 __func__, dev->format, (void *)addr_org,
		 (void *)(addr_org + c_offset));
}

/* tvd device for special, 0 ~ tvd_count-1 */
int tvd_info_special(void)
{
	return tvd_count;
}
EXPORT_SYMBOL(tvd_info_special);

int tvd_open_special(int tvd_fd)
{
	struct tvd_dev *dev = tvd[tvd_fd];
	int ret = -1;
	int i = 0;
	struct tvd_dmaqueue *active = &dev->vidq_special;
	struct tvd_dmaqueue *done = &dev->done_special;


	dev->row = 1;
	dev->column = 1;
	dev->mulit_channel_mode = 0;


	tvd_dbg("%s:\n", __func__);
	if (tvd_is_opened(dev)) {
		tvd_wrn("%s: device open busy\n", __func__);
		return -EBUSY;
	}
	dev->system = NTSC;


	/* gpio power, open only once */
	mutex_lock(&power_lock);
	if (!atomic_read(&gpio_power_enable_count)) {
		for (i = 0; i < atomic_read(&tvd_used_gpio_num); i++)
			ret = __tvd_gpio_request(&tvd_gpio_config[i]);
	}
	atomic_inc(&gpio_power_enable_count);

	/* pmu power */
	for (i = 0; i < atomic_read(&tvd_used_power_num); i++) {
		ret = __tvd_power_enable(i, true);
		if (ret)
			tvd_wrn("power(%s) enable failed.\n", &tvd_power[i][0]);
	}
	mutex_unlock(&power_lock);

	INIT_LIST_HEAD(&active->active);
	INIT_LIST_HEAD(&done->active);
	__tvd_init(dev);
		/* register irq */
	tvd_dbg("request_irq:%d\n", dev->irq);
	ret = request_irq(dev->irq, tvd_isr, IRQF_SHARED, dev->name, dev);
	if (__tvd_clk_init(dev))
		tvd_wrn("%s: clock init fail!\n", __func__);

	ret = __tvd_clk_enable(dev);

	tvd_init(dev->sel, dev->interface);

	dev->special_active = 1;
	tvd_start_opened(dev);

	return ret;
}
EXPORT_SYMBOL(tvd_open_special);

int tvd_close_special(int tvd_fd)
{
	struct tvd_dev *dev = tvd[tvd_fd];
	int ret = 0;
	int i = 0;
	struct tvd_dmaqueue *active = &dev->vidq_special;
	struct tvd_dmaqueue *done = &dev->done_special;

	tvd_dbg("%s:\n", __func__);
	tvd_stop_generating(dev);
	__tvd_clk_disable(dev);
	tvd_stop_opened(dev);
	tvd_dbg("Free tvd%d irq:%d\n", dev->sel, dev->irq);
	free_irq(dev->irq, dev);
	INIT_LIST_HEAD(&active->active);
	INIT_LIST_HEAD(&done->active);
	dev->special_active = 0;

	/* close pmu power */
	mutex_lock(&power_lock);
	for (i = 0; i < atomic_read(&tvd_used_power_num); i++) {
		ret = __tvd_power_enable(i, false);
		if (ret)
			tvd_wrn("power(%s) disable failed.\n", &tvd_power[i][0]);
	}

	if (atomic_dec_and_test(&gpio_power_enable_count)) {
		for (i = 0; i < atomic_read(&tvd_used_gpio_num); i++)
			gpio_free(tvd_gpio_config[i].gpio);
	}
	mutex_unlock(&power_lock);

	return ret;
}
EXPORT_SYMBOL(tvd_close_special);

int vidioc_s_fmt_vid_cap_special(int tvd_fd, struct v4l2_format *f)
{
	struct tvd_dev *dev = tvd[tvd_fd];
	int ret = 0;

	tvd_dbg("%s:\n", __func__);
	if (tvd_is_generating(dev)) {
		tvd_wrn("%s device busy\n", __func__);
		return -EBUSY;
	}
	dev->fmt = __get_format(dev, f);
	if (!dev->fmt) {
		tvd_wrn("Fourcc format (0x%08x) invalid.\n",
		       f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	dev->width = f->fmt.pix.width;
	dev->height = f->fmt.pix.height;
	dev->fmt->field = V4L2_FIELD_NONE;
	if (dev->height == 576) {
		dev->system = PAL;
		/* To solve the problem of PAL signal is not well.
		 * Accoding to the hardware designer, tvd need 29.7M
		 * clk input on PAL system, so here adjust clk again.
		 * Before this modify, PAL and NTSC have the same
		 * frequency which is 27M.
		 */
		__tvd_clk_init(dev);
	} else {
		dev->system = NTSC;
	}
	__tvd_config(dev);

	tvd_cagc_and_3d_config(dev);

	tvd_dbg("\ninterface=%d\nsystem=%s\nformat=%d\noutput_fmt=%s\n",
		dev->interface, (!dev->system) ? "NTSC" : "PAL", dev->format,
		(!dev->fmt->output_fmt) ? "YUV420" : "YUV422");
	tvd_dbg("\nwidth=%d\nheight=%d\ndev->sel=%d\n", dev->width, dev->height,
		dev->sel);

	return ret;
}
EXPORT_SYMBOL(vidioc_s_fmt_vid_cap_special);

int vidioc_g_fmt_vid_cap_special(int tvd_fd, struct v4l2_format *f)
{
	struct tvd_dev *dev = tvd[tvd_fd];
	u32 locked = 0, system = 2;

	f->fmt.pix.width = dev->width;
	f->fmt.pix.height = dev->height;

	__get_status(dev, &locked, &system);

	if (!locked) {
		tvd_dbg("%s: signal is not locked.\n", __func__);
		return -EAGAIN;
	} else {
		/* system: 1->pal, 0->ntsc */
		if (system == PAL) {
			f->fmt.pix.width = 720;
			f->fmt.pix.height = 576;
		} else if (system == NTSC) {
			f->fmt.pix.width = 720;
			f->fmt.pix.height = 480;
		} else {
			tvd_wrn("system is not sure.\n");
		}
	}

	tvd_dbg("system = %d, w = %d, h = %d\n", system, f->fmt.pix.width,
		 f->fmt.pix.height);

	return 0;
}
EXPORT_SYMBOL(vidioc_g_fmt_vid_cap_special);

int dqbuffer_special(int tvd_fd, struct tvd_buffer **buf)
{
	int ret = 0;
	unsigned long flags = 0;
	struct tvd_dev *dev = tvd[tvd_fd];
	struct tvd_dmaqueue *done = &dev->done_special;

	spin_lock_irqsave(&dev->slock, flags);
	if (!list_empty(&done->active)) {
		*buf = list_first_entry(&done->active, struct tvd_buffer, list);
		list_del(&((*buf)->list));
		(*buf)->state = VB2_BUF_STATE_DEQUEUED;
	} else {
		ret = -1;
	}
	spin_unlock_irqrestore(&dev->slock, flags);

	return ret;
}
EXPORT_SYMBOL(dqbuffer_special);

int qbuffer_special(int tvd_fd, struct tvd_buffer *buf)
{
	struct tvd_dev *dev = tvd[tvd_fd];
	struct tvd_dmaqueue *vidq = &dev->vidq_special;
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&dev->slock, flags);
	list_add_tail(&buf->list, &vidq->active);
	buf->state = VB2_BUF_STATE_QUEUED;
	spin_unlock_irqrestore(&dev->slock, flags);

	return ret;
}
EXPORT_SYMBOL(qbuffer_special);

int vidioc_streamon_special(int tvd_fd, enum v4l2_buf_type i)
{
	struct tvd_dev *dev = tvd[tvd_fd];
	struct tvd_dmaqueue *dma_q = &dev->vidq_special;
	struct tvd_buffer *buf = NULL;
	int ret = 0;

	tvd_dbg("%s:\n", __func__);
	mutex_lock(&dev->stream_lock);
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = -EINVAL;
		goto streamon_unlock;
	}
	if (tvd_is_generating(dev)) {
		tvd_wrn("stream has been already on\n");
		ret = -1;
		goto streamon_unlock;
	}
	dev->ms = 0;
	dev->jiffies = jiffies;
	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	if (!list_empty(&dma_q->active)) {
		buf = list_entry(dma_q->active.next, struct tvd_buffer, list);
	} else {
		tvd_wrn("stream on, but no buffer now.\n");
		goto streamon_unlock;
	}

	__tvd_set_addr_special(dev, buf);
	tvd_irq_status_clear(dev->sel, TVD_IRQ_FRAME_END);
	tvd_irq_enable(dev->sel, TVD_IRQ_FRAME_END);
	tvd_capture_on(dev->sel);
	tvd_start_generating(dev);

streamon_unlock:
	mutex_unlock(&dev->stream_lock);

	return ret;
}
EXPORT_SYMBOL(vidioc_streamon_special);

int vidioc_streamoff_special(int tvd_fd, enum v4l2_buf_type i)
{
	struct tvd_dev *dev = tvd[tvd_fd];
	struct tvd_dmaqueue *dma_q = &dev->vidq_special;
	struct tvd_dmaqueue *donelist = &dev->done_special;
	struct tvd_buffer *buffer;
	unsigned long flags = 0;
	int ret = 0;

	mutex_lock(&dev->stream_lock);

	tvd_dbg("%s:\n", __func__);
	if (!tvd_is_generating(dev)) {
		tvd_wrn("%s: stream has been already off\n", __func__);
		ret = 0;
		goto streamoff_unlock;
	}
	tvd_stop_generating(dev);
	dev->ms = 0;
	dev->jiffies = jiffies;
	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	tvd_irq_disable(dev->sel, TVD_IRQ_FRAME_END);
	tvd_irq_status_clear(dev->sel, TVD_IRQ_FRAME_END);
	tvd_capture_off(dev->sel);
	spin_lock_irqsave(&dev->slock, flags);

	while (!list_empty(&dma_q->active)) {
		buffer =
		    list_first_entry(&dma_q->active, struct tvd_buffer, list);
		list_del(&buffer->list);
		list_add(&buffer->list, &donelist->active);
	}

	spin_unlock_irqrestore(&dev->slock, flags);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ret = -EINVAL;
		goto streamoff_unlock;
	}
	if (ret != 0) {
		tvd_wrn("%s: videobu_streamoff error!\n", __func__);
		goto streamoff_unlock;
	}

streamoff_unlock:
	mutex_unlock(&dev->stream_lock);

	return ret;
}
EXPORT_SYMBOL(vidioc_streamoff_special);

static void (*tvd_buffer_done)(int tvd_fd);
void tvd_register_buffer_done_callback(void *func)
{
	tvd_here;
	tvd_buffer_done = func;
}
EXPORT_SYMBOL(tvd_register_buffer_done_callback);

static irqreturn_t tvd_isr_special(int irq, void *priv)
{
	struct tvd_buffer *buf;
	/*unsigned long flags;*/
	struct tvd_dev *dev = (struct tvd_dev *)priv;
	struct tvd_dmaqueue *dma_q = &dev->vidq_special;
	struct tvd_dmaqueue *done = &dev->done_special;

	if (tvd_is_generating(dev) == 0) {
		tvd_irq_status_clear(dev->sel, TVD_IRQ_FRAME_END);
		return IRQ_HANDLED;
	}


	if (0 == dev->first_flag) {
		dev->first_flag = 1;
		goto set_next_addr;
	}
	if (list_empty(&dma_q->active) ||
	    dma_q->active.next->next == (&dma_q->active)) {
		tvd_dbg("No active queue to serve\n");
		goto unlock;
	}

	buf = list_entry(dma_q->active.next, struct tvd_buffer, list);
	list_del(&buf->list);
	dev->ms += jiffies_to_msecs(jiffies - dev->jiffies);
	dev->jiffies = jiffies;
	list_add_tail(&buf->list, &done->active);

	if (tvd_buffer_done)
		tvd_buffer_done(dev->id);

	if (list_empty(&dma_q->active)) {
		tvd_dbg("%s: No more free frame\n", __func__);
		goto unlock;
	}
	if ((&dma_q->active) == dma_q->active.next->next) {
		tvd_dbg("No more free frame on next time\n");
		goto unlock;
	}

set_next_addr:
	buf = list_entry(dma_q->active.next->next, struct tvd_buffer, list);
	__tvd_set_addr_special(dev, buf);

unlock:

	tvd_irq_status_clear(dev->sel, TVD_IRQ_FRAME_END);

	return IRQ_HANDLED;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct tvd_dev *dev = video_drvdata(file);

	return vb2_qbuf(&dev->vb_vidq, dev->vfd->v4l2_dev->mdev, p);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int ret = 0;
	struct tvd_dev *dev = video_drvdata(file);

	ret = vb2_dqbuf(&dev->vb_vidq, p, file->f_flags & O_NONBLOCK);

	return ret;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static const struct v4l2_ctrl_ops tvd_ctrl_ops = {
	.g_volatile_ctrl = tvd_g_volatile_ctrl, .s_ctrl = tvd_s_ctrl,
};

static const struct v4l2_file_operations tvd_fops = {
	.owner = THIS_MODULE,
	.open = tvd_open,
	.release = tvd_close,
	.read = tvd_read,
	.poll = tvd_poll,
	.unlocked_ioctl = video_ioctl2,

#ifdef CONFIG_COMPAT
/*.compat_ioctl32 = tvd_compat_ioctl32,*/
#endif
	.mmap = tvd_mmap,
};

static const struct v4l2_ioctl_ops tvd_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs = vidioc_reqbufs,
	.vidioc_querybuf = vidioc_querybuf,
	.vidioc_qbuf = vidioc_qbuf,
	.vidioc_dqbuf = vidioc_dqbuf,
	.vidioc_enum_input = vidioc_enum_input,
	.vidioc_g_input = vidioc_g_input,
	.vidioc_s_input = vidioc_s_input,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
	.vidioc_g_parm = vidioc_g_parm,
	.vidioc_s_parm = vidioc_s_parm,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf = vidiocgmbuf,
#endif
};

static struct video_device tvd_template[] = {
	[0] = {
		.name = "tvd_0",
		.fops = &tvd_fops,
		.ioctl_ops = &tvd_ioctl_ops,
		.release = video_device_release,
	    },
	[1] = {
		.name = "tvd_1",
		.fops = &tvd_fops,
		.ioctl_ops = &tvd_ioctl_ops,
		.release = video_device_release,
	    },
	[2] = {
		.name = "tvd_2",
		.fops = &tvd_fops,
		.ioctl_ops = &tvd_ioctl_ops,
		.release = video_device_release,
	    },
	[3] = {
		.name = "tvd_3",
		.fops = &tvd_fops,
		.ioctl_ops = &tvd_ioctl_ops,
		.release = video_device_release,
	    },
	[4] = {
		.name = "tvd_multi_ch",
		.fops = &tvd_fops,
		.ioctl_ops = &tvd_ioctl_ops,
		.release = video_device_release,
	    },
};

static int __tvd_init_controls(struct v4l2_ctrl_handler *hdl)
{

	unsigned int ret = 0;

	tvd_here;

	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &tvd_ctrl_ops, V4L2_CID_BRIGHTNESS, 0, 255, 1,
			  128);
	v4l2_ctrl_new_std(hdl, &tvd_ctrl_ops, V4L2_CID_CONTRAST, 0, 128, 1, 0);
	v4l2_ctrl_new_std(hdl, &tvd_ctrl_ops, V4L2_CID_SATURATION, -4, 4, 1, 0);

	if (hdl->error) {
		tvd_wrn("%s: hdl init err!\n", __func__);
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
	}
	return ret;
}

static int __tvd_fetch_sysconfig(int sel, char *sub_name, int value[])
{
	char compat[32];
	u32 len = 0;
	struct device_node *node;
	int ret = 0;

	tvd_here;

	if (sel == -1)
		len = sprintf(compat, "allwinner,sunxi-tvd");
	else
		len = sprintf(compat, "allwinner,sunxi-tvd%d", sel);
	if (len > 32)
		tvd_wrn("size of mian_name is out of range\n");

	node = of_find_compatible_node(NULL, NULL, compat);
	if (!node) {
		tvd_wrn("of_find_compatible_node %s fail\n", compat);
		return -EINVAL;
	}

	if (of_property_read_u32_array(node, sub_name, value, 1)) {
		tvd_wrn("of_property_read_u32_array %s.%s fail\n", compat,
		       sub_name);
		return -EINVAL;
	}

	return ret;
}

static int __jude_config(struct tvd_dev *dev)
{
	int ret = 0;
	int id = dev->id;
	int tvd_used[TVD_MAX] = {0};
	/*int tvd_if[TVD_MAX] = {0};*/
	int value = 0;

	tvd_here;

	if (id > 3 || id < 0) {
		tvd_wrn("%s: id is wrong!\n", __func__);
		return -ENODEV;
	}

	/* first set sel as id */
	/*dev->sel = id;*/
	tvd_dbg("%s: sel = %d.\n", __func__, dev->sel);

	ret = __tvd_fetch_sysconfig(id, (char *)"tvd_used",
				    &tvd_used[id]);
	if (ret) {
		tvd_wrn("%s: fetch tvd_used %d err!", __func__, id);
		return -EINVAL;
	}
	if (!tvd_used[id]) {
		tvd_dbg("%s: tvd_used[%d] is null.\n", __func__, id);
		return -ENODEV;
	}
    /*
	ret = __tvd_fetch_sysconfig(id, (char *)"tvd_if",
				    &tvd_if[id]);
	if (ret) {
		tvd_wrn("%s: fetch tvd_if %d err!", __func__, id);
		return -EINVAL;
	}
	dev->interface = tvd_if[id];
	*/
	ret = __tvd_fetch_sysconfig(-1, (char *)"tvd_interface", &value);
	dev->interface = value;

	if (id > 0) {
		if (tvd_used[id] && dev->interface > 0) {
			/* when tvd0 used and was configed as ypbpr,can not use
			 * tvd1,2 */
			if (id == 1 || id == 2) {
				return -ENODEV;
			} else if (id == 3) {
				/* reset id as 1, for video4 ypbpr,video5 cvbs*/
				dev->id = 1;
				return 0;
			}
		} else {
			return 0;
		}
	}

	return 0;
}

#if defined(CONFIG_EXTCON)

static const unsigned int tvd_cable[] = {
	EXTCON_DISP_TVD,
	EXTCON_NONE,
};

/*static char switch_lock_name[20];*/
/*static char switch_system_name[20];*/
static struct extcon_dev *switch_lock[TVD_MAX];
static struct extcon_dev *switch_system[TVD_MAX];
static struct task_struct *tvd_task;

static int __tvd_auto_plug_init(struct tvd_dev *dev)
{
	int ret = -1;

	/*snprintf(switch_lock_name, sizeof(switch_lock_name), "tvd_lock%d",*/
		 /*dev->sel);*/

	switch_lock[dev->sel] =
	    devm_extcon_dev_allocate(&dev->pdev->dev, tvd_cable);
	if (IS_ERR_OR_NULL(switch_lock[dev->sel])) {
		tvd_wrn("Alloc switch lock error\n");
		goto err_lock_alloc;
	}

	ret = devm_extcon_dev_register(&dev->pdev->dev, switch_lock[dev->sel]);
	if (ret) {
		tvd_wrn("Register switch lock error\n");
		goto err_lock_regsiter;
	}

	/*snprintf(switch_system_name, sizeof(switch_system_name), "tvd_system%d",*/
		 /*dev->sel);*/

	switch_system[dev->sel] =
	    devm_extcon_dev_allocate(&dev->pdev->dev, tvd_cable);
	if (IS_ERR_OR_NULL(switch_system[dev->sel])) {
		tvd_wrn("Alloc switch lock error\n");
		goto err_system_alloc;
	}

	ret = devm_extcon_dev_register(&dev->pdev->dev, switch_system[dev->sel]);
	if (ret) {
		tvd_wrn("Register switch_system error\n");
		goto err_system_register;
	}

	return ret;

err_system_register:
	devm_extcon_dev_free(&dev->pdev->dev, switch_system[dev->sel]);
	devm_extcon_dev_unregister(&dev->pdev->dev, switch_system[dev->sel]);
err_system_alloc:
err_lock_regsiter:
	devm_extcon_dev_free(&dev->pdev->dev, switch_lock[dev->sel]);
	devm_extcon_dev_unregister(&dev->pdev->dev, switch_lock[dev->sel]);
err_lock_alloc:
	return ret;
}

static void __tvd_auto_plug_exit(struct tvd_dev *dev)
{
	devm_extcon_dev_free(&dev->pdev->dev, switch_lock[dev->sel]);
	devm_extcon_dev_unregister(&dev->pdev->dev, switch_lock[dev->sel]);

	devm_extcon_dev_free(&dev->pdev->dev, switch_system[dev->sel]);
	devm_extcon_dev_unregister(&dev->pdev->dev, switch_system[dev->sel]);
}

/**
 * __tvd_check_detect_toggle() - check if the hpd status toggled
 * @hpd: pointer to hpd status array[3]
 * @hpd_cur: current hpd status
 *
 * Only detect status keeps true for three times, it's plugin,
 * when it is false once, it's plugout.
 */
static bool __tvd_check_detect_toggle(bool *hpd, bool hpd_cur)
{
	bool ret = false;

	/* plug in */
	if ((hpd[0] == true) && (hpd[1] == true) && (hpd[2] == false) &&
	    (hpd_cur == true)) {
		ret = true;
		tvd_dbg("%s, plug in\n", __func__);
	}

	/* plug out */
	if ((hpd[0] == true) && (hpd[1] == true) && (hpd[2] == true) &&
	    (hpd_cur == false)) {
		ret = true;
		tvd_dbg("%s, plug out\n", __func__);
	}

	hpd[2] = hpd[1];
	hpd[1] = hpd[0];
	hpd[0] = hpd_cur;

	return ret;
}

static int __tvd_detect_thread(void *parg)
{
	s32 i = 0;
	u32 locked = 0;
	u32 system = 2;
	static u32 systems[TVD_MAX];
	static bool hpd[TVD_MAX][3];

	for (i = 0; i < TVD_MAX; i++) {
		systems[i] = NONE;
		hpd[i][0] = false;
		hpd[i][1] = false;
		hpd[i][2] = false;
	}

	for (;;) {
		if (kthread_should_stop())
			break;

		for (i = 0; i < tvd_count; i++) {
			tvd_get_status(i, &locked, &system);
			if (__tvd_check_detect_toggle(hpd[i], (locked == 1))) {
				tvd_wrn("hpd=%d, i = %d\n", locked, i);
				extcon_set_state_sync(switch_lock[i], EXTCON_DISP_TVD,
						      locked);
			}
			if (systems[i] != system) {
				tvd_wrn("system = %d, i = %d\n", system, i);
				systems[i] = system;
				extcon_set_state_sync(switch_system[i], EXTCON_DISP_TVD,
						      system);
			}
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(20);
	}

	return 0;
}

static int __tvd_auto_plug_enable(struct tvd_dev *dev)
{
	int ret = 0;
	int i = 0;

	dev->system = NTSC;

	/* gpio power, open only once */
	mutex_lock(&power_lock);
	if (!atomic_read(&gpio_power_enable_count)) {
		for (i = 0; i < atomic_read(&tvd_used_gpio_num); i++)
			ret = __tvd_gpio_request(&tvd_gpio_config[i]);
	}
	atomic_inc(&gpio_power_enable_count);

	/* pmu power */
	for (i = 0; i < atomic_read(&tvd_used_power_num); i++) {
		ret = __tvd_power_enable(i, true);
		if (ret)
			tvd_wrn("power(%s) enable failed.\n", &tvd_power[i][0]);
	}
	mutex_unlock(&power_lock);

	if (__tvd_clk_init(dev))
		tvd_wrn("%s: clock init fail!\n", __func__);

	ret = __tvd_clk_enable(dev);
	__tvd_init(dev);
	tvd_init(dev->sel, dev->interface);

	/* Set system as NTSC */
	dev->width = 720;
	dev->height = 480;
	dev->fmt = &formats[0];

	ret = __tvd_config(dev);

	/* enable detect thread */
	if (!tvd_task) {
		tvd_task = kthread_create(__tvd_detect_thread, (void *)0,
					  "tvd detect");
		if (IS_ERR(tvd_task)) {
			s32 err = 0;
			err = PTR_ERR(tvd_task);
			tvd_task = NULL;
			return err;
		}
		wake_up_process(tvd_task);
	}

	return ret;
}

static int __tvd_auto_plug_disable(struct tvd_dev *dev)
{
	int ret = 0;
	int i = 0;

	__tvd_clk_disable(dev);

	/* close pmu power */
	mutex_lock(&power_lock);
	for (i = 0; i < atomic_read(&tvd_used_power_num); i++) {
		ret = __tvd_power_enable(i, false);
		if (ret)
			tvd_wrn("power(%s) disable failed.\n", &tvd_power[i][0]);
	}

	if (atomic_dec_and_test(&gpio_power_enable_count)) {
		for (i = 0; i < atomic_read(&tvd_used_gpio_num); i++)
			gpio_free(tvd_gpio_config[i].gpio);
	}
	mutex_unlock(&power_lock);

	return ret;
}

#else
static int __tvd_auto_plug_init(struct tvd_dev *dev)
{
	return 0;
}

static void __tvd_auto_plug_exit(struct tvd_dev *dev)
{
}

static int __tvd_auto_plug_enable(struct tvd_dev *dev)
{
	tvd_wrn("You need to enable CONFIG_EXTCON\n");
	return 0;
}

static int __tvd_auto_plug_disable(struct tvd_dev *dev)
{
	return 0;
}
#endif


/**
 * @name       __tvd_multi_ch_probe_init
 * @brief      multi channel into 1 picture
 * @param[IN]  pdev:tvd driver platform device
 * @param[OUT]
 * @return     0 if success
 */
static int __tvd_multi_ch_probe_init(struct platform_device *pdev)
{
	int ret = 0, i = 0, value = 0;
	struct tvd_dev *dev;
	struct video_device *vfd;
	struct vb2_queue *q;
	struct device_node *sub_np[TVD_MAX];
	struct platform_device *sub_pdev[TVD_MAX];
	struct device_node *sub_tvd = NULL;
	char temp_char[40];

	tvd_here;

	for (i = 0; i < tvd_total_num; i++) {
		sub_tvd = of_parse_phandle(pdev->dev.of_node, "tvds", i);
		sub_pdev[i] = of_find_device_by_node(sub_tvd);
		if (sub_pdev[i] == NULL) {
			dev_err(&pdev->dev, "fail to find device for tvd%d!\n",
				i);
			return -1;
		}
		sub_np[i] = sub_pdev[i]->dev.of_node;
	}


	tvd_dbg("config tvd for MultiChannel mode\n");
	/*request mem for dev */
	dev = kzalloc(sizeof(struct tvd_dev), GFP_KERNEL);
	if (!dev) {
		tvd_wrn("request dev mem failed!\n");
		return -ENOMEM;
	}




	spin_lock_init(&dev->slock);

	/* fetch sysconfig,and judge support */
	ret = __tvd_fetch_sysconfig(-1, (char *)"tvd_interface", &value);
	dev->interface = value;
	ret += __tvd_fetch_sysconfig(-1, (char *)"tvd_format", &value);
	dev->format = value;
	ret += __tvd_fetch_sysconfig(-1, (char *)"tvd_system", &value);
	dev->system = value;
	if (ret) {
		tvd_wrn(
		    "get tvd tvd_interface, tvd_format or tvd_system failed!");
		ret = -EINVAL;
		goto FREEDEV;
	}

	if (dev->interface != CVBS_INTERFACE) {
		tvd_wrn("Multi channel mode is only support cvbs interface\n");
		ret = -EINVAL;
		goto FREEDEV;
	}

	dev->row = tvd_row;
	dev->column = tvd_column;
	dev->mulit_channel_mode = 1;
	dev->pdev = pdev;
	dev->generating = 0;
	dev->opened = 0;

	dev->regs_top = tvd_top;
	dev->clk_top = tvd_clk_top;
	dev->clk_mbus = tvd_mbus;
	dev->clk_top_rst = rst_tvd_top;

	/*tvd_status and dev->sel need to be updated*/
	for (i = 0; i < tvd_total_num; i++) {
		snprintf(temp_char, 40, "tvd_channel%d_en", i);
		ret = __tvd_fetch_sysconfig(-1, (char *)temp_char,
					    &dev->channel_index[i]);
		if (ret)
			dev_err(&pdev->dev,
				"get tvd_channel%d failed\n", i);
		else {
			if (dev->channel_index[i]) {
				tvd_status[i].tvd_used = 1;
				pdev->id = i;
				dev->id = i;
				dev->sel = i;
			}
		}
	}


	if (check_user_setting_rule(dev)) {
		dev_err(&pdev->dev, "violation rule\n");
		ret = -1;
		goto FREEDEV;
	}

	/*we will save irq info, base addr info and clk info to*/
	/*tvd_irq, tvd_addr and tvd_clk*/

	/*irq*/
	for (i = 0; i < tvd_total_num; ++i) {
		tvd_irq[i] = irq_of_parse_and_map(sub_np[i], 0);
		if (tvd_irq[i] <= 0) {
			dev_err(&pdev->dev,
				"failed to get tvd%d IRQ resource\n", i);
			ret = -ENXIO;
			goto IOMAP_TVD_ERR;
		}
	}

	dev->irq = tvd_irq[dev->sel]; /*need to be updated*/

	tvd_dbg("tvd %d device's irq is %d\n", dev->sel, dev->irq);

	/*addr*/
	for (i = 0; i < tvd_total_num; i++) {
		tvd_addr[i] = of_iomap(sub_np[i], 0);
		if (IS_ERR_OR_NULL(tvd_addr[i])) {
			dev_err(&pdev->dev, "unable to tvd[%d] registers\n", i);
			ret = -EINVAL;
			goto IOMAP_TOP_ERR;
		}
	}

	/* get tvd clk ,name fix */
	for (i = 0; i < tvd_total_num; i++) {
		snprintf(temp_char, 40, "clk_tvd%d", i);
		tvd_clk[i] = devm_clk_get(&pdev->dev, temp_char);
		if (IS_ERR_OR_NULL(tvd_clk[i])) {
			tvd_wrn("get tvd[%d] clk error!\n", i);
			goto IOMAP_TVD_ERR;
		}
		snprintf(temp_char, 40, "clk_bus_tvd%d", i);
		tvd_bus_clk[i] = devm_clk_get(&pdev->dev, temp_char);
		if (IS_ERR_OR_NULL(tvd_bus_clk[i])) {
			tvd_wrn("get tvd clk bus error!\n");
			goto IOMAP_TVD_ERR;
		}

		snprintf(temp_char, 40, "rst_bus_tvd%d", i);
		rst_tvd[i] = devm_reset_control_get_shared(&pdev->dev, temp_char);
		if (IS_ERR_OR_NULL(rst_tvd[i])) {
			tvd_wrn("get tvd clk RST error!\n");
			goto IOMAP_TVD_ERR;
		}
	}

	dev->clk = tvd_clk[dev->sel]; /*need to be updated*/
	dev->clk_bus = tvd_bus_clk[dev->sel];
	dev->rst_tvd = rst_tvd[dev->sel];

	/* register v4l2 device */
	sprintf(dev->v4l2_dev.name, "multich_v4l2_dev");
	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		tvd_wrn("Error registering v4l2 device\n");
		goto IOMAP_TVD_ERR;
	}

	ret = __tvd_init_controls(&dev->ctrl_handler);
	if (ret) {
		tvd_wrn("Error v4l2 ctrls new!!\n");
		goto V4L2_REGISTER_ERR;
	}
	dev->v4l2_dev.ctrl_handler = &dev->ctrl_handler;

	dev_set_drvdata(&dev->pdev->dev, dev);
	tvd_dbg("%s: v4l2 subdev register.\n", __func__);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(&dev->pdev->dev);
#endif
	vfd = video_device_alloc();
	if (!vfd) {
		tvd_wrn("%s: Error video_device_alloc!\n", __func__);
		goto V4L2_REGISTER_ERR;
	}
	*vfd = tvd_template[4];
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->release = video_device_release_empty;
	vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_STREAMING | V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;

	/* /dev/video8 for multich. */
	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 8);
	if (ret < 0) {
		tvd_wrn("Error video_register_device!!\n");
		goto VIDEO_DEVICE_ALLOC_ERR;
	}

	video_set_drvdata(vfd, dev);

	list_add_tail(&dev->devlist, &devlist);

	dev->vfd = vfd;
	tvd_dbg("Multi channel mode device registered as %s\n",
		video_device_node_name(vfd));


	/* initialize queue */
	q = &dev->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct tvd_buffer);
	q->ops = &tvd_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ret = vb2_queue_init(q);
	if (ret) {
		tvd_wrn("vb2_queue_init failed\n");
		goto VIDEO_DEVICE_REGISTER_ERR;
	}

	INIT_LIST_HEAD(&dev->vidq.active);
	ret = sysfs_create_group(&dev->pdev->dev.kobj,
				 &tvd_attribute_group[4]);
	if (ret) {
		tvd_wrn("sysfs_create failed\n");
		goto VB2_QUEUE_ERR;
	}

	mutex_init(&dev->stream_lock);
	mutex_init(&dev->opened_lock);
	mutex_init(&dev->buf_lock);
	tvd_dbg("interface:%d, format:%d, system:%d,dev->sel:%d\n",
		dev->interface, dev->format, dev->system, dev->sel);
	tvd_dbg("tvd_row:%d, tvd_column:%d\n", dev->row, dev->column);
	tvd_dbg("%d %d %d %d\n", dev->channel_index[0], dev->channel_index[1],
		dev->channel_index[2], dev->channel_index[3]);

	return 0;

VB2_QUEUE_ERR:
	vb2_queue_release(q);

VIDEO_DEVICE_REGISTER_ERR:
	v4l2_device_unregister(&dev->v4l2_dev);

VIDEO_DEVICE_ALLOC_ERR:
	video_device_release(vfd);

V4L2_REGISTER_ERR:
	v4l2_device_unregister(&dev->v4l2_dev);
IOMAP_TVD_ERR:
	iounmap((char __iomem *)dev->regs_tvd);

IOMAP_TOP_ERR:
	iounmap((char __iomem *)dev->regs_top);
FREEDEV:
	kfree(dev);
	return ret;
}

static int __tvd_probe_init(int sel, struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct tvd_dev *dev;
	int ret = 0;
	struct video_device *vfd;
	struct vb2_queue *q;
	char id[40] = {0};

	tvd_here;

	/*request mem for dev*/
	dev = kzalloc(sizeof(struct tvd_dev), GFP_KERNEL);
	if (!dev) {
		tvd_wrn("request dev mem failed!\n");
		return -ENOMEM;
	}

	pdev->id = sel;
	if (pdev->id < 0) {
		tvd_wrn("TVD failed to get alias id\n");
		ret = -EINVAL;
		goto freedev;
	}

	dev->mulit_channel_mode = 0;
	dev->id = pdev->id;
	dev->sel = sel;
	dev->pdev = pdev;
	dev->generating = 0;
	dev->opened = 0;

	spin_lock_init(&dev->slock);

	/* fetch sysconfig,and judge support */
	ret = __jude_config(dev);
	if (ret) {
		tvd_wrn("%s:tvd%d is not used by sysconfig.\n", __func__,
		       dev->id);
		ret = -EINVAL;
		goto freedev;
	}

	tvd[dev->id] = dev;
	memcpy(dev->name, pdev->name, sizeof(pdev->name) + 1);
	dev->irq = irq_of_parse_and_map(np, 0);
	if (dev->irq <= 0) {
		tvd_wrn("failed to get IRQ resource\n");
		ret = -ENXIO;
		goto iomap_tvd_err;
	}

	tvd_dbg("tvd %d device's irq is %d\n", dev->sel, dev->irq);

	dev->regs_tvd = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR_OR_NULL(dev->regs_tvd)) {
		dev_err(&pdev->dev, "unable to tvd registers\n");
		ret = -EINVAL;
		goto iomap_top_err;
	}
	dev->regs_top = tvd_top;
	dev->clk_top = tvd_clk_top;
	dev->clk_mbus = tvd_mbus;
	dev->clk_top_rst = rst_tvd_top;

	snprintf(id, 40, "clk_tvd%d", sel);
	/* get tvd clk ,name fix */
	dev->clk = devm_clk_get(&pdev->dev, id);
	if (IS_ERR_OR_NULL(dev->clk)) {
		tvd_wrn("get tvd clk error!\n");
		goto iomap_tvd_err;
	}
	snprintf(id, 40, "clk_bus_tvd%d", sel);
	dev->clk_bus = devm_clk_get(&pdev->dev, id);
	if (IS_ERR_OR_NULL(dev->clk_bus)) {
		tvd_wrn("get tvd clk bus error!\n");
		goto iomap_tvd_err;
	}

	snprintf(id, 40, "rst_bus_tvd%d", sel);
	dev->rst_tvd = devm_reset_control_get_shared(&pdev->dev, id);
	if (IS_ERR_OR_NULL(dev->rst_tvd)) {
		tvd_wrn("get tvd clk RST error!\n");
		goto iomap_tvd_err;
	}

	/* register v4l2 device */
	sprintf(dev->v4l2_dev.name, "tvd_v4l2_dev%d", dev->id);
	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		tvd_wrn("Error registering v4l2 device\n");
		goto iomap_tvd_err;
	}

	ret = __tvd_init_controls(&dev->ctrl_handler);
	if (ret) {
		tvd_wrn("Error v4l2 ctrls new!!\n");
		goto v4l2_register_err;
	}
	dev->v4l2_dev.ctrl_handler = &dev->ctrl_handler;

	dev_set_drvdata(&dev->pdev->dev, dev);
	tvd_dbg("%s: v4l2 subdev register.\n", __func__);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(&dev->pdev->dev);
#endif

	vfd = video_device_alloc();
	if (!vfd) {
		tvd_wrn("%s: Error video_device_alloc!\n", __func__);
		goto v4l2_register_err;
	}
	*vfd = tvd_template[dev->sel];
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->release = video_device_release_empty;

	vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, dev->id + 4);
	if (ret < 0) {
		tvd_wrn("Error video_register_device!!:%d\n", ret);
		goto video_device_alloc_err;
	}

	video_set_drvdata(vfd, dev);

	list_add_tail(&dev->devlist, &devlist);

	dev->vfd = vfd;
	tvd_dbg("V4L2 tvd device registered as %s\n",
		video_device_node_name(vfd));

	/* Initialize videobuf2 queue as per the buffer type */

	/* initialize queue */
	q = &dev->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct tvd_buffer);
	q->ops = &tvd_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ret = vb2_queue_init(q);
	if (ret) {
		tvd_wrn("vb2_queue_init failed\n");
		goto video_device_register_err;
	}

	INIT_LIST_HEAD(&dev->vidq.active);
	ret = sysfs_create_group(&dev->pdev->dev.kobj,
				 &tvd_attribute_group[dev->sel]);
	if (ret) {
		tvd_wrn("sysfs_create failed\n");
		goto vb2_queue_err;
	}

	mutex_init(&dev->stream_lock);
	mutex_init(&dev->opened_lock);
	mutex_init(&dev->buf_lock);

	if (tvd_hot_plug) {
		ret = __tvd_auto_plug_init(dev);
		if (!ret)
			ret = __tvd_auto_plug_enable(dev);
	}
	tvd_count++;

	return 0;

vb2_queue_err:
	vb2_queue_release(q);

video_device_register_err:
	v4l2_device_unregister(&dev->v4l2_dev);

video_device_alloc_err:
	video_device_release(vfd);

v4l2_register_err:
	v4l2_device_unregister(&dev->v4l2_dev);

iomap_tvd_err:
	iounmap((char __iomem *)dev->regs_tvd);

iomap_top_err:
	iounmap((char __iomem *)dev->regs_top);

freedev:
	kfree(dev);

	return ret;
}

static int tvd_probe(struct platform_device *pdev)
{
	int ret = 0, i = 0;
	struct device_node *sub_tvd = NULL;
	struct platform_device *sub_pdev = NULL;
	u32 multi_ch_in_1_mode = 0;
	char sub_name[32] = {0};
	const char *str;
	int value = 0;

	tvd_here;

	mutex_init(&power_lock);
	mutex_init(&fliter_lock);
	tvd_top = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR_OR_NULL(tvd_top)) {
		dev_err(&pdev->dev, "unable to map tvd top registers\n");
		ret = -EINVAL;
		goto OUT;
	}

	tvd_clk_top = devm_clk_get(&pdev->dev, "clk_bus_tvd_top");
	if (IS_ERR_OR_NULL(tvd_clk_top)) {
		tvd_wrn("get tvd clk error!\n");
		goto IOMAP_TVD_ERR;
	}

	tvd_mbus = devm_clk_get(&pdev->dev, "clk_mbus_tvd");
	if (IS_ERR_OR_NULL(tvd_mbus)) {
		tvd_wrn("get clk_mbus_tvd error!\n");
		goto IOMAP_TVD_ERR;
	}

	rst_tvd_top = devm_reset_control_get_shared(&pdev->dev, "rst_bus_tvd_top");
	if (IS_ERR_OR_NULL(rst_tvd_top)) {
		tvd_wrn("get rst_tvd_top error!\n");
		goto IOMAP_TVD_ERR;
	}

	of_property_read_u32(pdev->dev.of_node, "tvd_hot_plug", &tvd_hot_plug);

	for (i = 0; i < TVD_MAX_POWER_NUM; i++) {
		snprintf(sub_name, sizeof(sub_name), "tvd_power%d", i);
		if (!of_property_read_string(pdev->dev.of_node, sub_name,
					     &str)) {
			atomic_inc(&tvd_used_power_num);
			memcpy(&tvd_power[i][0], str, strlen(str) + 1);
			/*regu[i] = regulator_get(NULL, &tvd_power[i][0]);*/
			regu[i] = NULL;
		}
	}

	for (i = 0; i < TVD_MAX_GPIO_NUM; i++) {
		int gpio;
		snprintf(sub_name, sizeof(sub_name), "tvd_gpio%d", i);
		gpio = of_get_named_gpio_flags(
		    pdev->dev.of_node, sub_name, 0,
		    (enum of_gpio_flags *)&tvd_gpio_config[i]);
		if (gpio_is_valid(gpio))
			atomic_inc(&tvd_used_gpio_num);
	}

	if (of_property_read_u32(pdev->dev.of_node, "tvd-number",
				 &tvd_total_num) < 0) {
		dev_err(&pdev->dev,
			"unable to get tvd-number, force to one!\n");
		tvd_total_num = 1;
	}

	if (tvd_total_num > TVD_MAX) {
		dev_err(&pdev->dev,
			"total number of tvd module is greater then TVD_MAX!\n");
		ret = -1;
		goto IOMAP_TVD_ERR;
	}

	ret = __tvd_fetch_sysconfig(-1, (char *)"tvd_row", &value);
	if (ret) {
		dev_err(&pdev->dev, "unable to get tvd_row, force to one!\n");
		tvd_row = 1;
	} else
		tvd_row = value;

	ret = __tvd_fetch_sysconfig(-1, (char *)"tvd_column", &value);
	if (ret) {
		dev_err(&pdev->dev,
			"unable to get tvd_column, force to one!\n");
		tvd_column = 1;
	} else
		tvd_column = value;

	multi_ch_in_1_mode = tvd_row * tvd_column;
	if (multi_ch_in_1_mode > tvd_total_num) {
		dev_err(
		    &pdev->dev,
		    "tvd_row*tvd_column is greater then total tvd number!\n");
		ret = -1;
		goto IOMAP_TVD_ERR;
	}

	ret = 0;
	for (i = 0; i < tvd_total_num; i++) {
		sub_tvd = of_parse_phandle(pdev->dev.of_node, "tvds", i);
		sub_pdev = of_find_device_by_node(sub_tvd);
		if (!sub_pdev) {
			dev_err(&pdev->dev, "fail to find device for tvd%d!\n",
				i);
			continue;
		}
		if (sub_pdev) {
			ret = __tvd_probe_init(i, sub_pdev);
			if (ret != 0) {
				/* one tvd may init fail because of the
				 * sysconfig */
				tvd_dbg("tvd%d init is failed\n", i);
				ret = 0;
			}
		}
	}

	if (tvd_total_num > 1)
		ret = __tvd_multi_ch_probe_init(pdev);
	return ret;

IOMAP_TVD_ERR:

	iounmap((char __iomem *)tvd_top);

OUT:
	return ret;
}

static int tvd_release(void) /*fix*/
{
	struct tvd_dev *dev;
	struct list_head *list;

	tvd_here;

	tvd_dbg("%s: \n", __func__);
	while (!list_empty(&devlist)) {
		list = devlist.next;
		list_del(list);
		dev = list_entry(list, struct tvd_dev, devlist);
		kfree(dev);
	}
	tvd_dbg("tvd_release ok!\n");

	return 0;
}

static int tvd_remove(struct platform_device *pdev)
{
	struct tvd_dev *dev = (struct tvd_dev *)dev_get_drvdata(&(pdev)->dev);
	int i = 0;

	tvd_here;

	free_irq(dev->irq, dev);
	__tvd_clk_disable(dev);
	iounmap(dev->regs_top);
	iounmap(dev->regs_tvd);
	mutex_destroy(&dev->stream_lock);
	mutex_destroy(&dev->opened_lock);
	mutex_destroy(&dev->buf_lock);
	sysfs_remove_group(&dev->pdev->dev.kobj, &tvd_attribute_group[dev->id]);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&dev->pdev->dev);
#endif

	video_unregister_device(dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);

	for (i = 0; i < atomic_read(&tvd_used_power_num); i++) {
		if (regu[i] != NULL) {
			regulator_put(regu[i]);
			regu[i] = NULL;
		}
	}
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int tvd_runtime_suspend(struct device *d)
{
	return 0;
}

static int tvd_runtime_resume(struct device *d)
{
	return 0;
}

static int tvd_runtime_idle(struct device *d)
{
	if (d) {
		pm_runtime_mark_last_busy(d);
		pm_request_autosuspend(d);
	} else {
		tvd_wrn("%s, tvd device is null\n", __func__);
	}

	return 0;
}
#endif

static int tvd_suspend(struct device *d)
{
#if defined(CONFIG_EXTCON)
	if (tvd_task && tvd_hot_plug) {
		if (!kthread_stop(tvd_task))
			tvd_task = NULL;
	}
#endif
	return 0;
}

static int tvd_resume(struct device *d)
{
#if defined(CONFIG_EXTCON)
	if ((!tvd_task) && (tvd_hot_plug)) {
		tvd_task = kthread_create(__tvd_detect_thread, (void *)0,
					  "tvd detect");
		if (IS_ERR(tvd_task)) {
			s32 err = 0;
			err = PTR_ERR(tvd_task);
			tvd_task = NULL;
			return err;
		}
		wake_up_process(tvd_task);
	}
#endif
	return 0;
}

static void tvd_shutdown(struct platform_device *pdev)
{
}

static const struct dev_pm_ops tvd_runtime_pm_ops = {
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = tvd_runtime_suspend,
	.runtime_resume = tvd_runtime_resume,
	.runtime_idle = tvd_runtime_idle,
#endif
	.suspend = tvd_suspend,
	.resume = tvd_resume,
};

static const struct of_device_id sunxi_tvd_match[] = {
	{
		.compatible = "allwinner,sunxi-tvd",
	},
	{},
};

static struct platform_driver tvd_driver = {
	.probe = tvd_probe,
	.remove = tvd_remove,
	.shutdown = tvd_shutdown,
	.driver = {
		.name = TVD_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sunxi_tvd_match,
		.pm = &tvd_runtime_pm_ops,
	}
};

static int __init tvd_module_init(void)
{
	int ret = 0;
	int value = 0;

	tvd_here;
	ret = __tvd_fetch_sysconfig(-1, (char *)"tvd_sw", &value);
	if (ret) {
		tvd_wrn("Fetch tvd_sw from sys_config.fex fail!\n");
		goto OUT;
	}

	if (value != 1) {
		tvd_dbg("tvd not enable in sys_config.fex!\n");
		goto OUT;
	}

	ret = platform_driver_register(&tvd_driver);
	if (ret) {
		tvd_wrn("platform driver register failed\n");
		return ret;
	}
OUT:
	tvd_dbg("tvd_init end\n");
	return ret;
}

static void __exit tvd_module_exit(void)
{
	int i = 0;

	tvd_dbg("tvd_exit\n");
	if (tvd_hot_plug) {
		for (i = 0; i < tvd_count; i++)
			__tvd_auto_plug_exit(tvd[i]);
	}

	tvd_release();
	platform_driver_unregister(&tvd_driver);
}
#ifdef CONFIG_ARCH_SUN8IW17P1
subsys_initcall_sync(tvd_module_init);
#else
module_init(tvd_module_init);
#endif
module_exit(tvd_module_exit);

MODULE_AUTHOR("zhengxiaobin");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("tvd driver for sunxi");
