/*
 * ac107.c  --	ac107 ALSA Soc Audio driver
 *
 * Version: 1.0
 *
 * Author: panjunwen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/of.h>
#include <sound/tlv.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/sunxi-gpio.h>
#include <linux/gpio.h>
#include "ac107.h"

#define AC107_DEBUG_EN			1

#if AC107_DEBUG_EN
#define AC107_DEBUG(...)		printk(__VA_ARGS__)
#else
#define AC107_DEBUG(...)
#endif

#define AC107_ADC_PATTERN_SEL	ADC_PTN_NORMAL	/* 0:ADC normal,  1:0x5A5A5A,  2:0x123456,  3:0x000000,  4~7:I2S_RX_DATA,  other:reserved */

/* AC107 config */
#define AC107_CHIP_NUMS			1	/* range[1, 8] */
#define AC107_CHIP_NUMS_MAX		8	/* range[1, 8] */
#define AC107_SLOT_WIDTH		32	/* 8/12/16/20/24/28/32bit Slot Width */
#define AC107_ENCODING_EN		0	/* TX Encoding mode enable */
#define AC107_ENCODING_CH_NUMS	2	/* TX Encoding channel numbers, must be dual, range[1, 16] */
#define AC107_ENCODING_FMT		0	/* TX Encoding format:	0:first channel number 0,  other:first channel number 1 */
/*range[1, 1024], default PCM mode, I2S/LJ/RJ mode shall divide by 2 */
//#define AC107_LRCK_PERIOD		(AC107_SLOT_WIDTH*(AC107_ENCODING_EN ? 2 : AC107_CHIP_NUMS*2))
#define AC107_LRCK_PERIOD		(AC107_SLOT_WIDTH*(AC107_ENCODING_EN ? 2 : AC107_CHIP_NUMS))
#define AC107_MATCH_DTS_EN		1	/* AC107 match method select: 0: i2c_detect, 1:devices tree */

#define AC107_KCONTROL_EN		1
#define AC107_DAPM_EN			0
#define AC107_CODEC_RW_USER_EN	1
#define AC107_PGA_GAIN			ADC_PGA_GAIN_28dB	//-6dB and 0dB, 3~30dB, 1dB step
#define AC107_DMIC_EN			0	//0:ADC  1:DMIC
#define AC107_PDM_EN			0	//0:I2S  1:PDM

#define AC107_DVCC_NAME			"ac107_dvcc_1v8"
#define AC107_AVCC_VCCIO_NAME	"ac107_avcc_vccio_3v3"
#define AC107_RATES			(SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_KNOT)
#define AC107_FORMATS			(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static int ac107_regulator_en;
struct i2c_client *i2c_ctrl[AC107_CHIP_NUMS_MAX];

struct ac107_voltage_supply {
	struct regulator *dvcc_1v8;
	struct regulator *avcc_vccio_3v3;
};

struct ac107_priv {
	struct i2c_client *i2c;
	struct snd_soc_component *component;
	struct ac107_voltage_supply vol_supply;
	int reset_gpio;
};

static const struct regmap_config ac107_regmap_config = {
	.reg_bits = 8,		//Number of bits in a register address
	.val_bits = 8,		//Number of bits in a register value
};

struct real_val_to_reg_val {
	unsigned int real_val;
	unsigned int reg_val;
};

struct reg_default_value {
	u8 reg_addr;
	u8 default_val;
};

struct pll_div {
	u32 freq_in;
	u32 freq_out;
	u32 m1;
	u32 m2;
	u32 n;
	u32 k1;
	u32 k2;
};

static const struct real_val_to_reg_val ac107_sample_rate[] = {
	{8000, 0},
	{11025, 1},
	{12000, 2},
	{16000, 3},
	{22050, 4},
	{24000, 5},
	{32000, 6},
	{44100, 7},
	{48000, 8},
};

static const struct real_val_to_reg_val ac107_bclk_div[] = {
	{0, 0},
	{1, 1},
	{2, 2},
	{4, 3},
	{6, 4},
	{8, 5},
	{12, 6},
	{16, 7},
	{24, 8},
	{32, 9},
	{48, 10},
	{64, 11},
	{96, 12},
	{128, 13},
	{176, 14},
	{192, 15},
};

//FOUT =(FIN * N) / [(M1+1) * (M2+1)*(K1+1)*(K2+1)] ;	M1[0,31],  M2[0,1],  N[0,1023],  K1[0,31],  K2[0,1]
static const struct pll_div ac107_pll_div[] = {
	{400000, 12288000, 0, 0, 983, 15, 1},	//<out: 12.2875M>
	{512000, 12288000, 0, 0, 960, 19, 1},	//24576000/48
	{768000, 12288000, 0, 0, 640, 19, 1},	//24576000/32
	{800000, 12288000, 0, 0, 768, 24, 1},
	{1024000, 12288000, 0, 0, 480, 19, 1},	//24576000/24
	{1600000, 12288000, 0, 0, 384, 24, 1},
	{2048000, 12288000, 0, 0, 240, 19, 1},	//24576000/12
	{3072000, 12288000, 0, 0, 160, 19, 1},	//24576000/8
	{4096000, 12288000, 0, 0, 120, 19, 1},	//24576000/6
	{6000000, 12288000, 4, 0, 512, 24, 1},
	{6144000, 12288000, 1, 0, 160, 19, 1},	//24576000/4
	{12000000, 12288000, 9, 0, 512, 24, 1},
	{13000000, 12288000, 12, 0, 639, 25, 1},	//<out: 12.2885M>
	{15360000, 12288000, 9, 0, 320, 19, 1},
	{16000000, 12288000, 9, 0, 384, 24, 1},
	{19200000, 12288000, 11, 0, 384, 24, 1},
	{19680000, 12288000, 15, 1, 999, 24, 1},	//<out: 12.2877M>
	{24000000, 12288000, 9, 0, 256, 24, 1},

	{400000, 11289600, 0, 0, 1016, 17, 1},	//<out: 11.2889M>
	{512000, 11289600, 0, 0, 882, 19, 1},
	{768000, 11289600, 0, 0, 588, 19, 1},
	{800000, 11289600, 0, 0, 508, 17, 1},	//<out: 11.2889M>
	{1024000, 11289600, 0, 0, 441, 19, 1},
	{1600000, 11289600, 0, 0, 254, 17, 1},	//<out: 11.2889M>
	{2048000, 11289600, 1, 0, 441, 19, 1},
	{3072000, 11289600, 0, 0, 147, 19, 1},
	{4096000, 11289600, 3, 0, 441, 19, 1},
	{6000000, 11289600, 1, 0, 143, 18, 1},	//<out: 11.2895M>
	{6144000, 11289600, 1, 0, 147, 19, 1},
	{12000000, 11289600, 3, 0, 143, 18, 1},	//<out: 11.2895M>
	{13000000, 11289600, 12, 0, 429, 18, 1},	//<out: 11.2895M>
	{15360000, 11289600, 14, 0, 441, 19, 1},
	{16000000, 11289600, 24, 0, 882, 24, 1},
	{19200000, 11289600, 4, 0, 147, 24, 1},
	{19680000, 11289600, 13, 1, 771, 23, 1},	//<out: 11.28964M>
	{24000000, 11289600, 24, 0, 588, 24, 1},

	{12288000, 12288000, 9, 0, 400, 19, 1},	//24576000/2
	{11289600, 11289600, 9, 0, 400, 19, 1},	//22579200/2

	{24576000 / 1, 12288000, 9, 0, 200, 19, 1},	//24576000
	{24576000 / 16, 12288000, 0, 0, 320, 19, 1},	//1536000
	{24576000 / 64, 12288000, 0, 0, 640, 9, 1},	//384000
	{24576000 / 96, 12288000, 0, 0, 960, 9, 1},	//256000
	{24576000 / 128, 12288000, 0, 0, 512, 3, 1},	//192000
	{24576000 / 176, 12288000, 0, 0, 880, 4, 1},	//140000
	{24576000 / 192, 12288000, 0, 0, 960, 4, 1},	//128000

	{22579200 / 1, 11289600, 9, 0, 200, 19, 1},	//22579200
	{22579200 / 4, 11289600, 4, 0, 400, 19, 1},	//5644800
	{22579200 / 16, 11289600, 0, 0, 320, 19, 1},	//1411200
	{22579200 / 64, 11289600, 0, 0, 640, 9, 1},	//352800
	{22579200 / 96, 11289600, 0, 0, 960, 9, 1},	//235200
	{22579200 / 128, 11289600, 0, 0, 512, 3, 1},	//176400
	{22579200 / 176, 11289600, 0, 0, 880, 4, 1},	//128290
	{22579200 / 192, 11289600, 0, 0, 960, 4, 1},	//117600

	{22579200 / 6, 11289600, 2, 0, 360, 19, 1},	//3763200
	{22579200 / 8, 11289600, 0, 0, 160, 19, 1},	//2822400
	{22579200 / 12, 11289600, 0, 0, 240, 19, 1},	//1881600
	{22579200 / 24, 11289600, 0, 0, 480, 19, 1},	//940800
	{22579200 / 32, 11289600, 0, 0, 640, 19, 1},	//705600
	{22579200 / 48, 11289600, 0, 0, 960, 19, 1},	//470400
};

const struct reg_default_value ac107_reg_default_value[] = {
	/*** Chip reset ***/
	{CHIP_AUDIO_RST, 0x4B},

	/*** Power Control ***/
	{PWR_CTRL1, 0x00},
	{PWR_CTRL2, 0x11},

	/*** PLL Configure Control ***/
	{PLL_CTRL1, 0x48},
	{PLL_CTRL2, 0x00},
	{PLL_CTRL3, 0x03},
	{PLL_CTRL4, 0x0D},
	{PLL_CTRL5, 0x00},
	{PLL_CTRL6, 0x0F},
	{PLL_CTRL7, 0xD0},
	{PLL_LOCK_CTRL, 0x00},

	/*** System Clock Control ***/
	{SYSCLK_CTRL, 0x00},
	{MOD_CLK_EN, 0x00},
	{MOD_RST_CTRL, 0x00},

	/*** I2S Common Control ***/
	{I2S_CTRL, 0x00},
	{I2S_BCLK_CTRL, 0x00},
	{I2S_LRCK_CTRL1, 0x00},
	{I2S_LRCK_CTRL2, 0x00},
	{I2S_FMT_CTRL1, 0x00},
	{I2S_FMT_CTRL2, 0x55},
	{I2S_FMT_CTRL3, 0x60},

	/*** I2S TX Control ***/
	{I2S_TX_CTRL1, 0x00},
	{I2S_TX_CTRL2, 0x00},
	{I2S_TX_CTRL3, 0x00},
	{I2S_TX_CHMP_CTRL1, 0x00},
	{I2S_TX_CHMP_CTRL2, 0x00},

	/*** I2S RX Control ***/
	{I2S_RX_CTRL1, 0x00},
	{I2S_RX_CTRL2, 0x03},
	{I2S_RX_CTRL3, 0x00},
	{I2S_RX_CHMP_CTRL1, 0x00},
	{I2S_RX_CHMP_CTRL2, 0x00},

	/*** PDM Control ***/
	{PDM_CTRL, 0x00},

	/*** ADC Common Control ***/
	{ADC_SPRC, 0x00},
	{ADC_DIG_EN, 0x00},
	{DMIC_EN, 0x00},
	{HPF_EN, 0x03},

	/*** ADC Digital Channel Volume Control ***/
	{ADC1_DVOL_CTRL, 0xA0},
	{ADC2_DVOL_CTRL, 0xA0},

	/*** ADC Digital Mixer Source and Gain Control ***/
	{ADC1_DMIX_SRC, 0x01},
	{ADC2_DMIX_SRC, 0x02},

	/*** ADC_DIG_DEBUG ***/
	{ADC_DIG_DEBUG, 0x00},

	/*** Pad Function and Drive Control ***/
	{ADC_ANA_DEBUG1, 0x11},
	{ADC_ANA_DEBUG2, 0x11},
	{I2S_PADDRV_CTRL, 0x55},

	/*** ADC1 Analog Control ***/
	{ANA_ADC1_CTRL1, 0x00},
	{ANA_ADC1_CTRL2, 0x00},
	{ANA_ADC1_CTRL3, 0x00},
	{ANA_ADC1_CTRL4, 0x00},
	{ANA_ADC1_CTRL5, 0x00},

	/*** ADC2 Analog Control ***/
	{ANA_ADC2_CTRL1, 0x00},
	{ANA_ADC2_CTRL2, 0x00},
	{ANA_ADC2_CTRL3, 0x00},
	{ANA_ADC2_CTRL4, 0x00},
	{ANA_ADC2_CTRL5, 0x00},

	/*** ADC Dither Control ***/
	{ADC_DITHER_CTRL, 0x00},
};

const u8 ac107_kcontrol_dapm_reg[] = {
#if AC107_KCONTROL_EN
	ANA_ADC1_CTRL3, ANA_ADC2_CTRL3, ADC1_DVOL_CTRL, ADC2_DVOL_CTRL,
	ADC1_DMIX_SRC, ADC2_DMIX_SRC, ADC_DIG_DEBUG,
#endif

#if AC107_DAPM_EN
	DMIC_EN, ADC1_DMIX_SRC, ADC2_DMIX_SRC, I2S_TX_CHMP_CTRL1,
	I2S_TX_CHMP_CTRL2, ANA_ADC1_CTRL5, ANA_ADC2_CTRL5, ADC_DIG_EN,
#endif
};

static int ac107_read(u8 reg, u8 *rt_value, struct i2c_client *client);
static int ac107_update_bits(u8 reg, u8 mask, u8 value,
			     struct i2c_client *client);

#define AC107_KCONTROL_FUNC(n) \
int ac107_codec##n##_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)\
{\
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;\
	unsigned int reg = mc->reg;\
	unsigned int shift = mc->shift;\
	unsigned int max = mc->max;\
	unsigned int mask = (1 << fls(max)) - 1;\
	unsigned int invert = mc->invert;\
	u8 reg_val;\
\
	ac107_read(reg, &reg_val, i2c_ctrl[n]);\
	ucontrol->value.integer.value[0] = reg_val >> shift & mask;\
	if (invert) {\
		ucontrol->value.integer.value[0] = max - ucontrol->value.integer.value[0];\
	} \
	/*printk("read: REG-0x%02x, shift-%d, val-%d\n",reg,shift,ucontrol->value.integer.value[0]);*/\
\
	return 0;\
} \
\
int ac107_codec##n##_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)\
{\
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;\
	unsigned int reg = mc->reg;\
	unsigned int shift = mc->shift;\
	unsigned int max = mc->max;\
	unsigned int mask = (1 << fls(max)) - 1;\
	unsigned int invert = mc->invert;\
	u8 reg_val;\
\
	reg_val = ucontrol->value.integer.value[0] & mask;\
	if (invert) {\
		reg_val = max - reg_val;\
	} \
	ac107_update_bits(reg, mask<<shift, reg_val<<shift, i2c_ctrl[n]);\
	/*printk("write: REG-0x%02x, shift-%d, val-%d\n",reg,shift,reg_val);*/\
\
	return 0;\
}

AC107_KCONTROL_FUNC(0);
AC107_KCONTROL_FUNC(1);
AC107_KCONTROL_FUNC(2);
AC107_KCONTROL_FUNC(3);
AC107_KCONTROL_FUNC(4);
AC107_KCONTROL_FUNC(5);
AC107_KCONTROL_FUNC(6);
AC107_KCONTROL_FUNC(7);

static const DECLARE_TLV_DB_SCALE(adc_pga_gain_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(digital_vol_tlv, -11925, 75, 0);
static const DECLARE_TLV_DB_SCALE(digital_mix_vol_tlv, -600, 600, 0);

/*************************************** General(volume) controls *******************************************/
//ac107 volume controls
static const struct snd_kcontrol_new ac107_volume_controls[] = {
	//Channels PGA Gain
	SOC_SINGLE_EXT_TLV("Channel 1 PGA Gain", ANA_ADC1_CTRL3,
			   RX1_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec0_get,
			   ac107_codec0_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 2 PGA Gain", ANA_ADC2_CTRL3,
			   RX2_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec0_get,
			   ac107_codec0_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 3 PGA Gain", ANA_ADC1_CTRL3,
			   RX1_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec1_get,
			   ac107_codec1_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 4 PGA Gain", ANA_ADC2_CTRL3,
			   RX2_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec1_get,
			   ac107_codec1_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 5 PGA Gain", ANA_ADC1_CTRL3,
			   RX1_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec2_get,
			   ac107_codec2_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 6 PGA Gain", ANA_ADC2_CTRL3,
			   RX2_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec2_get,
			   ac107_codec2_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 7 PGA Gain", ANA_ADC1_CTRL3,
			   RX1_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec3_get,
			   ac107_codec3_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 8 PGA Gain", ANA_ADC2_CTRL3,
			   RX2_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec3_get,
			   ac107_codec3_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 9 PGA Gain", ANA_ADC1_CTRL3,
			   RX1_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec4_get,
			   ac107_codec4_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 10 PGA Gain", ANA_ADC2_CTRL3,
			   RX2_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec4_get,
			   ac107_codec4_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 11 PGA Gain", ANA_ADC1_CTRL3,
			   RX1_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec5_get,
			   ac107_codec5_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 12 PGA Gain", ANA_ADC2_CTRL3,
			   RX2_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec5_get,
			   ac107_codec5_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 13 PGA Gain", ANA_ADC1_CTRL3,
			   RX1_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec6_get,
			   ac107_codec6_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 14 PGA Gain", ANA_ADC2_CTRL3,
			   RX2_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec6_get,
			   ac107_codec6_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 15 PGA Gain", ANA_ADC1_CTRL3,
			   RX1_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec7_get,
			   ac107_codec7_put, adc_pga_gain_tlv),
	SOC_SINGLE_EXT_TLV("Channel 16 PGA Gain", ANA_ADC2_CTRL3,
			   RX2_PGA_GAIN_CTRL, 0x1f, 0, ac107_codec7_get,
			   ac107_codec7_put, adc_pga_gain_tlv),

	//Channels Digital Volume
	SOC_SINGLE_EXT_TLV("Channel 1 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec0_get, ac107_codec0_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 2 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec0_get, ac107_codec0_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 3 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec1_get, ac107_codec1_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 4 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec1_get, ac107_codec1_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 5 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec2_get, ac107_codec2_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 6 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec2_get, ac107_codec2_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 7 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec3_get, ac107_codec3_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 8 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec3_get, ac107_codec3_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 9 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec4_get, ac107_codec4_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 10 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec4_get, ac107_codec4_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 11 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec5_get, ac107_codec5_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 12 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec5_get, ac107_codec5_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 13 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec6_get, ac107_codec6_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 14 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec6_get, ac107_codec6_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 15 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec7_get, ac107_codec7_put,
			   digital_vol_tlv),
	SOC_SINGLE_EXT_TLV("Channel 16 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff,
			   0, ac107_codec7_get, ac107_codec7_put,
			   digital_vol_tlv),
};

//ac107 common controls
static const struct snd_kcontrol_new ac107_controls[] = {
#if 0
	SOC_SINGLE_TLV("ADC1 PGA Gain", ANA_ADC1_CTRL3, RX1_PGA_GAIN_CTRL, 0x1f,
		       0, adc_pga_gain_tlv),
	SOC_SINGLE_TLV("ADC2 PGA Gain", ANA_ADC2_CTRL3, RX2_PGA_GAIN_CTRL, 0x1f,
		       0, adc_pga_gain_tlv),

	SOC_SINGLE_TLV("CH1 Digital Volume", ADC1_DVOL_CTRL, 0, 0xff, 0,
		       digital_vol_tlv),
	SOC_SINGLE_TLV("CH2 Digital Volume", ADC2_DVOL_CTRL, 0, 0xff, 0,
		       digital_vol_tlv),

	SOC_SINGLE_TLV("CH1 ch1 Mixer Volume", ADC1_DMIX_SRC, ADC1_ADC1_DMXL_GC,
		       1, 0, digital_mix_vol_tlv),
	SOC_SINGLE_TLV("CH1 ch2 Mixer Volume", ADC1_DMIX_SRC, ADC1_ADC2_DMXL_GC,
		       1, 0, digital_mix_vol_tlv),

	SOC_SINGLE_TLV("CH2 ch1 Mixer Volume", ADC2_DMIX_SRC, ADC2_ADC1_DMXL_GC,
		       1, 0, digital_mix_vol_tlv),
	SOC_SINGLE_TLV("CH2 ch2 Mixer Volume", ADC2_DMIX_SRC, ADC2_ADC2_DMXL_GC,
		       1, 0, digital_mix_vol_tlv),
#endif
	//debug control
	SOC_SINGLE("ADC Pattern Sel", ADC_DIG_DEBUG, ADC_PTN_SEL, 0x7, 0),
	//SOC_SINGLE("MCLK Drive Sel", I2S_PADDRV_CTRL, MCLK_DRV, 0x3, 0),
	//SOC_SINGLE("SYSCLK Hold Time Sel", PLL_LOCK_CTRL, SYSCLK_HOLD_TIME, 0x7, 0),
};

/*************************************** DAPM controls *******************************************/
//ADC DMIC Source Select MUX
static const char *adc_dmic_src_mux_text[] = {
	"ADC switch", "DMIC switch"
};

static const struct soc_enum adc_dmic_src_mux_enum =
SOC_ENUM_SINGLE(DMIC_EN, DIG_MIC_EN, 2, adc_dmic_src_mux_text);
static const struct snd_kcontrol_new adc_dmic_src_mux =
SOC_DAPM_ENUM("ADC DMIC MUX", adc_dmic_src_mux_enum);

//ADC1 Digital Source Control Mixer
static const struct snd_kcontrol_new adc1_digital_src_mixer[] = {
	SOC_DAPM_SINGLE("ADC1 DAT switch", ADC1_DMIX_SRC, ADC1_ADC1_DMXL_SRC, 1,
			0),
	SOC_DAPM_SINGLE("ADC2 DAT switch", ADC1_DMIX_SRC, ADC1_ADC2_DMXL_SRC, 1,
			0),
};

//ADC2 Digital Source Control Mixer
static const struct snd_kcontrol_new adc2_digital_src_mixer[] = {
	SOC_DAPM_SINGLE("ADC1 DAT switch", ADC2_DMIX_SRC, ADC2_ADC1_DMXL_SRC, 1,
			0),
	SOC_DAPM_SINGLE("ADC2 DAT switch", ADC2_DMIX_SRC, ADC2_ADC2_DMXL_SRC, 1,
			0),
};

//I2S TX Ch1 Mapping Mux
static const char *i2s_tx_ch1_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch1_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL1, TX_CH1_MAP, 2, i2s_tx_ch1_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch1_map_mux =
SOC_DAPM_ENUM("I2S TX CH1 MUX", i2s_tx_ch1_map_mux_enum);

//I2S TX Ch2 Mapping Mux
static const char *i2s_tx_ch2_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch2_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL1, TX_CH2_MAP, 2, i2s_tx_ch2_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch2_map_mux =
SOC_DAPM_ENUM("I2S TX CH2 MUX", i2s_tx_ch2_map_mux_enum);

//I2S TX Ch3 Mapping Mux
static const char *i2s_tx_ch3_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch3_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL1, TX_CH3_MAP, 2, i2s_tx_ch3_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch3_map_mux =
SOC_DAPM_ENUM("I2S TX CH3 MUX", i2s_tx_ch3_map_mux_enum);

//I2S TX Ch4 Mapping Mux
static const char *i2s_tx_ch4_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch4_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL1, TX_CH4_MAP, 2, i2s_tx_ch4_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch4_map_mux =
SOC_DAPM_ENUM("I2S TX CH4 MUX", i2s_tx_ch4_map_mux_enum);

//I2S TX Ch5 Mapping Mux
static const char *i2s_tx_ch5_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch5_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL1, TX_CH5_MAP, 2, i2s_tx_ch5_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch5_map_mux =
SOC_DAPM_ENUM("I2S TX CH5 MUX", i2s_tx_ch5_map_mux_enum);

//I2S TX Ch6 Mapping Mux
static const char *i2s_tx_ch6_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch6_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL1, TX_CH6_MAP, 2, i2s_tx_ch6_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch6_map_mux =
SOC_DAPM_ENUM("I2S TX CH6 MUX", i2s_tx_ch6_map_mux_enum);

//I2S TX Ch7 Mapping Mux
static const char *i2s_tx_ch7_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch7_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL1, TX_CH7_MAP, 2, i2s_tx_ch7_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch7_map_mux =
SOC_DAPM_ENUM("I2S TX CH7 MUX", i2s_tx_ch7_map_mux_enum);

//I2S TX Ch8 Mapping Mux
static const char *i2s_tx_ch8_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch8_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL1, TX_CH8_MAP, 2, i2s_tx_ch8_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch8_map_mux =
SOC_DAPM_ENUM("I2S TX CH8 MUX", i2s_tx_ch8_map_mux_enum);

//I2S TX Ch9 Mapping Mux
static const char *i2s_tx_ch9_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch9_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL2, TX_CH9_MAP, 2, i2s_tx_ch9_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch9_map_mux =
SOC_DAPM_ENUM("I2S TX CH9 MUX", i2s_tx_ch9_map_mux_enum);

//I2S TX Ch10 Mapping Mux
static const char *i2s_tx_ch10_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch10_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL2, TX_CH10_MAP, 2, i2s_tx_ch10_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch10_map_mux =
SOC_DAPM_ENUM("I2S TX CH10 MUX", i2s_tx_ch10_map_mux_enum);

//I2S TX Ch11 Mapping Mux
static const char *i2s_tx_ch11_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch11_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL2, TX_CH11_MAP, 2, i2s_tx_ch11_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch11_map_mux =
SOC_DAPM_ENUM("I2S TX CH11 MUX", i2s_tx_ch11_map_mux_enum);

//I2S TX Ch12 Mapping Mux
static const char *i2s_tx_ch12_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch12_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL2, TX_CH12_MAP, 2, i2s_tx_ch12_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch12_map_mux =
SOC_DAPM_ENUM("I2S TX CH12 MUX", i2s_tx_ch12_map_mux_enum);

//I2S TX Ch13 Mapping Mux
static const char *i2s_tx_ch13_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch13_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL2, TX_CH13_MAP, 2, i2s_tx_ch13_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch13_map_mux =
SOC_DAPM_ENUM("I2S TX CH13 MUX", i2s_tx_ch13_map_mux_enum);

//I2S TX Ch14 Mapping Mux
static const char *i2s_tx_ch14_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch14_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL2, TX_CH14_MAP, 2, i2s_tx_ch14_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch14_map_mux =
SOC_DAPM_ENUM("I2S TX CH14 MUX", i2s_tx_ch14_map_mux_enum);

//I2S TX Ch15 Mapping Mux
static const char *i2s_tx_ch15_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch15_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL2, TX_CH15_MAP, 2, i2s_tx_ch15_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch15_map_mux =
SOC_DAPM_ENUM("I2S TX CH15 MUX", i2s_tx_ch15_map_mux_enum);

//I2S TX Ch16 Mapping Mux
static const char *i2s_tx_ch16_map_mux_text[] = {
	"ADC1 Sample switch", "ADC2 Sample switch"
};

static const struct soc_enum i2s_tx_ch16_map_mux_enum =
SOC_ENUM_SINGLE(I2S_TX_CHMP_CTRL2, TX_CH16_MAP, 2, i2s_tx_ch16_map_mux_text);
static const struct snd_kcontrol_new i2s_tx_ch16_map_mux =
SOC_DAPM_ENUM("I2S TX CH16 MUX", i2s_tx_ch16_map_mux_enum);

/*************************************** DAPM widgets *******************************************/
//ac107 dapm widgets
static const struct snd_soc_dapm_widget ac107_dapm_widgets[] = {
	//input widgets
	SND_SOC_DAPM_INPUT("MIC1P"),
	SND_SOC_DAPM_INPUT("MIC1N"),

	SND_SOC_DAPM_INPUT("MIC2P"),
	SND_SOC_DAPM_INPUT("MIC2N"),

	SND_SOC_DAPM_INPUT("DMIC"),

	//MIC PGA
	SND_SOC_DAPM_PGA("MIC1 PGA", ANA_ADC1_CTRL5, RX1_GLOBAL_EN, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC2 PGA", ANA_ADC2_CTRL5, RX2_GLOBAL_EN, 0, NULL, 0),

	//DMIC PGA
	SND_SOC_DAPM_PGA("DMICL PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMICR PGA", SND_SOC_NOPM, 0, 0, NULL, 0),

	//ADC DMIC MUX
	SND_SOC_DAPM_MUX("ADC DMIC MUX", ADC_DIG_EN, DG_EN, 0,
			 &adc_dmic_src_mux),

	//ADC1 VIR PGA
	SND_SOC_DAPM_PGA("ADC1 VIR PGA", ADC_DIG_EN, ENAD1, 0, NULL, 0),

	//ADC2 VIR PGA
	SND_SOC_DAPM_PGA("ADC2 VIR PGA", ADC_DIG_EN, ENAD2, 0, NULL, 0),

	//ADC1 DIG MIXER
	SND_SOC_DAPM_MIXER("ADC1 DIG MIXER", SND_SOC_NOPM, 0, 0,
			   adc1_digital_src_mixer,
			   ARRAY_SIZE(adc1_digital_src_mixer)),

	//ADC2 DIG MIXER
	SND_SOC_DAPM_MIXER("ADC2 DIG MIXER", SND_SOC_NOPM, 0, 0,
			   adc2_digital_src_mixer,
			   ARRAY_SIZE(adc2_digital_src_mixer)),

	//I2S TX CH1 MUX
	SND_SOC_DAPM_MUX("I2S TX CH1 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch1_map_mux),

	//I2S TX CH2 MUX
	SND_SOC_DAPM_MUX("I2S TX CH2 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch2_map_mux),

	//I2S TX CH3 MUX
	SND_SOC_DAPM_MUX("I2S TX CH3 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch3_map_mux),

	//I2S TX CH4 MUX
	SND_SOC_DAPM_MUX("I2S TX CH4 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch4_map_mux),

	//I2S TX CH5 MUX
	SND_SOC_DAPM_MUX("I2S TX CH5 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch5_map_mux),

	//I2S TX CH6 MUX
	SND_SOC_DAPM_MUX("I2S TX CH6 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch6_map_mux),

	//I2S TX CH7 MUX
	SND_SOC_DAPM_MUX("I2S TX CH7 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch7_map_mux),

	//I2S TX CH8 MUX
	SND_SOC_DAPM_MUX("I2S TX CH8 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch8_map_mux),

	//I2S TX CH9 MUX
	SND_SOC_DAPM_MUX("I2S TX CH9 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch9_map_mux),

	//I2S TX CH10 MUX
	SND_SOC_DAPM_MUX("I2S TX CH10 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch10_map_mux),

	//I2S TX CH11 MUX
	SND_SOC_DAPM_MUX("I2S TX CH11 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch11_map_mux),

	//I2S TX CH12 MUX
	SND_SOC_DAPM_MUX("I2S TX CH12 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch12_map_mux),

	//I2S TX CH13 MUX
	SND_SOC_DAPM_MUX("I2S TX CH13 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch13_map_mux),

	//I2S TX CH14 MUX
	SND_SOC_DAPM_MUX("I2S TX CH14 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch14_map_mux),

	//I2S TX CH15 MUX
	SND_SOC_DAPM_MUX("I2S TX CH15 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch15_map_mux),

	//I2S TX CH16 MUX
	SND_SOC_DAPM_MUX("I2S TX CH16 MUX", SND_SOC_NOPM, 0, 0,
			 &i2s_tx_ch16_map_mux),

	//AIF OUT -> (stream widget, stname must be same with codec dai_driver stream_name, which will be used to build dai widget)
	SND_SOC_DAPM_AIF_OUT("AIF ADC OUT", "Capture", 0, SND_SOC_NOPM, 0, 0),
};

/*************************************** DAPM routes *******************************************/
//ac107 dapm routes
static const struct snd_soc_dapm_route ac107_dapm_routes[] = {
	//MIC1 PGA
	{"MIC1 PGA", NULL, "MIC1P"},
	{"MIC1 PGA", NULL, "MIC1N"},

	//MIC2 PGA
	{"MIC2 PGA", NULL, "MIC2P"},
	{"MIC2 PGA", NULL, "MIC2N"},

	//DMIC PGA
	{"DMICL PGA", NULL, "DMIC"},
	{"DMICR PGA", NULL, "DMIC"},

	//ADC DMIC MUX
	{"ADC DMIC MUX", "ADC switch", "MIC1 PGA"},
	{"ADC DMIC MUX", "ADC switch", "MIC2 PGA"},
	{"ADC DMIC MUX", "DMIC switch", "DMICL PGA"},
	{"ADC DMIC MUX", "DMIC switch", "DMICR PGA"},

	//ADC1 VIR PGA
	{"ADC1 VIR PGA", NULL, "ADC DMIC MUX"},

	//ADC2 VIR PGA
	{"ADC2 VIR PGA", NULL, "ADC DMIC MUX"},

	//ADC1 DIG MIXER
	{"ADC1 DIG MIXER", "ADC1 DAT switch", "ADC1 VIR PGA"},
	{"ADC1 DIG MIXER", "ADC2 DAT switch", "ADC2 VIR PGA"},

	//ADC2 DIG MIXER
	{"ADC2 DIG MIXER", "ADC1 DAT switch", "ADC1 VIR PGA"},
	{"ADC2 DIG MIXER", "ADC2 DAT switch", "ADC2 VIR PGA"},

	//I2S TX CH1 MUX
	{"I2S TX CH1 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH1 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH2 MUX
	{"I2S TX CH2 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH2 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH3 MUX
	{"I2S TX CH3 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH3 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH4 MUX
	{"I2S TX CH4 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH4 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH5 MUX
	{"I2S TX CH5 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH5 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH6 MUX
	{"I2S TX CH6 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH6 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH7 MUX
	{"I2S TX CH7 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH7 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH8 MUX
	{"I2S TX CH8 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH8 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH9 MUX
	{"I2S TX CH9 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH9 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH10 MUX
	{"I2S TX CH10 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH10 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH11 MUX
	{"I2S TX CH11 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH11 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH12 MUX
	{"I2S TX CH12 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH12 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH13 MUX
	{"I2S TX CH13 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH13 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH14 MUX
	{"I2S TX CH14 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH14 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH15 MUX
	{"I2S TX CH15 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH15 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//I2S TX CH16 MUX
	{"I2S TX CH16 MUX", "ADC1 Sample switch", "ADC1 DIG MIXER"},
	{"I2S TX CH16 MUX", "ADC2 Sample switch", "ADC2 DIG MIXER"},

	//AIF ADC OUT
	{"AIF ADC OUT", NULL, "I2S TX CH1 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH2 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH3 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH4 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH5 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH6 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH7 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH8 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH9 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH10 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH11 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH12 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH13 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH14 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH15 MUX"},
	{"AIF ADC OUT", NULL, "I2S TX CH16 MUX"},
};

static int ac107_read(u8 reg, u8 *rt_value, struct i2c_client *client)
{
	int ret;
	u8 read_cmd[3] = { 0 };
	u8 cmd_len = 0;

	read_cmd[0] = reg;
	cmd_len = 1;

	if (client == NULL || client->adapter == NULL) {
		pr_err("ac107_read client or client->adapter is NULL\n");
		return -1;
	}

	ret = i2c_master_send(client, read_cmd, cmd_len);
	if (ret != cmd_len) {
		pr_err("ac107_read error1->[REG-0x%02x]\n", reg);
		return -1;
	}

	ret = i2c_master_recv(client, rt_value, 1);
	if (ret != 1) {
		pr_err("ac107_read error2->[REG-0x%02x], ret=%d\n", reg, ret);
		return -1;
	}

	return 0;
}

static int ac107_write(u8 reg, unsigned char value, struct i2c_client *client)
{
	int ret = 0;
	u8 write_cmd[2] = { 0 };

	write_cmd[0] = reg;
	write_cmd[1] = value;

	if (client == NULL || client->adapter == NULL) {
		pr_err("ac107_write client or client->adapter is NULL\n");
		return -1;
	}

	ret = i2c_master_send(client, write_cmd, 2);
	if (ret != 2) {
		pr_err("ac107_write error->[REG-0x%02x,val-0x%02x]\n", reg,
		       value);
		return -1;
	}

	return 0;
}

static int ac107_update_bits(u8 reg, u8 mask, u8 value,
			     struct i2c_client *client)
{
	u8 val_old, val_new;

	ac107_read(reg, &val_old, client);
	val_new = (val_old & ~mask) | (value & mask);
	if (val_new != val_old) {
		ac107_write(reg, val_new, client);
	}

	return 0;
}

#if 0
static int ac107_multi_chips_read(u8 reg, unsigned char *rt_value)
{
	u8 i;

	for (i = 0; i < AC107_CHIP_NUMS; i++) {
		ac107_read(reg, rt_value++, i2c_ctrl[i]);
	}

	return 0;
}
#endif

static int ac107_multi_chips_write(u8 reg, unsigned char value)
{
	u8 i;

	for (i = 0; i < AC107_CHIP_NUMS; i++) {
		ac107_write(reg, value, i2c_ctrl[i]);
	}

	return 0;
}

static int ac107_multi_chips_update_bits(u8 reg, u8 mask, u8 value)
{
	u8 i;

	for (i = 0; i < AC107_CHIP_NUMS; i++) {
		ac107_update_bits(reg, mask, value, i2c_ctrl[i]);
	}

	return 0;
}

static void ac107_hw_init(struct i2c_client *i2c)
{
	u8 reg_val;

	/*** Analog voltage enable ***/
	ac107_write(PWR_CTRL1, 0x80, i2c);	/*0x01=0x80: VREF Enable */
	ac107_write(PWR_CTRL2, 0x55, i2c);	/*0x02=0x55: MICBIAS1&2 Enable */

	/*** SYSCLK Config ***/
	ac107_update_bits(SYSCLK_CTRL, 0x1 << SYSCLK_EN, 0x1 << SYSCLK_EN, i2c);	/*SYSCLK Enable */
	ac107_write(MOD_CLK_EN, 0x07, i2c);	/*0x21=0x07: Module clock enable<I2S, ADC digital,  ADC analog> */
	ac107_write(MOD_RST_CTRL, 0x03, i2c);	/*0x22=0x03: Module reset de-asserted<I2S, ADC digital> */

	/*** I2S Common Config ***/
	ac107_update_bits(I2S_CTRL, 0x1 << SDO_EN, 0x1 << SDO_EN, i2c);	/*SDO enable */
	ac107_update_bits(I2S_BCLK_CTRL, 0x1 << EDGE_TRANSFER, 0x0 << EDGE_TRANSFER, i2c);	/*SDO drive data and SDI sample data at the different BCLK edge */
	ac107_update_bits(I2S_LRCK_CTRL1, 0x3 << LRCK_PERIODH,
			  ((AC107_LRCK_PERIOD - 1) >> 8) << LRCK_PERIODH, i2c);
	ac107_write(I2S_LRCK_CTRL2, (u8) (AC107_LRCK_PERIOD - 1), i2c);	/*config LRCK period */
	/*Encoding mode format select 0~N-1, Encoding mode enable, Turn to hi-z state (TDM) when not transferring slot */
	ac107_update_bits(I2S_FMT_CTRL1,
			0x1 << ENCD_FMT | 0x1 << ENCD_SEL | 0x1 << TX_SLOT_HIZ
			| 0x1 << TX_STATE,
			!!AC107_ENCODING_FMT << ENCD_FMT |
			!!AC107_ENCODING_EN << ENCD_SEL | 0x0 << TX_SLOT_HIZ
			| 0x1 << TX_STATE, i2c);
	ac107_update_bits(I2S_FMT_CTRL2, 0x7 << SLOT_WIDTH_SEL, (AC107_SLOT_WIDTH / 4 - 1) << SLOT_WIDTH_SEL, i2c);	/*8/12/16/20/24/28/32bit Slot Width */
	/*0x36=0x60: TX MSB first, SDOUT normal, PCM frame type, Linear PCM Data Mode */
	ac107_update_bits(I2S_FMT_CTRL3,
			0x1 << TX_MLS | 0x1 << SDOUT_MUTE | 0x1 << LRCK_WIDTH
			| 0x3 << TX_PDM,
			0x0 << TX_MLS | 0x0 << SDOUT_MUTE | 0x0 << LRCK_WIDTH
			| 0x0 << TX_PDM, i2c);

	ac107_update_bits(I2S_TX_CHMP_CTRL1, !AC107_DAPM_EN * 0xff, 0xaa, i2c);	/*0x3c=0xaa: TX CH1/3/5/7 map to adc1, TX CH2/4/6/8 map to adc2 */
	ac107_update_bits(I2S_TX_CHMP_CTRL2, !AC107_DAPM_EN * 0xff, 0xaa, i2c);	/*0x3d=0xaa: TX CH9/11/13/15 map to adc1, TX CH10/12/14/16 map to adc2 */

	/*PDM Interface Latch ADC1 data on rising clock edge. Latch ADC2 data on falling clock edge, PDM Enable */
	ac107_update_bits(PDM_CTRL, 0x1 << PDM_TIMING | 0x1 << PDM_EN,
			  0x0 << PDM_TIMING | !!AC107_PDM_EN << PDM_EN, i2c);

	/*** ADC DIG part Config***/
	ac107_update_bits(ADC_DIG_EN, !AC107_DAPM_EN * 0x7, 0x7, i2c);	/*0x61=0x07: Digital part globe enable, ADCs digital part enable */
	ac107_update_bits(DMIC_EN, !AC107_DAPM_EN * 0x1, !!AC107_DMIC_EN, i2c);	/*DMIC Enable */

	/* ADC pattern select */
#if AC107_KCONTROL_EN
	ac107_read(ADC_DIG_DEBUG, &reg_val, i2c);
	ac107_write(HPF_EN, !(reg_val & 0x7) * 0x03, i2c);
#else
	ac107_write(HPF_EN, !AC107_ADC_PATTERN_SEL * 0x03, i2c);
	ac107_update_bits(ADC_DIG_DEBUG, 0x7 << ADC_PTN_SEL,
			  (AC107_ADC_PATTERN_SEL & 0x7) << ADC_PTN_SEL, i2c);
#endif

	//ADC Digital Volume Config
	ac107_update_bits(ADC1_DVOL_CTRL, !AC107_KCONTROL_EN * 0xff, 0xA0, i2c);
	ac107_update_bits(ADC2_DVOL_CTRL, !AC107_KCONTROL_EN * 0xff, 0xA0, i2c);

	/*** ADCs analog PGA gain Config***/
	ac107_update_bits(ANA_ADC1_CTRL3,
			  !AC107_KCONTROL_EN * 0x1f << RX1_PGA_GAIN_CTRL,
			  AC107_PGA_GAIN << RX1_PGA_GAIN_CTRL, i2c);
	ac107_update_bits(ANA_ADC2_CTRL3,
			  !AC107_KCONTROL_EN * 0x1f << RX2_PGA_GAIN_CTRL,
			  AC107_PGA_GAIN << RX2_PGA_GAIN_CTRL, i2c);

	/*** ADCs analog global Enable***/
	ac107_update_bits(ANA_ADC1_CTRL5, !AC107_DAPM_EN * 0x1 << RX1_GLOBAL_EN,
			  0x1 << RX1_GLOBAL_EN, i2c);
	ac107_update_bits(ANA_ADC2_CTRL5, !AC107_DAPM_EN * 0x1 << RX2_GLOBAL_EN,
			  0x1 << RX2_GLOBAL_EN, i2c);

	//VREF Fast Start-up Disable
	ac107_update_bits(PWR_CTRL1, 0x1 << VREF_FSU_DISABLE,
			  0x1 << VREF_FSU_DISABLE, i2c);
}

static int ac107_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			    unsigned int freq, int dir)
{
	AC107_DEBUG("\n--->%s\n", __FUNCTION__);

	switch (clk_id) {
	case SYSCLK_SRC_MCLK:
		AC107_DEBUG("AC107 SYSCLK source select MCLK\n\n");
		ac107_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << SYSCLK_SRC, SYSCLK_SRC_MCLK << SYSCLK_SRC);	//System Clock Source Select MCLK
		break;
	case SYSCLK_SRC_BCLK:
		AC107_DEBUG("AC107 SYSCLK source select BCLK\n\n");
		ac107_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << SYSCLK_SRC, SYSCLK_SRC_BCLK << SYSCLK_SRC);	//System Clock Source Select BCLK
		break;
	case SYSCLK_SRC_PLL:
		AC107_DEBUG("AC107 SYSCLK source select PLL\n\n");
		ac107_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << SYSCLK_SRC, SYSCLK_SRC_PLL << SYSCLK_SRC);	//System Clock Source Select PLL
		break;
	default:
		pr_err("AC107 SYSCLK source config error:%d\n\n", clk_id);
		return -EINVAL;
	}

	//SYSCLK Enable
	ac107_multi_chips_update_bits(SYSCLK_CTRL, 0x1 << SYSCLK_EN,
				      0x1 << SYSCLK_EN);
	return 0;
}

static int ac107_set_pll(struct snd_soc_dai *dai, int pll_id, int source,
			 unsigned int freq_in, unsigned int freq_out)
{
	u32 i, m1, m2, n, k1, k2;
	AC107_DEBUG("\n--->%s\n", __FUNCTION__);

	freq_in = freq_in / 2;
	if (!freq_out)
		return 0;

	if (freq_in < 128000 || freq_in > 24576000) {
		pr_err
		    ("AC107 PLLCLK source input freq only support [128K,24M],while now %u\n\n",
		     freq_in);
		return -EINVAL;
	} else if ((freq_in == 12288000 || freq_in == 11289600)
		   && (pll_id == PLLCLK_SRC_MCLK || pll_id == PLLCLK_SRC_BCLK)) {
		//System Clock Source Select MCLK/BCLK, SYSCLK Enable
		AC107_DEBUG
		    ("AC107 don't need to use PLL, SYSCLK source select %s\n\n",
		     pll_id ? "BCLK" : "MCLK");
		ac107_multi_chips_update_bits(SYSCLK_CTRL,
					      0x3 << SYSCLK_SRC | 0x1 <<
					      SYSCLK_EN,
					      pll_id << SYSCLK_SRC | 0x1 <<
					      SYSCLK_EN);
		return 0;	//Don't need to use PLL
	}
	//PLL Clock Source Select
	switch (pll_id) {
	case PLLCLK_SRC_MCLK:
		AC107_DEBUG("AC107 PLLCLK input source select MCLK\n");
		ac107_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << PLLCLK_SRC,
					      PLLCLK_SRC_MCLK << PLLCLK_SRC);
		break;
	case PLLCLK_SRC_BCLK:
		AC107_DEBUG("AC107 PLLCLK input source select BCLK\n");
		ac107_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << PLLCLK_SRC,
					      PLLCLK_SRC_BCLK << PLLCLK_SRC);
		break;
	case PLLCLK_SRC_PDMCLK:
		AC107_DEBUG("AC107 PLLCLK input source select PDMCLK\n");
		ac107_multi_chips_update_bits(SYSCLK_CTRL, 0x3 << PLLCLK_SRC,
					      PLLCLK_SRC_PDMCLK << PLLCLK_SRC);
		break;
	default:
		pr_err("AC107 PLLCLK source config error:%d\n\n", pll_id);
		return -EINVAL;
	}

	//FOUT =(FIN * N) / [(M1+1) * (M2+1)*(K1+1)*(K2+1)] ;
	for (i = 0; i < ARRAY_SIZE(ac107_pll_div); i++) {
		if (ac107_pll_div[i].freq_in == freq_in
		    && ac107_pll_div[i].freq_out == freq_out) {
			m1 = ac107_pll_div[i].m1;
			m2 = ac107_pll_div[i].m2;
			n = ac107_pll_div[i].n;
			k1 = ac107_pll_div[i].k1;
			k2 = ac107_pll_div[i].k2;
			AC107_DEBUG
			    ("AC107 PLL freq_in match:%u, freq_out:%u\n\n",
			     freq_in, freq_out);
			break;
		}
	}

	if (i == ARRAY_SIZE(ac107_pll_div)) {
		pr_err
		    ("AC107 don't match PLLCLK freq_in and freq_out table\n\n");
		return -EINVAL;
	}
	//Config PLL DIV param M1/M2/N/K1/K2
	ac107_multi_chips_update_bits(PLL_CTRL2,
				      0x1f << PLL_PREDIV1 | 0x1 << PLL_PREDIV2,
				      m1 << PLL_PREDIV1 | m2 << PLL_PREDIV2);
	ac107_multi_chips_update_bits(PLL_CTRL3, 0x3 << PLL_LOOPDIV_MSB,
				      (n >> 8) << PLL_LOOPDIV_MSB);
	ac107_multi_chips_update_bits(PLL_CTRL4, 0xff << PLL_LOOPDIV_LSB,
				      (u8) n << PLL_LOOPDIV_LSB);
	ac107_multi_chips_update_bits(PLL_CTRL5,
				      0x1f << PLL_POSTDIV1 | 0x1 <<
				      PLL_POSTDIV2,
				      k1 << PLL_POSTDIV1 | k2 << PLL_POSTDIV2);

	//Config PLL module current
	//ac107_multi_chips_update_bits(PLL_CTRL1, 0x7<<PLL_IBIAS, 0x4<<PLL_IBIAS);
	//ac107_multi_chips_update_bits(PLL_CTRL6, 0x1f<<PLL_CP, 0xf<<PLL_CP);

	//PLL module enable
	ac107_multi_chips_update_bits(PLL_LOCK_CTRL, 0x1 << PLL_LOCK_EN, 0x1 << PLL_LOCK_EN);	//PLL CLK lock enable
	//ac107_multi_chips_update_bits(PLL_CTRL1, 0x1<<PLL_EN | 0x1<<PLL_COM_EN, 0x1<<PLL_EN | 0x1<<PLL_COM_EN);	//PLL Common voltage Enable, PLL Enable

	//PLLCLK Enable, SYSCLK Enable
	ac107_multi_chips_update_bits(SYSCLK_CTRL,
				      0x1 << PLLCLK_EN | 0x1 << SYSCLK_EN,
				      0x1 << PLLCLK_EN | 0x1 << SYSCLK_EN);

	return 0;
}

static int ac107_set_clkdiv(struct snd_soc_dai *dai, int div_id, int div)
{
	u32 i, bclk_div, bclk_div_reg_val;
	AC107_DEBUG("\n--->%s\n", __FUNCTION__);

	if (!div_id) {		//use div_id to judge Master/Slave mode,  0: Slave mode, 1: Master mode
		AC107_DEBUG
		    ("AC107 work as Slave mode, don't need to config BCLK_DIV\n\n");
		return 0;
	}

	//bclk_div = div / (AC107_LRCK_PERIOD);	//default PCM mode
	bclk_div = div/(2*AC107_LRCK_PERIOD); //I2S/LJ/RJ mode

	for (i = 0; i < ARRAY_SIZE(ac107_bclk_div); i++) {
		if (ac107_bclk_div[i].real_val == bclk_div) {
			bclk_div_reg_val = ac107_bclk_div[i].reg_val;
			AC107_DEBUG("AC107 set BCLK_DIV_[%u]\n\n", bclk_div);
			break;
		}
	}

	if (i == ARRAY_SIZE(ac107_bclk_div)) {
		pr_err("AC107 don't support BCLK_DIV_[%u]\n\n", bclk_div);
		return -EINVAL;
	}
	//AC107 set BCLK DIV
	ac107_multi_chips_update_bits(I2S_BCLK_CTRL, 0xf << BCLKDIV,
				      bclk_div_reg_val << BCLKDIV);
	return 0;
}

static int ac107_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	u8 i, tx_offset, i2s_mode, sign_ext, lrck_polarity, brck_polarity;
	struct ac107_priv *ac107 = dev_get_drvdata(dai->dev);
	AC107_DEBUG("\n--->%s\n", __FUNCTION__);

	//AC107 config Master/Slave mode
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:	//AC107 Master
		AC107_DEBUG("AC107 set to work as Master\n");
		ac107_update_bits(I2S_CTRL, 0x3 << LRCK_IOEN, 0x3 << LRCK_IOEN, ac107->i2c);	//BCLK & LRCK output
		break;
	case SND_SOC_DAIFMT_CBS_CFS:	//AC107 Slave
		AC107_DEBUG("AC107 set to work as Slave\n");
		ac107_update_bits(I2S_CTRL, 0x3 << LRCK_IOEN, 0x0 << LRCK_IOEN, ac107->i2c);	//BCLK & LRCK input
		break;
	default:
		pr_err("AC107 Master/Slave mode config error:%u\n\n",
		       (fmt & SND_SOC_DAIFMT_MASTER_MASK) >> 12);
		return -EINVAL;
	}
	for (i = 0; i < AC107_CHIP_NUMS; i++) {	//multi_chips: only one chip set as Master, and the others also need to set as Slave
		if (i2c_ctrl[i] == ac107->i2c)
			continue;
		ac107_update_bits(I2S_CTRL, 0x3 << LRCK_IOEN, 0x0 << LRCK_IOEN,
				  i2c_ctrl[i]);
	}

	//AC107 config I2S/LJ/RJ/PCM format
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		AC107_DEBUG("AC107 config I2S format\n");
		i2s_mode = LEFT_JUSTIFIED_FORMAT;
		tx_offset = 1;
		sign_ext = TRANSFER_ZERO_AFTER;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		AC107_DEBUG("AC107 config RIGHT-JUSTIFIED format\n");
		i2s_mode = RIGHT_JUSTIFIED_FORMAT;
		tx_offset = 0;
		sign_ext = SIGN_EXTENSION_MSB;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		AC107_DEBUG("AC107 config LEFT-JUSTIFIED format\n");
		i2s_mode = LEFT_JUSTIFIED_FORMAT;
		tx_offset = 0;
		sign_ext = TRANSFER_ZERO_AFTER;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		AC107_DEBUG("AC107 config PCM-A format\n");
		i2s_mode = PCM_FORMAT;
		tx_offset = 1;
		sign_ext = TRANSFER_ZERO_AFTER;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		AC107_DEBUG("AC107 config PCM-B format\n");
		i2s_mode = PCM_FORMAT;
		tx_offset = 0;
		sign_ext = TRANSFER_ZERO_AFTER;
		break;
	default:
		pr_err("AC107 I2S format config error:%u\n\n",
		       fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}
	ac107_multi_chips_update_bits(I2S_FMT_CTRL1,
				      0x3 << MODE_SEL | 0x1 << TX_OFFSET,
				      i2s_mode << MODE_SEL | tx_offset <<
				      TX_OFFSET);
	ac107_multi_chips_update_bits(I2S_FMT_CTRL3, 0x3 << SEXT,
				      sign_ext << SEXT);

	//AC107 config BCLK&LRCK polarity
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		AC107_DEBUG
		    ("AC107 config BCLK&LRCK polarity: BCLK_normal,LRCK_normal\n");
		brck_polarity = BCLK_NORMAL_DRIVE_N_SAMPLE_P;
		lrck_polarity = LRCK_LEFT_LOW_RIGHT_HIGH;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		AC107_DEBUG
		    ("AC107 config BCLK&LRCK polarity: BCLK_normal,LRCK_invert\n");
		brck_polarity = BCLK_NORMAL_DRIVE_N_SAMPLE_P;
		lrck_polarity = LRCK_LEFT_HIGH_RIGHT_LOW;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		AC107_DEBUG
		    ("AC107 config BCLK&LRCK polarity: BCLK_invert,LRCK_normal\n");
		brck_polarity = BCLK_INVERT_DRIVE_P_SAMPLE_N;
		lrck_polarity = LRCK_LEFT_LOW_RIGHT_HIGH;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		AC107_DEBUG
		    ("AC107 config BCLK&LRCK polarity: BCLK_invert,LRCK_invert\n");
		brck_polarity = BCLK_INVERT_DRIVE_P_SAMPLE_N;
		lrck_polarity = LRCK_LEFT_HIGH_RIGHT_LOW;
		break;
	default:
		pr_err("AC107 config BCLK/LRCLK polarity error:%u\n\n",
		       (fmt & SND_SOC_DAIFMT_INV_MASK) >> 8);
		return -EINVAL;
	}
	ac107_multi_chips_update_bits(I2S_BCLK_CTRL, 0x1 << BCLK_POLARITY,
				      brck_polarity << BCLK_POLARITY);
	ac107_multi_chips_update_bits(I2S_LRCK_CTRL1, 0x1 << LRCK_POLARITY,
				      lrck_polarity << LRCK_POLARITY);

	return 0;
}

static int ac107_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	u16 i, channels, channels_en, sample_resolution;
	struct ac107_priv *ac107 = dev_get_drvdata(dai->dev);
	AC107_DEBUG("\n--->%s\n", __FUNCTION__);

	//AC107 hw init
	for (i = 0; i < AC107_CHIP_NUMS; i++) {
		ac107_hw_init(i2c_ctrl[i]);
	}

	//AC107 set sample rate
	for (i = 0; i < ARRAY_SIZE(ac107_sample_rate); i++) {
		if (ac107_sample_rate[i].real_val ==
		    params_rate(params) /
		    (AC107_ENCODING_EN ? AC107_ENCODING_CH_NUMS / 2 : 1)) {
			ac107_multi_chips_update_bits(ADC_SPRC,
						      0xf << ADC_FS_I2S,
						      ac107_sample_rate
						      [i].reg_val <<
						      ADC_FS_I2S);
			break;
		}
	}

	//AC107 set channels
	channels =
	    params_channels(params) *
	    (AC107_ENCODING_EN ? AC107_ENCODING_CH_NUMS / 2 : 1);
	for (i = 0; i < (channels + 1) / 2; i++) {
		channels_en =
		    (channels >=
		     2 * (i + 1)) ? 0x0003 << (2 * i) : ((1 << (channels % 2)) -
							 1) << (2 * i);
		ac107_write(I2S_TX_CTRL1, channels - 1, i2c_ctrl[i]);
		ac107_write(I2S_TX_CTRL2, (u8) channels_en, i2c_ctrl[i]);
		ac107_write(I2S_TX_CTRL3, channels_en >> 8, i2c_ctrl[i]);
	}
	for (; i < AC107_CHIP_NUMS; i++) {
		ac107_write(I2S_TX_CTRL1, 0, i2c_ctrl[i]);
		ac107_write(I2S_TX_CTRL2, 0, i2c_ctrl[i]);
		ac107_write(I2S_TX_CTRL3, 0, i2c_ctrl[i]);
	}

	//AC107 set sample resorution
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		sample_resolution = 8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		sample_resolution = 16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		sample_resolution = 20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		sample_resolution = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		sample_resolution = 32;
		break;
	default:
		dev_err(dai->dev,
			"AC107 don't supported the sample resolution: %u\n",
			params_format(params));
		return -EINVAL;
	}
	ac107_multi_chips_update_bits(I2S_FMT_CTRL2, 0x7 << SAMPLE_RESOLUTION,
				      (sample_resolution / 4 -
				       1) << SAMPLE_RESOLUTION);

	//AC107 TX enable, Globle enable
	ac107_multi_chips_update_bits(I2S_CTRL, 0x1 << TXEN | 0x1 << GEN,
				      0x1 << TXEN | 0x1 << GEN);

	//AC107 PLL Enable and through MCLK Pin output Enable
	ac107_read(SYSCLK_CTRL, (u8 *)&i, ac107->i2c);
	if (i & 0x80) {		//PLLCLK Enable
		if (!(i & 0x0c)) {	//SYSCLK select MCLK
			//MCLK output Clock 24MHz from DPLL
			ac107_update_bits(I2S_CTRL, 0x1 << MCLK_IOEN,
					  0x1 << MCLK_IOEN, ac107->i2c);
			ac107_update_bits(I2S_PADDRV_CTRL, 0x03 << MCLK_DRV,
					  0x03 << MCLK_DRV, ac107->i2c);
			for (i = 0; i < AC107_CHIP_NUMS; i++) {	//multi_chips: only one chip MCLK output PLL_test, and the others MCLK config as input
				if (i2c_ctrl[i] == ac107->i2c)
					continue;
				ac107_update_bits(I2S_CTRL, 0x1 << MCLK_IOEN,
						  0x0 << MCLK_IOEN,
						  i2c_ctrl[i]);
			}
			//the chip which MCLK config as output, should select PLL as its SYCCLK source
			ac107_update_bits(SYSCLK_CTRL, 0x3 << SYSCLK_SRC,
					  SYSCLK_SRC_PLL << SYSCLK_SRC,
					  ac107->i2c);
			//the chip which MCLK config as output, PLL Common voltage Enable, PLL Enable
			ac107_update_bits(PLL_CTRL1,
					  0x1 << PLL_EN | 0x1 << PLL_COM_EN,
					  0x1 << PLL_EN | 0x1 << PLL_COM_EN,
					  ac107->i2c);
		} else if ((i & 0x0c) >> 2 == 0x2) {	//SYSCLK select PLL
			ac107_multi_chips_update_bits(PLL_LOCK_CTRL,
						      0x7 << SYSCLK_HOLD_TIME,
						      0x3 << SYSCLK_HOLD_TIME);
			//All chips PLL Common voltage Enable, PLL Enable
			ac107_multi_chips_update_bits(PLL_CTRL1,
						      0x1 << PLL_EN | 0x1 <<
						      PLL_COM_EN,
						      0x1 << PLL_EN | 0x1 <<
						      PLL_COM_EN);
		}
	}

	return 0;
}

static int ac107_hw_free(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai)
{
	u8 i, j;
	AC107_DEBUG("\n--->%s\n", __FUNCTION__);

	//AC107 I2S Globle disable
	ac107_multi_chips_update_bits(I2S_CTRL, 0x1 << GEN, 0x0 << GEN);

#if AC107_KCONTROL_EN || AC107_DAPM_EN
	for (i = 0; i < ARRAY_SIZE(ac107_reg_default_value); i++) {
		for (j = 0; j < sizeof(ac107_kcontrol_dapm_reg); j++) {
			if (ac107_reg_default_value[i].reg_addr ==
			    ac107_kcontrol_dapm_reg[j])
				break;
		}
		if (j == sizeof(ac107_kcontrol_dapm_reg)) {
			ac107_multi_chips_write(ac107_reg_default_value
						[i].reg_addr,
						ac107_reg_default_value
						[i].default_val);
		}
	}

#else

	AC107_DEBUG("AC107 reset all register to their default value\n\n");
	ac107_multi_chips_write(CHIP_AUDIO_RST, 0x12);
#endif

	return 0;
}

/*** define  ac107  dai_ops  struct ***/
static const struct snd_soc_dai_ops ac107_dai_ops = {
	/*DAI clocking configuration */
	.set_sysclk = ac107_set_sysclk,
	.set_pll = ac107_set_pll,
	.set_clkdiv = ac107_set_clkdiv,

	/*ALSA PCM audio operations */
	.hw_params = ac107_hw_params,
	.hw_free = ac107_hw_free,

	/*DAI format configuration */
	.set_fmt = ac107_set_fmt,
};

/*** define  ac107  dai_driver struct ***/
static struct snd_soc_dai_driver ac107_dai0 = {
	.name = "ac107-pcm0",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = AC107_CHIP_NUMS * 2,
		    .rates = AC107_RATES,
		    .formats = AC107_FORMATS,
		    },
	.ops = &ac107_dai_ops,
};

static struct snd_soc_dai_driver ac107_dai1 = {
	.name = "ac107-pcm1",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = AC107_CHIP_NUMS * 2,
		    .rates = AC107_RATES,
		    .formats = AC107_FORMATS,
		    },
	.ops = &ac107_dai_ops,
};

static struct snd_soc_dai_driver ac107_dai2 = {
	.name = "ac107-pcm2",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = AC107_CHIP_NUMS * 2,
		    .rates = AC107_RATES,
		    .formats = AC107_FORMATS,
		    },
	.ops = &ac107_dai_ops,
};

static struct snd_soc_dai_driver ac107_dai3 = {
	.name = "ac107-pcm3",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = AC107_CHIP_NUMS * 2,
		    .rates = AC107_RATES,
		    .formats = AC107_FORMATS,
		    },
	.ops = &ac107_dai_ops,
};

static struct snd_soc_dai_driver ac107_dai4 = {
	.name = "ac107-pcm4",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = AC107_CHIP_NUMS * 2,
		    .rates = AC107_RATES,
		    .formats = AC107_FORMATS,
		    },
	.ops = &ac107_dai_ops,
};

static struct snd_soc_dai_driver ac107_dai5 = {
	.name = "ac107-pcm5",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = AC107_CHIP_NUMS * 2,
		    .rates = AC107_RATES,
		    .formats = AC107_FORMATS,
		    },
	.ops = &ac107_dai_ops,
};

static struct snd_soc_dai_driver ac107_dai6 = {
	.name = "ac107-pcm6",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = AC107_CHIP_NUMS * 2,
		    .rates = AC107_RATES,
		    .formats = AC107_FORMATS,
		    },
	.ops = &ac107_dai_ops,
};

static struct snd_soc_dai_driver ac107_dai7 = {
	.name = "ac107-pcm7",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = AC107_CHIP_NUMS * 2,
		    .rates = AC107_RATES,
		    .formats = AC107_FORMATS,
		    },
	.ops = &ac107_dai_ops,
};

static struct snd_soc_dai_driver *ac107_dai[] = {
	&ac107_dai0,
	&ac107_dai1,
	&ac107_dai2,
	&ac107_dai3,
	&ac107_dai4,
	&ac107_dai5,
	&ac107_dai6,
	&ac107_dai7,
};

static int ac107_probe(struct snd_soc_component *component)
{
	struct ac107_priv *ac107 = dev_get_drvdata(component->dev);
	int ret = 0;

	component->regmap =
	    devm_regmap_init_i2c(ac107->i2c, &ac107_regmap_config);
	ret = PTR_ERR_OR_ZERO(component->regmap);
	if (ret) {
		dev_err(component->dev, "AC107 regmap init I2C Failed: %d\n", ret);
		return ret;
	}
	ac107->component = component;

#if AC107_KCONTROL_EN
	ac107_multi_chips_update_bits(ANA_ADC1_CTRL3, 0x1f << RX1_PGA_GAIN_CTRL, AC107_PGA_GAIN << RX1_PGA_GAIN_CTRL);	//ADC1 PGA Gain default
	ac107_multi_chips_update_bits(ANA_ADC2_CTRL3, 0x1f << RX2_PGA_GAIN_CTRL, AC107_PGA_GAIN << RX2_PGA_GAIN_CTRL);	//ADC2 PGA Gain default
	snd_soc_add_component_controls(component, ac107_volume_controls, AC107_CHIP_NUMS * 2);	//PGA Gain Control
	snd_soc_add_component_controls(component, ac107_volume_controls + 16, AC107_CHIP_NUMS * 2);	//Digital Volume Control
#endif

#if AC107_DAPM_EN
	ac107_multi_chips_write(I2S_TX_CHMP_CTRL1, 0xaa);	//defatul map:	TX CH1/3/5/7 map to adc1, TX CH2/4/6/8 map to adc2
	ac107_multi_chips_write(I2S_TX_CHMP_CTRL2, 0xaa);	//defatul map:	TX CH9/11/13/15 map to adc1, TX CH10/12/14/16 map to adc2
#endif

	return 0;
}

static void ac107_remove(struct snd_soc_component *component)
{
	return;
}

static int ac107_suspend(struct snd_soc_component *component)
{
	struct ac107_priv *ac107 = dev_get_drvdata(component->dev);

#if AC107_MATCH_DTS_EN
	if ((ac107_regulator_en & 0x1) && !IS_ERR(ac107->vol_supply.dvcc_1v8)) {
		regulator_disable(ac107->vol_supply.dvcc_1v8);
		ac107_regulator_en &= ~0x1;
	}

	if ((ac107_regulator_en & 0x2)
	    && !IS_ERR(ac107->vol_supply.avcc_vccio_3v3)) {
		regulator_disable(ac107->vol_supply.avcc_vccio_3v3);
		ac107_regulator_en &= ~0x2;
	}
	if (gpio_is_valid(ac107->reset_gpio)) {
		gpio_set_value(ac107->reset_gpio, 0);
		gpio_free(ac107->reset_gpio);
	}
#endif

	return 0;
}

static int ac107_resume(struct snd_soc_component *component)
{
	struct ac107_priv *ac107 = dev_get_drvdata(component->dev);
	int ret;

#if AC107_MATCH_DTS_EN
	if ((ac107_regulator_en & 0x1) && !IS_ERR(ac107->vol_supply.dvcc_1v8)) {
		ret = regulator_enable(ac107->vol_supply.dvcc_1v8);
		if (ret != 0)
			pr_err
			    ("[AC107] %s: some error happen, fail to enable regulator dvcc_1v8!\n",
			     __func__);
		ac107_regulator_en |= 0x1;
	}

	if ((ac107_regulator_en & 0x2)
	    && !IS_ERR(ac107->vol_supply.avcc_vccio_3v3)) {
		ret = regulator_enable(ac107->vol_supply.avcc_vccio_3v3);
		if (ret != 0)
			pr_err
			    ("[AC107] %s: some error happen, fail to enable regulator avcc_vccio_3v3!\n",
			     __func__);
		ac107_regulator_en |= 0x2;
	}
	if (gpio_is_valid(ac107->reset_gpio)) {
		ret = gpio_request(ac107->reset_gpio, "reset gpio");
		if (!ret) {
			gpio_direction_output(ac107->reset_gpio, 1);
			gpio_set_value(ac107->reset_gpio, 1);
			msleep(20);
		}
	}
#endif

	return 0;
}

static unsigned int ac107_component_read(struct snd_soc_component *component,
				     unsigned int reg)
{
	//AC107_DEBUG("\n--->%s\n",__FUNCTION__);
	u8 val_r;
	struct ac107_priv *ac107 = dev_get_drvdata(component->dev);

	ac107_read(reg, &val_r, ac107->i2c);
	return val_r;
}

static int ac107_component_write(struct snd_soc_component *component, unsigned int reg,
			     unsigned int value)
{
	//AC107_DEBUG("\n--->%s\n",__FUNCTION__);
	ac107_multi_chips_write(reg, value);
	return 0;
}

/*** define  ac107  codec_driver struct ***/
static const struct snd_soc_component_driver ac107_soc_component_driver = {
	.probe = ac107_probe,
	.remove = ac107_remove,
	.suspend = ac107_suspend,
	.resume = ac107_resume,

#if AC107_CODEC_RW_USER_EN
	.read = ac107_component_read,
	.write = ac107_component_write,
#endif
/*
#if AC107_KCONTROL_EN
	.controls = ac107_controls,
	.num_controls = ARRAY_SIZE(ac107_controls),
#endif
*/
#if AC107_DAPM_EN
	.dapm_widgets = ac107_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ac107_dapm_widgets),
	.dapm_routes = ac107_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(ac107_dapm_routes),
#endif
};

static ssize_t ac107_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int val = 0, flag = 0;
	u8 i = 0, reg, num, value_w, value_r;

	struct ac107_priv *ac107 = dev_get_drvdata(dev);
	val = simple_strtol(buf, NULL, 16);
	flag = (val >> 16) & 0xFF;

	if (flag) {
		reg = (val >> 8) & 0xFF;
		value_w = val & 0xFF;
		printk("\nWrite: start REG:0x%02x,val:0x%02x,count:0x%02x\n",
		       reg, value_w, flag);
		while (flag--) {
			ac107_write(reg, value_w, ac107->i2c);
			printk("Write 0x%02x to REG:0x%02x\n", value_w, reg);
			reg++;
		}
	} else {
		reg = (val >> 8) & 0xFF;
		num = val & 0xff;
		printk("\nRead: start REG:0x%02x,count:0x%02x\n", reg, num);

		do {
			value_r = 0;
			ac107_read(reg, &value_r, ac107->i2c);
			printk("REG[0x%02x]: 0x%02x;  ", reg, value_r);
			reg++;
			i++;
			if ((i == num) || (i % 4 == 0))
				printk("\n");
		} while (i < num);
	}

	return count;
}

static ssize_t ac107_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	printk("/*** AC107 driver version: V1.0 ***/\n");
	printk("echo flag|reg|val > ac107\n");
	printk("eg->read start addres=0x00,count=0xff: echo 00ff >ac107\n");
	printk
	    ("eg->write start addres=0x70,value=0xa0,count=0x2: echo 270a0 >ac107\n");
	//printk("eg write value:0xfe to address:0x06 :echo 106fe > ac107\n");
	return 0;
}

static DEVICE_ATTR(ac107, 0644, ac107_show, ac107_store);

static struct attribute *ac107_debug_attrs[] = {
	&dev_attr_ac107.attr,
	NULL,
};

static struct attribute_group ac107_debug_attr_group = {
	.name = "ac107_debug",
	.attrs = ac107_debug_attrs,
};

static int ac107_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *i2c_id)
{
	struct ac107_priv *ac107;
	struct device_node *np = i2c->dev.of_node;
	const char *regulator_name = NULL;
	int ret = 0;
	struct gpio_config config;

	ac107 = devm_kzalloc(&i2c->dev, sizeof(struct ac107_priv), GFP_KERNEL);
	if (ac107 == NULL) {
		dev_err(&i2c->dev, "Unable to allocate ac107 private data\n");
		return -ENOMEM;
	}

	ac107->i2c = i2c;
	dev_set_drvdata(&i2c->dev, ac107);

#if AC107_MATCH_DTS_EN
	if (!ac107_regulator_en) {
		ret = of_property_read_string(np, AC107_DVCC_NAME, &regulator_name);	//(const char**)
		if (ret) {
			pr_err("get ac107 DVCC regulator name failed \n");
		} else {
			ac107->vol_supply.dvcc_1v8 =
			    regulator_get(NULL, regulator_name);
			if (IS_ERR(ac107->vol_supply.dvcc_1v8)
			    || !ac107->vol_supply.dvcc_1v8) {
				pr_err("get ac107 dvcc_1v8 failed, return!\n");
				return -EFAULT;
			}
			regulator_set_voltage(ac107->vol_supply.dvcc_1v8,
					      1800000, 1800000);
			ret = regulator_enable(ac107->vol_supply.dvcc_1v8);
			if (ret != 0)
				pr_err
				    ("[AC107] %s: some error happen, fail to enable regulator dvcc_1v8!\n",
				     __func__);
			ac107_regulator_en |= 0x1;
		}

		ret = of_property_read_string(np, AC107_AVCC_VCCIO_NAME, &regulator_name);	//(const char**)
		if (ret) {
			pr_err("get ac107 AVCC_VCCIO regulator name failed \n");
		} else {
			ac107->vol_supply.avcc_vccio_3v3 =
			    regulator_get(NULL, regulator_name);
			if (IS_ERR(ac107->vol_supply.avcc_vccio_3v3)
			    || !ac107->vol_supply.avcc_vccio_3v3) {
				pr_err
				    ("get ac107 avcc_vccio_3v3 failed, return!\n");
				return -EFAULT;
			}
			regulator_set_voltage(ac107->vol_supply.avcc_vccio_3v3,
					      3300000, 3300000);
			ret =
			    regulator_enable(ac107->vol_supply.avcc_vccio_3v3);
			if (ret != 0)
				pr_err
				    ("[AC107] %s: some error happen, fail to enable regulator avcc_vccio_3v3!\n",
				     __func__);
			ac107_regulator_en |= 0x2;
		}

		/*gpio reset enable */
		ac107->reset_gpio = of_get_named_gpio_flags(np,
							    "gpio-reset", 0,
							    (enum of_gpio_flags
							     *)&config);
		if (gpio_is_valid(ac107->reset_gpio)) {
			ret = gpio_request(ac107->reset_gpio, "reset gpio");
			if (!ret) {
				gpio_direction_output(ac107->reset_gpio, 1);
				gpio_set_value(ac107->reset_gpio, 1);
				msleep(20);
			} else {
				pr_err
				    ("%s, line:%d, failed request reset gpio: %d!\n",
				     __func__, __LINE__, ac107->reset_gpio);
			}
		}
	}
#endif

	if (i2c_id->driver_data < AC107_CHIP_NUMS) {
		i2c_ctrl[i2c_id->driver_data] = i2c;
		ret = snd_soc_register_component(&i2c->dev, &ac107_soc_component_driver,
					ac107_dai[i2c_id->driver_data], 1);
		if (ret < 0) {
			dev_err(&i2c->dev,
				"Failed to register ac107 codec: %d\n", ret);
		}
	} else {
		pr_err("The wrong i2c_id number :%d\n",
		       (int)(i2c_id->driver_data));
	}

	ret = sysfs_create_group(&i2c->dev.kobj, &ac107_debug_attr_group);
	if (ret) {
		pr_err("failed to create attr group\n");
	}

	return ret;
}

static int ac107_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_component(&i2c->dev);
	return 0;
}

#if !AC107_MATCH_DTS_EN
static int ac107_i2c_detect(struct i2c_client *client,
			    struct i2c_board_info *info)
{
	u8 ac107_chip_id;
	struct i2c_adapter *adapter = client->adapter;

	ac107_read(CHIP_AUDIO_RST, &ac107_chip_id, client);
	AC107_DEBUG("\nAC107_Chip_ID on I2C-%d:0x%02X\n", adapter->nr,
		    ac107_chip_id);

	if (ac107_chip_id == 0x4B) {
		if (client->addr == 0x36) {
			strlcpy(info->type, "ac107_0", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x37) {
			strlcpy(info->type, "ac107_1", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x38) {
			strlcpy(info->type, "ac107_2", I2C_NAME_SIZE);
			return 0;
		} else if (client->addr == 0x39) {
			strlcpy(info->type, "ac107_3", I2C_NAME_SIZE);
			return 0;
		}
	}

	return -ENODEV;
}
#endif

static const unsigned short ac107_i2c_addr[] = {
#if AC107_CHIP_NUMS > 0
	0x36,
#endif

#if AC107_CHIP_NUMS > 1
	0x37,
#endif

#if AC107_CHIP_NUMS > 2
	0x38,
#endif

#if AC107_CHIP_NUMS > 3
	0x39,
#endif

#if AC107_CHIP_NUMS > 4
	0x36,
#endif

#if AC107_CHIP_NUMS > 5
	0x37,
#endif

#if AC107_CHIP_NUMS > 6
	0x38,
#endif

#if AC107_CHIP_NUMS > 7
	0x39,
#endif

	I2C_CLIENT_END,
};

static struct i2c_board_info const ac107_i2c_board_info[] = {
#if AC107_CHIP_NUMS > 0
	{I2C_BOARD_INFO("ac107_0", 0x36),},
#endif

#if AC107_CHIP_NUMS > 1
	{I2C_BOARD_INFO("ac107_1", 0x37),},
#endif

#if AC107_CHIP_NUMS > 2
	{I2C_BOARD_INFO("ac107_2", 0x38),},
#endif

#if AC107_CHIP_NUMS > 3
	{I2C_BOARD_INFO("ac107_3", 0x39),},
#endif

#if AC107_CHIP_NUMS > 4
	{I2C_BOARD_INFO("ac107_4", 0x36),},
#endif

#if AC107_CHIP_NUMS > 5
	{I2C_BOARD_INFO("ac107_5", 0x37),},
#endif

#if AC107_CHIP_NUMS > 6
	{I2C_BOARD_INFO("ac107_6", 0x38),},
#endif

#if AC107_CHIP_NUMS > 7
	{I2C_BOARD_INFO("ac107_7", 0x39),},
#endif

};

static const struct i2c_device_id ac107_i2c_id[] = {
#if AC107_CHIP_NUMS > 0
	{"ac107_0", 0},
#endif

#if AC107_CHIP_NUMS > 1
	{"ac107_1", 1},
#endif

#if AC107_CHIP_NUMS > 2
	{"ac107_2", 2},
#endif

#if AC107_CHIP_NUMS > 3
	{"ac107_3", 3},
#endif

#if AC107_CHIP_NUMS > 4
	{"ac107_4", 4},
#endif

#if AC107_CHIP_NUMS > 5
	{"ac107_5", 5},
#endif

#if AC107_CHIP_NUMS > 6
	{"ac107_6", 6},
#endif

#if AC107_CHIP_NUMS > 7
	{"ac107_7", 7},
#endif

	{}
};

MODULE_DEVICE_TABLE(i2c, ac107_i2c_id);

static struct of_device_id ac107_dt_ids[] = {
#if AC107_CHIP_NUMS > 0
	{.compatible = "ac107_0",},
#endif

#if AC107_CHIP_NUMS > 1
	{.compatible = "ac107_1",},
#endif

#if AC107_CHIP_NUMS > 2
	{.compatible = "ac107_2",},
#endif

#if AC107_CHIP_NUMS > 3
	{.compatible = "ac107_3",},
#endif

#if AC107_CHIP_NUMS > 4
	{.compatible = "ac107_4",},
#endif

#if AC107_CHIP_NUMS > 5
	{.compatible = "ac107_5",},
#endif

#if AC107_CHIP_NUMS > 6
	{.compatible = "ac107_6",},
#endif

#if AC107_CHIP_NUMS > 7
	{.compatible = "ac107_7",},
#endif
	{},
};
MODULE_DEVICE_TABLE(of, ac107_dt_ids);

static struct i2c_driver ac107_i2c_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		   .name = "ac107",
		   .owner = THIS_MODULE,
#if AC107_MATCH_DTS_EN
		   .of_match_table = ac107_dt_ids,
#endif
		   },
	.probe = ac107_i2c_probe,
	.remove = ac107_i2c_remove,
	.id_table = ac107_i2c_id,
#if !AC107_MATCH_DTS_EN
	.address_list = ac107_i2c_addr,
	.detect = ac107_i2c_detect,
#endif
};

static int __init ac107_init(void)
{
	int ret;
	ret = i2c_add_driver(&ac107_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register ac107 i2c driver : %d \n", ret);

	return ret;
}

module_init(ac107_init);

static void __exit ac107_exit(void)
{
	i2c_del_driver(&ac107_i2c_driver);
}

module_exit(ac107_exit);

MODULE_DESCRIPTION("ASoC ac107 codec driver");
MODULE_AUTHOR("panjunwen");
MODULE_LICENSE("GPL");
