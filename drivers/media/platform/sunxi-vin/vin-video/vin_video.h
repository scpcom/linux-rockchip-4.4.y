
/*
 * vin_video.h for video api
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
 * Yang Feng <yangfeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _VIN_VIDEO_H_
#define _VIN_VIDEO_H_

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>
#include "../platform/platform_cfg.h"
#include "../vin-vipp/sunxi_scaler.h"
#include "dma_reg.h"

/* buffer for one video frame */
struct vin_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
	void *paddr;
	enum vb2_buffer_state state;
};

#define VIN_SD_PAD_SINK		0
#define VIN_SD_PAD_SOURCE	1
#define VIN_SD_PADS_NUM		2

enum vin_subdev_ind {
	VIN_IND_SENSOR,
	VIN_IND_MIPI,
	VIN_IND_CSI,
	VIN_IND_ISP,
	VIN_IND_SCALER,
	VIN_IND_CAPTURE,
	VIN_IND_TDM_RX,
	VIN_IND_ACTUATOR,
	VIN_IND_FLASH,
	VIN_IND_STAT,
	VIN_IND_MAX,
};

enum vin_state_flags {
	VIN_LPM,
	VIN_STREAM,
	VIN_BUSY,
	VIN_STATE_MAX,
};

#define vin_lpm(dev) test_bit(VIN_LPM, &(dev)->state)
#define vin_busy(dev) test_bit(VIN_BUSY, &(dev)->state)
#define vin_streaming(dev) test_bit(VIN_STREAM, &(dev)->state)
#define CEIL_EXP(a, b) (((a)>>(b)) + (((a)&((1<<(b))-1)) ? 1 : 0))

struct vin_pipeline {
	struct media_pipeline pipe;
	struct v4l2_subdev *sd[VIN_IND_MAX];
};

enum vin_fmt_flags {
	VIN_FMT_YUV = (1 << 0),
	VIN_FMT_RAW = (1 << 1),
	VIN_FMT_RGB = (1 << 2),
	VIN_FMT_OSD = (1 << 3),
	VIN_FMT_MAX,
	/* all possible flags raised */
	VIN_FMT_ALL = (((VIN_FMT_MAX - 1) << 1) - 1),
};


struct vin_fmt {
	u32	mbus_code;
	char	*name;
	u32	mbus_type;
	u32	fourcc;
	u16	memplanes;
	u16	colplanes;
	u8	depth[VIDEO_MAX_PLANES];
	u16	mdataplanes;
	u16	flags;
	enum v4l2_colorspace color;
	enum v4l2_field	field;
};

struct vin_addr {
	u32	y;
	u32	cb;
	u32	cr;
};

struct vin_frame {
	u32	o_width;
	u32	o_height;
	u32	offs_h;
	u32	offs_v;
	u32	width;
	u32	height;
	unsigned long		payload[VIDEO_MAX_PLANES];
	unsigned long		bytesperline[VIDEO_MAX_PLANES];
	struct vin_addr	paddr;
	struct vin_fmt	fmt;
};

/* osd settings */
struct vin_osd {
	u8 is_set;
	u8 ov_set_cnt;
	u8 overlay_en;
	u8 cover_en;
	u8 orl_en;
	u8 overlay_cnt;
	u8 cover_cnt;
	u8 orl_cnt;
	u8 inv_th;
	u8 inverse_close[MAX_OVERLAY_NUM];
	u8 inv_w_rgn[MAX_OVERLAY_NUM];
	u8 inv_h_rgn[MAX_OVERLAY_NUM];
	u8 global_alpha[MAX_OVERLAY_NUM];
	u8 y_bmp_avp[MAX_OVERLAY_NUM];
	u8 yuv_cover[3][MAX_COVER_NUM];
	u8 yuv_orl[3][MAX_ORL_NUM];
	u8 orl_width;
	int chromakey;
	enum vipp_osd_argb overlay_fmt;
	struct vin_mm ov_mask[2];	/* bitmap addr */
	struct v4l2_rect ov_win[MAX_OVERLAY_NUM];	/* position */
	struct v4l2_rect cv_win[MAX_COVER_NUM];	/* position */
	struct v4l2_rect orl_win[MAX_ORL_NUM];	/* position */
	int rgb_cover[MAX_COVER_NUM];
	int rgb_orl[MAX_ORL_NUM];
	struct vin_fmt *fmt;
};

struct vin_vid_cap {
	struct video_device vdev;
	struct vin_frame frame;
	struct vin_osd osd;
	/* video capture */
	struct vb2_queue vb_vidq;
	struct device *dev;
	struct list_head vidq_active;
	struct list_head vidq_done;
	unsigned int isp_wdr_mode;
	unsigned int capture_mode;
	unsigned int buf_byte_size; /* including main and thumb buffer */
	/*working state */
	bool registered;
	bool special_active;
	bool dma_parms_alloc;
	struct mutex lock;
	unsigned int first_flag; /* indicate the first time triggering irq */
	struct timespec64 ts;
	spinlock_t slock;
	struct vin_pipeline pipe;
	struct vin_core *vinc;
	struct v4l2_subdev subdev;
	struct media_pad vd_pad;
	struct media_pad sd_pads[VIN_SD_PADS_NUM];
	bool user_subdev_api;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *ae_win[4];	/* wb win cluster */
	struct v4l2_ctrl *af_win[4];	/* af win cluster */
	struct work_struct s_stream_task;
	struct work_struct pipeline_reset_task;
	unsigned long state;
	unsigned int frame_delay_cnt;
	struct dma_lbc_cmp lbc_cmp;
	struct dma_bufa_threshold threshold;
	void (*vin_buffer_process)(int id);
};

#if defined CONFIG_ARCH_SUN8IW12P1
static inline int vin_cmp(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static inline void vin_swap(void *a, void *b, int size)
{
	int t = *(int *)a;
	*(int *)a = *(int *)b;
	*(int *)b = t;
}

static inline int vin_unique(int *a, int number)
{
	int i, k = 0;

	for (i = 1; i < number; i++) {
		if (a[k] != a[i]) {
			k++;
			a[k] = a[i];
		}
	}
	return k + 1;
}
#endif
int vin_set_addr(struct vin_core *vinc, struct vb2_buffer *vb,
		      struct vin_frame *frame, struct vin_addr *paddr);
int vin_timer_init(struct vin_core *vinc);
void vin_timer_del(struct vin_core *vinc);
void vin_timer_update(struct vin_core *vinc, int ms);
int sensor_flip_option(struct vin_vid_cap *cap, struct v4l2_control c);
void vin_set_next_buf_addr(struct vin_core *vinc);
void vin_get_rest_buf_cnt(struct vin_core *vinc);
int vin_initialize_capture_subdev(struct vin_core *vinc);
void vin_cleanup_capture_subdev(struct vin_core *vinc);

#endif /*_VIN_VIDEO_H_*/
