/*
 * A V4L2 driver for sc2232 Raw cameras.
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
MODULE_DESCRIPTION("A low-level driver for SC2232 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (27*1000*1000)
#define V4L2_IDENT_SENSOR 0x2232

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

/*
 * The SC2232 i2c address
 */
#define I2C_ADDR 0x60

#define SENSOR_NUM 0x2
#define SENSOR_NAME "sc2232_mipi"
#define SENSOR_NAME_2 "sc2232_mipi_2"

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {

};

static struct regval_list sensor_1080p25_regs[] = {
	{0x0103, 0x01},   //74.25 sysclk 297M cntclk
	{0x0100, 0x00},

	//close mipi
	//0x3018,0x1f,
	//0x3019,0xff,
	//0x301c,0xb4,

	{0x320c, 0x0a},
	{0x320d, 0x50},  //a80->a50

	{0x3e01, 0x23},

	{0x363c, 0x05},  //04
	{0x3635, 0xa8}, //c0
	{0x363b, 0x0d}, //0d

	{0x3620, 0x08},
	{0x3622, 0x02},
	{0x3635, 0xc0},//
	{0x3908, 0x10},

	{0x3624, 0x08}, //count_clk inv  need debug  flash row in one channel

	{0x5000, 0x06},  //rts column test
	{0x3e06, 0x00},
	{0x3e08, 0x03},
	{0x3e09, 0x10},
	{0x3333, 0x10},
	{0x3306, 0x7e},

	//0x3e08,0x1f,
	//0x3e09,0x1f,
	//0x3e06,0x03,
	{0x3902, 0x05},
	//0x3909,0x01,  //auto blc
	//0x390a,0xf5,  //auto blc

	{0x3213, 0x08},

	{0x337f, 0x03},
	{0x3368, 0x04},
	{0x3369, 0x00},
	{0x336a, 0x00},
	{0x336b, 0x00},
	{0x3367, 0x08},
	{0x330e, 0x30},

	{0x3366, 0x7c}, // div_rst gap

	{0x3633, 0x42},

	{0x330b, 0xe0},

	{0x3637, 0x57},

	{0x3302, 0x1f},
	{0x3309, 0xde},
	//0x303f,0x81, // pclk sel pll_sclk_dig_div
	//leage current
	{0x3907, 0x00},
	{0x3908, 0x61},
	{0x3902, 0x45},
	{0x3905, 0xb8},
	//0x3904,0x06,//10.18
	{0x3e01, 0x8c},
	{0x3e02, 0x10},
	{0x3e06, 0x00},

	{0x3038, 0x48},
	{0x3637, 0x5d},
	{0x3e06, 0x00},


	//0921
	{0x3908, 0x11},

	{0x335e, 0x01},  //ana dithering
	{0x335f, 0x03},
	{0x337c, 0x04},
	{0x337d, 0x06},
	{0x33a0, 0x05},
	{0x3301, 0x04},

	{0x3633, 0x4f},  //prnu

	{0x3622, 0x06},  //blksun
	{0x3630, 0x08},
	{0x3631, 0x84},
	{0x3306, 0x30},

	{0x366e, 0x08},  // ofs auto en [3]
	{0x366f, 0x22},  // ofs+finegain  real ofs in 0x3687[4:0]

	{0x3637, 0x59},  // FW to 4.6k //9.22

	{0x3320, 0x06},//New ramp offset timing
	//0x3321,0x06,
	{0x3326, 0x00},
	{0x331e, 0x11},
	{0x331f, 0xc1},
	{0x3303, 0x20},
	{0x3309, 0xd0},
	{0x330b, 0xbe},
	{0x3306, 0x36},

	{0x3635, 0xc2}, //TxVDD,HVDD
	{0x363b, 0x0a},
	{0x3038, 0x88},

	//9.22
	{0x3638, 0x1f},//ramp_gen by sc 0x30
	{0x3636, 0x25},//
	{0x3625, 0x02},
	{0x331b, 0x83},
	{0x3333, 0x30},

	//10.18
	{0x3635, 0xa0},
	{0x363b, 0x0a},
	{0x363c, 0x05},

	{0x3314, 0x13},//preprecharge

	//20171101 reduce hvdd pump lighting
	{0x3038, 0xc8},// high pump clk,low lighting
	{0x363b, 0x0b},//high hvdd ,low lighting
	{0x3632, 0x18},//large current,low ligting  0x38 (option)

	//20171102 reduce hvdd pump lighting
	{0x3038, 0xff},// high pump clk,low lighting

	{0x3639, 0x09},
	{0x3621, 0x28},
	{0x3211, 0x0c},

	//20171106
	{0x366f, 0x26},

	//20171121
	{0x366f, 0x2f},
	{0x3320, 0x01},
	{0x3306, 0x48},
	{0x331e, 0x19},
	{0x331f, 0xc9},

	{0x330b, 0xd3},//[c8,dd]	20180627
	{0x3620, 0x28},

	//20171122
	{0x3309, 0x60},
	{0x331f, 0x59},
	{0x3308, 0x10},
	{0x3630, 0x0c},

	//digital ctrl
	{0x3f00, 0x07},  // bit[2] = 1
	{0x3f04, 0x05},
	{0x3f05, 0x04},  // hts / 2 - 0x24

	{0x3802, 0x01},
	{0x3235, 0x08},
	{0x3236, 0xc8}, // vts x 2 - 2

	//20171127
	{0x3630, 0x1c},

	//20171130
	{0x33aa, 0x10},//low power

	//20171208  logical   inter
	{0x3670, 0x04}, //0x3631 3670[2] enable  0x3631 in 0x3682
	{0x3677, 0x84},//gain<gain0
	{0x3678, 0x88},//gain0=<gain<gain1
	{0x3679, 0x88},//gain>=gain1
	{0x367e, 0x08},//gain0 {3e08[4:2],3e09[3:1]}
	{0x367f, 0x28},//gain1

	{0x3670, 0x0c}, //0x3633 3670[3] enable  0x3633 in 0x3683    20171227
	{0x3690, 0x34},//gain<gain0
	{0x3691, 0x11},//gain0=<gain<gain1
	{0x3692, 0x42},//gain>=gain1
	{0x369c, 0x08},//gain0{3e08[4:2],3e09[3:1]}
	{0x369d, 0x28},//gain1

	{0x360f, 0x01}, //0x3622 360f[0] enable  0x3622 in 0x3680
	{0x3671, 0xc6},//gain<gain0
	{0x3672, 0x06},//gain0=<gain<gain1
	{0x3673, 0x16},//gain>=gain1
	{0x367a, 0x28},//gain0{3e08[4:2],3e09[3:1]}
	{0x367b, 0x3f},//gain1

	{0x3364, 0x15},//0x3301, 0x3364[4] en, read from 0x3373
	{0x33b6, 0x07},//gain 0	gain<2x
	{0x33b7, 0x1f},//gain 1	gain<8x
	{0x33b8, 0x08},
	{0x33b9, 0x28},
	{0x33ba, 0x48},

	//20171225 BLC power save mode
	{0x3222, 0x29},
	{0x3901, 0x02},
	{0x3905, 0x98},

	//20171227
	{0x3e1e, 0x34},//digital finegain enable

	//20180102
	{0x3039, 0x02},
	{0x303a, 0x3a}, //VCO=405M
	{0x303b, 0x07}, //PCLK=67.5M
	{0x3035, 0xb2}, //CntClk=270

	{0x320c, 0x09},//hts=2400
	{0x320d, 0x60},
	{0x3f04, 0x04},
	{0x3f05, 0x8c},  // hts / 2 - 0x24

	//20180113
	{0x3314, 0x08},

	//20180511
	{0x391b, 0x80},
	{0x5780, 0xff},
	{0x5781, 0x04},
	{0x5785, 0x18},

	//20180627
	//vco=18/1*5*5*1.5=675
	//sysclk=pclk=675/10=67.5
	//mipiclk = 675/1
	//mipipclk = 675/10=67.5
	//countclk=18*15=270
	//0x3039,0x01,
	//0x303a,0x3b,
	//0x303b,0x0e,
	//0x303c,0x0e,
	//0x303d,0x23,
	//0x3034,0x01,
	//0x3035,0x8a,
	//0x3036,0x00,

	//20190118
	//vco=27/2*2*5*5=675
	//sclk=vco/10=67.5
	//mipiclk=675
	//countclk=27*10=270
	{0x3039, 0x24},
	{0x303a, 0x3b},
	{0x303b, 0x0e},
	{0x303c, 0x0e},
	{0x303d, 0x23},
	{0x3034, 0x01},
	{0x3035, 0xb2},
	{0x3036, 0x00},

	{0x3200, 0x00},
	{0x3201, 0x00},
	{0x3202, 0x00},
	{0x3203, 0x00},
	{0x3204, 0x07},
	{0x3205, 0x8f},
	{0x3206, 0x04},
	{0x3207, 0x47},
	{0x3208, 0x07},
	{0x3209, 0x88},
	{0x320a, 0x04},
	{0x320b, 0x40},
	{0x3211, 0x04},
	{0x3213, 0x04},

	{0x3018, 0x13},//[7:5] lane_num-1
	{0x3031, 0x0a},//[3:0] bitmode
	{0x3037, 0x20},//[6:5] bitsel
	{0x3001, 0xFE},//[0] c_y
	{0x4603, 0x00},//[0] data_fifo mipi mode
	{0x4837, 0x1d},//[7:0] pclk period * 2
	{0x4827, 0x48},//[7:0] hs_prepare_time[7:0]
	{0x3802, 0x00},
	{0x363a, 0x9f},

	//2lane
	{0x303b, 0x16},
	{0x303c, 0x0e},
	{0x3018, 0x33},//[7:5] lane_num-1
	{0x4837, 0x3b},//[7:0] pclk period * 2

	//0x320e,0x04,//default=0x465
	//0x320f,0x65,

	//sram
	{0x3f02, 0x02},
	{0x3f06, 0x21},
	{0x3f08, 0x04},
	{0x3f09, 0x22},

	{0x330a, 0x00},
	{0x330b, 0xd2},//[ns,of][ca,dc]
	{0x3306, 0x44},//[hl,bs][32,50]
	//0x330e,0x30,//39

	{0x3209, 0x80},//1920x1080
	{0x320b, 0x38},
	{0x3211, 0x08},

	{0x3906, 0x0a},//blc cut 32 cols of left & right

	//init
	{0x3301, 0x08},
	{0x3306, 0x48},
	{0x3632, 0x08},
	{0x3e00, 0x00},
	{0x3e01, 0x8c},
	{0x3e02, 0x60},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x10},

	{0x0100, 0x01},
};

static struct regval_list sensor_1080p20_regs[] = {

	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x3034, 0x80},
	{0x3039, 0x80},
	{0x3001, 0xfe},
	{0x3018, 0x33},
	{0x3031, 0x0a},
	{0x3034, 0x07},
	{0x3035, 0x52},
	{0x3037, 0x20},
	{0x3038, 0xff},
	{0x3039, 0x46},
	{0x303a, 0x35},
	{0x303b, 0x16},
	{0x303c, 0x0e},
	{0x303d, 0x03},
	{0x3200, 0x00},
	{0x3201, 0x00},
	{0x3202, 0x00},
	{0x3203, 0x00},
	{0x3204, 0x07},
	{0x3205, 0x8f},
	{0x3206, 0x04},
	{0x3207, 0x47},
	{0x3208, 0x07},
	{0x3209, 0x80},
	{0x320a, 0x04},
	{0x320b, 0x38},
	{0x320c, 0x08},/*hts 2200 0x898*/
	{0x320d, 0x98},
	{0x320e, 0x04},/*vts 1125 0x465*/
	{0x320f, 0x65},
	{0x3211, 0x08},
	{0x3213, 0x08},
	{0x3222, 0x29},
	{0x3235, 0x08},
	{0x3236, 0xc8},
	{0x3301, 0x04},
	{0x3302, 0x1f},
	{0x3303, 0x20},
	{0x3306, 0x48},
	{0x3308, 0x10},
	{0x3309, 0x50},
	{0x330b, 0xd3},
	{0x330e, 0x30},
	{0x3314, 0x08},
	{0x331b, 0x83},
	{0x331e, 0x19},
	{0x331f, 0x49},
	{0x3320, 0x01},
	{0x3326, 0x00},
	{0x3333, 0x30},
	{0x335e, 0x01},
	{0x335f, 0x03},
	{0x3366, 0x7c},
	{0x3367, 0x08},
	{0x3368, 0x04},
	{0x3369, 0x00},
	{0x336a, 0x00},
	{0x336b, 0x00},
	{0x337c, 0x04},
	{0x337d, 0x06},
	{0x337f, 0x03},
	{0x33a0, 0x05},
	{0x33aa, 0x10},
	{0x360f, 0x01},
	{0x3620, 0x28},
	{0x3621, 0x28},
	{0x3622, 0x06},
	{0x3624, 0x08},
	{0x3625, 0x02},
	{0x3630, 0x1c},
	{0x3631, 0x84},
	{0x3632, 0x08},
	{0x3633, 0x4f},
	{0x3635, 0xa0},
	{0x3636, 0x25},
	{0x3637, 0x59},
	{0x3638, 0x1f},
	{0x3639, 0x09},
	{0x363a, 0x9f},
	{0x363b, 0x0b},
	{0x363c, 0x05},
	{0x366e, 0x08},
	{0x366f, 0x2f},
	{0x3670, 0x0c},
	{0x3671, 0xc6},
	{0x3672, 0x06},
	{0x3673, 0x16},
	{0x3677, 0x84},
	{0x3678, 0x88},
	{0x3679, 0x88},
	{0x367a, 0x28},
	{0x367b, 0x3f},
	{0x367e, 0x08},
	{0x367f, 0x28},
	{0x3690, 0x34},
	{0x3691, 0x11},
	{0x3692, 0x42},
	{0x369c, 0x08},
	{0x369d, 0x28},
	{0x3802, 0x01},
	{0x3901, 0x02},
	{0x3902, 0x45},
	{0x3905, 0x98},
	{0x3907, 0x00},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x3e00, 0x00},
	{0x3e01, 0x8c},
	{0x3e02, 0x60},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x10},
	{0x3e1e, 0x34},
	{0x3f00, 0x07},
	{0x3f04, 0x04},
	{0x3f05, 0x38},
	{0x4603, 0x00},
	{0x4827, 0x48},
	{0x4837, 0x50},
	{0x5000, 0x06},
	{0x5780, 0xff},
	{0x5781, 0x04},
	{0x5785, 0x18},
	{0x3034, 0x07},
	{0x3039, 0x46},
	{0x0100, 0x01},
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

static int sc2232_sensor_vts;
/*static int sc2232_sensor_svr;*/
static int shutter_delay = 1;
static int shutter_delay_cnt;
static int fps_change_flag;

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	data_type explow, exphigh;
	struct sensor_info *info = to_state(sd);

	exphigh = (unsigned char) (0xff & (exp_val>>7));
	explow = (unsigned char) (0xf0 & (exp_val<<1));

	sensor_write(sd, 0x3e02, explow);
	sensor_write(sd, 0x3e01, exphigh);

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
	int gainana = gain_val;

#if 1
	if (gainana < 0x20) {
		gainhigh = 0x00;
		gainlow = gainana;
		sensor_write(sd, 0x3301, 0x04);
		sensor_write(sd, 0x3306, 0x48);
		sensor_write(sd, 0x3632, 0x08);
	} else if (gainana < 2 * 0x20) {
		gainhigh = 0x01;
		gainlow = gainana >> 1;
		sensor_write(sd, 0x3301, 0x0c);
		sensor_write(sd, 0x3306, 0x48);
		sensor_write(sd, 0x3632, 0x08);
	} else if (gainana < 4 * 0x20) {
		gainhigh = 0x03;
		gainlow = gainana >> 2;
		sensor_write(sd, 0x3301, 0x10);
		sensor_write(sd, 0x3306, 0x48);
		sensor_write(sd, 0x3632, 0x08);
	} else if (gainana < 8 * 0x20) {
		gainhigh = 0x07;
		gainlow = gainana >> 3;
		sensor_write(sd, 0x3301, 0x10);
		sensor_write(sd, 0x3306, 0x48);
		sensor_write(sd, 0x3632, 0x08);
	} else {
		gainhigh = 0x07;
		gainlow = 0x1f;
		if (gainana < 16 * 0x20) {
			gaindiglow = gainana >> 1;
			gaindighigh = 0x00;
			sensor_write(sd, 0x3301, 0xa8);
			sensor_write(sd, 0x3306, 0x78);
			sensor_write(sd, 0x3632, 0x48);
		} else if (gainana < 32 * 0x20) {
			gaindiglow = gainana >> 2;
			gaindighigh = 0x01;
			sensor_write(sd, 0x3301, 0xa8);
			sensor_write(sd, 0x3306, 0x78);
			sensor_write(sd, 0x3632, 0x78);
		} else if (gainana < 64 * 0x20) {
			gaindiglow = gainana >> 3;
			gaindighigh = 0x03;
			sensor_write(sd, 0x3301, 0xa8);
			sensor_write(sd, 0x3306, 0x78);
			sensor_write(sd, 0x3632, 0x78);
		} else if (gainana < 128 * 0x20) {
			gaindiglow = gainana >> 4;
			gaindighigh = 0x07;
			sensor_write(sd, 0x3301, 0xa8);
			sensor_write(sd, 0x3306, 0x78);
			sensor_write(sd, 0x3632, 0x78);
		} else if (gainana < 256 * 0x20) {
			gaindiglow = gainana >> 5;
			gaindighigh = 0x0f;
			sensor_write(sd, 0x3301, 0xa8);
			sensor_write(sd, 0x3306, 0x78);
			sensor_write(sd, 0x3632, 0x78);
		} else {
			gaindiglow = 0xfc;
			gaindighigh = 0x0f;
			sensor_write(sd, 0x3301, 0xa8);
			sensor_write(sd, 0x3306, 0x78);
			sensor_write(sd, 0x3632, 0x78);
		}
	}

	sensor_write(sd, 0x3e09, (unsigned char)gainlow);
	sensor_write(sd, 0x3e08, (unsigned char)gainhigh << 2);
	sensor_write(sd, 0x3e07, (unsigned char)gaindiglow);
	sensor_write(sd, 0x3e06, (unsigned char)gaindighigh);
	sensor_dbg("sensor_set_anagain = %d, 0x%x, 0x%x Done!\n", gain_val, gainhigh << 2, gainlow);
	sensor_dbg("digital_gain = 0x%x, 0x%x Done!\n", gaindighigh, gaindiglow);
#endif

	/*sensor_write(sd, 0x3e09, (unsigned char)(gain_val & 0xff));*/
	/*sensor_write(sd, 0x3e08, (unsigned char)(gain_val >> 8) & 0xff);*/
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
			/*sensor_write(sd, 0x320f, sc2232_sensor_vts / (sc2232_sensor_svr + 1) & 0xFF);
			sensor_write(sd, 0x320e, sc2232_sensor_vts / (sc2232_sensor_svr + 1) >> 8 & 0xFF);
			sensor_write(sd, 0x302d, 0);
			shutter_delay_cnt = 0;*/
			fps_change_flag = 0;
		} else
			shutter_delay_cnt++;
	}

	sensor_write(sd, 0x3812, 0x00);
	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);
	sensor_write(sd, 0x3812, 0x30);

	sensor_dbg("sensor_set_gain exp = %d, %d Done!\n", gain_val, exp_val);

	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}

static int sensor_s_fps(struct v4l2_subdev *sd,
			struct sensor_fps *fps)
{
	/*data_type rdval1, rdval2, rdval3;*/
	struct sensor_info *info = to_state(sd);
	struct sensor_win_size *wsize = info->current_wins;

	sc2232_sensor_vts = wsize->pclk/fps->fps/wsize->hts;
	fps_change_flag = 1;
	/*sensor_write(sd, 0x302d, 1);
	sensor_read(sd, 0x30f8, &rdval1);
	sensor_read(sd, 0x30f9, &rdval2);
	sensor_read(sd, 0x30fa, &rdval3);

	sensor_dbg("sc2232_sensor_svr: %d, vts: %d.\n", sc2232_sensor_svr, (rdval1 | (rdval2<<8) | (rdval3<<16)));*/
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
		usleep_range(10000, 12000);
		vin_set_mclk_freq(sd, MCLK);
		usleep_range(30000, 32000);
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
	unsigned int SENSOR_ID = 0;
	data_type rdval;
	int cnt = 0;

	sensor_read(sd, 0x3107, &rdval);
	SENSOR_ID |= (rdval << 8);
	sensor_read(sd, 0x3108, &rdval);
	SENSOR_ID |= (rdval);
	sensor_print("V4L2_IDENT_SENSOR = 0x%x\n", SENSOR_ID);

	while ((SENSOR_ID != V4L2_IDENT_SENSOR) && (cnt < 5)) {
		sensor_read(sd, 0x3107, &rdval);
		SENSOR_ID |= (rdval << 8);
		sensor_read(sd, 0x3108, &rdval);
		SENSOR_ID |= (rdval);
		sensor_print("retry = %d, V4L2_IDENT_SENSOR = %x\n",
			cnt, SENSOR_ID);
		cnt++;
		}
	if (SENSOR_ID != V4L2_IDENT_SENSOR)
		return -ENODEV;

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
	info->width = 1920;
	info->height = 1080;
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
	 .width = 1920,
	 .height = 1080,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 2200,
	 .vts = 1125,
	 .pclk = 49 * 5000 * 1000,
	 .mipi_bps = 247 * 5000 * 1000,
	 .fps_fixed = 20,
	 .bin_factor = 1,
	 .intg_min = 3 << 4,
	 .intg_max = (1125 - 2) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .regs = sensor_1080p20_regs,
	 .regs_size = ARRAY_SIZE(sensor_1080p20_regs),
	 .set_size = NULL,
	 },
#if 1
	{
	 .width = 1920,
	 .height = 1080,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 2400,
	 .vts = 1125,
	 .pclk = 67 * 5000 * 1000,
	 .mipi_bps = 337 * 5000 * 1000,
	 .fps_fixed = 25,
	 .bin_factor = 1,
	 .intg_min = 3 << 4,
	 .intg_max = (1125 - 2) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .regs = sensor_1080p25_regs,
	 .regs_size = ARRAY_SIZE(sensor_1080p25_regs),
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
		cfg->flags = 0 | V4L2_MBUS_CSI2_2_LANE | V4L2_MBUS_CSI2_CHANNEL_0 | V4L2_MBUS_CSI2_CHANNEL_1;
	else
		cfg->flags = 0 | V4L2_MBUS_CSI2_2_LANE | V4L2_MBUS_CSI2_CHANNEL_0;
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
	/*data_type rdval_l, rdval_h;*/
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
	sc2232_sensor_vts = wsize->vts;

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
	info->combo_mode = CMB_TERMINAL_RES | CMB_PHYA_OFFSET2 | MIPI_NORMAL_MODE;
	info->time_hs = 0x23;
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
