/*
 * A V4L2 driver for sc4233p Raw cameras.
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
MODULE_DESCRIPTION("A low-level driver for SC4233P sensors");
MODULE_LICENSE("GPL");

#define MCLK              (27*1000*1000)
#define V4L2_IDENT_SENSOR 0x4235

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

#define HDR_RATIO 16

/*
 * The SC4233P i2c address
 */
#define I2C_ADDR 0x60

#define SENSOR_NUM 0x2
#define SENSOR_NAME "sc4233p_mipi"
#define SENSOR_NAME_2 "sc4233p_mipi_2"

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {

};

static struct regval_list sensor_12b4M30_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},

	{0x3e01, 0xc2},
	{0x3e02, 0xf0},

	{0x3633, 0x44},
	{0x3306, 0x60},

	{0x3320, 0x01},
	{0x3638, 0x2a},
	{0x3304, 0x30},
	{0x331e, 0x29},
	{0x3309, 0x60},
	{0x331f, 0x59},

	{0x3635, 0x40},
	{0x363b, 0x03},

	{0x3352, 0x02},
	{0x3356, 0x1f},

	///////digital updates start////////////
	// 08-07-2018

	{0x33e0, 0xc0},
	{0x33e1, 0x08},

	{0x33e2, 0x10},
	{0x33e3, 0x10},
	{0x33e4, 0x20},
	{0x33e5, 0x08},
	{0x33e6, 0x08},
	{0x33e7, 0x10},

	{0x33e8, 0x10},
	{0x33e9, 0x10},
	{0x33ea, 0x20},
	{0x33eb, 0x04},
	{0x33ec, 0x04},
	{0x33ed, 0x10},

	{0x33f4, 0x10},
	{0x33f5, 0x10},
	{0x33f6, 0x20},
	{0x33f7, 0x08},
	{0x33f8, 0x08},
	{0x33f9, 0x10},

	{0x33fa, 0x10},
	{0x33fb, 0x10},
	{0x33fc, 0x20},
	{0x33fd, 0x04},
	{0x33fe, 0x04},
	{0x33ff, 0x10},

	//dig 20180806
	{0x336d, 0x01},//[6] comp az end sel 1:st_comp_rst 0:st_cntdown_gap
	{0x337f, 0x2d},//[5] stg2 end sel 1:st_comp_rst 0:st_cntdown_gap
	{0x3320, 0x09},
	{0x366e, 0x04},//[2] pwc logic fine gain sel 1:20~3f 0:10~1f
	{0x360f, 0x04},//[2] array logic fine gain sel 1:20~3f 0:10~1f
	{0x3364, 0x0e},//[3] 0x3301 logic fine gain sel 1:20~3f 0:10~1f
	{0x3e14, 0xb1},//gcvt double en
	{0x3253, 0x06},//power save mode exit point

	//blc stable range, 20180806
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x3905, 0xd8}, //one channel

	{0x3399, 0xff}, //0928

	//end

	//0801
	{0x3637, 0x22},  //4.8k fullwell 188CG
	{0x3908, 0x11},

	//30fps 12bit
	//0x36e9,0x20,
	{0x4837, 0x3b},
	//0x36fb,0x09, //14 is wrong , need debug
	{0x4501, 0xb4},
	{0x3637, 0x22},
	{0x3e09, 0x20},

	//new pll config
	{0x3106, 0x81}, // use sysclk from mipipll

	{0x3301, 0x30},
	//0x3304,0x20,
	//0x331e,0x19,
	{0x3309, 0x30},
	{0x331f, 0x29},

	{0x3306, 0x70},
	{0x3634, 0x34}, // buwending?

	//25fps
	{0x320c, 0x06},
	{0x320d, 0xc0},

	{0x330b, 0xd8},

	//0907
	{0x3630, 0xa8}, //blksun
	{0x3631, 0x80},
	{0x3622, 0xee},

	{0x4509, 0x20}, //ini code

	//2688x1520
	{0x3200, 0x00},
	{0x3201, 0x00}, //xaddr start=0
	{0x3204, 0x0a},
	{0x3205, 0x87}, //xaddr end=2695  2696 selected

	{0x3202, 0x00}, //yaddr start=0
	{0x3203, 0x00},
	{0x3206, 0x05}, //xaddr end=1527  1528 selected
	{0x3207, 0xf7},

	{0x3208, 0x0a},
	{0x3209, 0x80},
	{0x320a, 0x05},
	{0x320b, 0xf0},

	{0x3211, 0x04},
	{0x3213, 0x04},

	{0x320c, 0x05}, //1440x2 hts
	{0x320d, 0xa0},
	{0x320e, 0x06}, //1562 vts
	{0x320f, 0x1a},

	{0x3308, 0x10},
	{0x3306, 0x70},
	{0x330b, 0xd0},
	{0x3309, 0x50},
	{0x331f, 0x49},

	{0x3e01, 0xba},
	{0x3e02, 0xe0},

	//mipi
	{0x3018, 0x73},//[7:5] lane_num-1 [4] digital mipi pad en 1:mipi 0:DVP
	{0x3031, 0x0a},//[3:0] bitmode
	{0x3037, 0x20}, //[6:5] phy bit_res 00:8 01:10 10:12

	//0927
	{0x3038, 0x22},
	{0x3366, 0x92},

	{0x337a, 0x08}, //count_keep/up
	{0x337b, 0x10},
	{0x33a3, 0x0c},

	//precharge
	{0x3314, 0x94},
	{0x330e, 0x14},  //[2,2e]
	{0x334c, 0x10},  //preprecharge

	//20180929 high temp
	{0x3363, 0x00},
	{0x3273, 0x01},
	{0x3933, 0x28},
	{0x3934, 0x20},
	{0x3940, 0x78},
	{0x3942, 0x08},
	{0x3943, 0x28},
	{0x3980, 0x00},
	{0x3981, 0x00},
	{0x3982, 0x00},
	{0x3983, 0x00},
	{0x3984, 0x00},
	{0x3985, 0x00},
	{0x3986, 0x00},
	{0x3987, 0x00},
	{0x3988, 0x00},
	{0x3989, 0x00},
	{0x398a, 0x00},
	{0x398b, 0x04},
	{0x398c, 0x00},
	{0x398d, 0x04},
	{0x398e, 0x00},
	{0x398f, 0x08},
	{0x3990, 0x00},
	{0x3991, 0x10},
	{0x3992, 0x03},
	{0x3993, 0xd8},
	{0x3994, 0x03},
	{0x3995, 0xe0},
	{0x3996, 0x03},
	{0x3997, 0xf0},
	{0x3998, 0x03},
	{0x3999, 0xf8},
	{0x399a, 0x00},
	{0x399b, 0x00},
	{0x399c, 0x00},
	{0x399d, 0x08},
	{0x399e, 0x00},
	{0x399f, 0x10},
	{0x39a0, 0x00},
	{0x39a1, 0x18},
	{0x39a2, 0x00},
	{0x39a3, 0x28},
	{0x39af, 0x58},
	{0x39b5, 0x30},
	{0x39b6, 0x00},
	{0x39b7, 0x34},
	{0x39b8, 0x00},
	{0x39bc, 0x00},
	{0x39bd, 0x00},
	{0x39be, 0x00},
	{0x39c5, 0x61},
	{0x39db, 0x20},
	{0x39dc, 0x00},
	{0x39de, 0x20},
	{0x39df, 0x00},
	{0x39e0, 0x00},
	{0x39e1, 0x00},
	{0x39e2, 0x00},
	{0x39e3, 0x00},
	{0x3940, 0x6c},//20190124

	//internal logic_20181010
	{0x3364, 0x1e},//bit[4] comprst auto enable  bit[3] 0~gain 10~1f 1~ gain 20~3f  //0x33f2 readout
	{0x3301, 0x30},//comprst sel0
	{0x3393, 0x30}, //comprst sel1
	{0x3394, 0x30}, //comprst sel2
	{0x3395, 0x30}, //comprst sel3
	{0x3390, 0x08}, //comprst gain0
	{0x3391, 0x08}, //comprst gain1
	{0x3392, 0x08}, //comprst gain2
	{0x3670, 0x48},//bit[3] 3633 auto enable bit[6] 363a auto enable/ /0x3683 readout auto 3633 //0x3686 readout auto 363a
	{0x366e, 0x04},// bit[2] 0~gain 10~1f 1~ gain 20~3f
	{0x3690, 0x43},//3633 sel0
	{0x3691, 0x43}, //3633 sel1
	{0x3692, 0x43}, //3633 sel2
	{0x369c, 0x08}, //3633 gain0
	{0x369d, 0x08}, //3633 gain1
	{0x3699, 0x80},//3633a sel0
	{0x369a, 0x9f}, //363a sel1
	{0x369b, 0x9f}, //363a sel2
	{0x36a2, 0x08}, //363a gain0
	{0x36a3, 0x08}, //363a gain1
	{0x360f, 0x05},// bit[2] 0~gain 10~1f 1~ gain 20~3f bit[0] 3622 auto enable   //0x3680 readout
	{0x3671, 0xee},//3622 sel0
	{0x3672, 0x0e}, //3622 sel1
	{0x3673, 0x0e}, //3622 sel2
	{0x367a, 0x08}, //3622 gain0
	{0x367b, 0x08}, //3622 gain1

	//20181126
	{0x5784, 0x10},
	{0x5785, 0x08},
	{0x5787, 0x06},
	{0x5788, 0x06},
	{0x5789, 0x00},
	{0x578a, 0x06},
	{0x578b, 0x06},
	{0x578c, 0x00},
	{0x5790, 0x10},
	{0x5791, 0x10},
	{0x5792, 0x00},
	{0x5793, 0x10},
	{0x5794, 0x10},
	{0x5795, 0x00},
	{0x57c4, 0x10},
	{0x57c5, 0x08},
	{0x57c7, 0x06},
	{0x57c8, 0x06},
	{0x57c9, 0x00},
	{0x57ca, 0x06},
	{0x57cb, 0x06},
	{0x57cc, 0x00},
	{0x57d0, 0x10},
	{0x57d1, 0x10},
	{0x57d2, 0x00},
	{0x57d3, 0x10},
	{0x57d4, 0x10},
	{0x57d5, 0x00},
	{0x33e0, 0xa0},
	{0x33e1, 0x08},
	{0x33e2, 0x00},
	{0x33e3, 0x10},
	{0x33e4, 0x10},
	{0x33e5, 0x00},
	{0x33e6, 0x10},
	{0x33e7, 0x10},
	{0x33e8, 0x00},
	{0x33e9, 0x10},
	{0x33ea, 0x16},
	{0x33eb, 0x00},
	{0x33ec, 0x10},
	{0x33ed, 0x18},
	{0x33ee, 0xa0},
	{0x33ef, 0x08},
	{0x33f4, 0x00},
	{0x33f5, 0x10},
	{0x33f6, 0x10},
	{0x33f7, 0x00},
	{0x33f8, 0x10},
	{0x33f9, 0x10},
	{0x33fa, 0x00},
	{0x33fb, 0x10},
	{0x33fc, 0x16},
	{0x33fd, 0x00},
	{0x33fe, 0x10},
	{0x33ff, 0x18},

	//modify analog fine gain 0x40~0x7f
	{0x3636, 0x20},
	{0x3637, 0x11},
	{0x3e25, 0x03},
	{0x3e26, 0x40},//blc 1xgain dethering

	//0x3304,0x30,//40
	//0x3309,0x50,//60
	{0x330b, 0xe0},//[bs,to][1,d0,100] 0x3306=0x70
	//0x3306,0x70,//[hl,bs][1,58,80] 0x330b=0xe0
	//0x3301,0x30,//[14,41]
	//0x330e,0x14,//[8,2d]

	//20181228
	{0x39c5, 0x41},//high temperature blc flick
	{0x39c8, 0x00},

	//20190130
	{0x3635, 0x60},//txvdd bypass

	//20190312	2lane 12bit 30fps
	{0x3641, 0x0c},
	{0x36e9, 0x25},
	{0x36ea, 0x3b},
	{0x36eb, 0x16},
	{0x36ec, 0x0e},
	{0x36ed, 0x14},
	{0x4837, 0x3b},    //0928

	{0x36f9, 0x20},    //675Mbps cntclk
	{0x36fa, 0x27},
	{0x36fb, 0x09},
	{0x36fc, 0x01},
	{0x36fd, 0x14},
	{0x3641, 0x00},
	//vco=27/2*2*4*1.5*5=810
	//sysclk=vco/2/6=67.5
	//mipiclk=vco/2=405
	//mipipclk=mipiclk/2/6=33.75
	{0x3031, 0x0c},//bit mode 12bit
	{0x3037, 0x40},
	{0x301f, 0x02},//setting id

	//20190428
	{0x39c5, 0x21},//high temperature alphaOpt

	//init
	{0x3e00, 0x00},//max exposure = vts*2-0x0a, min exposure = 3
	{0x3e01, 0xc2},
	{0x3e02, 0xa0},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3633, 0x43},
	{0x3622, 0xee},
	{0x363a, 0x80},

	{0x0100, 0x01},
};

static struct regval_list sensor_10b4M30_hdr_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},

	{0x3e01, 0xc2},
	{0x3e02, 0xf0},

	{0x3633, 0x44},
	{0x3306, 0x60},

	{0x3320, 0x01},
	{0x3638, 0x2a},
	{0x3304, 0x30},
	{0x331e, 0x29},
	{0x3309, 0x60},
	{0x331f, 0x59},

	{0x3635, 0x40},
	{0x363b, 0x03},

	{0x3352, 0x02},
	{0x3356, 0x1f},


///////digital updates start////////////
// 08-07-2018

	{0x33e0, 0xc0},
	{0x33e1, 0x08},

	{0x33e2, 0x10},
	{0x33e3, 0x10},
	{0x33e4, 0x20},
	{0x33e5, 0x08},
	{0x33e6, 0x08},
	{0x33e7, 0x10},

	{0x33e8, 0x10},
	{0x33e9, 0x10},
	{0x33ea, 0x20},
	{0x33eb, 0x04},
	{0x33ec, 0x04},
	{0x33ed, 0x10},

	{0x33f4, 0x10},
	{0x33f5, 0x10},
	{0x33f6, 0x20},
	{0x33f7, 0x08},
	{0x33f8, 0x08},
	{0x33f9, 0x10},

	{0x33fa, 0x10},
	{0x33fb, 0x10},
	{0x33fc, 0x20},
	{0x33fd, 0x04},
	{0x33fe, 0x04},
	{0x33ff, 0x10},

//dig 20180806
	{0x336d, 0x01},//[6] comp az end sel 1:st_comp_rst 0:st_cntdown_gap
	{0x337f, 0x2d},//[5] stg2 end sel 1:st_comp_rst 0:st_cntdown_gap
	{0x3320, 0x09},
	{0x366e, 0x04},//[2] pwc logic fine gain sel 1:20~3f 0:10~1f
	{0x360f, 0x04},//[2] array logic fine gain sel 1:20~3f 0:10~1f
	{0x3364, 0x0e},//[3] 0x3301 logic fine gain sel 1:20~3f 0:10~1f
	{0x3e14, 0xb1},//gcvt double en
	{0x3253, 0x06},//power save mode exit point

//blc stable range, 20180806
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x3905, 0xd8}, //one channel

	{0x3399, 0xff}, //0928

//end

//0801
	{0x3637, 0x22},  //4.8k fullwell 188CG
	{0x3908, 0x11},

//30fps 12bit
//0x36e9,0x20,
	{0x4837, 0x3b},
//0x36fb,0x09, //14 is wrong , need debug
	{0x4501, 0xb4},
	{0x3637, 0x22},
	{0x3e09, 0x20},

//new pll config
	{0x3106, 0x81}, // use sysclk from mipipll

	{0x3301, 0x30},

	{0x3309, 0x30},
	{0x331f, 0x29},

	{0x3306, 0x70},
	{0x3634, 0x34}, // buwending?

//25fps
	{0x320c, 0x06},
	{0x320d, 0xc0},

	{0x330b, 0xd8},

//0907
	{0x3630, 0xa8}, //blksun
	{0x3631, 0x80},
	{0x3622, 0xee},

	{0x4509, 0x20}, //ini code

//2688x1520
	{0x3200, 0x00},
	{0x3201, 0x00}, //xaddr start=0
	{0x3204, 0x0a},
	{0x3205, 0x87}, //xaddr end=2695  2696 selected

	{0x3202, 0x00}, //yaddr start=0
	{0x3203, 0x00},
	{0x3206, 0x05}, //xaddr end=1527  1528 selected
	{0x3207, 0xf7},

	{0x3208, 0x0a},
	{0x3209, 0x80},
	{0x320a, 0x05},
	{0x320b, 0xf0},

	{0x3211, 0x04},
	{0x3213, 0x04},

	{0x320c, 0x05}, // hts 1440x2
	{0x320d, 0xa0},
	{0x320e, 0x06},
	{0x320f, 0x1a},

//0918 10bit mipi 675Mbps x 4
	{0x3641, 0x0c},
	{0x36e9, 0x50},
	{0x36ea, 0x27},
	{0x36eb, 0x04},
	{0x36ec, 0x05},
	{0x36ed, 0x14},
	{0x4837, 0x3b},    //0928

	{0x36f9, 0x20},    //675Mbps cntclk
	{0x36fa, 0x27},
	{0x36fb, 0x09},
	{0x36fc, 0x01},
	{0x36fd, 0x14},
	{0x3641, 0x00},

	{0x3308, 0x10},
	{0x3306, 0x70},
	{0x330b, 0xd0},
	{0x3309, 0x60},
	{0x331f, 0x59},

	{0x4501, 0xa4}, //10bit
	{0x3637, 0x44},
	{0x4509, 0x10},

	{0x3e01, 0xba},
	{0x3e02, 0xe0},

//mipi
	{0x3018, 0x73},//[7:5] lane_num-1 [4] digital mipi pad en 1:mipi 0:DVP
	{0x3031, 0x0a},//[3:0] bitmode
	{0x3037, 0x20}, //[6:5] phy bit_res 00:8 01:10 10:12

//0927
	{0x3038, 0x22},
	{0x3366, 0x92},

	{0x337a, 0x08}, //count_keep/up
	{0x337b, 0x10},
	{0x33a3, 0x0c},

//precharge
	{0x3314, 0x94},
	{0x330e, 0x1a},  //[2,28]
	{0x334c, 0x10},  //preprecharge

//20180929 high temp
	{0x3363, 0x00},
	{0x3273, 0x01},
	{0x3933, 0x28},
	{0x3934, 0x20},
	{0x3940, 0x78},
	{0x3942, 0x08},
	{0x3943, 0x28},
	{0x3980, 0x00},
	{0x3981, 0x00},
	{0x3982, 0x00},
	{0x3983, 0x00},
	{0x3984, 0x00},
	{0x3985, 0x00},
	{0x3986, 0x00},
	{0x3987, 0x00},
	{0x3988, 0x00},
	{0x3989, 0x00},
	{0x398a, 0x00},
	{0x398b, 0x04},
	{0x398c, 0x00},
	{0x398d, 0x04},
	{0x398e, 0x00},
	{0x398f, 0x08},
	{0x3990, 0x00},
	{0x3991, 0x10},
	{0x3992, 0x03},
	{0x3993, 0xd8},
	{0x3994, 0x03},
	{0x3995, 0xe0},
	{0x3996, 0x03},
	{0x3997, 0xf0},
	{0x3998, 0x03},
	{0x3999, 0xf8},
	{0x399a, 0x00},
	{0x399b, 0x00},
	{0x399c, 0x00},
	{0x399d, 0x08},
	{0x399e, 0x00},
	{0x399f, 0x10},
	{0x39a0, 0x00},
	{0x39a1, 0x18},
	{0x39a2, 0x00},
	{0x39a3, 0x28},
	{0x39af, 0x58},
	{0x39b5, 0x30},
	{0x39b6, 0x00},
	{0x39b7, 0x34},
	{0x39b8, 0x00},
	{0x39bc, 0x00},
	{0x39bd, 0x00},
	{0x39be, 0x00},
	{0x39c5, 0x61},
	{0x39db, 0x20},
	{0x39dc, 0x00},
	{0x39de, 0x20},
	{0x39df, 0x00},
	{0x39e0, 0x00},
	{0x39e1, 0x00},
	{0x39e2, 0x00},
	{0x39e3, 0x00},
	{0x3940, 0x68},//20190124
	{0x398a, 0x00},
	{0x398b, 0x08},
	{0x398c, 0x00},
	{0x398d, 0x10},
	{0x398e, 0x00},
	{0x398f, 0x18},
	{0x3990, 0x00},
	{0x3991, 0x20},

//internal logic_20181010
	{0x3364, 0x1e},//bit[4] comprst auto enable  bit[3] 0~gain 10~1f 1~ gain 20~3f  //0x33f2 readout
	{0x3301, 0x30},//comprst sel0
	{0x3393, 0x30}, //comprst sel1
	{0x3394, 0x30}, //comprst sel2
	{0x3395, 0x30}, //comprst sel3
	{0x3390, 0x08}, //comprst gain0
	{0x3391, 0x08}, //comprst gain1
	{0x3392, 0x08}, //comprst gain2
	{0x3670, 0x48},//bit[3] 3633 auto enable bit[6] 363a auto enable/ /0x3683 readout auto 3633 //0x3686 readout auto 363a
	{0x366e, 0x04},// bit[2] 0~gain 10~1f 1~ gain 20~3f
	{0x3690, 0x43},//3633 sel0
	{0x3691, 0x44}, //3633 sel1
	{0x3692, 0x44}, //3633 sel2
	{0x369c, 0x08}, //3633 gain0
	{0x369d, 0x08}, //3633 gain1
	{0x3699, 0x8c}, //363a sel0
	{0x369a, 0x96}, //363a sel1
	{0x369b, 0x9f}, //363a sel2
	{0x36a2, 0x08}, //363a gain0
	{0x36a3, 0x08}, //363a gain1
	{0x360f, 0x05},// bit[2] 0~gain 10~1f 1~ gain 20~3f bit[0] 3622 auto enable   //0x3680 readout
	{0x3671, 0xee},//3622 sel0
	{0x3672, 0x6e}, //3622 sel1
	{0x3673, 0x6e}, //3622 sel2
	{0x367a, 0x08}, //3622 gain0
	{0x367b, 0x08}, //3622 gain1

//20181126
	{0x5784, 0x10},
	{0x5785, 0x08},
	{0x5787, 0x06},
	{0x5788, 0x06},
	{0x5789, 0x00},
	{0x578a, 0x06},
	{0x578b, 0x06},
	{0x578c, 0x00},
	{0x5790, 0x10},
	{0x5791, 0x10},
	{0x5792, 0x00},
	{0x5793, 0x10},
	{0x5794, 0x10},
	{0x5795, 0x00},
	{0x57c4, 0x10},
	{0x57c5, 0x08},
	{0x57c7, 0x06},
	{0x57c8, 0x06},
	{0x57c9, 0x00},
	{0x57ca, 0x06},
	{0x57cb, 0x06},
	{0x57cc, 0x00},
	{0x57d0, 0x10},
	{0x57d1, 0x10},
	{0x57d2, 0x00},
	{0x57d3, 0x10},
	{0x57d4, 0x10},
	{0x57d5, 0x00},
	{0x33e0, 0xa0},
	{0x33e1, 0x08},
	{0x33e2, 0x00},
	{0x33e3, 0x10},
	{0x33e4, 0x10},
	{0x33e5, 0x00},
	{0x33e6, 0x10},
	{0x33e7, 0x10},
	{0x33e8, 0x00},
	{0x33e9, 0x10},
	{0x33ea, 0x16},
	{0x33eb, 0x00},
	{0x33ec, 0x10},
	{0x33ed, 0x18},
	{0x33ee, 0xa0},
	{0x33ef, 0x08},
	{0x33f4, 0x00},
	{0x33f5, 0x10},
	{0x33f6, 0x10},
	{0x33f7, 0x00},
	{0x33f8, 0x10},
	{0x33f9, 0x10},
	{0x33fa, 0x00},
	{0x33fb, 0x10},
	{0x33fc, 0x16},
	{0x33fd, 0x00},
	{0x33fe, 0x10},
	{0x33ff, 0x18},

//modify analog fine gain 0x40~0x7f
	{0x3636, 0x20},
	{0x3637, 0x22},
	{0x3e25, 0x03},
	{0x3e26, 0x40},//blc 1xgain dethering слох

//0x3304,0x30,//40
//0x3309,0x60,//70
	{0x330b, 0xe0},//[bs,to][1,d0,f0]
//0x3306,0x70,//[hl,bs][1,60,80]
//0x3301,0x30,//[hl,to][1,14--1a,39--36]
//0x330e,0x1a,//[e,20]

//20181228
	{0x39c5, 0x41},//high temperature blc flick
	{0x39c8, 0x00},

//20190105
	{0x4837, 0x1d},

//20190130
	{0x3635, 0x60},//txvdd bypass

//init
	{0x3e00, 0x00},//max exposure = vts*2-0x0a, min exposure = 3
	{0x3e01, 0xc2},
	{0x3e02, 0xa0},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3633, 0x43},
	{0x3622, 0xee},
	{0x363a, 0x80},

//shdr
	{0x3220, 0x53},
	{0x4816, 0x51},
	{0x320e, 0x0c}, // vts 3124
	{0x320f, 0x34},
	{0x3250, 0x3f},
	{0x3e23, 0x00},
	{0x3e24, 0xb6},
	{0x3e12, 0x03},
	{0x3e13, 0x40},
	{0x4500, 0x08},

//vc mirror test
//grp hold time
	{0x4500, 0x08},
	{0x3225, 0x02},
	{0x3235, 0x18},    //(real vts-1),real_vts:{320e,320f}*2 in halfrow_mode
	{0x3236, 0x67},
	{0x3237, 0x02},
	{0x3238, 0x90},   //real_hts-'d64,real_hts:{320c,320d}/2 in halfrow_mode

//0x3221,0x80,//blc re-calc when flip
	{0x3905, 0x98},//4 channel blc

	{0x301f, 0x03},//setting id

//20190428
	{0x39c5, 0x21},//high temperature alphaOpt

//init
	{0x3e03, 0x0b},
	{0x3e00, 0x01},
	{0x3e01, 0x6e},
	{0x3e02, 0xe0},
	{0x3e04, 0x16},
	{0x3e05, 0x00},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e10, 0x00},
	{0x3e11, 0x80},
	{0x3e12, 0x03},
	{0x3e13, 0x40},

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

static int sc4233p_sensor_vts;
static int sc4233p_sensor_svr;
static int shutter_delay = 1;
static int shutter_delay_cnt;
static int fps_change_flag;

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	data_type explow, expmid, exphigh;
	struct sensor_info *info = to_state(sd);

	if (info->isp_wdr_mode == ISP_DOL_WDR_MODE) {
		exphigh = (unsigned char) (0xf & (exp_val>>15));
		expmid = (unsigned char) (0xff & (exp_val>>7));
		explow = (unsigned char) (0xf0 & (exp_val<<1));

		sensor_write(sd, 0x3e02, explow);
		sensor_write(sd, 0x3e01, expmid);
		sensor_write(sd, 0x3e00, exphigh);

		sensor_dbg("sensor_set_long_exp = %d line Done!\n", exp_val);

		exp_val /= HDR_RATIO;
		exphigh = (unsigned char) (0xff & (exp_val>>7));
		explow = (unsigned char) (0xf0 & (exp_val<<1));

		sensor_write(sd, 0x3e05, explow);
		sensor_write(sd, 0x3e04, exphigh);

		sensor_dbg("sensor_set_short_exp = %d line Done!\n", exp_val);

	} else {
		exphigh = (unsigned char) (0xf & (exp_val>>15));
		expmid = (unsigned char) (0xff & (exp_val>>7));
		explow = (unsigned char) (0xf0 & (exp_val<<1));

		sensor_write(sd, 0x3e02, explow);
		sensor_write(sd, 0x3e01, expmid);
		sensor_write(sd, 0x3e00, exphigh);
		sensor_dbg("sensor_set_exp = %d line Done!\n", exp_val);
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

static int sensor_s_gain(struct v4l2_subdev *sd, int gain_val)
{
	struct sensor_info *info = to_state(sd);
	data_type gainlow = 0;
	data_type gainhigh = 0;
	data_type gaindiglow = 0x80;
	data_type gaindighigh = 0x00;

	int gainana = gain_val << 1;

	if (gainana < 0x40) { //32 - 64
		gainhigh = 0x03;
		gainlow = gainana;
	} else if (gainana < 2 * 0x40) { //64 - 128
		gainhigh = 0x07;
		gainlow = gainana >> 1;
	} else if (gainana < 4 * 0x40) { //128 - 256
		gainhigh = 0x0f;
		gainlow = gainana >> 2;
	} else if (gainana < 8 * 0x40) { //256 - 512
		gainhigh = 0x1f;
		gainlow = gainana >> 3;
	} else {
		gainhigh = 0x1f;
		gainlow = 0x3f;
		if (gainana < 16 * 0x40) { //512 - 1024
			gaindiglow = gainana >> 2;
			gaindighigh = 0x00;
		} else if (gainana < 32 * 0x40) { // 1024 - 2048
			gaindiglow = gainana >> 3;
			gaindighigh = 0x01;
		} else if (gainana < 64 * 0x40) { // 2048 - 4096
			gaindiglow = gainana >> 4;
			gaindighigh = 0x03;
		} else if (gainana < 128 * 0x40) { // 4096 - 8192
			gaindiglow = gainana >> 5;
			gaindighigh = 0x07;
		} else if (gainana < 256 * 0x40) { // 8192 - 12864
			gaindiglow = gainana >> 6;
			gaindighigh = 0x0f;
		} else {
			gaindiglow = 0xfc;
			gaindighigh = 0x0f;
		}

	}

	if (info->isp_wdr_mode == ISP_DOL_WDR_MODE) {
		sensor_write(sd, 0x3e13, (unsigned char)gainlow);
		sensor_write(sd, 0x3e12, (unsigned char)gainhigh);
		sensor_write(sd, 0x3e11, (unsigned char)gaindiglow);
		sensor_write(sd, 0x3e10, (unsigned char)gaindighigh);
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
//	if (rdval != (V4L2_IDENT_SENSOR>>8))
//		return -ENODEV;
	sensor_print("0x3107 = 0x%x\n", rdval);
	sensor_read(sd, 0x3108, &rdval);
//	if (rdval != (V4L2_IDENT_SENSOR&0xff))
//		return -ENODEV;
	sensor_print("0x3108 = 0x%x\n", rdval);
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
	info->width = 2688;
	info->height = 1520;
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
	 .width = 2688,
	 .height = 1520,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 2880,
	 .vts = 1562,
	 .pclk = 135 * 1000 * 1000,
	 .mipi_bps = 576 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .intg_min = 3 << 4,
	 .intg_max = (1562 - 8) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .regs = sensor_12b4M30_regs,
	 .regs_size = ARRAY_SIZE(sensor_12b4M30_regs),
	 .set_size = NULL,
	 },

	 {
	 .width = 2688,
	 .height = 1520,
	 .hoffset = 0,
	 .voffset = 0,
	 .hts = 2880,
	 .vts = 3124,
	 .pclk = 270 * 1000 * 1000,
	 .mipi_bps = 675 * 1000 * 1000,
	 .fps_fixed = 30,
	 .bin_factor = 1,
	 .if_mode = MIPI_VC_WDR_MODE,
	 .wdr_mode = ISP_DOL_WDR_MODE,
	 .intg_min = 3 << 4,
	 .intg_max = (3124 - 8) << 4,
	 .gain_min = 1 << 4,
	 .gain_max = 1440 << 4,
	 .regs = sensor_10b4M30_hdr_regs,
	 .regs_size = ARRAY_SIZE(sensor_10b4M30_hdr_regs),
	 .set_size = NULL,
	 },

};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	struct sensor_info *info = to_state(sd);

	cfg->type = V4L2_MBUS_CSI2;
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
