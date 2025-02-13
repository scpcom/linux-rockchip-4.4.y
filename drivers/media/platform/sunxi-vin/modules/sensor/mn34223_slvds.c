/*
 * A V4L2 driver for mn34223 Raw cameras.
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

MODULE_AUTHOR("lwj");
MODULE_DESCRIPTION("A low-level driver for MN34223 sensors");
MODULE_LICENSE("GPL");

#define MCLK (27*1000*1000)
#define V4L2_IDENT_SENSOR 0x0223

#define WDR_RATIO 16

/*
 * Our nominal (default) frame rate.
 */

#define SENSOR_FRAME_RATE 30

/*
 * The IMX317 i2c address
 */
#define I2C_ADDR 0x20

#define SENSOR_NAME "mn34223_slvds"

/*
 * The default register settings
 */

static struct regval_list sensor_default_regs[] = {

};

static struct regval_list sensor_1080p30_wdr_regs[] = {
	{0x300E, 0x0001},
	{0x300F, 0x0000},
	{0x0305, 0x0002},
	{0x0307, 0x0030},
	{0x3000, 0x0000},
	{0x3001, 0x0003},
	{0x0112, 0x000C},
	{0x0113, 0x000C},
	{0x3004, 0x0003},
	{0x3005, 0x0067},
	{0x3007, 0x0010},
	{0x3008, 0x0091},
	{0x300B, 0x0000},
	{0x3018, 0x0043},
	{0x3019, 0x0010},
	{0x301A, 0x00B9},
	{0x3000, 0x0000},
	{0x3001, 0x0053},
	{0x300E, 0x0000},
	{0x300F, 0x0000},
	{0x0202, 0x0000},
	{0x0203, 0x0020}, /*long exposure time set*/
	{0x0340, 0x0004},
	{0x0341, 0x00E2},
	{0x0342, 0x0014},
	{0x0343, 0x00A0},
	{0x0346, 0x0000},
	{0x0347, 0x003C},
	{0x034A, 0x0004},
	{0x034B, 0x007F},
	{0x034E, 0x0004},
	{0x034F, 0x0044},
	{0x3036, 0x0000},
	{0x3039, 0x002E},
	{0x3041, 0x002C},
	{0x3058, 0x000F},
	{0x306E, 0x000C},
	{0x306F, 0x0000},
	{0x3074, 0x0001},
	{0x3098, 0x0000},
	{0x3099, 0x0000},
	{0x309A, 0x0001},
	{0x3101, 0x0001},
	{0x3104, 0x0004},
	{0x3106, 0x0000},
	{0x3107, 0x00C0},
	{0x312B, 0x0020}, /*short1 exposure time set*/
	{0x312D, 0x0020}, /*short2 exposure time set*/
	{0x312F, 0x0020}, /*short3 exposure time set*/
	{0x3141, 0x0040},
	{0x3143, 0x0003},
	{0x3144, 0x0004},
	{0x3145, 0x0003},
	{0x3146, 0x0005},
	{0x3147, 0x0005},
	{0x3148, 0x0002},
	{0x3149, 0x0002},
	{0x314A, 0x0005},
	{0x314B, 0x0003},
	{0x314C, 0x0006},
	{0x314D, 0x0007},
	{0x314E, 0x0006},
	{0x314F, 0x0006},
	{0x3150, 0x0007},
	{0x3152, 0x0006},
	{0x3153, 0x00E3},
	{0x3155, 0x00CA},
	{0x3157, 0x00CA},
	{0x3159, 0x00CA},
	{0x315B, 0x00CA},
	{0x315D, 0x00CA},
	{0x315F, 0x00CA},
	{0x3161, 0x00CA},
	{0x3163, 0x00CA},
	{0x3165, 0x00CA},
	{0x3167, 0x00CA},
	{0x3169, 0x00CA},
	{0x316B, 0x00CA},
	{0x316D, 0x00CA},
	{0x316F, 0x00C6},
	{0x3171, 0x00CA},
	{0x3173, 0x00CA},
	{0x3175, 0x0080},
	{0x318E, 0x0020},
	{0x318F, 0x0070},
	{0x3196, 0x0008},
	{0x31FC, 0x0002},
	{0x31FE, 0x0007},
	{0x323C, 0x0070},
	{0x323E, 0x0000},
	{0x3243, 0x00D1},
	{0x3246, 0x0001},
	{0x3247, 0x00D6},
	{0x3248, 0x0000},
	{0x3249, 0x0000},
	{0x324A, 0x0030},
	{0x324B, 0x0018},
	{0x324C, 0x0002},
	{0x3253, 0x00D4},
	{0x3256, 0x0011},
	{0x3258, 0x0001},
	{0x3259, 0x00E6},
	{0x325A, 0x0039},
	{0x3272, 0x0055},
	{0x3280, 0x0030},
	{0x3282, 0x000E},
	{0x3285, 0x001B},
	{0x3288, 0x0001},
	{0x3289, 0x0000},
	{0x330E, 0x0005},
	{0x3310, 0x0002},
	{0x3315, 0x001F},
	{0x331A, 0x0002},
	{0x331B, 0x0002},
	{0x332C, 0x0002},
	{0x3339, 0x0002},
	{0x336B, 0x0003},
	{0x339F, 0x0003},
	{0x33A2, 0x0003},
	{0x33A3, 0x0003},
	{0x3000, 0x0000},
	{0x3001, 0x00D3},
	{0x0100, 0x0001},
	{0x0101, 0x0000},
};

static struct regval_list sensor_1080p60_regs[] = {
	{0x300E, 0x01},
	{0x300F, 0x00},
	{0x0305, 0x01},
	{0x0307, 0x21},
	{0x3000, 0x00},
	{0x3001, 0x03},
	{0x0112, 0x0C},
	{0x0113, 0x0C},
	{0x3004, 0x03},
	{0x3005, 0x64},
	{0x3007, 0x10},
	{0x3008, 0x91},
	{0x300B, 0x00},
	{0x3018, 0x43},
	{0x3019, 0x10},
	{0x301A, 0xB9},
	{0x3000, 0x00},
	{0x3001, 0x53},
	{0x300E, 0x00},
	{0x300F, 0x00},
	{0x0202, 0x04},
	{0x0203, 0x63}, /*expousre*/

	{0x0204, 0x02}, /*gain*/
	{0x0205, 0x00},

	{0x0340, 0x04},
	{0x0341, 0x65},
	{0x0342, 0x08},
	{0x0343, 0x98},
	{0x0346, 0x00},
	{0x0347, 0x3C},
	{0x034A, 0x04},
	{0x034B, 0x7F},
	{0x034E, 0x04},
	{0x034F, 0x44},
	{0x3036, 0x00},
	{0x3039, 0x2E},
	{0x3041, 0x2C},
	{0x3058, 0x0F},

	{0x306E, 0x0C},
	{0x306F, 0x00},
	{0x3074, 0x01},
	{0x3098, 0x00},
	{0x3099, 0x00},
	{0x309A, 0x01},
	{0x3101, 0x00},
	{0x3104, 0x04},
	{0x3106, 0x00},
	{0x3107, 0xC0},
	{0x312B, 0x00},
	{0x312D, 0x00},
	{0x312F, 0x00},
	{0x3141, 0x40},
	{0x3143, 0x02},
	{0x3144, 0x02},
	{0x3145, 0x02},
	{0x3146, 0x00},
	{0x3147, 0x02},
	{0x3148, 0x02},
	{0x3149, 0x02},
	{0x314A, 0x01},
	{0x314B, 0x02},
	{0x314C, 0x02},
	{0x314D, 0x02},
	{0x314E, 0x01},
	{0x314F, 0x02},
	{0x3150, 0x02},
	{0x3152, 0x04},
	{0x3153, 0xE3},
	{0x3155, 0xCA},
	{0x3157, 0xCA},
	{0x3159, 0xCA},
	{0x315B, 0xCA},
	{0x315D, 0xCA},
	{0x315F, 0xCA},
	{0x3161, 0xCA},
	{0x3163, 0xCA},
	{0x3165, 0xCA},
	{0x3167, 0xCA},
	{0x3169, 0xCA},
	{0x316B, 0xCA},
	{0x316D, 0xCA},
	{0x316F, 0xC6},
	{0x3171, 0xCA},
	{0x3173, 0xCA},
	{0x3175, 0x80},
	{0x318E, 0x20},
	{0x318F, 0x70},
	{0x3196, 0x08},
	{0x31FC, 0x02},
	{0x31FE, 0x07},
	{0x323C, 0x71},
	{0x323E, 0x01},
	{0x3243, 0xD7},
	{0x3246, 0x01},
	{0x3247, 0x79},
	{0x3248, 0x00},
	{0x3249, 0x00},
	{0x324A, 0x30},
	{0x324B, 0x18},
	{0x324C, 0x02},
	{0x3253, 0xDE},
	{0x3256, 0x11},
	{0x3258, 0x01},
	{0x3259, 0x49},
	{0x325A, 0x39},
	{0x3272, 0x46},
	{0x3280, 0x30},
	{0x3282, 0x0E},
	{0x3285, 0x1B},
	{0x3288, 0x01},
	{0x3289, 0x00},
	{0x330E, 0x05},
	{0x3310, 0x02},
	{0x3315, 0x1F},
	{0x331A, 0x02},
	{0x331B, 0x02},
	{0x332C, 0x02},
	{0x3339, 0x02},
	{0x336B, 0x03},
	{0x339F, 0x03},
	{0x33A2, 0x03},
	{0x33A3, 0x03},
	{0x3000, 0x00},
	{0x3001, 0xD3},
	{0x0100, 0x01},
	{0x0101, 0x00},
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

static int mn34223_sensor_vts;
static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	data_type explow, expmid;
	unsigned int exp_val_short = 0;
	struct sensor_info *info = to_state(sd);

	if (exp_val > 0x1fffff)
		exp_val = 0x1fffff;

	expmid = (unsigned char)((0x0ff000 & exp_val) >> 12);
	explow = (unsigned char)((0x000ff0 & exp_val) >> 4);

	sensor_write(sd, 0x0202, expmid);
	sensor_write(sd, 0x0203, explow);
	sensor_write(sd, 0x0221, 0);

	sensor_dbg("set exp_line: %d 0x%x 0x%x\n", exp_val, expmid, explow);

	if (info->isp_wdr_mode == ISP_DOL_WDR_MODE) {
		exp_val_short = exp_val / WDR_RATIO;
		expmid = (unsigned char)((0x0ff000 & exp_val_short) >> 12);
		explow = (unsigned char)((0x000ff0 & exp_val_short) >> 4);
		sensor_write(sd, 0x312A, expmid);
		sensor_write(sd, 0x312B, explow);
		sensor_dbg("set exp_line_short: %d 0x%x 0x%x\n",
			exp_val_short, expmid, explow);
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

unsigned short gain2db[497] = {
	0, 6, 11, 16, 21, 25, 30, 34, 38, 41, 45, 48, 52, 55, 58, 61, 64,
	67, 70, 73, 75, 78, 80, 83, 85, 87, 89, 92, 94, 96, 98, 100, 102, 104,
	    106, 107, 109, 111, 113,
	114, 116, 118, 119, 121, 122, 124, 125, 127, 128, 130, 131, 133, 134,
	    135, 137, 138, 139, 141,
	142, 143, 144, 146, 147, 148, 149, 150, 151, 153, 154, 155, 156, 157,
	    158, 159, 160, 161, 162,
	163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 173, 174, 175,
	    176, 177, 178, 179, 179,
	180, 181, 182, 183, 184, 184, 185, 186, 187, 187, 188, 189, 190, 190,
	    191, 192, 193, 193, 194,
	195, 196, 196, 197, 198, 198, 199, 200, 200, 201, 202, 202, 203, 204,
	    204, 205, 205, 206, 207,
	207, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215,
	    216, 216, 217, 217, 218,
	218, 219, 219, 220, 221, 221, 222, 222, 223, 223, 224, 224, 225, 225,
	    226, 226, 227, 227, 228,
	228, 229, 229, 230, 230, 231, 231, 232, 232, 233, 233, 234, 234, 234,
	    235, 235, 236, 236, 237,
	237, 238, 238, 239, 239, 239, 240, 240, 241, 241, 242, 242, 242, 243,
	    243, 244, 244, 245, 245,
	245, 246, 246, 247, 247, 247, 248, 248, 249, 249, 249, 250, 250, 251,
	    251, 251, 252, 252, 252,
	253, 253, 254, 254, 254, 255, 255, 255, 256, 256, 257, 257, 257, 258,
	    258, 258, 259, 259, 259,
	260, 260, 260, 261, 261, 261, 262, 262, 262, 263, 263, 264, 264, 264,
	    265, 265, 265, 266, 266,
	266, 266, 267, 267, 267, 268, 268, 268, 269, 269, 269, 270, 270, 270,
	    271, 271, 271, 272, 272,
	272, 272, 273, 273, 273, 274, 274, 274, 275, 275, 275, 276, 276, 276,
	    276, 277, 277, 277, 278,
	278, 278, 278, 279, 279, 279, 280, 280, 280, 280, 281, 281, 281, 282,
	    282, 282, 282, 283, 283,
	283, 283, 284, 284, 284, 285, 285, 285, 285, 286, 286, 286, 286, 287,
	    287, 287, 287, 288, 288,
	288, 288, 289, 289, 289, 289, 290, 290, 290, 291, 291, 291, 291, 292,
	    292, 292, 292, 292, 293,
	293, 293, 293, 294, 294, 294, 294, 295, 295, 295, 295, 296, 296, 296,
	    296, 297, 297, 297, 297,
	298, 298, 298, 298, 298, 299, 299, 299, 299, 300, 300, 300, 300, 301,
	    301, 301, 301, 301, 302,
	302, 302, 302, 303, 303, 303, 303, 303, 304, 304, 304, 304, 304, 305,
	    305, 305, 305, 306, 306,
	306, 306, 306, 307, 307, 307, 307, 307, 308, 308, 308, 308, 309, 309,
	    309, 309, 309, 310, 310,
	310, 310, 310, 311, 311, 311, 311, 311, 312, 312, 312, 312, 312, 313,
	    313, 313, 313, 313, 314,
	314, 314, 314, 314, 315, 315, 315, 315, 315, 316, 316, 316, 316, 316,
	    316, 317, 317, 317, 317,
	317, 318, 318, 318, 318, 318, 319, 319, 319, 319, 319, 319, 320, 320,
	    320, 320, 320, 320, 320, 320, 320,
};
static int sensor_s_gain(struct v4l2_subdev *sd, int gain_val)
{
	struct sensor_info *info = to_state(sd);

	if (gain_val < 1 * 16)
		gain_val = 16;
	if (gain_val > 1000 * 16 - 1)
		gain_val = 1000 * 16 - 1;

	if (gain_val < 32 * 16) { /* again only */
		sensor_write(sd, 0x0204, 1 + gain2db[gain_val - 16] / 256);
		sensor_write(sd, 0x0205, (gain2db[gain_val - 16]) & 0xff);
	} else {  /* again and dgain */
		sensor_write(sd, 0x0204, 0x2);
		sensor_write(sd, 0x0205, 0x40);
		sensor_write(sd, 0x3108, 1 + gain2db[(gain_val>>5) - 16] / 256);
		sensor_write(sd, 0x3109, (gain2db[(gain_val>>5) - 16]) & 0xff);
	}

	sensor_dbg("set gain: %d\n", gain_val);

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

	sensor_s_exp(sd, exp_val);
	sensor_s_gain(sd, gain_val);

	sensor_dbg("sensor_set_gain exp = %d, %d Done!\n", gain_val, exp_val);

	info->exp = exp_val;
	info->gain = gain_val;
	return 0;
}

static void sensor_g_combo_sync_code(struct v4l2_subdev *sd,
				struct combo_sync_code *sync)
{
	int i;

	__u64 lvds_lane_sof[] = {0x00000020, 0x00000120, 0x00000060, 0x00000160,
				0x00000000, 0x00000000, 0x00000000, 0x00000000,
				0x00000000, 0x00000000, 0x00000000, 0x00000000};

	__u64 lvds_lane_eof[] = {0x00000030, 0x00000130, 0x00000070, 0x00000170,
				0x00000000, 0x00000000, 0x00000000, 0x00000000,
				0x00000000, 0x00000000, 0x00000000, 0x00000000};


	__u64 lvds_lane_sol[] = {0x00000000, 0x00000100, 0x00000040, 0x00000140,
				0x00000000, 0x00000000, 0x00000000, 0x00000000,
				0x00000000, 0x00000000, 0x00000000, 0x00000000};

	__u64 lvds_lane_eol[] = {0x00000010, 0x00000110, 0x00000050, 0x00000150,
				0x00000000, 0x00000000, 0x00000000, 0x00000000,
				0x00000000, 0x00000000, 0x00000000, 0x00000000};

	for (i = 0; i < 12; i++) {
		sync->lane_sof[i].low_bit = lvds_lane_sof[i];
		sync->lane_sof[i].high_bit = 0xFFFF0000;
		sync->lane_sol[i].low_bit = lvds_lane_sol[i];
		sync->lane_sol[i].high_bit = 0xFFFF0000;
		sync->lane_eol[i].low_bit = lvds_lane_eol[i];
		sync->lane_eol[i].high_bit = 0xFFFF0000;
		sync->lane_eof[i].low_bit = lvds_lane_eof[i];
		sync->lane_eof[i].high_bit = 0xFFFF0000;
	}
}

static void sensor_g_combo_lane_map(struct v4l2_subdev *sd,
				struct combo_lane_map *map)
{
	map->lvds_lane0 = LVDS_MAPPING_A_D2_TO_LANE0;
	map->lvds_lane1 = LVDS_MAPPING_A_D1_TO_LANE1;
	map->lvds_lane2 = LVDS_MAPPING_A_D0_TO_LANE2;
	map->lvds_lane3 = LVDS_MAPPING_A_D3_TO_LANE3;
	map->lvds_lane4 = LVDS_LANE4_NO_USE;
	map->lvds_lane5 = LVDS_LANE5_NO_USE;
	map->lvds_lane6 = LVDS_LANE6_NO_USE;
	map->lvds_lane7 = LVDS_LANE7_NO_USE;
	map->lvds_lane8 = LVDS_LANE8_NO_USE;
	map->lvds_lane9 = LVDS_LANE9_NO_USE;
	map->lvds_lane10 = LVDS_LANE10_NO_USE;
	map->lvds_lane11 = LVDS_LANE11_NO_USE;
}

static void sensor_g_combo_wdr_cfg(struct v4l2_subdev *sd,
				struct combo_wdr_cfg *wdr)
{
	wdr->line_code_mode = 0;

	wdr->line_cnt = 0;

	wdr->code_mask = 0xf000;
	wdr->wdr_fid_mode_sel = 1;
	wdr->wdr_fid_map_en = 0x3;
	wdr->wdr_fid0_map_sel = 0xd;
	wdr->wdr_fid1_map_sel = 0xc;
	wdr->wdr_fid2_map_sel = 0;
	wdr->wdr_fid3_map_sel = 0;

	wdr->wdr_en_multi_ch = 1;
	wdr->wdr_ch0_height = 0x454;
	wdr->wdr_ch1_height = 0x454;
	wdr->wdr_ch2_height = 0x454;
	wdr->wdr_ch3_height = 0x454;
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
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_set_pmu_channel(sd, DVDD, ON);
		vin_set_mclk(sd, ON);
		usleep_range(10000, 12000);
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		usleep_range(7000, 8000);
		vin_set_mclk_freq(sd, MCLK);
		usleep_range(10000, 12000);
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
		usleep_range(10000, 12000);
		break;
	case 1:
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		usleep_range(10000, 12000);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	data_type rdval = 0;
	sensor_read(sd, 0x0204, &rdval);
	sensor_dbg("%s read value is 0x%x\n", __func__, rdval);
	sensor_read(sd, 0x0205, &rdval);
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
	case GET_COMBO_SYNC_CODE:
		sensor_g_combo_sync_code(sd, (struct combo_sync_code *)arg);
		break;
	case GET_COMBO_LANE_MAP:
		sensor_g_combo_lane_map(sd, (struct combo_lane_map *)arg);
		break;
	case GET_COMBO_WDR_CFG:
		sensor_g_combo_wdr_cfg(sd, (struct combo_wdr_cfg *)arg);
		break;
	case SET_FPS:
		ret = 0;
		break;
	case VIDIOC_VIN_SENSOR_EXP_GAIN:
		ret = sensor_s_exp_gain(sd, (struct sensor_exp_gain *)arg);
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
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
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
	.width      = HD1080_WIDTH,
	.height     = HD1080_HEIGHT+10,
	.hoffset    = 0,
	.voffset    = 0,
	.hts        = 5280,
	.vts        = 1250,
	.pclk       = 198000000,
	.mipi_bps   = 594000000,
	.fps_fixed  = 30,
	.if_mode    = LVDS_4CODE_WDR_MODE,
	.wdr_mode   = ISP_DOL_WDR_MODE,
	.bin_factor = 1,
	.intg_min   = 1 << 4,
	.intg_max   = (1250 - 2) << 4,
	.gain_min   = 1 << 4,
	.gain_max   = 1000 << 4,
	.regs       = sensor_1080p30_wdr_regs,
	.regs_size  = ARRAY_SIZE(sensor_1080p30_wdr_regs),
	.set_size   = NULL,
	},
	{
	.width      = HD1080_WIDTH,
	.height     = HD1080_HEIGHT,
	.hoffset    = 0,
	.voffset    = 16,
	.hts        = 2200,
	.vts        = 1125,
	.pclk       = 148500000,
	.mipi_bps   = 445500000,
	.fps_fixed  = 60,
	.bin_factor = 1,
	.intg_min   = 1 << 4,
	.intg_max   = (1125 - 2) << 4,
	.gain_min   = 1 << 4,
	.gain_max   = 1000 << 4,
	.regs       = sensor_1080p60_regs,
	.regs_size  = ARRAY_SIZE(sensor_1080p60_regs),
	.set_size   = NULL,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_get_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	struct sensor_info *info = to_state(sd);

	cfg->type = V4L2_MBUS_SUBLVDS;
	if (info->isp_wdr_mode == ISP_DOL_WDR_MODE)
		cfg->flags = 0 | V4L2_MBUS_SUBLVDS_4_LANE | V4L2_MBUS_CSI2_CHANNEL_0 | V4L2_MBUS_CSI2_CHANNEL_1;
	else
		cfg->flags = 0 | V4L2_MBUS_SUBLVDS_4_LANE | V4L2_MBUS_CSI2_CHANNEL_0;

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
	mn34223_sensor_vts = wsize->vts;

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
static struct cci_driver cci_drv = {
	.name = SENSOR_NAME,
	.addr_width = CCI_BITS_16,
	.data_width = CCI_BITS_8,
};

static int sensor_init_controls(struct v4l2_subdev *sd, const struct v4l2_ctrl_ops *ops)
{
	struct sensor_info *info = to_state(sd);
	struct v4l2_ctrl_handler *handler = &info->handler;
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	v4l2_ctrl_handler_init(handler, 2);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 1 * 16,
				2816 * 16, 1, 1 * 16);
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
	info->combo_mode = CMB_TERMINAL_RES | LVDS_NORMAL_MODE;
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
	return cci_dev_init_helper(&sensor_driver);
}

static __exit void exit_sensor(void)
{
	cci_dev_exit_helper(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);
