/*
 * ak4490.c  --  audio driver for AK4490
 *
 * Copyright (C) 2014 Asahi Kasei Microdevices Corporation
 *  Author                Date        Revision
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                      14/08/11	    1.0
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <mach/board.h>
#include <linux/switch_gpio_msp430.h>

#include <linux/wakelock.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "ak4490.h"
#include "fpga_audio_interface.h"
#include <linux/regulator/machine.h>
#include <linux/moduleparam.h>

extern void AudioBridge_disable();
extern void AudioBridge_enable();
extern void iomux_set_gpio_mode(int gpio);
extern const struct snd_kcontrol_new* get_audiobirdge_control_table(int* num_controls);

// when reboot devices hal will reopen device sometime
// hal will call ak4490_set_dai_mute  to unmute output
// so set a flag to indicate shutdown operation ,disable 
// to unmute output
static bool isShutdown=false;

int snd_soc_get_volsw_2r_ak4490(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw_2r_ak4490(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int ak4490_read_i2c_lr(u8 lr,u8 reg);
int ak4490_update_bits_lr(u8 lr, unsigned short reg,
				unsigned int mask, unsigned int value);
int ak4490_write_i2c_lr(u8 lr,u8 reg,u8 data);

int ak4490_update_bits_lr2(unsigned short reg,
				unsigned int mask, unsigned int value);

static int ak4490_get_am_hp_mute( struct snd_kcontrol       *kcontrol, struct snd_ctl_elem_value  *ucontrol);

static int ak4490_set_am_hp_mute( struct snd_kcontrol       *kcontrol, struct snd_ctl_elem_value  *ucontrol);

enum lpf_type ak4490_get_lpf_type();
void ak4490_set_am_power(int on);


static uint8_t first_play=0;
int pmute=2000;
module_param_named(po_mute, pmute, int, 0644);

/* Digital-filter selector for BOTH AK4490 DACs. Values follow enum lpf_type:
 * 0=sharp, 1=slow, 2=short-delay-sharp, 3=short-delay-slow, 4=super-slow roll-off.
 * Writable via /sys/module/snd_soc_ak4490/parameters/selected_filter and applied to
 * the DACs immediately on write (see ak4490_selected_filter_ops, defined below once
 * ak4490_set_lpf()/gpak4490 are in scope). Default 0 = AKM power-on sharp roll-off. */
int selected_filter = LPF_SHARP_ROLL_OFF;
int oversampling_freq = 384000; // allow to set custom oversampting rate
module_param(oversampling_freq, int, 0644);
int superslow = 0; // 0 - disable superslow filter, 1 - enable superslow filter
module_param(superslow, int, 0644);

#define AK4490_DEBUG			//used at debug mode
#define AK4490_CONTIF_DEBUG		//used at debug mode

#ifdef AK4490_DEBUG
#define akdbgprt printk
#else
#define akdbgprt(format, arg...) do {} while (0)
#endif

/* #define POP_DEBUG */

#define AUDIO_LINK_SHUTDOWN_DELAY 5
#define AUDIO_LINK_HP_DELAY 20
#define WAKE_LOCK_TIMEOUT	((AUDIO_LINK_SHUTDOWN_DELAY+1) * HZ)
static struct wake_lock audiolink_wakelock;

#define  DAC_MUTE_ACTIVE        GPIO_HIGH
#define  DAC_MUTE_UNACTIVE      GPIO_LOW
#define  DAC_RESET_ACTIVE       GPIO_LOW
#define  DAC_RESET_UNACTIVE     GPIO_HIGH
#define  AM_POWER_UNACTIVE      GPIO_LOW
#define  AM_POWER_ACTIVE        GPIO_HIGH

enum lpf_type gak4490_defalult_lpf=LPF_DELAY_SLOW_ROLL_OFF;
static fpga_fmt_t fpga_fmt;
static void ak4490_soft_mute(int on);
/* AK4490 Codec Private Data */
struct ak4490_priv {
	struct snd_soc_codec codec;
	u8 reg_cache[AK4490_MAX_REGISTERS];
	int fs1;         // Sampling Frequency
	int nBickFreq;   //  0: 48fs for 24bit,  1: 64fs or more for 32bit
	int nDSDSel;
    unsigned int dac_reset;
    unsigned int dac_mute;
    unsigned int am_power;
    unsigned int am_power_status;
    struct i2c_client* client_l;
    struct i2c_client* client_r;
};

static struct ak4490_priv* gpak4490=NULL;
// static struct snd_soc_codec *ak4490_codec;
// static struct ak4490_priv *ak4490_data;
#if 1
static void ak4490_work_func(struct work_struct *work)
{

    printk("ak4490_work_func \n");


    gpio_direction_output(gpak4490->dac_mute,DAC_MUTE_ACTIVE);
    link_set_status(LINK_MUTE_ON);
    msleep(10);
    ak4490_set_am_power(0);
    link_stop(0);
    wake_unlock(&audiolink_wakelock);

}

static DECLARE_DELAYED_WORK(ak4490_delay_work, ak4490_work_func);
#endif
/* ak4490 register cache & default register settings */
static const u8 ak4490_reg[AK4490_MAX_REGISTERS] = {
	0x04,	/*	0x00	AK4490_00_CONTROL1			*/
	0x22,	/*	0x01	AK4490_01_CONTROL2			*/
	0x08,	/*	0x02	AK4490_02_CONTROL3			*/
	0xFF,	/*	0x03	AK4490_03_LCHATT			*/
	0xFF,	/*	0x04	AK4490_04_RCHATT			*/
	0x00,	/*	0x05	AK4490_05_CONTROL4			*/
	0x00,	/*	0x06	AK4490_06_CONTROL5			*/
	0x00,	/*	0x07	AK4490_07_CONTROL6			*/
	0x00,	/*	0x08	AK4490_08_CONTROL7			*/
	0x00,	/*	0x09	AK4490_09_CONTROL8			*/
};

static const struct {
	int readable;   /* Mask of readable bits */
	int writable;   /* Mask of writable bits */
} ak4490_access_masks[] = {
    { 0xFF, 0xEF },	//0x00
    { 0xFF, 0xFF },	//0x01
    { 0xFF, 0xBF },	//0x02
    { 0xFF, 0xFF },	//0x03
    { 0xFF, 0xFF },	//0x04
    { 0xFF, 0xC3 },	//0x05
    { 0xFF, 0xFB },	//0x06
    { 0xFF, 0x01 },	//0x07
    { 0xFF, 0x03 },	//0x08
    { 0xFF, 0x03 },	//0x09
};

/* Volume control:
 * from -127 to 0 dB in 0.5 dB steps (mute instead of -127.5 dB) */
static DECLARE_TLV_DB_SCALE(latt_tlv, -12750, 50, 0);
static DECLARE_TLV_DB_SCALE(ratt_tlv, -12750, 50, 0);

static const char *ak4490_ecs_select_texts[] = {"768kHz", "384kHz"};

static const char *ak4490_dem_select_texts[] = {"44.1kHz", "OFF", "48kHz", "32kHz"};
static const char *ak4490_dzfm_select_texts[] = {"Separated", "ANDed"};

static const char *ak4490_sellr_select_texts[] = {"Rch", "Lch"};
static const char *ak4490_dckb_select_texts[] = {"Falling", "Rising"};
static const char *ak4490_dcks_select_texts[] = {"512fs", "768fs"};

static const char *ak4490_dsdd_select_texts[] = {"Normal", "Volume Bypass"};

static const char *ak4490_sc_select_texts[] = {"Setting 1", "Setting 2", "Setting 3"};
static const char *ak4490_dsdf_select_texts[] = {"50kHz", "150kHz"};


static const struct soc_enum ak4490_dac_enum[] = {
	SOC_ENUM_SINGLE(AK4490_00_CONTROL1, 5,
			ARRAY_SIZE(ak4490_ecs_select_texts), ak4490_ecs_select_texts),

	SOC_ENUM_SINGLE(AK4490_01_CONTROL2, 1,
			ARRAY_SIZE(ak4490_dem_select_texts), ak4490_dem_select_texts),
	SOC_ENUM_SINGLE(AK4490_01_CONTROL2, 6,
			ARRAY_SIZE(ak4490_dzfm_select_texts), ak4490_dzfm_select_texts),

	SOC_ENUM_SINGLE(AK4490_02_CONTROL3, 1,
			ARRAY_SIZE(ak4490_sellr_select_texts), ak4490_sellr_select_texts),
	SOC_ENUM_SINGLE(AK4490_02_CONTROL3, 4,
			ARRAY_SIZE(ak4490_dckb_select_texts), ak4490_dckb_select_texts),
	SOC_ENUM_SINGLE(AK4490_02_CONTROL3, 5,
			ARRAY_SIZE(ak4490_dcks_select_texts), ak4490_dcks_select_texts),

	SOC_ENUM_SINGLE(AK4490_06_CONTROL5, 1,
			ARRAY_SIZE(ak4490_dsdd_select_texts), ak4490_dsdd_select_texts),

	SOC_ENUM_SINGLE(AK4490_08_CONTROL7, 0,
			ARRAY_SIZE(ak4490_sc_select_texts), ak4490_sc_select_texts),

	SOC_ENUM_SINGLE(AK4490_09_CONTROL8, 1,
			ARRAY_SIZE(ak4490_dsdf_select_texts), ak4490_dsdf_select_texts),

};

static const char *ak4490_dsdsel_select_texts[] = {"2.8224MHz", "5.6448MHz", "11.2896MHz", "22.5792MHz"};
static const char *ak4490_bickfreq_select[] = {"48fs", "64fs"};

static const struct soc_enum ak4490_dac_enum2[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ak4490_dsdsel_select_texts), ak4490_dsdsel_select_texts), 
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ak4490_bickfreq_select), ak4490_bickfreq_select), 
};

static inline u32 ak4490_read_reg_cache(struct snd_soc_codec *, u16);

static int ak4490_get_dsdsel(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);

    ucontrol->value.enumerated.item[0] = ak4490->nDSDSel;

    return 0;
}

static int ak4490_set_dsdsel(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);

	ak4490->nDSDSel = ucontrol->value.enumerated.item[0];

	if ( ak4490->nDSDSel == 0 ) { 	//  2.8224MHz
		snd_soc_update_bits(codec, AK4490_06_CONTROL5, 0x01, 0x00);
		snd_soc_update_bits(codec, AK4490_09_CONTROL8, 0x01, 0x00);
	}
	else if( ak4490->nDSDSel == 1 ) {	// 5.6448MHz
		snd_soc_update_bits(codec, AK4490_06_CONTROL5, 0x01, 0x01);
		snd_soc_update_bits(codec, AK4490_09_CONTROL8, 0x01, 0x00);
	}
	else /* if ( ak4490->nDSDSel == 2 ) */ {	// 11.2896MHz
		snd_soc_update_bits(codec, AK4490_06_CONTROL5, 0x01, 0x00);
		snd_soc_update_bits(codec, AK4490_09_CONTROL8, 0x01, 0x01);
	}/*
	else {					// 22.5792MHz
		snd_soc_update_bits(codec, AK4490_06_CONTROL5, 0x01, 0x01);
		snd_soc_update_bits(codec, AK4490_09_CONTROL8, 0x01, 0x01);
	}*/
	return 0;
}

static int ak4490_get_bickfs(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);

    ucontrol->value.enumerated.item[0] = ak4490->nBickFreq;

    return 0;
}

static int ak4490_set_bickfs(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);
    
	ak4490->nBickFreq = ucontrol->value.enumerated.item[0];
	
    return 0;
}

#ifdef AK4490_DEBUG

static const char *test_reg_select[]   = 
{
    "read AK4490 Reg 00:09",
};

static const struct soc_enum ak4490_enum[] = 
{
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(test_reg_select), test_reg_select),
};

static int nTestRegNo = 0;

static int get_test_reg(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    /* Get the current output routing */
    ucontrol->value.enumerated.item[0] = nTestRegNo;

    return 0;
}

static int set_test_reg(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    u32    currMode = ucontrol->value.enumerated.item[0];
	int    i, value;
	int	   regs, rege;

	nTestRegNo = currMode;

	regs = 0x00;
	rege = 0x09;

	for ( i = regs ; i <= rege ; i++ ){
		value = snd_soc_read(codec, i);
		printk("***AK4490 Addr,Reg=(%x, %x)\n", i, value);
	}

	return 0;
}
#endif

static const char *am_mute_select[] = {"mute_off","mute_on"};
static const char *digital_filter_select[] = {"sharp_roll-off",
                                              "slow_roll-off",
                                              "short_delay_sharp_roll-off",
                                              "short_delay_slow_roll-off",
                                              "super_slow_roll-off"};

static const struct soc_enum am_mute_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(am_mute_select), am_mute_select),
};

static const struct soc_enum digital_filter_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(digital_filter_select), digital_filter_select),
};


static DECLARE_TLV_DB_SCALE(ak4490_digital_volume_tlv, -12700, 50, 1);

/* ak4490 dapm widgets */
static const struct snd_soc_dapm_widget ak4490_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("AK4490 DAC", "NULL", AK4490_00_CONTROL1, 0, 0),
	SND_SOC_DAPM_AIF_IN("AK4490 SDTI", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("AK4490 AOUT"),
};

static const struct snd_soc_dapm_route ak4490_intercon[] = 
{
	{"AK4490 DAC", "NULL", "AK4490 SDTI"},
	{"AK4490 AOUT", "NULL", "AK4490 DAC"},
};

# if 1
int snd_soc_get_volsw_lpf(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = ak4490_get_lpf_type();

	return 0;
}

int snd_soc_put_volsw_lpf(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);


    ak4490_set_lpf((enum lpf_type)ucontrol->value.integer.value[0]);

	return 0;
}


static int ak4490_get_lpf_control(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);

    ucontrol->value.enumerated.item[0] = ak4490_get_lpf_type();



    return 0;
}

static int ak4490_set_lpf_control(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);




    ak4490_set_lpf((enum lpf_type)ucontrol->value.enumerated.item[0]);

    	return 0;
}


static int ak4490_get_am_hp_mute(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);

    ucontrol->value.enumerated.item[0] = link_get_status(LINK_MUTE_ON);



    return 0;
}

static int ak4490_set_am_hp_mute(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);



    if(ucontrol->value.enumerated.item[0]){

        ak4490_soft_mute(1);
        msleep(40);

        gpio_direction_output(ak4490->dac_mute,DAC_MUTE_ACTIVE);
        link_set_status(LINK_MUTE_ON);
    } else{
        ak4490_soft_mute(0);
        gpio_direction_output(ak4490->dac_mute,DAC_MUTE_UNACTIVE);
        link_set_status(LINK_MUTE_OFF);
    }

	return 0;
}

void set_codec_to_mute(bool on)
{
        if (on) {
            ak4490_soft_mute(1);
            msleep(40);

            gpio_direction_output(gpak4490->dac_mute,DAC_MUTE_ACTIVE);
            link_set_status(LINK_MUTE_ON);
        } else {
            ak4490_soft_mute(0);
            gpio_direction_output(gpak4490->dac_mute,DAC_MUTE_UNACTIVE);
            link_set_status(LINK_MUTE_OFF);
        }
}
EXPORT_SYMBOL_GPL(set_codec_to_mute);

static int snd_soc_get_volsw_ak4490(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	int mask = (1 << fls(max)) - 1;
    u8 dac;

    if(strstr(kcontrol->id.name, " Lch"))
        dac='L';
    else
        dac='R';


	ucontrol->value.integer.value[0] =
        (ak4490_read_i2c_lr(dac,reg)>> shift) & mask;
		/* (snd_soc_read(codec, reg) >> shift) & mask; */
	if (ucontrol->value.integer.value[0])
		ucontrol->value.integer.value[0] =
			max + 1 - ucontrol->value.integer.value[0];

	if (shift != rshift) {
		ucontrol->value.integer.value[1] =
            (ak4490_read_i2c_lr(dac,reg)>> shift) & mask;
			/* (snd_soc_read(codec, reg) >> rshift) & mask; */
		if (ucontrol->value.integer.value[1])
			ucontrol->value.integer.value[1] =
				max + 1 - ucontrol->value.integer.value[1];
	}

	return 0;
}

static int snd_soc_put_volsw_ak4490(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	int mask = (1 << fls(max)) - 1;
	unsigned short val, val2, val_mask;
    u8 dac;

    if(strstr(kcontrol->id.name, " Lch"))
        dac='L';
    else
        dac='R';


	val = (ucontrol->value.integer.value[0] & mask);

	val_mask = mask << shift;
	if (val)
		val = max + 1 - val;
	val = val << shift;
	if (shift != rshift) {
		val2 = (ucontrol->value.integer.value[1] & mask);
		val_mask |= mask << rshift;
		if (val2)
			val2 = max + 1 - val2;
		val |= val2 << rshift;
	}
    return ak4490_update_bits_lr(dac,reg,val_mask,val);
	/* return snd_soc_update_bits(codec, reg, val_mask, val); */
}

int snd_soc_get_enum_double_ak4490(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val, bitmask;
    u8 dac;

    if(strstr(kcontrol->id.name, " Lch"))
        dac='L';
    else
        dac='R';

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
    val=ak4490_read_i2c_lr(dac,e->reg);
	/* val = snd_soc_read(codec, e->reg); */
	ucontrol->value.enumerated.item[0]
		= (val >> e->shift_l) & (bitmask - 1);
    /* printk("ct7302  reg %x val %x  value %x \n",e->reg,val,ucontrol->value.enumerated.item[0]); */
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] =
			(val >> e->shift_r) & (bitmask - 1);

	return 0;
}
int snd_soc_put_enum_double_ak4490(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val;
	unsigned int mask, bitmask;
    u8 dac;

    if(strstr(kcontrol->id.name, " Lch"))
        dac='L';
    else
        dac='R';

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->max - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= (bitmask - 1) << e->shift_r;
	}

    return ak4490_update_bits_lr(dac,e->reg,mask,val);
	/* return snd_soc_update_bits_locked(codec, e->reg, mask, val); */
}

int snd_soc_get_volsw_2r_ak4490(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
    char dac;

    if(strstr(kcontrol->id.name, " Lch"))
        dac='L';
    else
        dac='R';


	ucontrol->value.integer.value[0] =
        (ak4490_read_i2c_lr(dac, reg) >> shift) & mask;
		/* (snd_soc_read(codec, reg) >> shift) & mask; */
	ucontrol->value.integer.value[1] =
        (ak4490_read_i2c_lr(dac, reg2) >> shift) & mask;
		/* (snd_soc_read(codec, reg2) >> shift) & mask; */
	if (invert) {
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] =
			max - ucontrol->value.integer.value[1];
	}

	return 0;
}

int snd_soc_put_volsw_2r_ak4490(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	int err;
	unsigned int val, val2, val_mask;
    u8 dac;

    if(strstr(kcontrol->id.name, " Lch"))
        dac='L';
    else
        dac='R';

	val_mask = mask << shift;
	val = (ucontrol->value.integer.value[0] & mask);
	val2 = (ucontrol->value.integer.value[1] & mask);

	if (invert) {
		val = max - val;
		val2 = max - val2;
	}

	val = val << shift;
	val2 = val2 << shift;

	/* err = snd_soc_update_bits_locked(codec, reg, val_mask, val); */
	err = ak4490_update_bits_lr(dac, reg, val_mask, val);
	if (err < 0)
		return err;

	/* err = snd_soc_update_bits_locked(codec, reg2, val_mask, val2); */
	err = ak4490_update_bits_lr(dac, reg2, val_mask, val2);
	return err;
}


#endif
static const struct snd_kcontrol_new ak4490_snd_controls[] = {
#if 0
	SOC_SINGLE_TLV("AK4490 Lch Digital Volume",
			AK4490_03_LCHATT, 0, 0xFF, 0, latt_tlv),
	SOC_SINGLE_TLV("AK4490 Rch Digital Volume",
			AK4490_04_RCHATT, 0, 0xFF, 0, ratt_tlv),

	SOC_ENUM("AK4490 EX DF I/F clock", ak4490_dac_enum[0]), 
	SOC_ENUM("AK4490 De-emphasis Response", ak4490_dac_enum[1]), 
	SOC_ENUM("AK4490 Data Zero Detect Mode", ak4490_dac_enum[2]),
	SOC_ENUM("AK4490 Data Selection at Mono Mode", ak4490_dac_enum[3]), 

	SOC_ENUM("AK4490 Polarity of DCLK", ak4490_dac_enum[4]),
	SOC_ENUM("AK4490 DCKL Frequency", ak4490_dac_enum[5]),

	SOC_ENUM("AK4490 DDSD Play Back Path", ak4490_dac_enum[6]),
	SOC_ENUM("AK4490 Sound control", ak4490_dac_enum[7]),
	SOC_ENUM("AK4490 Cut Off of DSD Filter", ak4490_dac_enum[8]),

	SOC_ENUM_EXT("AK4490 DSD Data Stream", ak4490_dac_enum2[0], ak4490_get_dsdsel, ak4490_set_dsdsel),
	SOC_ENUM_EXT("AK4490 BICK Frequency Select", ak4490_dac_enum2[1], ak4490_get_bickfs, ak4490_set_bickfs),

	SOC_SINGLE("AK4490 External Digital Filter", AK4490_00_CONTROL1, 6, 1, 0),
	SOC_SINGLE("AK4490 MCLK Frequncy Auto Setting", AK4490_00_CONTROL1, 7, 1, 0),
	SOC_SINGLE("AK4490 Soft Mute Control", AK4490_01_CONTROL2, 0, 1, 0),
	SOC_SINGLE("AK4490 Short delay sharp filter", AK4490_01_CONTROL2, 5, 1, 0),
	SOC_SINGLE("AK4490 Data Zero Detect Enable", AK4490_01_CONTROL2, 7, 1, 0),
	SOC_SINGLE("AK4490 Slow Roll-off Filter", AK4490_02_CONTROL3, 0, 1, 0),
	SOC_SINGLE("AK4490 Invering Enable of DZF", AK4490_02_CONTROL3, 4, 1, 0),
	SOC_SINGLE("AK4490 Mono Mode", AK4490_02_CONTROL3, 3, 1, 0),
	SOC_SINGLE("AK4490 Super Slow Roll-off Filter", AK4490_05_CONTROL4, 0, 1, 0),
	SOC_SINGLE("AK4490 AOUTR Phase Inverting", AK4490_05_CONTROL4, 6, 1, 0),
	SOC_SINGLE("AK4490 AOUTL Phase Inverting", AK4490_05_CONTROL4, 7, 1, 0),
	SOC_SINGLE("AK4490 DSD Mute Release", AK4490_06_CONTROL5, 3, 1, 0),
	SOC_SINGLE("AK4490 DSD Mute Control Hold", AK4490_06_CONTROL5, 4, 1, 0),
	SOC_SINGLE("AK4490 DSDR is detected", AK4490_06_CONTROL5, 5, 1, 0),
	SOC_SINGLE("AK4490 DSDL is detected", AK4490_06_CONTROL5, 6, 1, 0),
	SOC_SINGLE("AK4490 DSD Data Mute", AK4490_06_CONTROL5, 7, 1, 0),
	SOC_SINGLE("AK4490 Synchronization Control", AK4490_07_CONTROL6, 0, 1, 0),


#ifdef AK4490_DEBUG
	SOC_ENUM_EXT("Reg Read", ak4490_enum[0], get_test_reg, set_test_reg),
#endif

#else
	SOC_DOUBLE_R_EXT_TLV_AK4490("AK4490 Lch Digital Volume",
		AK4490_03_LCHATT,AK4490_04_RCHATT, 0, 0XFF, 0,
        snd_soc_get_volsw_2r_ak4490,snd_soc_put_volsw_2r_ak4490, ak4490_digital_volume_tlv),

	SOC_DOUBLE_R_EXT_TLV_AK4490("AK4490 Rch Digital Volume",
		AK4490_03_LCHATT,AK4490_04_RCHATT, 0, 0XFF, 0,
        snd_soc_get_volsw_2r_ak4490,snd_soc_put_volsw_2r_ak4490, ak4490_digital_volume_tlv),


	SOC_ENUM_EXT_AK4490("AM HP Mute", am_mute_enum, ak4490_get_am_hp_mute, ak4490_set_am_hp_mute),

	SOC_ENUM_EXT_AK4490("digital filter", digital_filter_enum, ak4490_get_lpf_control, ak4490_set_lpf_control),

    SOC_SINGLE_EXT_AK4490("DAC Digital Filter Mode",0,0,0x04,0,snd_soc_get_volsw_lpf,snd_soc_put_volsw_lpf),
#endif


};


/* enum lpf_type { */
    /* LPF_SHARP_ROLL_OFF=0, */
    /* LPF_SLOW_ROLL_OFF, */
    /* LPF_DELAY_SHARP_ROLL_OFF, */
    /* LPF_DELAY_SLOW_ROLL_OFF, */
    /* LPF_SUPPER_SLOW_ROLL_OFF, */
/* }; */

void ak4490_set_am_power(int on)
{
    if(!gpak4490){
        printk("%s %d  gpak4490 is NULL !!\n",__func__,__LINE__);
        return;
    }

    printk("%s %d  %d !!\n",__func__,__LINE__,on);
    if(on && gpak4490->am_power_status==AM_POWER_UNACTIVE){
        gpio_direction_output(gpak4490->am_power,AM_POWER_ACTIVE);
        gpak4490->am_power_status=AM_POWER_ACTIVE;
    }else if(!on && gpak4490->am_power_status==AM_POWER_ACTIVE){
        gpio_direction_output(gpak4490->am_power,AM_POWER_UNACTIVE);
        gpak4490->am_power_status=AM_POWER_UNACTIVE;
    }else{

        printk("%s : unblanced power operation !!! failed %d !!\n",__func__,on);
    }
}
void ak4490_set_lo_mute(int mute)
{
    if(!gpak4490){
        printk("%s %d  gpak4490 is NULL !!\n",__func__,__LINE__);
        return;
    }

    if(mute){
        printk("lo set mute \n");
        gpio_direction_output(gpak4490->dac_mute,DAC_MUTE_ACTIVE);
    }else{
        printk("lo set unmute \n");
        gpio_direction_output(gpak4490->dac_mute,DAC_MUTE_UNACTIVE);
    }
}
EXPORT_SYMBOL_GPL(ak4490_set_lo_mute);

void ak4490_set_lpf(enum lpf_type lpf)
{
    
    u8 SSLOW,SD,SLOW;

    if(lpf>LPF_SUPPER_SLOW_ROLL_OFF){
        printk("ak4490 set spf type error 0x%x \n",lpf);
        return;
    }
    gak4490_defalult_lpf=lpf;
    SSLOW=((u8)lpf &0x04)>>2 <<0;
    SD   =((u8)lpf &0x02)>>1 <<5;
    SLOW =((u8)lpf &0x01)>>0 <<0;

    akdbgprt("[AK4490] set_lpf type=%d (SSLOW=%d SD=%d SLOW=%d)\n",
             (int)lpf, SSLOW?1:0, SD?1:0, SLOW?1:0);

    ak4490_update_bits_lr('L',AK4490_05_CONTROL4,0x01,SSLOW);
    ak4490_update_bits_lr('L',AK4490_01_CONTROL2,0x20,SD);
    ak4490_update_bits_lr('L',AK4490_02_CONTROL3,0x01,SLOW);


    ak4490_update_bits_lr('R',AK4490_05_CONTROL4,0x01,SSLOW);
    ak4490_update_bits_lr('R',AK4490_01_CONTROL2,0x20,SD);
    ak4490_update_bits_lr('R',AK4490_02_CONTROL3,0x01,SLOW);

}
enum lpf_type ak4490_get_lpf_type()
{

    int ret1,ret2,ret3;
    u8 SSLOW,SD,SLOW;
    u8 type=0;

    SSLOW=0x01;
    SD   =0x01<<5;
    SLOW =0x01;



    ret1=ak4490_read_i2c_lr('L',AK4490_05_CONTROL4);
    ret2=ak4490_read_i2c_lr('L',AK4490_01_CONTROL2);
    ret3=ak4490_read_i2c_lr('L',AK4490_02_CONTROL3);


    SSLOW=((u8)ret1 & SSLOW)<<2;
    SD=(((u8)ret2 & SD)>>5)<<1;
    SLOW=(u8)ret3 & SLOW;


    type=SSLOW | SD | SLOW;

    akdbgprt("control get lpf type :0x%x \n",type);

    return (enum lpf_type) type;
}

/* Apply the digital-filter selection to both DACs when userspace writes the
 * selected_filter module param (e.g. `echo 2 > .../parameters/selected_filter`). */
static int ak4490_selected_filter_set(const char *val,
				      const struct kernel_param *kp)
{
	int f, rv;

	/* Validate before committing so a bad write leaves the current value intact. */
	rv = kstrtoint(val, 0, &f);
	if (rv)
		return rv;
	if (f < LPF_SHARP_ROLL_OFF || f > LPF_SUPPER_SLOW_ROLL_OFF)
		return -EINVAL;
	selected_filter = f;
	if (gpak4490)		/* codec probed: apply now (else next write applies it) */
		ak4490_set_lpf((enum lpf_type)f);
	return 0;
}

static const struct kernel_param_ops ak4490_selected_filter_ops = {
	.set = ak4490_selected_filter_set,
	.get = param_get_int,
};
module_param_cb(selected_filter, &ak4490_selected_filter_ops,
		&selected_filter, 0644);

static void ak4490_set_mono_lr()
{
    

    ak4490_update_bits_lr('L',AK4490_02_CONTROL3,0x0A,0x08);
    ak4490_update_bits_lr('R',AK4490_02_CONTROL3,0x0A,0x0A);

}
static void ak4490_soft_mute(int on)
{
    static u8 vol_R1,vol_R2,vol_L1,vol_L2;
    static int ret=0;

    if(on){

        /* ret+=vol_R1=ak4490_read_i2c_lr('R',AK4490_03_LCHATT); */
        /* ret+=vol_R2=ak4490_read_i2c_lr('R',AK4490_04_RCHATT); */
        /* ret+=vol_L1=ak4490_read_i2c_lr('L',AK4490_03_LCHATT); */
        /* ret+=vol_L2=ak4490_read_i2c_lr('L',AK4490_04_RCHATT); */


        /* ret+=ak4490_write_i2c_lr('R',AK4490_03_LCHATT,0); */
        /* ret+=ak4490_write_i2c_lr('R',AK4490_04_RCHATT,0); */
        /* ret+=ak4490_write_i2c_lr('L',AK4490_03_LCHATT,0); */
        /* ret+=ak4490_write_i2c_lr('L',AK4490_04_RCHATT,0); */
        ak4490_update_bits_lr2(AK4490_01_CONTROL2,0x01,0x01);

        if(ret){
            ret=0;
            printk("mute on is not succeeded!! \n");
            return ;
        }
    }else{

        if(ret){
            ret=0;
            printk("mute on is not succeeded!! mute off not execute \n");
            return ;
        }

        ak4490_update_bits_lr2(AK4490_01_CONTROL2,0x01,0x00);
        /* ak4490_write_i2c_lr('R',AK4490_03_LCHATT,vol_R1); */
        /* ak4490_write_i2c_lr('R',AK4490_04_RCHATT,vol_R2); */
        /* ak4490_write_i2c_lr('L',AK4490_03_LCHATT,vol_L1); */
        /* ak4490_write_i2c_lr('L',AK4490_04_RCHATT,vol_L2); */

    }




}

static int ak4490_startup(struct snd_pcm_substream *substream,
                          struct snd_soc_dai *dai)
{
    int ret=0,count=10;
    struct snd_soc_codec *codec = dai->codec;
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);

	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);
   ret=work_busy((struct work_struct*)&ak4490_delay_work);
    if(ret&WORK_BUSY_PENDING){
        printk(KERN_WARNING "ak4490 work_func WORK_BUSY_PENDING cancel it\n");
        cancel_delayed_work_sync(&ak4490_delay_work);
        wake_unlock(&audiolink_wakelock);
        /* return 0; */
    }
    if(ret&WORK_BUSY_RUNNING){

        printk("ak4490 work_func WORK_BUSY_RUNNING wait it to finish\n");
        flush_delayed_work(&ak4490_delay_work);

    }

    /* link_start(); */

    if(ak4490->am_power_status != AM_POWER_ACTIVE){

        link_set_status(LINK_MCU_WORK);
        link_set_status(LINK_MUTE_ON);
        gpio_direction_output(ak4490->dac_mute,DAC_MUTE_ACTIVE);
        msleep(10);
        ak4490_set_am_power(1);
        /* msleep(1000); */
    }

    link_start();
    
    if(!first_play){

        first_play=1;

        ak4490_soft_mute(1);
        gpio_direction_output(ak4490->dac_mute,DAC_MUTE_ACTIVE);
        link_set_status(LINK_MUTE_ON);
        msleep(200);

    }

    return 0;

}
static int ak4490_shutdown(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
    int ret=0;
    struct snd_soc_codec *codec = dai->codec;
	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);

    first_play=0;
    wake_lock(&audiolink_wakelock);
    schedule_delayed_work(&ak4490_delay_work,msecs_to_jiffies(AUDIO_LINK_SHUTDOWN_DELAY * 1000));

    return ret;
}

static int ak4490_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);

	u8 	dfs;
	u8  dfs2;
    u8 dsdsel1,dsdsel0;
	int nfs1;

	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);

#ifdef CONFIG_SND_SOC_AK4490_CUSTOM_DRIVER
        nfs1 = oversampling_freq;
#else
        nfs1 = params_rate(params);
#endif
//	nfs1 = params_rate(params);
	ak4490->fs1 = nfs1;

	dfs = snd_soc_read(codec, AK4490_01_CONTROL2);
	dfs &= ~AK4490_DFS;
	
	dfs2 = snd_soc_read(codec, AK4490_05_CONTROL4);
	dfs2 &= ~AK4490_DFS2;

	switch (nfs1) {
		case 32000:
		case 44100:
		case 48000:
			dfs |= AK4490_DFS_48KHZ;
			dfs2 |= AK4490_DFS2_48KHZ;
			break;
		case 64000:
		case 88200:
		case 96000:
			dfs |= AK4490_DFS_96KHZ;
			dfs2 |= AK4490_DFS2_48KHZ;
            dsdsel0=0;
            dsdsel1=0;
			break;
		case 176400:
		case 192000:
			dfs |= AK4490_DFS_192KHZ;
			dfs2 |= AK4490_DFS2_48KHZ;
            dsdsel0=1;
            dsdsel1=0;
			break;
		case 352800:
		case 384000:
			dfs |= AK4490_DFS_384KHZ;
			dfs2 |= AK4490_DFS2_384KHZ;
            dsdsel0=0;
            dsdsel1=1;
			break;
		case 705600:
		case 768000:
			dfs |= AK4490_DFS_768KHZ;
			dfs2 |= AK4490_DFS2_384KHZ;
	 //   dsdsel0=1;
         //   dsdsel1=1;
			break;
		default:
			return -EINVAL;
	}

	akdbgprt("\t[AK4490] %s(%d)  Sampling rate %d\n",__FUNCTION__,__LINE__,nfs1 ,dfs);

    /* gpio_direction_output(ak4490->dac_mute,DAC_MUTE_ACTIVE); */

#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER)
    /* ct7302_rate_set(params_rate(params)); */

    /* ak4490_soft_mute(1); */
    /* msleep(50); */
    // DSD switch need set dia to PDN see pdf P32 DSD Mode
#ifdef DSD_SWITCH_PDN
    if(audio_fpga_current_mode(FMT_DSD))
        gpio_direction_output(ak4490->dac_reset,DAC_RESET_ACTIVE);
#endif

    audioBridge_rate_set(params_rate(params),fpga_fmt);
    msleep(5);

#ifdef DSD_SWITCH_PDN
    /* if(audio_fpga_current_mode(FMT_DSD)){ */
        gpio_direction_output(ak4490->dac_reset,DAC_RESET_UNACTIVE);
        msleep(5);
    /* } */
#endif

    //reset internal timing circuit 
    if(fpga_fmt == FMT_PCM){
        ak4490_update_bits_lr2(AK4490_00_CONTROL1,0x01,0x00);
        msleep(5);
        ak4490_update_bits_lr2(AK4490_00_CONTROL1,0x01,0x01);
    }


    /* ak4490_soft_mute(0); */

#ifdef POP_DEBUG
    printk("%s %d FPGA CMD\n",__FUNCTION__,__LINE__);
    msleep(2000);
#endif
#endif
    if(fpga_fmt == FMT_DSD){

        // set DSD Sampling rate
        /* ak4490_soft_mute(1); */
        msleep(50);
        ak4490_update_bits_lr('L',AK4490_06_CONTROL5,0x01,dsdsel0);
        ak4490_update_bits_lr('L',AK4490_09_CONTROL8,0x01,dsdsel1);
        ak4490_update_bits_lr('R',AK4490_06_CONTROL5,0x01,dsdsel0);
        ak4490_update_bits_lr('R',AK4490_09_CONTROL8,0x01,dsdsel1);

#ifdef POP_DEBUG
    printk(" %s %d DSD set rate\n",__FUNCTION__,__LINE__);
    msleep(2000);
#endif
        msleep(50);
        /* ak4490_soft_mute(0); */

    }else{
        /* ak4490_soft_mute(1); */
        msleep(50);
        snd_soc_write(codec, AK4490_01_CONTROL2, dfs);
        snd_soc_write(codec, AK4490_05_CONTROL4, dfs2);

#ifdef POP_DEBUG
    printk(" %s %d PCM set rate\n",__FUNCTION__,__LINE__);
    msleep(2000);
#endif
        msleep(200);
        /* ak4490_soft_mute(0); */
    }
    /* gpio_direction_output(ak4490->dac_mute,DAC_MUTE_UNACTIVE); */

	return 0;
}

static int ak4490_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
//	struct snd_soc_codec *codec = dai->codec;

	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);


	return 0;
}

static int ak4490_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);
	u8 format;
	u8 format2;

	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);

	/* set master/slave audio interface */
	format = snd_soc_read(codec, AK4490_00_CONTROL1);
	format &= ~AK4490_DIF;

	format2 = snd_soc_read(codec, AK4490_02_CONTROL3);
	format2 &= ~AK4490_DIF_DSD;

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
		case SND_SOC_DAIFMT_IB_IF:
		case SND_SOC_DAIFMT_IB_NF:
		case SND_SOC_DAIFMT_NB_IF:
			break;
	}

    /* master or slave */
    switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
        case SND_SOC_DAIFMT_CBS_CFS:
            break;
        case SND_SOC_DAIFMT_CBM_CFM:
            break;
        case SND_SOC_DAIFMT_CBS_CFM:
        case SND_SOC_DAIFMT_CBM_CFS:
        default:
            dev_err(codec->dev, "Clock mode unsupported FMT 0x%x",fmt & SND_SOC_DAIFMT_MASTER_MASK);
           return -EINVAL;
    }

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			format |= AK4490_DIF_I2S_MODE;
            		fpga_fmt=FMT_PCM;
			if ( ak4490->nBickFreq == 1 ) 	format |= AK4490_DIF_32BIT_MODE;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			format |= AK4490_DIF_MSB_MODE;
            		fpga_fmt=FMT_PCM;
			if ( ak4490->nBickFreq == 1 ) 	format |= AK4490_DIF_32BIT_MODE;
			break;
		case SND_SOC_DAIFMT_DSD:
			format2 |= AK4490_DIF_DSD_MODE;
            		fpga_fmt=FMT_DSD;
			break;
		case SND_SOC_DAIFMT_SOP:
            		fpga_fmt=FMT_SOP;
			break;
		default:
            		fpga_fmt=FMT_PCM;
			return -EINVAL;
	}


	snd_soc_write(codec, AK4490_00_CONTROL1, format);
	snd_soc_write(codec, AK4490_02_CONTROL3, format2);
#ifdef POP_DEBUG
	/* set format */
    printk(" %s %d Write format \n",__FUNCTION__,__LINE__);
    msleep(2000);
#endif

    ak4490_set_lpf(gak4490_defalult_lpf);
    /* set mono mode L R  for two DAC */
    ak4490_set_mono_lr();
#ifdef POP_DEBUG
    printk(" %s %d set lpf\n",__FUNCTION__,__LINE__);
    msleep(2000);
#endif

    // disable de emphasis  
    ak4490_update_bits_lr2(AK4490_01_CONTROL2,0x06,0x02);

    /* ak4490_update_bits_lr('R',AK4490_01_CONTROL2,0x06,0x02); */

    //enable zero detect
    ak4490_update_bits_lr2(AK4490_01_CONTROL2,0xC0,0xC0);
    ak4490_update_bits_lr2(AK4490_02_CONTROL3,0x04,0x00);

    //disable output phase inverting
    ak4490_update_bits_lr2(AK4490_05_CONTROL4,0xC0,0x00);

    //enalbe dsd mute
    ak4490_update_bits_lr2(AK4490_06_CONTROL5,0x90,0x80);
    // DSD mclk 512fs,Falling edge
    ak4490_update_bits_lr2(AK4490_02_CONTROL3,0x30,0x00);

#ifdef POP_DEBUG
    printk(" %s %d  disable emphasis\n",__FUNCTION__,__LINE__);
    msleep(2000);
#endif

#ifdef POP_DEBUG
    printk(" %s %d \n",__FUNCTION__,__LINE__);
    msleep(2000);
#endif
	return 0;
}

static int ak4490_volatile(struct snd_soc_codec *codec, unsigned int reg)
{
	int	ret;

	switch (reg) {
//		case :
//			ret = 1;
		default:
			ret = 0;
			break;
	}
	return ret;
}

static int ak4490_readable(struct snd_soc_codec *codec, unsigned int reg)
{

	if (reg >= ARRAY_SIZE(ak4490_access_masks))
		return 0;
	return ak4490_access_masks[reg].readable != 0;
}

/*
* Read ak4490 register cache
 */
static inline u32 ak4490_read_reg_cache(struct snd_soc_codec *codec, u16 reg)
{
    u8 *cache = codec->reg_cache;
    BUG_ON(reg > ARRAY_SIZE(ak4490_reg));
    return (u32)cache[reg];
}

#ifdef AK4490_CONTIF_DEBUG
/*
 * Write ak4490 register cache
 */
static inline void ak4490_write_reg_cache(
struct snd_soc_codec *codec, 
u16 reg,
u16 value)
{
    u8 *cache = codec->reg_cache;
    BUG_ON(reg > ARRAY_SIZE(ak4490_reg));
    cache[reg] = (u8)value;
}

unsigned int ak4490_i2c_read(struct snd_soc_codec *codec, unsigned int reg)
{

	int ret,ret1;

	ret = i2c_smbus_read_byte_data(codec->control_data, (u8)(reg & 0xFF));
	/* ret1 = i2c_smbus_read_byte_data(gpak4490->client_r, (u8)(reg & 0xFF)); */

	if (ret < 0) {
		akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);
	}
	return ret;
}

static int ak4490_i2c_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	ak4490_write_reg_cache(codec, reg, value);

	akdbgprt("\t[ak4490] %s: (addr,data)=(%x, %x)\n",__FUNCTION__, reg, value);

    /* if(i2c_smbus_write_byte_data(codec->control_data, (u8)(reg & 0xFF), (u8)(value & 0xFF))<0) { */
        /* akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__); */
        /* return EIO; */
    /* } */
	
    if(gpak4490 && gpak4490->client_l && gpak4490->client_r){
        i2c_smbus_write_byte_data(gpak4490->client_l, (u8)(reg & 0xFF), (u8)(value & 0xFF));
        i2c_smbus_write_byte_data(gpak4490->client_r, (u8)(reg & 0xFF), (u8)(value & 0xFF));
    }else
        printk("%s  gpak4490 client is not ready !!!\n",__func__);

	return 0;
}
#endif
int ak4490_read_i2c_blk(u8 reg, u8* data,u8 len)
{
    int ret1,ret2;

    ret1=i2c_smbus_read_i2c_block_data(gpak4490->client_l,reg,len,data);
    ret2=i2c_smbus_read_i2c_block_data(gpak4490->client_r,reg,len,data+len);
    return ret1+ret2<<8;
}
EXPORT_SYMBOL_GPL(ak4490_read_i2c_blk);
int ak4490_write_i2c_lr(u8 lr,u8 reg,u8 data)
{
    int ret1,ret2;

    if(gpak4490 && gpak4490->client_l && gpak4490->client_r){
        if(lr == 'L' || lr == 'l'){
            ret1=i2c_smbus_write_byte_data(gpak4490->client_l, reg,data);
        }else if(lr == 'R' || lr == 'r'){

            ret1=i2c_smbus_write_byte_data(gpak4490->client_r, reg,data);
        }else{

            printk(KERN_WARNING "ak4490_write_i2c_lr get error %c \n",lr);

        }
    }else
        printk("%s  gpak4490 client is not ready for all L R!!!\n",__func__);
    return ret1;
}
EXPORT_SYMBOL_GPL(ak4490_write_i2c_lr);
int ak4490_read_i2c_lr(u8 lr,u8 reg)
{
    int ret1,ret2;

    if(gpak4490 && gpak4490->client_l && gpak4490->client_r){
        if(lr == 'L' || lr == 'l'){
            ret1=i2c_smbus_read_byte_data(gpak4490->client_l, reg);
        }else if(lr == 'R' || lr == 'r'){

            ret1=i2c_smbus_read_byte_data(gpak4490->client_r, reg);
        }
    }else
        printk("%s  gpak4490 client is not ready for all L R!!!\n",__func__);
    return ret1;
}
EXPORT_SYMBOL_GPL(ak4490_read_i2c_lr);
/**
 * ak4490_update_bits_lr- update codec register bits R or L DAC
 * @i2c: audio i2c device 
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value.
 *
 * Returns 1 for change, 0 for no change, or negative error code.
 */
int ak4490_update_bits_lr(u8 lr, unsigned short reg,
				unsigned int mask, unsigned int value)
{
	int change;
	unsigned int old, new;
	int ret;

	/* ret = snd_soc_read(codec, reg); */
    ret = ak4490_read_i2c_lr(lr,reg);
	if (ret < 0)
		return ret;

	old = ret;
	new = (old & ~mask) | value;
	change = old != new;
	if (change) {
		/* ret = snd_soc_write(codec, reg, new); */
        ret = ak4490_write_i2c_lr(lr,reg,new);
		if (ret < 0)
			return ret;
	}

	return change;
}

int ak4490_update_bits_lr2(unsigned short reg,
				unsigned int mask, unsigned int value)
{
    int ret=0;
    ret=ak4490_update_bits_lr('L',reg,mask,value);
    if(ret<0)
        return ret;
    ret=ak4490_update_bits_lr('R',reg,mask,value);

    return ret;
}





// * for AK4490
static int ak4490_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *codec_dai)
{
	int i,data,data1,ret = 0;
    
   struct snd_soc_codec *codec = codec_dai->codec;

	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);

    /* WARN_ON(1); */
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        akdbgprt("[AK4490] Trigger CMD SNDRV_PCM_TRIGGER_START 0x%x 0x%x\n",gpak4490->client_l->addr,gpak4490->client_r->addr);
        
            /* AudioBridge_enable(); */
       break;
    case SNDRV_PCM_TRIGGER_STOP:

        /* link_set_status(LINK_MUTE_ON); */
            /* AudioBridge_disable(); */
        akdbgprt("[AK4490] Trigger CMD SNDRV_PCM_TRIGGER_STOP \n");
        /* do something to stop the PCM engine */
        break;
    default:
        akdbgprt("[AK4490] Trigger CMD %d \n",cmd);
        return -EINVAL;
    }

	return ret;
}


static int ak4490_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
	case SND_SOC_BIAS_STANDBY:
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

static int ak4490_set_dai_mute(struct snd_soc_dai *dai, int mute) 
{
    struct snd_soc_codec *codec = dai->codec;
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);
	int nfs, ndt, ret;
    int count=30;
	
	nfs = ak4490->fs1;
	
#ifdef POP_DEBUG
    /* msleep(2000); */
#endif
	
	akdbgprt("\t[AK4490] %s mute[%s]\n",__FUNCTION__, mute ? "ON":"OFF");
    if(fpga_fmt == FMT_DSD){


    }
	if (mute) {	//SMUTE: 1 , MUTE

        ak4490_soft_mute(1);
        gpio_direction_output(ak4490->dac_mute,DAC_MUTE_ACTIVE);
        link_set_status(LINK_MUTE_ON);
		/* ak4490_update_bits_lr2(AK4490_06_CONTROL5, 0x80, 0x80);  */
		/* ak4490_update_bits_lr2(AK4490_01_CONTROL2, 0x01, 0x01);  */
		/* snd_soc_update_bits(codec, AK4490_01_CONTROL2, 0x01, 0x01);  */
		ndt = 7424000 / nfs;
		mdelay(ndt);


	}
	else {		// SMUTE: 0 ,NORMAL operation

        // waiting mcu finish poweron sequence
        while(!link_get_status(LINK_ANALOG_POWER) && count>=0){
            msleep(100);
            count--;
        }

        if(count <0 ){
            printk("\n *************************\n");
            printk("ERROR Waiting AM power time out \n\n");
        }
        /* msleep(700); */
        /* if(pmute>=1300) */
            /* msleep(pmute-1300); */
        if(!isShutdown ){
            ak4490_soft_mute(0);
            gpio_direction_output(ak4490->dac_mute,DAC_MUTE_UNACTIVE);
            link_set_status(LINK_MUTE_OFF);
            akdbgprt("\t[AK4490] %s mute %d set unmute\n",__FUNCTION__,mute);
        }
		/* ak4490_update_bits_lr2(AK4490_01_CONTROL2, 0x01, 0x00);  */
/*
#ifdef CONFIG_SND_SOC_AK4490_CUSTOM_DRIVER
                switch (selected_filter) {
		case 1:
			ret = snd_soc_read(codec, AK4490_02_CONTROL3); // slow bit
                        ret |= 1 << 0;
                        snd_soc_write(codec, AK4490_02_CONTROL3, ret);
                        ret = snd_soc_read(codec, AK4490_01_CONTROL2); // sd bit
                        ret &= ~(1 << 5);
                        snd_soc_write(codec, AK4490_01_CONTROL2, ret);
			break;
		case 2:
			ret = snd_soc_read(codec, AK4490_02_CONTROL3);
                        ret &= ~(1 << 0);
                        snd_soc_write(codec, AK4490_02_CONTROL3, ret);
                        ret = snd_soc_read(codec, AK4490_01_CONTROL2);
                        ret |= 1 << 5;
                        snd_soc_write(codec, AK4490_01_CONTROL2, ret);
			break;
		case 3:
			ret = snd_soc_read(codec, AK4490_02_CONTROL3);
                        ret &= ~(1 << 0);
                        snd_soc_write(codec, AK4490_02_CONTROL3, ret);
                        ret = snd_soc_read(codec, AK4490_01_CONTROL2);
                        ret &= ~(1 << 5);
                        snd_soc_write(codec, AK4490_01_CONTROL2, ret);
			break;
		case 4:
			ret = snd_soc_read(codec, AK4490_02_CONTROL3);
                        ret |= 1 << 0;
                        snd_soc_write(codec, AK4490_02_CONTROL3, ret);
                        ret = snd_soc_read(codec, AK4490_01_CONTROL2);
                        ret |= 1 << 5;
                        snd_soc_write(codec, AK4490_01_CONTROL2, ret);
			break;
		default:
			return -EINVAL;
	}
                if(superslow == 1) {
                    ret = snd_soc_read(codec, AK4490_05_CONTROL4);
                    ret |= 1 << 0;
                    snd_soc_write(codec, AK4490_05_CONTROL4, ret);
                }
                if(superslow == 0) {
                    ret = snd_soc_read(codec, AK4490_05_CONTROL4);
                    ret &= ~(1 << 0);
                    snd_soc_write(codec, AK4490_05_CONTROL4, ret);
                    
                }
#endif	

*/
}

	return 0;
}

#define AK4490_RATES		(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
				SNDRV_PCM_RATE_192000|SNDRV_PCM_RATE_64000 |\
                		SNDRV_PCM_RATE_384000|SNDRV_PCM_RATE_352800 |\
				SNDRV_PCM_RATE_768000|SNDRV_PCM_RATE_705600)

#define AK4490_FORMATS		SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE |SNDRV_PCM_FMTBIT_SOP |\
                            SNDRV_PCM_FMTBIT_DSD64_F32 |  SNDRV_PCM_FMTBIT_DSD128_F32  |SNDRV_PCM_FMTBIT_DSD256_F32

static struct snd_soc_dai_ops ak4490_dai_ops = {
    .startup    = ak4490_startup,
    .set_fmt	= ak4490_set_dai_fmt,
	.set_sysclk	= ak4490_set_dai_sysclk,
	.hw_params	= ak4490_hw_params,
	.digital_mute = ak4490_set_dai_mute,
	.trigger = ak4490_trigger,
    .shutdown   = ak4490_shutdown,
};

struct snd_soc_dai_driver ak4490_dai[] = {   
	{										 
		.name = "ak4490 HiFi",
		.playback = {
		       .stream_name = "Playback",
		       .channels_min = 1,
		       .channels_max = 2,
		       .rates = AK4490_RATES,
		       .formats = AK4490_FORMATS,
		},
		.ops = &ak4490_dai_ops,
	},										 
};

static int ak4490_init_reg(struct snd_soc_codec *codec)
{

	ak4490_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static int ak4490_probe(struct snd_soc_codec *codec)
{
	struct ak4490_priv *ak4490 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;
    int num_controls;
    struct snd_kcontrol_new *ctl=NULL;

	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

#ifdef AK4490_CONTIF_DEBUG
	codec->write = ak4490_i2c_write;
	codec->read = ak4490_i2c_read;
#endif

    ctl=get_audiobirdge_control_table(&num_controls);
    if(ctl!=NULL && num_controls>0){

        snd_soc_add_controls(codec, ctl,num_controls);
	    akdbgprt("\t[AK4490 add CT7302 new control] %s(%d)\n",__FUNCTION__,__LINE__);

    }

    // open DAC power
    link_set_status(LINK_MCU_WORK);
    ak4490_write_i2c_lr('L',0x00,0x00);
    ak4490_write_i2c_lr('R',0x00,0x00);
    /* link_set_status(LINK_DAC_POWER_ON); */
//	ak4490_codec = codec;

	akdbgprt("\t[AK4490] %s(%d) ak4490=%x\n",__FUNCTION__,__LINE__, (int)ak4490);

	ak4490_init_reg(codec);

	akdbgprt("\t[AK4490 Effect] %s(%d)\n",__FUNCTION__,__LINE__);

    wake_lock_init(&audiolink_wakelock, WAKE_LOCK_SUSPEND, "audiolink");
#ifdef CONFIG_SND_SOC_AK4490_CUSTOM_DRIVER
        ak4490->fs1 = oversampling_freq;
#else
	ak4490->fs1 = 48000;
#endif
	ak4490->nBickFreq = 0;		
	ak4490->nDSDSel = 0;

    /* AudioBridge_disable(); */
	return ret;
}

static int ak4490_remove(struct snd_soc_codec *codec)
{

	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4490_set_bias_level(codec, SND_SOC_BIAS_OFF);


    link_set_status(LINK_DAC_POWER_OFF);
	return 0;
}

static int ak4490_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	ak4490_set_bias_level(codec, SND_SOC_BIAS_OFF);

    /* link_set_status(LINK_MCU_STOP); */
    /* AudioBridge_disable(); */
    /* link_set_status(LINK_DAC_POWER_OFF); */
	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);
	return 0;
}

static int ak4490_resume(struct snd_soc_codec *codec)
{

	/* ak4490_init_reg(codec); */

    /* link_set_status(LINK_MCU_WORK); */
    /* AudioBridge_enable(); */
    /* link_set_status(LINK_DAC_POWER_ON); */
	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);
	return 0;
}

static int ak4490_gpio_init(struct ak4490_priv* priv)
{
    int ret;
    struct regulator* ldo9;
    if(priv->dac_reset != INVALID_GPIO){
        iomux_set_gpio_mode(priv->dac_reset);
        ret = gpio_request(priv->dac_reset, "ak4490 reset pin");
        if (ret) {
            dev_err(priv->codec.dev, "%s: %d gpio reset request failed\n", __func__, priv->dac_reset);
            return -EBUSY;
        }
    }

    if(priv->dac_mute!= INVALID_GPIO){
        iomux_set_gpio_mode(iomux_gpio_to_mode(priv->dac_mute));
		/* iomux_set(GPIO3_D4); */
        ret = gpio_request(priv->dac_mute, "ak4490 mute pin");
        if (ret) {
            dev_err(priv->codec.dev, "%s: %d gpio mute request failed\n", __func__, priv->dac_mute);
            return -EBUSY;
        }
    }


    if(priv->am_power!= INVALID_GPIO){
        ret = gpio_request(priv->am_power, "ak4490 am power pin");
        if (ret) {
            dev_err(priv->codec.dev, "%s: %d gpio am power request failed\n", __func__, priv->am_power);
            return -EBUSY;
        }
    }



    // Disable jackline power
    // power disabled in board init
#if 0
    ldo9=regulator_get(NULL,"act_ldo9");
    if(ldo9 == NULL || IS_ERR(ldo9)){

        printk("act ldo9 is not available \n");
    }else{

        /* regulator_set_voltage(ldo9,2000000,2000000); */
        /* regulator_enable(ldo9); */
        /* msleep(1000); */
        regulator_disable(ldo9);
        /* msleep(1000); */
        /* regulator_enable(ldo9); */
        regulator_put(ldo9);

    }
#endif
        



    gpio_direction_output(priv->am_power,AM_POWER_UNACTIVE);
    priv->am_power_status=AM_POWER_UNACTIVE;

	gpio_pull_updown(priv->dac_mute,GPIOPullUp);

    gpio_direction_output(priv->dac_mute,DAC_MUTE_ACTIVE);

    gpio_direction_output(priv->dac_reset,DAC_RESET_ACTIVE);
    msleep(5);
    gpio_direction_output(priv->dac_reset,DAC_RESET_UNACTIVE);
    msleep(50);
    gpio_direction_output(priv->dac_mute,DAC_MUTE_UNACTIVE);
    
    return 0;

}

struct snd_soc_codec_driver soc_codec_dev_ak4490 = {
	.probe = ak4490_probe,
	.remove = ak4490_remove,
	.suspend =	ak4490_suspend,
	.resume =	ak4490_resume,

	.controls = ak4490_snd_controls,
	.num_controls = ARRAY_SIZE(ak4490_snd_controls),

	.set_bias_level = ak4490_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(ak4490_reg),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = ak4490_reg,
	.readable_register = ak4490_readable,
	.volatile_register = ak4490_volatile,	
	.dapm_widgets = ak4490_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ak4490_dapm_widgets),
	.dapm_routes = ak4490_intercon,
	.num_dapm_routes = ARRAY_SIZE(ak4490_intercon),
};
EXPORT_SYMBOL_GPL(soc_codec_dev_ak4490);


static int ak4490_i2c_probe(struct i2c_client *i2c,
                            const struct i2c_device_id *id)
{
	struct ak4490_priv *ak4490;
	struct snd_soc_codec *codec;
	int ret=0;
	
    struct ak4490_platform_data* pdata=i2c->dev.platform_data;


	akdbgprt("\t[AK4490] %s(%d)\n",__FUNCTION__,__LINE__);

    printk(KERN_ERR "ak4490  device name %s id: %c  i2c addr: 0x%x \n",
           id->name,id->driver_data,i2c->addr);


    if(gpak4490){

        if(id->driver_data == 'R')
            gpak4490->client_r=i2c;
        else
            gpak4490->client_l=i2c;

        return ret;
    }

	ak4490 = kzalloc(sizeof(struct ak4490_priv), GFP_KERNEL);
	if (ak4490 == NULL) return -ENOMEM;


    gpak4490=ak4490;
    if(id->driver_data == 'L')
        gpak4490->client_l=i2c;
    else
        gpak4490->client_r=i2c;


    codec = &ak4490->codec;

    ak4490->dac_reset=pdata->dac_reset;
    ak4490->dac_mute=pdata->dac_mute;
    ak4490->am_power=pdata->am_power;



	i2c_set_clientdata(i2c, ak4490);
	codec->control_data = i2c;
//	ak4490_data = ak4490;

	codec->dev = &i2c->dev;

    ak4490_gpio_init(ak4490);
	snd_soc_codec_set_drvdata(codec, ak4490);

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_ak4490, &ak4490_dai[0], ARRAY_SIZE(ak4490_dai));
	if (ret < 0){
		kfree(ak4490);
		akdbgprt("\t[AK4490 Error!] %s(%d)\n",__FUNCTION__,__LINE__);
	}
    isShutdown =false;
	return ret;
}

static int __devexit ak4490_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static int ak4490_i2c_shutdown(struct i2c_client *client)
{
    static  int count=0;

    if(count)
        return 0;
    count ++;
	akdbgprt("\t[AK4490] %s(%d) start\n", __FUNCTION__,__LINE__);

    isShutdown =true;
    ak4490_soft_mute(1);
    gpio_direction_output(gpak4490->dac_mute,DAC_MUTE_ACTIVE);
    link_stop(0);
    msleep(1000);
	akdbgprt("\t[AK4490] %s(%d) end pwoer %d \n", __FUNCTION__,__LINE__,link_get_status(LINK_ANALOG_POWER));
    return 0;
}
static const struct i2c_device_id ak4490_i2c_id[] = {
	{ "AK4490 DAC L", 'L' },
	{ "AK4490 DAC R", 'R' },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4490_i2c_id);

static struct i2c_driver ak4490_i2c_driver = {
	.driver = {
		.name = "ak4490",
		.owner = THIS_MODULE,
	},
	.probe = ak4490_i2c_probe,
    .shutdown = ak4490_i2c_shutdown,
	.remove = __devexit_p(ak4490_i2c_remove),
	.id_table = ak4490_i2c_id,
};

static int __init ak4490_modinit(void)
{

	akdbgprt("\t[AK4490] %s(%d)\n", __FUNCTION__,__LINE__);

	return i2c_add_driver(&ak4490_i2c_driver);
}

/* module_init(ak4490_modinit); */
device_initcall_sync(ak4490_modinit);
static void __exit ak4490_exit(void)
{
	i2c_del_driver(&ak4490_i2c_driver);
}
module_exit(ak4490_exit);


MODULE_DESCRIPTION("ASoC ak4490 codec driver");
MODULE_LICENSE("GPL");
