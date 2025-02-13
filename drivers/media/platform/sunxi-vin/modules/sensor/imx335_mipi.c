/*
 * A V4L2 driver for imx335 Raw cameras.
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
 *    Liang WeiJie <liangweijie@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <linux/io.h>

#include "camera.h"
#include "sensor_helper.h"

MODULE_AUTHOR("myf");
MODULE_DESCRIPTION("A low-level driver for IMX335 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (24*1000*1000)
#define V4L2_IDENT_SENSOR 0x0335

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

/*
 * The IMX335 i2c address
 */
#define I2C_ADDR 0x34

#define SENSOR_NUM 0x2
#define SENSOR_NAME "imx335_mipi"
#define SENSOR_NAME_2 "imx335_mipi_2"

#define DOL_RHS1	162
#define DOL_RATIO	16

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {

};
#if 0
static struct regval_list sensor_10b2k30_regs[] = {
	{0x3000, 0x01}, //stanby
	{0x3002, 0x01}, //master mode
	{0x3004, 0x04},
	{0x3004, 0x00},
	//All pixel AD Conversion 10bit / Output 10 bit / 891 Mbps /30fps
	{0x3018, 0x00}, //All-pixel scan mode
	{0x3030, 0x94}, //VMAX 4500
	{0x3031, 0x11}, //VMAX
	{0x3032, 0x00}, //VMAX
	{0x3034, 0x26}, //HMAX 550
	{0x3035, 0x02}, //HMAX
	{0x304c, 0x14}, //OPB_SIZE_V
	{0x304e, 0x00}, //HREVERSE
	{0x304f, 0x00}, //VREVERSE
	{0x3050, 0x00}, //AD10bit
	{0x3056, 0xac}, //Y_OUT_SIZE  1964
	{0x3057, 0x07}, //Y_OUT_SIZE
	{0x3072, 0x28},
	{0x3073, 0x00},
	{0x3074, 0xb0},
	{0x3075, 0x00},
	{0x3076, 0x58},
	{0x3077, 0x0f},

	//All pixel
	{0x3078, 0x01},
	{0x3079, 0x02},
	{0x307a, 0xff},
	{0x307b, 0x02},
	{0x307c, 0x00},
	{0x307d, 0x00},
	{0x307e, 0x00},
	{0x307f, 0x00},
	{0x3080, 0x01},
	{0x3081, 0x02},
	{0x3082, 0xff},
	{0x3083, 0x02},
	{0x3084, 0x00},
	{0x3085, 0x00},
	{0x3086, 0x00},
	{0x3087, 0x00},
	{0x30a4, 0x33},
	{0x30a8, 0x10},
	{0x30a9, 0x04},
	{0x30ac, 0x00},
	{0x30ad, 0x00},
	{0x30b0, 0x10},
	{0x30b1, 0x08},
	{0x30b4, 0x00},
	{0x30b5, 0x00},
	{0x30b6, 0x00},
	{0x30b7, 0x00},
	{0x3112, 0x08},
	{0x3113, 0x00},
	{0x3116, 0x08},
	{0x3117, 0x00},

	{0x3199, 0x00},
	{0x319d, 0x00},
	{0x3300, 0x00},
	{0x341c, 0xff},
	{0x341d, 0x01},
	{0x3a01, 0x03},
	{0x3a18, 0x7f},
	{0x3a19, 0x00},
	{0x3a1a, 0x37},
	{0x3a1b, 0x00},
	{0x3a1c, 0x37},
	{0x3a1d, 0x00},
	{0x3a1e, 0xf7},
	{0x3a1f, 0x00},

	{0x3a20, 0x3f},
	{0x3a21, 0x00},
	{0x3a22, 0x6f},
	{0x3a23, 0x00},
	{0x3a24, 0x3f},
	{0x3a25, 0x00},
	{0x3a26, 0x5f},
	{0x3a27, 0x00},
	{0x3a28, 0x2f},
	{0x3a29, 0x00},

	//----891Mbps/lane 24MHz
	{0x300c, 0x3b},
	{0x300d, 0x2a},
	{0x314c, 0x29},
	{0x314d, 0x01},
	{0x315a, 0x06},
	{0x3168, 0xa0},
	{0x316a, 0x7e},
	{0x319e, 0x02},

	{0x3000, 0x00},
	{0x3002, 0x00},
};
#endif
static struct regval_list sensor_12b_2592x1944_p30_regs[] = {
	// 2592x1944-12bit-30fps
	{0x3000, 0x01}, //stanby
	{0x3002, 0x00}, //master mode
	{0x3004, 0x04},
	{0x3004, 0x00},
	//All pixel AD Conversion 12bit / Output 12 bit / 1188 Mbps /30fps
	{0x3018, 0x00}, //All-pixel scan mode
	{0x3030, 0x94}, //VMAX 4500
	{0x3031, 0x11}, //VMAX
	{0x3032, 0x00}, //VMAX
	{0x3034, 0x26}, //HMAX 550
	{0x3035, 0x02}, //HMAX
	{0x304c, 0x14}, //OPB_SIZE_V
	{0x304e, 0x00}, //HREVERSE
	{0x304f, 0x00}, //VREVERSE
	{0x3050, 0x01}, //AD12bit
	{0x3056, 0xac},
	{0x3057, 0x07},
	{0x3072, 0x28},
	{0x3073, 0x00},
	{0x3074, 0xb0},
	{0x3075, 0x00},
	{0x3076, 0x58},
	{0x3077, 0x0f},

	{0x3199, 0x00},
	{0x319d, 0x01}, //12bit AD
	{0x3300, 0x00},
	{0x341c, 0x47}, //12bit AD
	{0x341d, 0x00},
	{0x3a01, 0x03},
	{0x3a18, 0x8f},
	{0x3a19, 0x00},
	{0x3a1a, 0x4f},
	{0x3a1b, 0x00},
	{0x3a1c, 0x47},
	{0x3a1d, 0x00},
	{0x3a1e, 0x37},
	{0x3a1f, 0x01},

	{0x3a20, 0x4f},
	{0x3a21, 0x00},
	{0x3a22, 0x87},
	{0x3a23, 0x00},
	{0x3a24, 0x4f},
	{0x3a25, 0x00},
	{0x3a26, 0x7f},
	{0x3a27, 0x00},
	{0x3a28, 0x3f},
	{0x3a29, 0x00},

	//----1188Mbps/lane 24MHz
	{0x300c, 0x3b},
	{0x300d, 0x2a},
	{0x314c, 0xc6},
	{0x314d, 0x00},
	{0x315a, 0x02},
	{0x3168, 0xa0},
	{0x316a, 0x7e},
	{0x319e, 0x01},

	{0x3000, 0x00}, //operation
};
#if 0
static struct regval_list sensor_10b_2592x1944_p30wdr_regs[] = {
	// 2592x1944-10bit-30fps
	{0x3000, 0x01}, //stanby
	{0x3002, 0x00}, //master mode
	{0x3004, 0x04},
	{0x3004, 0x00},
	{0x30e8, 0x8c}, //gain

	//All pixel AD Conversion 10bit / Output 10 bit / 1188 Mbps /30fps
	{0x3018, 0x00}, //All-pixel scan mode
	{0x3030, 0x94}, //VMAX 4500
	{0x3031, 0x11}, //VMAX
	{0x3032, 0x00}, //VMAX
	{0x3034, 0x13}, //HMAX 275
	{0x3035, 0x01}, //HMAX
	{0x304c, 0x14}, //OPB_SIZE_V
	{0x304e, 0x00}, //HREVERSE
	{0x304f, 0x00}, //VREVERSE
	{0x3050, 0x00}, //AD10bit
	{0x3056, 0xac},
	{0x3057, 0x07},
	{0x3072, 0x28},
	{0x3073, 0x00},
	{0x3074, 0xb0},
	{0x3075, 0x00},
	{0x3076, 0x58},
	{0x3077, 0x0f},

	{0x3199, 0x00},
	{0x319d, 0x00}, //10bit AD
	{0x3300, 0x00},
	{0x341c, 0xff}, //10bit AD
	{0x341d, 0x01},
	{0x3a01, 0x03},
	{0x3a18, 0x8f},
	{0x3a19, 0x00},
	{0x3a1a, 0x4f},
	{0x3a1b, 0x00},
	{0x3a1c, 0x47},
	{0x3a1d, 0x00},
	{0x3a1e, 0x37},
	{0x3a1f, 0x01},

	{0x3a20, 0x4f},
	{0x3a21, 0x00},
	{0x3a22, 0x87},
	{0x3a23, 0x00},
	{0x3a24, 0x4f},
	{0x3a25, 0x00},
	{0x3a26, 0x7f},
	{0x3a27, 0x00},
	{0x3a28, 0x3f},
	{0x3a29, 0x00},

	//----1188Mbps/lane 24MHz
	{0x300c, 0x3b},
	{0x300d, 0x2a},
	{0x314c, 0xc6},
	{0x314d, 0x00},
	{0x315a, 0x02},
	{0x3168, 0xa0},
	{0x316a, 0x7e},
	{0x319e, 0x01},
	//wdr
	{0x319f, 0x00}, //VCEN - LI (virtual channel mode)
	{0x3048, 0x01}, //WDMODE - DOL mode
	{0x3049, 0x01}, //WDSEL - DOL 2 frame
	{0x304a, 0x04}, //WD_SET1 - DOL 2 frame
	{0x304b, 0x03}, //WD_SET2 - DOL
	//{0x3050, 0x00}, //ADBIT - 10bit
	{0x3058, 0xe8}, //SHR0
	{0x3059, 0x21},
	{0x305a, 0x00},
	{0x305c, 0x8e}, //SHR1
	{0x305d, 0x00},
	{0x305e, 0x00},

	{0x3068, (DOL_RHS1 & 0xff)},	//RHS1
	{0x3069, ((DOL_RHS1 >> 8) & 0xff)},
	{0x306a, ((DOL_RHS1 >> 16) & 0x0f)},
	//{0x319d, 0x00},	//MDBIT - 10bit
	//{0x341c, 0xff},	//ADBIT1 - 10bit
	//{0x341d, 0x01},
	{0x304c, 0x00},	//OPB_SIZE_V - LI mode
	{0x3056, 0xc5}, //Y_OUT_SIZE  LI
	{0x3057, 0x07},
	{0x31d7, 0x01}, //XVSMSKCNT

	{0x3000, 0x00}, //operation
};
#endif
/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 *
 */

static struct regval_list sensor_fmt_raw[] = {

};

/*
 * Code for dealing with controls.
 * fill with different sensor module
 * different sensor module has different settings here
 * if not support the follow function ,retrun -EINVAL
 */

static int sensor_g_exp(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	*value = info->exp;
	sensor_dbg("sensor_get_exposure = %d\n", info->exp);
	return 0;
}

static int imx335_sensor_vts;
static int imx335_sensor_svr;
static int shutter_delay = 1;
static int shutter_delay_cnt;
static int fps_change_flag;

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	data_type explow, expmid, exphigh;
	int exptime,  exp_val_m;
	struct sensor_info *info = to_state(sd);
	//return 0;
	if (info->isp_wdr_mode == ISP_DOL_WDR_MODE) {
		// LEF
		exptime = (imx335_sensor_vts<<1) - (exp_val>>4);
		if (exptime < DOL_RHS1 + 18) {
			exptime = DOL_RHS1 + 18;
			exp_val = ((imx335_sensor_vts << 1) - exptime) << 4;
		}
		sensor_dbg("long exp_val: %d, exptime: %d\n", exp_val, exptime);

		exphigh	= (unsigned char) ((0x00f0000 & exptime) >> 16);
		expmid	= (unsigned char) ((0x000ff00 & exptime) >> 8);
		explow	= (unsigned char) ((0x00000ff & exptime));

		sensor_write(sd, 0x3058, explow);
		sensor_write(sd, 0x3059, expmid);
		sensor_write(sd, 0x305a, exphigh);
		// SEF
		exp_val_m = exp_val / DOL_RATIO;
		if (exp_val_m < 4 * 16)
			exp_val_m = 4 * 16;
		exptime = DOL_RHS1 - (exp_val_m >> 4);
		if (exptime < 18)
			exptime = 18;

		sensor_dbg("short exp_val: %d, exptime: %d\n", exp_val_m, exptime);

		exphigh	= (unsigned char) ((0x00f0000 & exptime) >> 16);
		expmid	= (unsigned char) ((0x000ff00 & exptime) >> 8);
		explow	= (unsigned char) ((0x00000ff & exptime));

		sensor_write(sd, 0x305c, explow);
		sensor_write(sd, 0x305d, expmid);
		sensor_write(sd, 0x305e, exphigh);
	} else {
		exptime = imx335_sensor_vts - (exp_val >> 4) - 1;
		exphigh = (unsigned char)((0x00f0000 & exptime) >> 16);
		expmid =  (unsigned char)((0x000ff00 & exptime) >> 8);
		explow =  (unsigned char)((0x00000ff & exptime));
		sensor_write(sd, 0x3058, explow);
		sensor_write(sd, 0x3059, expmid);
		sensor_write(sd, 0x305a, exphigh);
		sensor_dbg("sensor_set_exp = %d %d line Done!\n", exp_val, exptime);
	}
	info->exp = exp_val;
	return 0;
}

static int sensor_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	*value = info->gain;
	sensor_dbg("sensor_get_gain = %d\n", info->gain);
	return 0;
}

unsigned char gain2db[497] = {
	0,   2,   3,	 5,   6,   8,	9,  11,  12,  13,  14,	15,  16,  17,
	18,  19,  20,	21,  22,  23,  23,  24,  25,  26,  27,	27,  28,  29,
	29,  30,  31,	31,  32,  32,  33,  34,  34,  35,  35,	36,  36,  37,
	37,  38,  38,	39,  39,  40,  40,  41,  41,  41,  42,	42,  43,  43,
	44,  44,  44,	45,  45,  45,  46,  46,  47,  47,  47,	48,  48,  48,
	49,  49,  49,	50,  50,  50,  51,  51,  51,  52,  52,	52,  52,  53,
	53,  53,  54,	54,  54,  54,  55,  55,  55,  56,  56,	56,  56,  57,
	57,  57,  57,	58,  58,  58,  58,  59,  59,  59,  59,	60,  60,  60,
	60,  60,  61,	61,  61,  61,  62,  62,  62,  62,  62,	63,  63,  63,
	63,  63,  64,	64,  64,  64,  64,  65,  65,  65,  65,	65,  66,  66,
	66,  66,  66,	66,  67,  67,  67,  67,  67,  68,  68,	68,  68,  68,
	68,  69,  69,	69,  69,  69,  69,  70,  70,  70,  70,	70,  70,  71,
	71,  71,  71,	71,  71,  71,  72,  72,  72,  72,  72,	72,  73,  73,
	73,  73,  73,	73,  73,  74,  74,  74,  74,  74,  74,	74,  75,  75,
	75,  75,  75,	75,  75,  75,  76,  76,  76,  76,  76,	76,  76,  77,
	77,  77,  77,	77,  77,  77,  77,  78,  78,  78,  78,	78,  78,  78,
	78,  79,  79,	79,  79,  79,  79,  79,  79,  79,  80,	80,  80,  80,
	80,  80,  80,	80,  80,  81,  81,  81,  81,  81,  81,	81,  81,  81,
	82,  82,  82,	82,  82,  82,  82,  82,  82,  83,  83,	83,  83,  83,
	83,  83,  83,	83,  83,  84,  84,  84,  84,  84,  84,	84,  84,  84,
	84,  85,  85,	85,  85,  85,  85,  85,  85,  85,  85,	86,  86,  86,
	86,  86,  86,	86,  86,  86,  86,  86,  87,  87,  87,	87,  87,  87,
	87,  87,  87,	87,  87,  88,  88,  88,  88,  88,  88,	88,  88,  88,
	88,  88,  88,	89,  89,  89,  89,  89,  89,  89,  89,	89,  89,  89,
	89,  90,  90,	90,  90,  90,  90,  90,  90,  90,  90,	90,  90,  91,
	91,  91,  91,	91,  91,  91,  91,  91,  91,  91,  91,	91,  92,  92,
	92,  92,  92,	92,  92,  92,  92,  92,  92,  92,  92,	93,  93,  93,
	93,  93,  93,	93,  93,  93,  93,  93,  93,  93,  93,	94,  94,  94,
	94,  94,  94,	94,  94,  94,  94,  94,  94,  94,  94,	95,  95,  95,
	95,  95,  95,	95,  95,  95,  95,  95,  95,  95,  95,	95,  96,  96,
	96,  96,  96,	96,  96,  96,  96,  96,  96,  96,  96,	96,  96,  97,
	97,  97,  97,	97,  97,  97,  97,  97,  97,  97,  97,	97,  97,  97,
	97,  98,  98,	98,  98,  98,  98,  98,  98,  98,  98,	98,  98,  98,
	98,  98,  98,	99,  99,  99,  99,  99,  99,  99,  99,	99,  99,  99,
	99,  99,  99,	99,  99,  99, 100, 100, 100, 100, 100, 100, 100, 100,
	100, 100, 100, 100, 100, 100, 100,
};
static int sensor_s_gain(struct v4l2_subdev *sd, int gain_val)
{
	struct sensor_info *info = to_state(sd);
	//return 0;
	if (gain_val < 1 * 16)
		gain_val = 16;

		if (gain_val < 32 * 16) {
			sensor_write(sd, 0x30e8, gain2db[gain_val - 16]);
		} else {
			sensor_write(sd, 0x30e8, gain2db[(gain_val>>5) - 16] + 100);
		}
	sensor_dbg("sensor_set_gain = %d, Done!\n", gain_val);
	info->gain = gain_val;

	return 0;
}

static int sensor_s_exp_gain(struct v4l2_subdev *sd,
			     struct sensor_exp_gain *exp_gain)
{
	struct sensor_info *info = to_state(sd);
	int exp_val, gain_val;

	exp_val = exp_gain->exp_val;
	gain_val = exp_gain->gain_val;

	if (gain_val < 1 * 16)
		gain_val = 16;
	if (exp_val > 0xfffff)
		exp_val = 0xfffff;
	if (fps_change_flag) {
		if (shutter_delay_cnt == shutter_delay) {
			sensor_write(sd, 0x3030, imx335_sensor_vts / (imx335_sensor_svr + 1) & 0xFF);
			sensor_write(sd, 0x3031, imx335_sensor_vts / (imx335_sensor_svr + 1) >> 8 & 0xFF);
			sensor_write(sd, 0x3032, imx335_sensor_vts / (imx335_sensor_svr + 1) >> 16);
			sensor_write(sd, 0x3001, 0);
			shutter_delay_cnt = 0;
			fps_change_flag = 0;
		} else
			shutter_delay_cnt++;
	}

	sensor_write(sd, 0x3001, 0x01);
	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);
	sensor_write(sd, 0x3001, 0x00);
	sensor_dbg("sensor_set_gain exp = %d, %d Done!\n", gain_val, exp_val);

	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}

static int sensor_s_fps(struct v4l2_subdev *sd,
			struct sensor_fps *fps)
{
	data_type rdval1, rdval2, rdval3;
	struct sensor_info *info = to_state(sd);
	struct sensor_win_size *wsize = info->current_wins;
	imx335_sensor_vts = wsize->pclk/fps->fps/wsize->hts;
	fps_change_flag = 1;
	sensor_write(sd, 0x3001, 1);

	sensor_read(sd, 0x3030, &rdval1);
	sensor_read(sd, 0x3031, &rdval2);
	sensor_read(sd, 0x3032, &rdval3);

	sensor_dbg("imx335_sensor_svr: %d, vts: %d.\n", imx335_sensor_svr, (rdval1 | (rdval2<<8) | (rdval3<<16)));
	return 0;
}
static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	int ret;
	data_type rdval;

	ret = sensor_read(sd, 0x3000, &rdval);
	if (ret != 0)
		return ret;

	if (on_off == STBY_ON)
		ret = sensor_write(sd, 0x3000, rdval | 0x01);
	else
		ret = sensor_write(sd, 0x3000, rdval & 0xfe);
	return ret;
}

static int sensor_s_stby(struct v4l2_subdev *sd, int *on_off)
{
	return sensor_s_sw_stby(sd, *on_off);
}

/*
 * Stuff that knows about the sensor.
 */
static int sensor_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;

	switch (on) {
	case STBY_ON:
		sensor_dbg("STBY_ON!\n");
		cci_lock(sd);
		ret = sensor_s_sw_stby(sd, STBY_ON);
		if (ret < 0)
			sensor_err("soft stby falied!\n");
		usleep_range(1000, 1200);
		cci_unlock(sd);
		break;
	case STBY_OFF:
		sensor_dbg("STBY_OFF!\n");
		cci_lock(sd);
		usleep_range(1000, 1200);
		ret = sensor_s_sw_stby(sd, STBY_OFF);
		if (ret < 0)
			sensor_err("soft stby off falied!\n");
		cci_unlock(sd);
		break;
	case PWR_ON:
		sensor_dbg("PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		vin_gpio_set_status(sd, POWER_EN, 1);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_HIGH);
		vin_set_pmu_channel(sd, IOVDD, ON);
		usleep_range(2000, 2200);
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_set_pmu_channel(sd, DVDD, ON);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(100, 120);
		vin_set_mclk(sd, ON);
		usleep_range(100, 120);
		vin_set_mclk_freq(sd, MCLK);
		usleep_range(3000, 3200);
		cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_dbg("PWR_OFF!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_set_mclk(sd, OFF);
		vin_set_pmu_channel(sd, AVDD, OFF);
		vin_set_pmu_channel(sd, IOVDD, OFF);
		vin_set_pmu_channel(sd, DVDD, OFF);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_LOW);
		vin_gpio_set_status(sd, RESET, 0);
		vin_gpio_set_status(sd, PWDN, 0);
		vin_gpio_set_status(sd, POWER_EN, 0);
		cci_unlock(sd);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sensor_reset(struct v4l2_subdev *sd, u32 val)
{
	switch (val) {
	case 0:
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		usleep_range(100, 120);
		break;
	case 1:
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(100, 120);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	data_type rdval = 0;
	sensor_read(sd, 0x3008, &rdval);
	sensor_dbg("%s read value is 0x%x\n", __func__, rdval);
	return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	sensor_dbg("sensor_init\n");

	/*Make sure it is a target sensor */
	ret = sensor_detect(sd);
	if (ret) {
		sensor_err("chip found is not an target chip.\n");
		return ret;
	}

	info->focus_status = 0;
	info->low_speed = 0;
	info->width = HD1080_WIDTH;
	info->height = HD1080_HEIGHT;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 30;	/* 30fps */

	return 0;
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	int ret = 0;
	struct sensor_info *info = to_state(sd);

	switch (cmd) {
	case GET_CURRENT_WIN_CFG:
		if (info->current_wins != NULL) {
			memcpy(arg, info->current_wins,
				sizeof(struct sensor_win_size));
			ret = 0;
		} else {
			sensor_err("empty wins!\n");
			ret = -1;
		}
		break;
	case SET_FPS:
		ret = 0;
		break;
	case VIDIOC_VIN_SENSOR_EXP_GAIN:
		ret = sensor_s_exp_gain(sd, (struct sensor_exp_gain *)arg);
		break;
	case VIDIOC_VIN_SENSOR_SET_FPS:
		ret = sensor_s_fps(sd, (struct sensor_fps *)arg);
		break;
	case VIDIOC_VIN_SENSOR_CFG_REQ:
		sensor_cfg_req(sd, (struct sensor_config *)arg);
		break;
	case SET_SENSOR_STANDBY:
		sensor_s_stby(sd, (int *)arg);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

/*
 * Store information about the video data format.
 */
static struct sensor_format_struct sensor_formats[] = {
	{
		.desc = "Raw RGB Bayer",
		//.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.regs = sensor_fmt_raw,
		.regs_size = ARRAY_SIZE(sensor_fmt_raw),
		.bpp = 1
	},
};
#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct sensor_win_size sensor_win_sizes[] = {
#if 0
	{
	 .width = 1800,
	 .height = 1800,
	 .hoffset = 396,
	 .voffset = 72,
	 .hts = 550,
	 .vts = 4500,
	 .pclk = 74250000,
	 .mipi_bps = 891 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .lp_mode = SENSOR_LP_DISCONTINUOUS,
	 .intg_min = 1 << 4,
	 .intg_max = (4500 - 10) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 2000 << 4,
	 .regs = sensor_10b2k30_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b2k30_regs),
	 .set_size = NULL,
	 },
#endif

	 {
	 .width = 2592,
	 .height = 1944,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 550,
	 .vts = 4500,
	 .pclk = 74250000,
	 .mipi_bps = 1188 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .lp_mode = SENSOR_LP_DISCONTINUOUS,
	 .intg_min = 1 << 4,
	 .intg_max = (4500 - 10) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 2000 << 4,
	 .regs = sensor_12b_2592x1944_p30_regs,
	 .regs_size = ARRAY_SIZE(sensor_12b_2592x1944_p30_regs),
	 .set_size = NULL,
	 },

	 {
	 .width = 2560,
	 .height = 1440,
	 .hoffset = 16,
	 .voffset = 252,
	 .hts = 550,
	 .vts = 4500,
	 .pclk = 74250000,
	 .mipi_bps = 1188 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .lp_mode = SENSOR_LP_DISCONTINUOUS,
	 .intg_min = 1 << 4,
	 .intg_max = (4500 - 10) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 2000 << 4,
	 .regs = sensor_12b_2592x1944_p30_regs,
	 .regs_size = ARRAY_SIZE(sensor_12b_2592x1944_p30_regs),
	 .set_size = NULL,
	 },

#if 0
	 {
	 .width = 2592,
	 .height = 1944,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 275,
	 .vts = 4500,
	 .pclk = 74250000,
	 .mipi_bps = 1188 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .lp_mode = SENSOR_LP_DISCONTINUOUS,
	 .if_mode = MIPI_DOL_WDR_MODE,
	 .wdr_mode = ISP_DOL_WDR_MODE,
	 .intg_min = 1 << 4,
	 .intg_max = (4500 - 10) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 2000 << 4,
	 .regs = sensor_10b_2592x1944_p30wdr_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b_2592x1944_p30wdr_regs),
	 .set_size = NULL,
	 },

	 {
	 .width = 2560,
	 .height = 1440,
	 .hoffset = 16,
	 .voffset = 252,
	 .hts = 275,
	 .vts = 4500,
	 .pclk = 74250000,
	 .mipi_bps = 1188 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .lp_mode = SENSOR_LP_DISCONTINUOUS,
	 .if_mode = MIPI_DOL_WDR_MODE,
	 .wdr_mode = ISP_DOL_WDR_MODE,
	 .intg_min = 1 << 4,
	 .intg_max = (4500 - 10) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 2000 << 4,
	 .regs = sensor_10b_2592x1944_p30wdr_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b_2592x1944_p30wdr_regs),
	 .set_size = NULL,
	 },
#endif
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	struct sensor_info *info = to_state(sd);

	cfg->type = V4L2_MBUS_CSI2_DPHY;
	if (info->isp_wdr_mode == ISP_DOL_WDR_MODE)
		cfg->flags = 0 | V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_CHANNEL_0 | V4L2_MBUS_CSI2_CHANNEL_1;
	else
		cfg->flags = 0 | V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_CHANNEL_0;

	return 0;
}

static int sensor_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sensor_info *info =
			container_of(ctrl->handler, struct sensor_info, handler);
	struct v4l2_subdev *sd = &info->sd;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return sensor_g_gain(sd, &ctrl->val);
	case V4L2_CID_EXPOSURE:
		return sensor_g_exp(sd, &ctrl->val);
	}
	return -EINVAL;
}

static int sensor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sensor_info *info =
			container_of(ctrl->handler, struct sensor_info, handler);
	struct v4l2_subdev *sd = &info->sd;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return sensor_s_gain(sd, ctrl->val);
	case V4L2_CID_EXPOSURE:
		return sensor_s_exp(sd, ctrl->val);
	}
	return -EINVAL;
}

static int sensor_reg_init(struct sensor_info *info)
{
	int ret;
	data_type rdval_l, rdval_h;
	struct v4l2_subdev *sd = &info->sd;
	struct sensor_format_struct *sensor_fmt = info->fmt;
	struct sensor_win_size *wsize = info->current_wins;

	ret = sensor_write_array(sd, sensor_default_regs,
				 ARRAY_SIZE(sensor_default_regs));
	if (ret < 0) {
		sensor_err("write sensor_default_regs error\n");
		return ret;
	}

	sensor_dbg("sensor_reg_init\n");

	sensor_write_array(sd, sensor_fmt->regs, sensor_fmt->regs_size);

	if (wsize->regs)
		sensor_write_array(sd, wsize->regs, wsize->regs_size);

	if (wsize->set_size)
		wsize->set_size(sd);

	info->width = wsize->width;
	info->height = wsize->height;
	imx335_sensor_vts = wsize->vts;
	sensor_read(sd, 0x300E, &rdval_l);
	sensor_read(sd, 0x300F, &rdval_h);
	imx335_sensor_svr = (rdval_h << 8) | rdval_l;
	sensor_dbg("s_fmt set width = %d, height = %d\n", wsize->width,
		     wsize->height);

	return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sensor_info *info = to_state(sd);

	sensor_dbg("%s on = %d, %d*%d fps: %d code: %x\n", __func__, enable,
		     info->current_wins->width, info->current_wins->height,
		     info->current_wins->fps_fixed, info->fmt->mbus_code);

	if (!enable)
		return 0;

	return sensor_reg_init(info);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops sensor_ctrl_ops = {
	.g_volatile_ctrl = sensor_g_ctrl,
	.s_ctrl = sensor_s_ctrl,
};

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.reset = sensor_reset,
	.init = sensor_init,
	.s_power = sensor_power,
	.ioctl = sensor_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sensor_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.s_stream = sensor_s_stream,
};

static const struct v4l2_subdev_pad_ops sensor_pad_ops = {
	.enum_mbus_code = sensor_enum_mbus_code,
	.enum_frame_size = sensor_enum_frame_size,
	.get_fmt = sensor_get_fmt,
	.set_fmt = sensor_set_fmt,
	.get_mbus_config = sensor_get_mbus_config,
};

static const struct v4l2_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
	.pad = &sensor_pad_ops,
};

/* ----------------------------------------------------------------------- */
static struct cci_driver cci_drv[] = {
	{
		.name = SENSOR_NAME,
		.addr_width = CCI_BITS_16,
		.data_width = CCI_BITS_8,
	}, {
		.name = SENSOR_NAME_2,
		.addr_width = CCI_BITS_16,
		.data_width = CCI_BITS_8,
	}
};

static int sensor_init_controls(struct v4l2_subdev *sd, const struct v4l2_ctrl_ops *ops)
{
	struct sensor_info *info = to_state(sd);
	struct v4l2_ctrl_handler *handler = &info->handler;
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	v4l2_ctrl_handler_init(handler, 2);

	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 1 * 1600,
			      256 * 1600, 1, 1 * 1600);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_EXPOSURE, 0,
			      65536 * 16, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (handler->error) {
		ret = handler->error;
		v4l2_ctrl_handler_free(handler);
	}

	sd->ctrl_handler = handler;

	return ret;
}

static int sensor_dev_id;

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct sensor_info *info;
	int i;

	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	if (client) {
		for (i = 0; i < SENSOR_NUM; i++) {
			if (!strcmp(cci_drv[i].name, client->name))
				break;
		}
		cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv[i]);
	} else {
		cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv[sensor_dev_id++]);
	}

	sensor_init_controls(sd, &sensor_ctrl_ops);

	mutex_init(&info->lock);

#ifdef CONFIG_SAME_I2C
	info->sensor_i2c_addr = I2C_ADDR >> 1;
#endif
	info->fmt = &sensor_formats[0];
	info->fmt_pt = &sensor_formats[0];
	info->win_pt = &sensor_win_sizes[0];
	info->fmt_num = N_FMTS;
	info->win_size_num = N_WIN_SIZES;
	info->sensor_field = V4L2_FIELD_NONE;
	info->combo_mode = CMB_TERMINAL_RES | CMB_PHYA_OFFSET3 | MIPI_NORMAL_MODE;
	info->stream_seq = MIPI_BEFORE_SENSOR;
	info->af_first_flag = 1;
	info->exp = 0;
	info->gain = 0;

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd;
	int i;

	if (client) {
		for (i = 0; i < SENSOR_NUM; i++) {
			if (!strcmp(cci_drv[i].name, client->name))
				break;
		}
		sd = cci_dev_remove_helper(client, &cci_drv[i]);
	} else {
		sd = cci_dev_remove_helper(client, &cci_drv[sensor_dev_id++]);
	}

	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME, 0},
	{}
};

static const struct i2c_device_id sensor_id_2[] = {
	{SENSOR_NAME_2, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sensor_id);
MODULE_DEVICE_TABLE(i2c, sensor_id_2);

static struct i2c_driver sensor_driver[] = {
	{
		.driver = {
			   .owner = THIS_MODULE,
			   .name = SENSOR_NAME,
			   },
		.probe = sensor_probe,
		.remove = sensor_remove,
		.id_table = sensor_id,
	}, {
		.driver = {
			   .owner = THIS_MODULE,
			   .name = SENSOR_NAME_2,
			   },
		.probe = sensor_probe,
		.remove = sensor_remove,
		.id_table = sensor_id_2,
	},
};
static __init int init_sensor(void)
{
	int i, ret = 0;

	sensor_dev_id = 0;

	for (i = 0; i < SENSOR_NUM; i++)
		ret = cci_dev_init_helper(&sensor_driver[i]);

	return ret;
}

static __exit void exit_sensor(void)
{
	int i;

	sensor_dev_id = 0;

	for (i = 0; i < SENSOR_NUM; i++)
		cci_dev_exit_helper(&sensor_driver[i]);
}

module_init(init_sensor);
module_exit(exit_sensor);
