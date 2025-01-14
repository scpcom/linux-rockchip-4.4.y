/*
 * A V4L2 driver for sc8353 Raw cameras.
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
MODULE_DESCRIPTION("A low-level driver for SC8353 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (27*1000*1000)
#define V4L2_IDENT_SENSOR 0x8235

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

/*
 * The SC8353 i2c address
 */
#define I2C_ADDR 0x60

#define SENSOR_NUM 0x2
#define SENSOR_NAME "sc8353_mipi"
#define SENSOR_NAME_2 "sc8353_mipi_2"

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {
{0x0103, 0x01},
{0xffff, 0x10},
{0x0100, 0x00},

{0x336c, 0xc2},

{0x363b, 0x08},
{0x3635, 0x42},
{0x3624, 0x45},

///////digital updates start////////////
// 05-23-2018

//0x33e0,0xa0,
{0x33e1, 0x08},

{0x33e2, 0x18},
{0x33e3, 0x10},
{0x33e4, 0x0c},

{0x33e5, 0x10},
{0x33e6, 0x06},
{0x33e7, 0x02},

{0x33e8, 0x18},
{0x33e9, 0x10},
{0x33ea, 0x0c},

{0x33eb, 0x10},
{0x33ec, 0x04},
{0x33ed, 0x02},

{0x33ee, 0xa0},
{0x33ef, 0x08},
{0x33f4, 0x18},
{0x33f5, 0x10},
{0x33f6, 0x0c},
{0x33f7, 0x10},
{0x33f8, 0x06},
{0x33f9, 0x02},
{0x33fa, 0x18},
{0x33fb, 0x10},
{0x33fc, 0x0c},
{0x33fd, 0x10},
{0x33fe, 0x04},
{0x33ff, 0x02},

{0x5799, 0x06},

{0x3e1b, 0x2a},           //0x3e03 03 mode support fine gain 40-->7f 1204B
{0x3e25, 0x03},           //blc 1x gain threshold
{0x3e26, 0x40},           //blc 1x gain threshold
{0x3e16, 0x00},
{0x3e17, 0xac},           //dcg gain value
{0x3e18, 0x00},
{0x3e19, 0xac},           //dcg open point

//for dcg always on
{0x3e0e, 0x09},
{0x3e1e, 0x76},           //[7] gcvt_dcg_en [3] gcvt_se_dcg_en
{0x3e25, 0x23},           //blc 1x gain threshold
{0x3e26, 0x40},           //blc 1x gain threshold
//end

{0x3e1b, 0x3a},           //0x3e03 03 mode support fine gain 40-->7f

{0x57aa, 0x2f},            //dpc
{0x57ab, 0xff},            // [7] edge optimization

//mipi
{0x3018, 0x72},           //[7:5] lane_num-1 [4] digital mipi pad en 1:mipi 0:DVP
{0x3019, 0x00},           //[3:0] lane disable
{0x3031, 0x0a},           //[3:0] bitmode
{0x3037, 0x20},           //[6:5] bit sel 0:8bit 1:10bit 10:12bit
{0x4501, 0xa4},
{0x4509, 0x10},
{0x4837, 0x1c},           //[7:0] pclk period * 2

//mipi end
////digital updates end
{0x320c, 0x08},
{0x320d, 0x98},
{0x330b, 0xc0},
{0x3306, 0xe0},
{0x3309, 0x50},
{0x331f, 0x41},
{0x3301, 0x20},
{0x3638, 0x25},

{0x3e00, 0x01},
{0x3e01, 0x18},

{0x3637, 0x15},
{0x3632, 0x28},

{0x3635, 0x20},
{0x3366, 0x92},

//1106
{0x363d, 0x01},
{0x3637, 0x11},
{0x3632, 0x08},
{0x3628, 0x80}, // cmp pd option
{0x3638, 0x08},
{0x3306, 0xb0},
{0x3309, 0x40},
{0x331f, 0x31},

{0x330b, 0xb0},

{0x3633, 0x22},

{0x3301, 0x30},

{0x3e00, 0x01}, //ms exp
{0x3e01, 0x00},
{0x3e02, 0x00},


//preprecharge
{0x3314, 0x94},
{0x330e, 0x50}, //[50,60]
{0x334c, 0x10},

{0x3e09, 0x40}, // fine gain 0x40 -> 1x gain
{0x3637, 0x1e},

//1107   3.5k ADC fullwell, 3.25k used 4.7K total linear
{0x3e08, 0x03},
{0x3637, 0x52},
{0x3622, 0x07},
{0x3631, 0x80},
{0x3630, 0x80},

{0x3306, 0xa8},
{0x330b, 0x48},

//1203
{0x3306, 0xa8},
{0x3638, 0x22},
//0x3304,0x50,
//0x331e,0x41,
{0x3309, 0x70},
{0x331f, 0x61},
{0x3301, 0x40},
{0x3308, 0x20},

//1204
{0x320c, 0x08}, //hts=4200
{0x320d, 0x34},
{0x330b, 0x28},
{0x3306, 0xa0},
{0x3301, 0x38},

{0x335d, 0x60},

{0x337f, 0x33}, //new auto precharge  330e in 33f0   [7:6] 11: close div_rst 00:open div_rst
{0x3368, 0x07},
{0x3369, 0x00},
{0x336a, 0x00},
{0x336b, 0x00},
{0x3367, 0x08},
{0x330e, 0x58},

//1206
{0x33af, 0x48}, //0x331e - 0x11
{0x3638, 0x0a},
{0x3306, 0xa8},
{0x3309, 0x68},
{0x331f, 0x59},

{0x330d, 0x28},
{0x339e, 0x24},
{0x33aa, 0x24},
{0x3332, 0x24},
{0x3350, 0x24},
{0x3358, 0x24},
{0x335c, 0x24},

//pixfpn optimize 1210
{0x3628, 0x83},
{0x363e, 0x00},

{0x3633, 0x53}, //logic
{0x3630, 0x80},
{0x3622, 0xf7},
{0x363d, 0x01},
{0x363e, 0x00},
{0x363a, 0x88},

//20181127 digital logic
{0x3e1b, 0x3a},           //fine gain 0x40~0x7f
{0x3e26, 0x40},
//0x3622 auto logic read 0x3680 for auto value
{0x360f, 0x01},           //[0] 3622 auto en
{0x367a, 0x48},           //gain0
{0x367b, 0x78},           //gain1
{0x3671, 0xf7},           //sel0
{0x3672, 0xf7},           //sel1
{0x3673, 0x17},           //sel2
{0x3670, 0x4a},           //[1]3630 auto en [3] 3633 auto en, [6] 363a auto en
{0x36d0, 0x10},           //[4] 363d auto en
//0x3630 auto logic read 0x3681 for auto value
{0x367c, 0x48},           //gain0
{0x367d, 0x78},           //gain1
{0x3674, 0x80},           //sel0
{0x3675, 0x85},           //sel1
{0x3676, 0xa5},           //sel2
//0x3633 auto logic read 0x3683 for auto value
{0x369c, 0x48},           //gain0  0x0740
{0x369d, 0x78},           //gain1  0x1f40
{0x3690, 0x53},           //sel0
{0x3691, 0x63},           //sel1
{0x3692, 0x54},           //sel2
//0x363a auto logic read 0x3686 for auto value
{0x36a2, 0x48},           //gain0
{0x36a3, 0x78},           //gain1
{0x3699, 0x88},           //sel0
{0x369a, 0x9f},           //sel1
{0x369b, 0x9f},           //sel2
//0x363d auto logic read 0x368f for auto value
{0x36bb, 0x48},           //gain0
{0x36bc, 0x78},           //gain1
{0x36c9, 0x05},           //sel0
{0x36ca, 0x05},           //sel1
{0x36cb, 0x05},           //sel2
//0x363e auto logic read 0x36cf for auto value
{0x36d0, 0x30},
{0x36d1, 0x48},            //gain0
{0x36d2, 0x78},           //gain 1
{0x36cc, 0x00},           //sel 0
{0x36cd, 0x10},           //sel 1
{0x36ce, 0x1a},            //sel 2
//0x3301 auto logic read 0x33f2 for auto value
{0x3364, 0x16},           //0x3364[4] comprst auto enable
{0x3301, 0x1c},           //comprst sel0 when dcg off
{0x3393, 0x1c},            //comprst sel1 when dcg off
{0x3394, 0x28},            //comprst sel2 when dcg off
{0x3395, 0x60},            //comprst sel3 when dcg off
{0x3390, 0x08},            //comprst gain0 when dcg off
{0x3391, 0x18},            //comprst gain1 when dcg off
{0x3392, 0x38},            //comprst gain2 when dcg off
{0x3399, 0x1c},           //comprst sel0 when dcg on
{0x339a, 0x1c},            //comprst sel1 when dcg on
{0x339b, 0x28},            //comprst sel2 when dcg on
{0x339c, 0x60},            //comprst sel3 when dcg on
{0x3396, 0x08},            //comprst gain0 when dcg on
{0x3397, 0x18},            //comprst gain1 when dcg on
{0x3398, 0x38},            //comprst gain2 when dcg on
//           //           //           //auto logic end

//1218 blc update
{0x3241, 0x00},
{0x3243, 0x03},
{0x3271, 0x1c},
{0x3273, 0x1f},
{0x3248, 0x00},
{0x3901, 0x08},

//1214, 0x5011,0x85? TODO
{0x394c, 0x0f},
{0x394d, 0x20},
{0x394e, 0x08},
{0x394f, 0x90},
{0x3981, 0x70},
{0x3984, 0x20}, //blc trigger,alpha trigger
{0x3987, 0x08},
{0x39ec, 0x08}, //BLC_OFFSET
{0x39ed, 0x00},
{0x3982, 0x00}, //Auto Alpha
{0x3983, 0x00},
{0x3980, 0x71}, //alpha Ratio & Offset
{0x39b4, 0x0c}, //PosH
{0x39b5, 0x1c},
{0x39b6, 0x38},
{0x39b7, 0x5b},
{0x39b8, 0x50},
{0x39b9, 0x38},
{0x39ba, 0x20},
{0x39bb, 0x10},
{0x39bc, 0x0c}, //PosV
{0x39bd, 0x16},
{0x39be, 0x21},
{0x39bf, 0x36},
{0x39c0, 0x3b},
{0x39c1, 0x2a},
{0x39c2, 0x16},
{0x39c3, 0x0c},
{0x39a2, 0x03},//KV
{0x39a3, 0xe3},
{0x39a4, 0x03},
{0x39a5, 0xf2},
{0x39a6, 0x03},
{0x39a7, 0xf6},
{0x39a8, 0x03},
{0x39a9, 0xfa},
{0x39aa, 0x03},
{0x39ab, 0xff},
{0x39ac, 0x00},
{0x39ad, 0x06},
{0x39ae, 0x00},
{0x39af, 0x09},
{0x39b0, 0x00},
{0x39b1, 0x12},
{0x39b2, 0x00},
{0x39b3, 0x22},
{0x39c6, 0x07},
{0x39c7, 0xf8},
{0x39c9, 0x07},
{0x39ca, 0xf8},
{0x3990, 0x03}, //KH
{0x3991, 0xfd},
{0x3992, 0x03},
{0x3993, 0xfc},
{0x3994, 0x00},
{0x3995, 0x00},
{0x3996, 0x00},
{0x3997, 0x05},
{0x3998, 0x00},
{0x3999, 0x09},
{0x399a, 0x00},
{0x399b, 0x12},
{0x399c, 0x00},
{0x399d, 0x12},
{0x399e, 0x00},
{0x399f, 0x18},
{0x39a0, 0x00},
{0x39a1, 0x14},
{0x39cc, 0x00}, //KLeft
{0x39cd, 0x1b},
{0x39ce, 0x00},
{0x39cf, 0x00}, //KRight
{0x39d0, 0x1b},
{0x39d1, 0x00},
{0x39e2, 0x15}, //slope
{0x39e3, 0x87},
{0x39e4, 0x12},
{0x39e5, 0xb7},
{0x39e6, 0x00},
{0x39e7, 0x8c},
{0x39e8, 0x01},
{0x39e9, 0x31},
{0x39ea, 0x01},
{0x39eb, 0xd7},
{0x39c5, 0x30}, //Temp Thres

//20181225
{0x3038, 0x22}, //pump driver, mclk/2=27/2=13.5-->[10M,20M]

//20181228
{0x330b, 0x48},

{0x3641, 0x0c},
//567M count_clk
{0x36f9, 0x57},
{0x36fa, 0x39},
{0x36fb, 0x13},
{0x36fc, 0x10},
{0x36fd, 0x14},
//vco=27/4*2*4*3*7=1134
//sclk=vco/2/4=141.75
//countclk=vco/2=567

{0x36e9, 0x53}, //708.75M mipi 70.875M mipiclk
{0x36ea, 0x39},
{0x36eb, 0x06},
{0x36ec, 0x05},
{0x36ed, 0x24},
//vco=27/4*5*3*7=708.75
//mipiclk=vco

{0x3641, 0x00},

//20190105
{0x3632, 0x88}, //idac_bufs

//20190325
{0x3635, 0x02},
{0x3902, 0xc5}, //mirror & flip, trig blc

//20190408
{0x3038, 0x44}, //pump driver, mclk/4=27/4
{0x3632, 0x98},
{0x3e14, 0x31}, //blc trig
{0x3e1b, 0x3a},
{0x3248, 0x04},
{0x3901, 0x00}, //blc 24 rows
{0x3904, 0x18},
{0x3987, 0x0b}, //flip
{0x363b, 0x06}, //hvdd 3.186v
{0x3905, 0xd8},

{0x301f, 0x02}, //setting id

//init
{0x3e00, 0x01},
{0x3e01, 0x18},
{0x3e02, 0xa0},
{0x3e03, 0x0b},
{0x3e08, 0x03},
{0x3e09, 0x40},

{0x0100, 0x01},  // 567M count_clk 141.75M sysclk
};

static struct regval_list sensor_10b4k30_regs[] = {
};

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

static int sc4233p_sensor_vts;
static int sc4233p_sensor_svr;
static int shutter_delay = 1;
static int shutter_delay_cnt;
static int fps_change_flag;

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	data_type explow, expmid, exphigh;
	struct sensor_info *info = to_state(sd);

	exphigh = (unsigned char) (0xf & (exp_val>>15));
	expmid = (unsigned char) (0xff & (exp_val>>7));
	explow = (unsigned char) (0xf0 & (exp_val<<1));

	sensor_write(sd, 0x3e02, explow);
	sensor_write(sd, 0x3e01, expmid);
	sensor_write(sd, 0x3e00, exphigh);
	sensor_dbg("sensor_set_exp = %d line Done!\n", exp_val);

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

static int sensor_s_gain(struct v4l2_subdev *sd, int gain_val)
{
	struct sensor_info *info = to_state(sd);
	data_type gainlow = 0;
	data_type gainhigh = 0;
	data_type gaindiglow = 0x80;
	data_type gaindighigh = 0x00;
	int gainana = gain_val << 2;

	if (gainana < 0x80) {
		gainhigh = 0x03;
		gainlow = gainana;
	} else if (gainana < 2 * 0x80) {
		gainhigh = 0x07;
		gainlow = gainana >> 1;
	} else if (gainana < 4 * 0x80) {
		gainhigh = 0x0f;
		gainlow = gainana >> 2;
	} else if (gainana < 8 * 0x80) {
		gainhigh = 0x1f;
		gainlow = gainana >> 3;
	} else {
		gainhigh = 0x1f;
		gainlow = 0x7f;
		if (gainana < 16 * 0x80) {
			gaindiglow = gainana >> 3;
			gaindighigh = 0x00;
		} else if (gainana < 32 * 0x80) {
			gaindiglow = gainana >> 4;
			gaindighigh = 0x01;
		} else if (gainana < 64 * 0x80) {
			gaindiglow = gainana >> 5;
			gaindighigh = 0x03;
		} else if (gainana < 128 * 0x80) {
			gaindiglow = gainana >> 6;
			gaindighigh = 0x07;
		} else if (gainana < 256 * 0x80) {
			gaindiglow = gainana >> 7;
			gaindighigh = 0x0f;
		} else {
			gaindiglow = 0xfc;
			gaindighigh = 0x0f;
		}
	}

	sensor_write(sd, 0x3e09, (unsigned char)gainlow);
	sensor_write(sd, 0x3e08, (unsigned char)gainhigh);
	sensor_write(sd, 0x3e07, (unsigned char)gaindiglow);
	sensor_write(sd, 0x3e06, (unsigned char)gaindighigh);

	sensor_dbg("sensor_set_anagain = %d, 0x%x, 0x%x Done!\n", gain_val, gainhigh, gainlow);
	sensor_dbg("digital_gain = 0x%x, 0x%x Done!\n", gaindighigh, gaindiglow);
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
			//sensor_write(sd, 0x320f, sc4233p_sensor_vts / (sc4233p_sensor_svr + 1) & 0xFF);
			//sensor_write(sd, 0x320e, sc4233p_sensor_vts / (sc4233p_sensor_svr + 1) >> 8 & 0xFF);
			//sensor_write(sd, 0x302d, 0);
			shutter_delay_cnt = 0;
			fps_change_flag = 0;
		} else
			shutter_delay_cnt++;
	}

	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);

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

	sc4233p_sensor_vts = wsize->pclk/fps->fps/wsize->hts;
	fps_change_flag = 1;
	//sensor_write(sd, 0x302d, 1);

	//sensor_read(sd, 0x30f8, &rdval1);
	//sensor_read(sd, 0x30f9, &rdval2);
	//sensor_read(sd, 0x30fa, &rdval3);

	sensor_dbg("sc4233p_sensor_svr: %d, vts: %d.\n", sc4233p_sensor_svr, (rdval1 | (rdval2<<8) | (rdval3<<16)));
	return 0;
}

static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	int ret;
	data_type rdval;

	ret = sensor_read(sd, 0x0100, &rdval);
	if (ret != 0)
		return ret;

	if (on_off == STBY_ON)
		ret = sensor_write(sd, 0x0100, rdval&0xfe);
	else
		ret = sensor_write(sd, 0x0100, rdval|0x01);
	return ret;
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
		usleep_range(10000, 12000);
		cci_unlock(sd);
		break;
	case STBY_OFF:
		sensor_dbg("STBY_OFF!\n");
		cci_lock(sd);
		usleep_range(10000, 12000);
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
		vin_set_pmu_channel(sd, DVDD, ON);
		vin_set_pmu_channel(sd, AVDD, ON);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(10000, 12000);
		vin_set_mclk(sd, ON);
		usleep_range(1000, 1200);
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
		vin_set_pmu_channel(sd, AFVDD, OFF);
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
		usleep_range(1000, 1200);
		break;
	case 1:
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(1000, 1200);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	data_type rdval = 0;

	sensor_read(sd, 0x3107, &rdval);
	sensor_print("0x3107 = 0x%x\n", rdval);
	//if (rdval != (V4L2_IDENT_SENSOR>>8))
	//	return -ENODEV;

	sensor_read(sd, 0x3108, &rdval);
	sensor_print("0x3108 = 0x%x\n", rdval);
	//if (rdval != (V4L2_IDENT_SENSOR&0xff))
	//	return -ENODEV;

	sensor_read(sd, 0x3e03, &rdval);
	sensor_print("0x3e03 = 0x%x\n", rdval);

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
	info->width = 3840;
	info->height = 2160;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;
	info->exp = 0;

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
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
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

	{
	 .width = 3840,
	 .height = 2160,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 2100,
	 .vts = 4500,
	 .pclk = 283500000,
	 .mipi_bps = 576 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .intg_min = 3 << 4,
	 .intg_max = (4500 - 10) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .regs = sensor_10b4k30_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b4k30_regs),
	 .set_size = NULL,
	 },

};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
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
	struct sensor_exp_gain exp_gain;

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
	sc4233p_sensor_vts = wsize->vts;
//	sensor_read(sd, 0x300E, &rdval_l);
//	sensor_read(sd, 0x300F, &rdval_h);
//	sc4233p_sensor_svr = (rdval_h << 8) | rdval_l;

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
	.s_parm = sensor_s_parm,
	.g_parm = sensor_g_parm,
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

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 1 * 1600,
			      256 * 1600, 1, 1 * 1600);
	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_EXPOSURE, 1,
			      65536 * 16, 1, 1);
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
	info->combo_mode = CMB_TERMINAL_RES | CMB_PHYA_OFFSET1 | MIPI_NORMAL_MODE;
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
