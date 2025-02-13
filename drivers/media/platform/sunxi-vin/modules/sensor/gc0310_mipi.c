/*
 * A V4L2 driver for GC0310 Raw cameras.
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zheng ZeQun <zequnzheng@allwinnertech.com>
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

MODULE_AUTHOR("lwj");
MODULE_DESCRIPTION("A low-level driver for GC0310 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (24*1000*1000)
#define V4L2_IDENT_SENSOR 0xa310

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

/*
 * The GC0310 i2c address
 */
#define I2C_ADDR 0x42

#define SENSOR_NAME "gc0310_mipi"


struct cfg_array { /* coming later */
	struct regval_list *regs;
	int size;
};

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {

};


static struct regval_list sensor_480p20_regs[] = {
	/* /////////////////////////////////////////////// */
	/* ////////////////  system reg  ///////////////// */
	/* /////////////////////////////////////////////// */
	{0xfe, 0xf0},
	{0xfe, 0xf0},
	{0xfe, 0x00},
	{0xfc, 0x0e},
	{0xfc, 0x0e},
	{0xf2, 0x80},
	{0xf3, 0x00},
	{0xf7, 0x1b},
	{0xf8, 0x04},
	{0xf9, 0x8e},
	{0xfa, 0x11},

	/* /////////////////////////////////////////////// */
	/* ////////////////  CISCTL reg   //////////////// */
	/* /////////////////////////////////////////////// */
	{0x00, 0x2f},
	{0x01, 0x0f},
	{0x02, 0x04},
	{0x03, 0x03},
	{0x04, 0x50},
	{0x09, 0x00},
	{0x0a, 0x00},
	{0x0b, 0x00},
	{0x0c, 0x04},
	{0x0d, 0x01},
	{0x0e, 0xe8},
	{0x0f, 0x02},
	{0x10, 0x88},
	{0x16, 0x00},
	{0x17, 0x14},
	{0x18, 0x1a},
	{0x19, 0x14},
	{0x1b, 0x48},
	{0x1c, 0x1c},
	{0x1e, 0x6b},
	{0x1f, 0x28},
	{0x20, 0x8b}, /* 0x89 travis20140801 */
	{0x21, 0x49},
	{0x22, 0xb0},
	{0x23, 0x04},
	{0x24, 0x16},
	{0x34, 0x20},

	/* /////////////////////////////////////////////// */
	/* ///////////////////   BLK   /////////////////// */
	/* /////////////////////////////////////////////// */
	{0x26, 0x23},
	{0x28, 0xff},
	{0x29, 0x00},
	{0x32, 0x00},
	{0x33, 0x10},
	{0x37, 0x20},
	{0x38, 0x10},
	{0x47, 0x80},
	{0x4e, 0x66},
	{0xa8, 0x02},
	{0xa9, 0x80},

	/* /////////////////////////////////////////////// */
	/* /////////////////  ISP reg   ////////////////// */
	/* /////////////////////////////////////////////// */
	{0x40, 0xff},
	{0x41, 0x21},
	{0x42, 0xcf},
	{0x44, 0x02},
	{0x45, 0xa8},
	{0x46, 0x02},
	{0x4a, 0x11},
	{0x4b, 0x01},
	{0x4c, 0x20},
	{0x4d, 0x05},
	{0x4f, 0x01},
	{0x50, 0x01},
	{0x55, 0x01},
	{0x56, 0xe0},
	{0x57, 0x02},
	{0x58, 0x80},

	/* /////////////////////////////////////////////// */
	/* //////////////////   GAIN   /////////////////// */
	/* /////////////////////////////////////////////// */
	{0x70, 0x70},
	{0x5a, 0x84},
	{0x5b, 0xc9},
	{0x5c, 0xed},
	{0x77, 0x74},
	{0x78, 0x40},
	{0x79, 0x5f},

	/* /////////////////////////////////////////////// */
	/* //////////////////   DNDD  //////////////////// */
	/* /////////////////////////////////////////////// */
	{0x82, 0x14},
	{0x83, 0x0b},
	{0x89, 0xf0},

	/* /////////////////////////////////////////////// */
	/* /////////////////   EEINTP  /////////////////// */
	/* /////////////////////////////////////////////// */
	{0x8f, 0xaa},
	{0x90, 0x8c},
	{0x91, 0x90},
	{0x92, 0x03},
	{0x93, 0x03},
	{0x94, 0x05},
	{0x95, 0x65},
	{0x96, 0xf0},

	/* /////////////////////////////////////////////// */
	/* ////////////////////  ASDE  /////////////////// */
	/* /////////////////////////////////////////////// */
	{0xfe, 0x00},

	{0x9a, 0x20},
	{0x9b, 0x80},
	{0x9c, 0x40},
	{0x9d, 0x80},

	{0xa1, 0x30},
	{0xa2, 0x32},
	{0xa4, 0x30},
	{0xa5, 0x30},
	{0xaa, 0x10},
	{0xac, 0x22},


	/* /////////////////////////////////////////////// */
	/* //////////////////   GAMMA   ////////////////// */
	/* /////////////////////////////////////////////// */
	{0xfe, 0x00}, /* default */
	{0xbf, 0x08},
	{0xc0, 0x16},
	{0xc1, 0x28},
	{0xc2, 0x41},
	{0xc3, 0x5a},
	{0xc4, 0x6c},
	{0xc5, 0x7a},
	{0xc6, 0x96},
	{0xc7, 0xac},
	{0xc8, 0xbc},
	{0xc9, 0xc9},
	{0xca, 0xd3},
	{0xcb, 0xdd},
	{0xcc, 0xe5},
	{0xcd, 0xf1},
	{0xce, 0xfa},
	{0xcf, 0xff},

	/* /////////////////////////////////////////////// */
	/* //////////////////   YCP  ///////////////////// */
	/* /////////////////////////////////////////////// */
	{0xd0, 0x40},
	{0xd1, 0x34},
	{0xd2, 0x34},
	{0xd3, 0x40},
	{0xd6, 0xf2},
	{0xd7, 0x1b},
	{0xd8, 0x18},
	{0xdd, 0x03},

	/* /////////////////////////////////////////////// */
	/* ///////////////////   AEC   /////////////////// */
	/* /////////////////////////////////////////////// */
	{0xfe, 0x01},
	{0x05, 0x30},
	{0x06, 0x75},
	{0x07, 0x40},
	{0x08, 0xb0},
	{0x0a, 0xc5},
	{0x0b, 0x11},
	{0x0c, 0x00},
	{0x12, 0x52},
	{0x13, 0x38},
	{0x18, 0x95},
	{0x19, 0x96},
	{0x1f, 0x20},
	{0x20, 0xc0},
	{0x3e, 0x40},
	{0x3f, 0x57},
	{0x40, 0x7d},
	{0x03, 0x60},
	{0x44, 0x02},

	/* /////////////////////////////////////////////// */
	/* ///////////////////   AWB   /////////////////// */
	/* /////////////////////////////////////////////// */
	{0xfe, 0x01},
	{0x1c, 0x91},
	{0x21, 0x15},
	{0x50, 0x80},
	{0x56, 0x04},
	{0x59, 0x08},
	{0x5b, 0x02},
	{0x61, 0x8d},
	{0x62, 0xa7},
	{0x63, 0xd0},
	{0x65, 0x06},
	{0x66, 0x06},
	{0x67, 0x84},
	{0x69, 0x08},
	{0x6a, 0x25},
	{0x6b, 0x01},
	{0x6c, 0x00},
	{0x6d, 0x02},
	{0x6e, 0xf0},
	{0x6f, 0x80},
	{0x76, 0x80},
	{0x78, 0xaf},
	{0x79, 0x75},
	{0x7a, 0x40},
	{0x7b, 0x50},
	{0x7c, 0x0c},

	{0x90, 0xc9}, /* stable AWB */
	{0x91, 0xbe},
	{0x92, 0xe2},
	{0x93, 0xc9},
	{0x95, 0x1b},
	{0x96, 0xe2},
	{0x97, 0x49},
	{0x98, 0x1b},
	{0x9a, 0x49},
	{0x9b, 0x1b},
	{0x9c, 0xc3},
	{0x9d, 0x49},
	{0x9f, 0xc7},
	{0xa0, 0xc8},
	{0xa1, 0x00},
	{0xa2, 0x00},
	{0x86, 0x00},
	{0x87, 0x00},
	{0x88, 0x00},
	{0x89, 0x00},
	{0xa4, 0xb9},
	{0xa5, 0xa0},
	{0xa6, 0xba},
	{0xa7, 0x92},
	{0xa9, 0xba},
	{0xaa, 0x80},
	{0xab, 0x9d},
	{0xac, 0x7f},
	{0xae, 0xbb},
	{0xaf, 0x9d},
	{0xb0, 0xc8},
	{0xb1, 0x97},
	{0xb3, 0xb7},
	{0xb4, 0x7f},
	{0xb5, 0x00},
	{0xb6, 0x00},
	{0x8b, 0x00},
	{0x8c, 0x00},
	{0x8d, 0x00},
	{0x8e, 0x00},
	{0x94, 0x55},
	{0x99, 0xa6},
	{0x9e, 0xaa},
	{0xa3, 0x0a},
	{0x8a, 0x00},
	{0xa8, 0x55},
	{0xad, 0x55},
	{0xb2, 0x55},
	{0xb7, 0x05},
	{0x8f, 0x00},
	{0xb8, 0xcb},
	{0xb9, 0x9b},

	/* /////////////////////////////////////////////// */
	/* ///////////////////   CC    /////////////////// */
	/* /////////////////////////////////////////////// */
	{0xfe, 0x01},

	{0xd0, 0x38}, /* skin red */
	{0xd1, 0x00},
	{0xd2, 0x02},
	{0xd3, 0x04},
	{0xd4, 0x38},
	{0xd5, 0x12},

	{0xd6, 0x30},
	{0xd7, 0x00},
	{0xd8, 0x0a},
	{0xd9, 0x16},
	{0xda, 0x39},
	{0xdb, 0xf8},

	/* /////////////////////////////////////////////// */
	/* ///////////////////   LSC   /////////////////// */
	/* /////////////////////////////////////////////// */
	{0xfe, 0x01},
	{0xc1, 0x3c},
	{0xc2, 0x50},
	{0xc3, 0x00},
	{0xc4, 0x40},
	{0xc5, 0x30},
	{0xc6, 0x30},
	{0xc7, 0x10},
	{0xc8, 0x00},
	{0xc9, 0x00},
	{0xdc, 0x20},
	{0xdd, 0x10},
	{0xdf, 0x00},
	{0xde, 0x00},

	/* /////////////////////////////////////////////// */
	/* //////////////////  Histogram  //////////////// */
	/* /////////////////////////////////////////////// */
	{0x01, 0x10},
	{0x0b, 0x31},
	{0x0e, 0x50},
	{0x0f, 0x0f},
	{0x10, 0x6e},
	{0x12, 0xa0},
	{0x15, 0x60},
	{0x16, 0x60},
	{0x17, 0xe0},

	/* /////////////////////////////////////////////// */
	/* /////////////   Measure Window   ////////////// */
	/* /////////////////////////////////////////////// */
	{0xcc, 0x0c},
	{0xcd, 0x10},
	{0xce, 0xa0},
	{0xcf, 0xe6},

	/* /////////////////////////////////////////////// */
	/* ////////////////   dark sun   ///////////////// */
	/* /////////////////////////////////////////////// */
	{0x45, 0xf7},
	{0x46, 0xff},
	{0x47, 0x15},
	{0x48, 0x03},
	{0x4f, 0x60},

	/* /////////////////banding///////////////////// */
	{0xfe, 0x00},
	{0x05, 0x01},
	{0x06, 0x18},
	{0x07, 0x00},
	{0x08, 0x10},
	{0xfe, 0x01},
	{0x25, 0x00},
	{0x26, 0x9a},
	{0x27, 0x01},
	{0x28, 0x34}, /* 30fps */
	{0x29, 0x01},
	{0x2a, 0x34}, /* 25fps */
	{0x2b, 0x01},
	{0x2c, 0x34}, /* 20fps */
	{0x2d, 0x01},
	{0x2e, 0x34}, /* 16.6fos */
	{0x2f, 0x01},
	{0x30, 0x34}, /* 12.5fps */
	{0x31, 0x01},
	{0x32, 0x34}, /* 10fps */
	{0x33, 0x01},
	{0x34, 0x34},
	{0xfe, 0x00},

	/* /////////////////////////////////////////////// */
	/* //////////////////   MIPI   /////////////////// */
	/* /////////////////////////////////////////////// */
	{0xfe, 0x03},
	{0x40, 0x08},
	{0x42, 0x00},
	{0x43, 0x00},
	{0x01, 0x03},
	{0x10, 0x84},

	{0x01, 0x03},
	{0x02, 0x11}, /* 00 20150522 */
	{0x03, 0x94},
	{0x04, 0x01},
	{0x05, 0x00},
	{0x06, 0x80},
	{0x11, 0x1e},
	{0x12, 0x00},
	{0x13, 0x05},
	{0x15, 0x10},
	{0x21, 0x10},
	{0x22, 0x01},
	{0x23, 0x10},
	{0x24, 0x02},
	{0x25, 0x10},
	{0x26, 0x03},
	{0x29, 0x02},
	{0x2a, 0x0a},
	{0x2b, 0x04},

	{0x10, 0x94}, /* stream on */
	{0xfe, 0x00},
};


/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 *
 */

static struct regval_list sensor_fmt_yuv422_yuyv[] = {
	{0x44, 0x02}, /* YCbYCr */
};

/*
 *static struct regval_list sensor_fmt_yuv422_yvyu[] = {
 *	{0x44, 0x03}, //YCrYCb
 *};
 */

/*
 *static struct regval_list sensor_fmt_yuv422_vyuy[] = {
 *	{0x44, 0x01}, //CrYCbY
 *};
 */

/*
 *static struct regval_list sensor_fmt_yuv422_uyvy[] = {
 *	{0x44, 0x00}, //CbYCrY
 *};
 */

/*
 * The white balance settings
 * Here only tune the R G B channel gain.
 * The white balance enalbe bit is modified in sensor_s_autowb and sensor_s_wb
 */
static struct regval_list sensor_wb_manual[] = {
	/* null */
};

static struct regval_list sensor_wb_auto_regs[] = {
	{0xfe, 0x00},
	{0x42, 0xcf},
};

static struct regval_list sensor_wb_incandescence_regs[] = {
	/* bai re guang */
	{0xfe, 0x00},
	{0x42, 0xcd},
	{0x77, 0x48},
	{0x78, 0x40},
	{0x79, 0x5c},
};

static struct regval_list sensor_wb_fluorescent_regs[] = {
	/* ri guang deng */
	{0xfe, 0x00},
	{0x42, 0xcd},
	{0x77, 0x40},
	{0x78, 0x42},
	{0x79, 0x50},
};

static struct regval_list sensor_wb_tungsten_regs[] = {
	/* wu si deng */
	{0xfe, 0x00},
	{0x42, 0xcd},
	{0x77, 0x40},
	{0x78, 0x54},
	{0x79, 0x70},
};

static struct regval_list sensor_wb_horizon[] = {
	/* null */
};

static struct regval_list sensor_wb_daylight_regs[] = {
	/* tai yang guang */
	{0xfe, 0x00},
	{0x42, 0xcd},
	{0x77, 0x74},
	{0x78, 0x52},
	{0x79, 0x40},
};

static struct regval_list sensor_wb_flash[] = {
	/* null */
};


static struct regval_list sensor_wb_cloud_regs[] = {
	{0xfe, 0x00},
	{0x42, 0xcd},
	{0x77, 0x8c}, /* WB_manual_gain */
	{0x78, 0x50},
	{0x79, 0x40},
};

static struct regval_list sensor_wb_shade[] = {
	/* null */
};

static struct cfg_array sensor_wb[] = {
	{
		/* V4L2_WHITE_BALANCE_MANUAL */
		.regs = sensor_wb_manual,
		.size = ARRAY_SIZE(sensor_wb_manual),
	},
	{
		/* V4L2_WHITE_BALANCE_AUTO */
		.regs = sensor_wb_auto_regs,
		.size = ARRAY_SIZE(sensor_wb_auto_regs),
	},
	{
		/* V4L2_WHITE_BALANCE_INCANDESCENT */
		.regs = sensor_wb_incandescence_regs,
		.size = ARRAY_SIZE(sensor_wb_incandescence_regs),
	},
	{
		/* V4L2_WHITE_BALANCE_FLUORESCENT */
		.regs = sensor_wb_fluorescent_regs,
		.size = ARRAY_SIZE(sensor_wb_fluorescent_regs),
	},
	{
		/* V4L2_WHITE_BALANCE_FLUORESCENT_H */
		.regs = sensor_wb_tungsten_regs,
		.size = ARRAY_SIZE(sensor_wb_tungsten_regs),
	},
	{
		/* V4L2_WHITE_BALANCE_HORIZON */
		.regs = sensor_wb_horizon,
		.size = ARRAY_SIZE(sensor_wb_horizon),
	},
	{
		/* V4L2_WHITE_BALANCE_DAYLIGHT */
		.regs = sensor_wb_daylight_regs,
		.size = ARRAY_SIZE(sensor_wb_daylight_regs),
	},
	{
		/* V4L2_WHITE_BALANCE_FLASH */
		.regs = sensor_wb_flash,
		.size = ARRAY_SIZE(sensor_wb_flash),
	},
	{
		/* V4L2_WHITE_BALANCE_CLOUDY */
		.regs = sensor_wb_cloud_regs,
		.size = ARRAY_SIZE(sensor_wb_cloud_regs),
	},
	{
		/* V4L2_WHITE_BALANCE_SHADE */
		.regs = sensor_wb_shade,
		.size = ARRAY_SIZE(sensor_wb_shade),
	},
};


/*
 * The color effect settings
 */
static struct regval_list sensor_colorfx_none_regs[] = {
	{0xfe, 0x00},
	{0x43, 0x00},
};

static struct regval_list sensor_colorfx_bw_regs[] = {
	{0xfe, 0x00},
	{0x43, 0x02},
	{0xda, 0x00},
	{0xdb, 0x00},
};

static struct regval_list sensor_colorfx_sepia_regs[] = {
	{0xfe, 0x00},
	{0x43, 0x02},
	{0xda, 0xd0},
	{0xdb, 0x28},
};

static struct regval_list sensor_colorfx_negative_regs[] = {
	{0xfe, 0x00},
	{0x43, 0x01},
};

static struct regval_list sensor_colorfx_emboss_regs[] = {

};

static struct regval_list sensor_colorfx_sketch_regs[] = {

};

static struct regval_list sensor_colorfx_sky_blue_regs[] = {
	{0xfe, 0x00},
	{0x43, 0x02},
	{0xda, 0x50},
	{0xdb, 0xe0},
};

static struct regval_list sensor_colorfx_grass_green_regs[] = {
	{0xfe, 0x00},
	{0x43, 0x02},
	{0xda, 0xc0},
	{0xdb, 0xc0},
};

static struct regval_list sensor_colorfx_skin_whiten_regs[] = {

};

static struct regval_list sensor_colorfx_vivid_regs[] = {
	/* NULL */
};

static struct regval_list sensor_colorfx_aqua_regs[] = {
	/* null */
};

static struct regval_list sensor_colorfx_art_freeze_regs[] = {
	/* null */
};

static struct regval_list sensor_colorfx_silhouette_regs[] = {
	/* null */
};

static struct regval_list sensor_colorfx_solarization_regs[] = {
	/* null */
};

static struct regval_list sensor_colorfx_antique_regs[] = {
	/* null */
};

static struct regval_list sensor_colorfx_set_cbcr_regs[] = {
	/* null */
};

static struct cfg_array sensor_colorfx[] = {
	{
		/* V4L2_COLORFX_NONE = 0, */
		.regs = sensor_colorfx_none_regs,
		.size = ARRAY_SIZE(sensor_colorfx_none_regs),
	},
	{
		/* V4L2_COLORFX_BW   = 1, */
		.regs = sensor_colorfx_bw_regs,
		.size = ARRAY_SIZE(sensor_colorfx_bw_regs),
	},
	{
		/* V4L2_COLORFX_SEPIA  = 2, */
		.regs = sensor_colorfx_sepia_regs,
		.size = ARRAY_SIZE(sensor_colorfx_sepia_regs),
	},
	{
		/* V4L2_COLORFX_NEGATIVE = 3, */
		.regs = sensor_colorfx_negative_regs,
		.size = ARRAY_SIZE(sensor_colorfx_negative_regs),
	},
	{
		/* V4L2_COLORFX_EMBOSS = 4, */
		.regs = sensor_colorfx_emboss_regs,
		.size = ARRAY_SIZE(sensor_colorfx_emboss_regs),
	},
	{
		/* V4L2_COLORFX_SKETCH = 5, */
		.regs = sensor_colorfx_sketch_regs,
		.size = ARRAY_SIZE(sensor_colorfx_sketch_regs),
	},
	{
		/* V4L2_COLORFX_SKY_BLUE = 6, */
		.regs = sensor_colorfx_sky_blue_regs,
		.size = ARRAY_SIZE(sensor_colorfx_sky_blue_regs),
	},
	{
		/* V4L2_COLORFX_GRASS_GREEN = 7, */
		.regs = sensor_colorfx_grass_green_regs,
		.size = ARRAY_SIZE(sensor_colorfx_grass_green_regs),
	},
	{
		/* V4L2_COLORFX_SKIN_WHITEN = 8, */
		.regs = sensor_colorfx_skin_whiten_regs,
		.size = ARRAY_SIZE(sensor_colorfx_skin_whiten_regs),
	},
	{
		/* V4L2_COLORFX_VIVID = 9, */
		.regs = sensor_colorfx_vivid_regs,
		.size = ARRAY_SIZE(sensor_colorfx_vivid_regs),
	},
	{
		/* V4L2_COLORFX_AQUA = 10, */
		.regs = sensor_colorfx_aqua_regs,
		.size = ARRAY_SIZE(sensor_colorfx_aqua_regs),
	},
	{
		/* V4L2_COLORFX_ART_FREEZE = 11, */
		.regs = sensor_colorfx_art_freeze_regs,
		.size = ARRAY_SIZE(sensor_colorfx_art_freeze_regs),
	},
	{
		/* V4L2_COLORFX_SILHOUETTE = 12, */
		.regs = sensor_colorfx_silhouette_regs,
		.size = ARRAY_SIZE(sensor_colorfx_silhouette_regs),
	},
	{
		/* V4L2_COLORFX_SOLARIZATION = 13, */
		.regs = sensor_colorfx_solarization_regs,
		.size = ARRAY_SIZE(sensor_colorfx_solarization_regs),
	},
	{
		/* V4L2_COLORFX_ANTIQUE = 14, */
		.regs = sensor_colorfx_antique_regs,
		.size = ARRAY_SIZE(sensor_colorfx_antique_regs),
	},
	{
		/* V4L2_COLORFX_SET_CBCR = 15, */
		.regs = sensor_colorfx_set_cbcr_regs,
		.size = ARRAY_SIZE(sensor_colorfx_set_cbcr_regs),
	},
};

/*
 * The brightness setttings
 */
static struct regval_list sensor_brightness_neg4_regs[] = {
	/* NULL */
};

static struct regval_list sensor_brightness_neg3_regs[] = {
	/* NULL */
};

static struct regval_list sensor_brightness_neg2_regs[] = {
	/* NULL */
};

static struct regval_list sensor_brightness_neg1_regs[] = {
	/* NULL */
};

static struct regval_list sensor_brightness_zero_regs[] = {
	/* NULL */
};

static struct regval_list sensor_brightness_pos1_regs[] = {
	/* NULL */
};

static struct regval_list sensor_brightness_pos2_regs[] = {
	/* NULL */
};

static struct regval_list sensor_brightness_pos3_regs[] = {
	/* NULL */
};

static struct regval_list sensor_brightness_pos4_regs[] = {
	/* NULL */
};

static struct cfg_array sensor_brightness[] = {
	{
		.regs = sensor_brightness_neg4_regs,
		.size = ARRAY_SIZE(sensor_brightness_neg4_regs),
	},
	{
		.regs = sensor_brightness_neg3_regs,
		.size = ARRAY_SIZE(sensor_brightness_neg3_regs),
	},
	{
		.regs = sensor_brightness_neg2_regs,
		.size = ARRAY_SIZE(sensor_brightness_neg2_regs),
	},
	{
		.regs = sensor_brightness_neg1_regs,
		.size = ARRAY_SIZE(sensor_brightness_neg1_regs),
	},
	{
		.regs = sensor_brightness_zero_regs,
		.size = ARRAY_SIZE(sensor_brightness_zero_regs),
	},
	{
		.regs = sensor_brightness_pos1_regs,
		.size = ARRAY_SIZE(sensor_brightness_pos1_regs),
	},
	{
		.regs = sensor_brightness_pos2_regs,
		.size = ARRAY_SIZE(sensor_brightness_pos2_regs),
	},
	{
		.regs = sensor_brightness_pos3_regs,
		.size = ARRAY_SIZE(sensor_brightness_pos3_regs),
	},
	{
		.regs = sensor_brightness_pos4_regs,
		.size = ARRAY_SIZE(sensor_brightness_pos4_regs),
	},
};

/*
 * The contrast setttings
 */
static struct regval_list sensor_contrast_neg4_regs[] = {
	/* NULL */
};

static struct regval_list sensor_contrast_neg3_regs[] = {
	/* NULL */
};

static struct regval_list sensor_contrast_neg2_regs[] = {
	/* NULL */
};

static struct regval_list sensor_contrast_neg1_regs[] = {
	/* NULL */
};

static struct regval_list sensor_contrast_zero_regs[] = {
	/* NULL */
};

static struct regval_list sensor_contrast_pos1_regs[] = {
	/* NULL */
};

static struct regval_list sensor_contrast_pos2_regs[] = {
	/* NULL */
};

static struct regval_list sensor_contrast_pos3_regs[] = {
	/* NULL */
};

static struct regval_list sensor_contrast_pos4_regs[] = {
};

static struct cfg_array sensor_contrast[] = {
	{
		.regs = sensor_contrast_neg4_regs,
		.size = ARRAY_SIZE(sensor_contrast_neg4_regs),
	},
	{
		.regs = sensor_contrast_neg3_regs,
		.size = ARRAY_SIZE(sensor_contrast_neg3_regs),
	},
	{
		.regs = sensor_contrast_neg2_regs,
		.size = ARRAY_SIZE(sensor_contrast_neg2_regs),
	},
	{
		.regs = sensor_contrast_neg1_regs,
		.size = ARRAY_SIZE(sensor_contrast_neg1_regs),
	},
	{
		.regs = sensor_contrast_zero_regs,
		.size = ARRAY_SIZE(sensor_contrast_zero_regs),
	},
	{
		.regs = sensor_contrast_pos1_regs,
		.size = ARRAY_SIZE(sensor_contrast_pos1_regs),
	},
	{
		.regs = sensor_contrast_pos2_regs,
		.size = ARRAY_SIZE(sensor_contrast_pos2_regs),
	},
	{
		.regs = sensor_contrast_pos3_regs,
		.size = ARRAY_SIZE(sensor_contrast_pos3_regs),
	},
	{
		.regs = sensor_contrast_pos4_regs,
		.size = ARRAY_SIZE(sensor_contrast_pos4_regs),
	},
};

/*
 * The saturation setttings
 */
static struct regval_list sensor_saturation_neg4_regs[] = {
	/* NULL */
};

static struct regval_list sensor_saturation_neg3_regs[] = {
	/* NULL */
};

static struct regval_list sensor_saturation_neg2_regs[] = {
	/* NULL */
};

static struct regval_list sensor_saturation_neg1_regs[] = {
	/* NULL */
};

static struct regval_list sensor_saturation_zero_regs[] = {
	/* NULL */
};

static struct regval_list sensor_saturation_pos1_regs[] = {
	/* NULL */
};

static struct regval_list sensor_saturation_pos2_regs[] = {
	/* NULL */
};

static struct regval_list sensor_saturation_pos3_regs[] = {
	/* NULL */
};

static struct regval_list sensor_saturation_pos4_regs[] = {
	/* NULL */
};

static struct cfg_array sensor_saturation[] = {
	{
		.regs = sensor_saturation_neg4_regs,
		.size = ARRAY_SIZE(sensor_saturation_neg4_regs),
	},
	{
		.regs = sensor_saturation_neg3_regs,
		.size = ARRAY_SIZE(sensor_saturation_neg3_regs),
	},
	{
		.regs = sensor_saturation_neg2_regs,
		.size = ARRAY_SIZE(sensor_saturation_neg2_regs),
	},
	{
		.regs = sensor_saturation_neg1_regs,
		.size = ARRAY_SIZE(sensor_saturation_neg1_regs),
	},
	{
		.regs = sensor_saturation_zero_regs,
		.size = ARRAY_SIZE(sensor_saturation_zero_regs),
	},
	{
		.regs = sensor_saturation_pos1_regs,
		.size = ARRAY_SIZE(sensor_saturation_pos1_regs),
	},
	{
		.regs = sensor_saturation_pos2_regs,
		.size = ARRAY_SIZE(sensor_saturation_pos2_regs),
	},
	{
		.regs = sensor_saturation_pos3_regs,
		.size = ARRAY_SIZE(sensor_saturation_pos3_regs),
	},
	{
		.regs = sensor_saturation_pos4_regs,
		.size = ARRAY_SIZE(sensor_saturation_pos4_regs),
	},
};

/*
 * The exposure target setttings
 */
static struct regval_list sensor_ev_neg4_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x10},
	{0xfe, 0x00},
};

static struct regval_list sensor_ev_neg3_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x18},
	{0xfe, 0x00},
};

static struct regval_list sensor_ev_neg2_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x20},
	{0xfe, 0x00},
};

static struct regval_list sensor_ev_neg1_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x28},
	{0xfe, 0x00},
};

static struct regval_list sensor_ev_zero_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x34},
	{0xfe, 0x00},
};

static struct regval_list sensor_ev_pos1_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x40},
	{0xfe, 0x00},
};

static struct regval_list sensor_ev_pos2_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x48},
	{0xfe, 0x00},
};

static struct regval_list sensor_ev_pos3_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x50},
	{0xfe, 0x00},
};

static struct regval_list sensor_ev_pos4_regs[] = {
	{0xfe, 0x01},
	{0x13, 0x58},
	{0xfe, 0x00},
};

static struct cfg_array sensor_ev[] = {
	{
		.regs = sensor_ev_neg4_regs,
		.size = ARRAY_SIZE(sensor_ev_neg4_regs),
	},
	{
		.regs = sensor_ev_neg3_regs,
		.size = ARRAY_SIZE(sensor_ev_neg3_regs),
	},
	{
		.regs = sensor_ev_neg2_regs,
		.size = ARRAY_SIZE(sensor_ev_neg2_regs),
	},
	{
		.regs = sensor_ev_neg1_regs,
		.size = ARRAY_SIZE(sensor_ev_neg1_regs),
	},
	{
		.regs = sensor_ev_zero_regs,
		.size = ARRAY_SIZE(sensor_ev_zero_regs),
	},
	{
		.regs = sensor_ev_pos1_regs,
		.size = ARRAY_SIZE(sensor_ev_pos1_regs),
	},
	{
		.regs = sensor_ev_pos2_regs,
		.size = ARRAY_SIZE(sensor_ev_pos2_regs),
	},
	{
		.regs = sensor_ev_pos3_regs,
		.size = ARRAY_SIZE(sensor_ev_pos3_regs),
	},
	{
		.regs = sensor_ev_pos4_regs,
		.size = ARRAY_SIZE(sensor_ev_pos4_regs),
	},
};

static int sensor_g_hflip(struct v4l2_subdev *sd, __s32 *value)
{
	return 0;
}

static int sensor_s_hflip(struct v4l2_subdev *sd, int value)
{
	return 0;
}

static int sensor_g_vflip(struct v4l2_subdev *sd, __s32 *value)
{
	return 0;
}

static int sensor_s_vflip(struct v4l2_subdev *sd, int value)
{
	return 0;
}

static int sensor_g_autogain(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_autogain(struct v4l2_subdev *sd, int value)
{
	return -EINVAL;
}

static int sensor_g_autoexp(struct v4l2_subdev *sd, __s32 *value)
{
	int ret;
	struct sensor_info *info = to_state(sd);
	data_type val;

	ret = sensor_write(sd, 0xfe, 0x00);
	if (ret < 0) {
		sensor_err("sensor_write err at sensor_g_autoexp!\n");
		return ret;
	}

	ret = sensor_read(sd, 0xd2, &val);
	if (ret < 0) {
		sensor_err("sensor_read err at sensor_g_autoexp!\n");
		return ret;
	}

	val &= 0x80;
	if (val == 0x80)
		*value = V4L2_EXPOSURE_AUTO;
	else
		*value = V4L2_EXPOSURE_MANUAL;

	info->autoexp = *value;
	return 0;
}

static int sensor_s_autoexp(struct v4l2_subdev *sd,
				enum v4l2_exposure_auto_type value)
{
	int ret;
	struct sensor_info *info = to_state(sd);
	data_type val;

	ret = sensor_write(sd, 0xfe, 0x00);
	if (ret < 0) {
		sensor_err("sensor_write err at sensor_s_autoexp!\n");
		return ret;
	}

	ret = sensor_read(sd, 0xd2, &val);
	if (ret < 0) {
		sensor_err("sensor_read err at sensor_s_autoexp!\n");
		return ret;
	}

	switch (value) {
	case V4L2_EXPOSURE_AUTO:
		val |= 0x80;
		break;
	case V4L2_EXPOSURE_MANUAL:
		val &= 0x7f;
		break;
	case V4L2_EXPOSURE_SHUTTER_PRIORITY:
		return -EINVAL;
	case V4L2_EXPOSURE_APERTURE_PRIORITY:
		return -EINVAL;
	default:
		return -EINVAL;
	}

	/* ret = sensor_write(sd, 0xd2, val); */
	if (ret < 0) {
		sensor_err("sensor_write err at sensor_s_autoexp!\n");
		return ret;
	}

	usleep_range(10000, 12000);

	info->autoexp = value;
	return 0;
}

static int sensor_g_autowb(struct v4l2_subdev *sd, int *value)
{
	int ret;
	struct sensor_info *info = to_state(sd);
	data_type val;

	ret = sensor_write(sd, 0xfe, 0x00);
	if (ret < 0) {
		sensor_err("sensor_write err at sensor_g_autowb!\n");
		return ret;
	}

	ret = sensor_read(sd, 0x42, &val);
	if (ret < 0) {
		sensor_err("sensor_read err at sensor_g_autowb!\n");
		return ret;
	}

	val &= (1<<1);
	val = val>>1; /* 0x22 bit1 is awb enable */

	*value = val;
	info->autowb = *value;
	return 0;
}

static int sensor_s_autowb(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct sensor_info *info = to_state(sd);
	data_type val;

	ret = sensor_write_array(sd, sensor_wb_auto_regs,
				ARRAY_SIZE(sensor_wb_auto_regs));
	if (ret < 0) {
		sensor_err("sensor_write_array err at sensor_s_autowb!\n");
		return ret;
	}

	ret = sensor_write(sd, 0xfe, 0x00);
	if (ret < 0) {
		sensor_err("sensor_write err at sensor_s_autowb!\n");
		return ret;
	}
	ret = sensor_read(sd, 0x42, &val);
	if (ret < 0) {
		sensor_err("sensor_read err at sensor_s_autowb!\n");
		return ret;
	}

	switch (value) {
	case 0:
		val &= 0xfd;
		break;
	case 1:
		val |= 0x02;
		break;
	default:
		break;
	}
	ret = sensor_write(sd, 0x42, val);
	if (ret < 0) {
		sensor_err("sensor_write err at sensor_s_autowb!\n");
		return ret;
	}

	usleep_range(10000, 12000);

	info->autowb = value;
	return 0;
}

static int sensor_g_hue(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_hue(struct v4l2_subdev *sd, int value)
{
	return -EINVAL;
}

static int sensor_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_gain(struct v4l2_subdev *sd, int value)
{
	return -EINVAL;
}
/* ******************************end of **************************** */

static int sensor_g_brightness(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->brightness;
	return 0;
}

static int sensor_s_brightness(struct v4l2_subdev *sd, int value)
{
	struct sensor_info *info = to_state(sd);

	if (info->brightness == value)
		return 0;

	if (value < 0 || value > 8)
		return -ERANGE;

	sensor_write_array(sd, sensor_brightness[value].regs,
				sensor_brightness[value].size);

	info->brightness = value;
	return 0;
}

static int sensor_g_contrast(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->contrast;
	return 0;
}

static int sensor_s_contrast(struct v4l2_subdev *sd, int value)
{
	struct sensor_info *info = to_state(sd);

	if (info->contrast == value)
		return 0;

	if (value < 0 || value > 8)
		return -ERANGE;

	sensor_write_array(sd, sensor_contrast[value].regs,
				sensor_contrast[value].size);

	info->contrast = value;
	return 0;
}

static int sensor_g_saturation(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->saturation;
	return 0;
}

static int sensor_s_saturation(struct v4l2_subdev *sd, int value)
{
	struct sensor_info *info = to_state(sd);

	if (info->saturation == value)
		return 0;

	if (value < 0 || value > 8)
		return -ERANGE;

	sensor_write_array(sd, sensor_saturation[value].regs,
				sensor_saturation[value].size);

	info->saturation = value;
	return 0;
}

static int sensor_g_exp_bias(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->exp_bias;
	return 0;
}

static int sensor_s_exp_bias(struct v4l2_subdev *sd, int value)
{
	struct sensor_info *info = to_state(sd);

	if (info->exp_bias == value)
		return 0;

	if (value < 0 || value > 8)
		return -ERANGE;

	sensor_write_array(sd, sensor_ev[value].regs,
				sensor_ev[value].size);

	info->exp_bias = value;
	return 0;
}

static int sensor_g_wb(struct v4l2_subdev *sd, int *value)
{
	struct sensor_info *info = to_state(sd);
	enum v4l2_auto_n_preset_white_balance *wb_type =
				(enum v4l2_auto_n_preset_white_balance *)value;

	*wb_type = info->wb;

	return 0;
}

static int sensor_s_wb(struct v4l2_subdev *sd,
				enum v4l2_auto_n_preset_white_balance value)
{
	struct sensor_info *info = to_state(sd);

	if (info->capture_mode == V4L2_MODE_IMAGE)
		return 0;

	sensor_write_array(sd, sensor_wb[value].regs,
				sensor_wb[value].size);

	if (value == V4L2_WHITE_BALANCE_AUTO)
		info->autowb = 1;
	else
		info->autowb = 0;

	info->wb = value;
	return 0;
}

static int sensor_g_colorfx(struct v4l2_subdev *sd,
				__s32 *value)
{
	struct sensor_info *info = to_state(sd);
	enum v4l2_colorfx *clrfx_type = (enum v4l2_colorfx *)value;

	*clrfx_type = info->clrfx;
	return 0;
}

static int sensor_s_colorfx(struct v4l2_subdev *sd,
				enum v4l2_colorfx value)
{
	struct sensor_info *info = to_state(sd);

	if (info->clrfx == value)
		return 0;

	sensor_write_array(sd, sensor_colorfx[value].regs,
				sensor_colorfx[value].size);

	info->clrfx = value;
	return 0;
}

static int sensor_g_flash_mode(struct v4l2_subdev *sd,
				__s32 *value)
{
	struct sensor_info *info = to_state(sd);
	enum v4l2_flash_led_mode *flash_mode =
				(enum v4l2_flash_led_mode *)value;

	*flash_mode = info->flash_mode;
	return 0;
}

static int sensor_s_flash_mode(struct v4l2_subdev *sd,
				enum v4l2_flash_led_mode value)
{
	struct sensor_info *info = to_state(sd);

	info->flash_mode = value;
	return 0;
}

/*
 * Stuff that knows about the sensor.
 */
static int sensor_power(struct v4l2_subdev *sd, int on)
{
	switch (on) {
	case STBY_ON:
		sensor_print("STBY_ON!\n");
		cci_lock(sd);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(300, 310);
		vin_set_mclk(sd, OFF);
		cci_unlock(sd);
		break;
	case STBY_OFF:
		sensor_print("STBY_OFF!\n");
		cci_lock(sd);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(300, 310);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		usleep_range(100, 120);
		cci_unlock(sd);
		break;
	case PWR_ON:
		sensor_print("PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		usleep_range(100, 120);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_HIGH);
		vin_set_pmu_channel(sd, IOVDD, ON);
		vin_set_pmu_channel(sd, AVDD, ON);
		usleep_range(200, 220);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		usleep_range(100, 120);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(100, 120);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(300, 310);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		usleep_range(300, 310);
		cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_print("PWR_OFF!\n");
		cci_lock(sd);
		usleep_range(100, 120);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(100, 120);
		vin_set_mclk(sd, OFF);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_LOW);
		vin_set_pmu_channel(sd, AVDD, OFF);
		vin_set_pmu_channel(sd, IOVDD, OFF);
		usleep_range(100, 120);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(100, 120);
		vin_gpio_set_status(sd, RESET, 0);
		vin_gpio_set_status(sd, PWDN, 0);
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
	unsigned int SENSOR_ID = 0;
	data_type val;
	int cnt = 0;

	sensor_read(sd, 0xf0, &val);
	SENSOR_ID |= (val << 8);
	sensor_read(sd, 0xf1, &val);
	SENSOR_ID |= (val);
	sensor_print("V4L2_IDENT_SENSOR = 0x%x\n", SENSOR_ID);

	while ((SENSOR_ID != V4L2_IDENT_SENSOR) && (cnt < 5)) {
		sensor_read(sd, 0xf0, &val);
		SENSOR_ID |= (val << 8);
		sensor_read(sd, 0xf1, &val);
		SENSOR_ID |= (val);
		sensor_print("retry = %d, V4L2_IDENT_SENSOR = %x\n", cnt, SENSOR_ID);
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

	sensor_print("sensor_init\n");

	/*Make sure it is a target sensor */
	ret = sensor_detect(sd);
	if (ret) {
		sensor_err("chip found is not an target chip.\n");
		return ret;
	}

	info->focus_status = 0;
	info->low_speed = 0;
	info->width = 640;
	info->height = 480;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 30; /* 30fps */

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
		.desc = "YUYV 4:2:2",
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.regs = sensor_fmt_yuv422_yuyv,
		.regs_size = ARRAY_SIZE(sensor_fmt_yuv422_yuyv),
		.bpp = 2
	},
};
#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct sensor_win_size sensor_win_sizes[] = {
	{
	.width = 640,
	.height = 480,
	.hoffset = 0,
	.voffset = 0,
	.regs = sensor_480p20_regs,
	.regs_size = ARRAY_SIZE(sensor_480p20_regs),
	.set_size = NULL,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->flags = 0 | V4L2_MBUS_CSI2_1_LANE | V4L2_MBUS_CSI2_CHANNEL_0;

	return 0;
}

static int sensor_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sensor_info *info =
			container_of(ctrl->handler, struct sensor_info, handler);
	struct v4l2_subdev *sd = &info->sd;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return sensor_g_brightness(sd, &ctrl->val);
	case V4L2_CID_CONTRAST:
		return sensor_g_contrast(sd, &ctrl->val);
	case V4L2_CID_SATURATION:
		return sensor_g_saturation(sd, &ctrl->val);
	case V4L2_CID_HUE:
		return sensor_g_hue(sd, &ctrl->val);
	case V4L2_CID_VFLIP:
		return sensor_g_vflip(sd, &ctrl->val);
	case V4L2_CID_HFLIP:
		return sensor_g_hflip(sd, &ctrl->val);
	case V4L2_CID_GAIN:
		return sensor_g_gain(sd, &ctrl->val);
	case V4L2_CID_AUTOGAIN:
		return sensor_g_autogain(sd, &ctrl->val);
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_AUTO_EXPOSURE_BIAS:
		return sensor_g_exp_bias(sd, &ctrl->val);
	case V4L2_CID_EXPOSURE_AUTO:
		return sensor_g_autoexp(sd, &ctrl->val);
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		return sensor_g_wb(sd, &ctrl->val);
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return sensor_g_autowb(sd, &ctrl->val);
	case V4L2_CID_COLORFX:
		return sensor_g_colorfx(sd, &ctrl->val);
	case V4L2_CID_FLASH_LED_MODE:
		return sensor_g_flash_mode(sd, &ctrl->val);
	}
	return -EINVAL;
}

static int sensor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sensor_info *info =
			container_of(ctrl->handler, struct sensor_info, handler);
	struct v4l2_subdev *sd = &info->sd;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return sensor_s_brightness(sd, ctrl->val);
	case V4L2_CID_CONTRAST:
		return sensor_s_contrast(sd, ctrl->val);
	case V4L2_CID_SATURATION:
		return sensor_s_saturation(sd, ctrl->val);
	case V4L2_CID_HUE:
		return sensor_s_hue(sd, ctrl->val);
	case V4L2_CID_VFLIP:
		return sensor_s_vflip(sd, ctrl->val);
	case V4L2_CID_HFLIP:
		return sensor_s_hflip(sd, ctrl->val);
	case V4L2_CID_GAIN:
		return sensor_s_gain(sd, ctrl->val);
	case V4L2_CID_AUTOGAIN:
		return sensor_s_autogain(sd, ctrl->val);
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_AUTO_EXPOSURE_BIAS:
		return sensor_s_exp_bias(sd, ctrl->val);
	case V4L2_CID_EXPOSURE_AUTO:
		return sensor_s_autoexp(sd,
			(enum v4l2_exposure_auto_type) ctrl->val);
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		return sensor_s_wb(sd,
			(enum v4l2_auto_n_preset_white_balance) ctrl->val);
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return sensor_s_autowb(sd, ctrl->val);
	case V4L2_CID_COLORFX:
		return sensor_s_colorfx(sd, (enum v4l2_colorfx) ctrl->val);
	case V4L2_CID_FLASH_LED_MODE:
		return sensor_s_flash_mode(sd,
			(enum v4l2_flash_led_mode) ctrl->val);
	}
	return -EINVAL;
}

static int sensor_reg_init(struct sensor_info *info)
{
	int ret;
	struct v4l2_subdev *sd = &info->sd;
	struct sensor_format_struct *sensor_fmt = info->fmt;
	struct sensor_win_size *wsize = info->current_wins;

	ret = sensor_write_array(sd, sensor_default_regs,
				ARRAY_SIZE(sensor_default_regs));
	if (ret < 0) {
		sensor_err("write sensor_default_regs error\n");
		return ret;
	}

	sensor_print("sensor_reg_init\n");

	sensor_write_array(sd, sensor_fmt->regs, sensor_fmt->regs_size);

	if (wsize->regs)
		sensor_write_array(sd, wsize->regs, wsize->regs_size);

	if (wsize->set_size)
		wsize->set_size(sd);

	info->width = wsize->width;
	info->height = wsize->height;

	sensor_print("s_fmt set width = %d, height = %d\n", wsize->width,
				wsize->height);

	return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sensor_info *info = to_state(sd);

	sensor_print("%s on = %d, %d*%d fps: %d code: %x\n", __func__, enable,
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
static struct cci_driver cci_drv = {
		.name = SENSOR_NAME,
		.addr_width = CCI_BITS_8,
		.data_width = CCI_BITS_8,
};

static const s64 sensor_exp_bias_qmenu[] = {
	-4, -3, -2, -1, 0, 1, 2, 3, 4,
};

static int sensor_init_controls(struct v4l2_subdev *sd, const struct v4l2_ctrl_ops *ops)
{
	struct sensor_info *info = to_state(sd);
	struct v4l2_ctrl_handler *handler = &info->handler;
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	v4l2_ctrl_handler_init(handler, 15);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_CONTRAST, 0, 128, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_SATURATION, -4, 4, 1, 1);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_HUE,  -180, 180, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 1*16, 64*16-1, 1, 1*16);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_EXPOSURE, 0, 65536*16, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_ctrl_new_int_menu(handler, ops, V4L2_CID_AUTO_EXPOSURE_BIAS,
				ARRAY_SIZE(sensor_exp_bias_qmenu) - 1,
				ARRAY_SIZE(sensor_exp_bias_qmenu) / 2, sensor_exp_bias_qmenu);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_EXPOSURE_AUTO,
				V4L2_EXPOSURE_APERTURE_PRIORITY, 0, V4L2_EXPOSURE_AUTO);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
				V4L2_WHITE_BALANCE_SHADE, 0, V4L2_WHITE_BALANCE_AUTO);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_COLORFX,
				V4L2_COLORFX_SET_CBCR, 0, V4L2_COLORFX_NONE);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_FLASH_LED_MODE,
		V4L2_FLASH_LED_MODE_RED_EYE, 0, V4L2_FLASH_LED_MODE_NONE);

	if (handler->error) {
		ret = handler->error;
		v4l2_ctrl_handler_free(handler);
	}

	sd->ctrl_handler = handler;

	return ret;
}

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct sensor_info *info;

	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv);
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
	info->stream_seq = MIPI_BEFORE_SENSOR;
	info->af_first_flag = 1;
	info->exp = 0;
	info->gain = 0;

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd;

	sd = cci_dev_remove_helper(client, &cci_drv);

	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
		.driver = {
				.owner = THIS_MODULE,
				.name = SENSOR_NAME,
				},
		.probe = sensor_probe,
		.remove = sensor_remove,
		.id_table = sensor_id,
};
static __init int init_sensor(void)
{
	int ret = 0;

	ret = cci_dev_init_helper(&sensor_driver);

	return ret;
}

static __exit void exit_sensor(void)
{
	cci_dev_exit_helper(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);
