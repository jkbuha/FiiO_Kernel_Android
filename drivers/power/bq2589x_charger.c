/*
 * BQ2589x battery charging driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/power/bq2589x_reg.h>
#include <mach/board.h>
#include <mach/system.h>

extern void usb_path_selecter(char );


static int debug=0;
module_param(debug, int, S_IRUGO|S_IWUSR);
#define dprintk(level,dev, fmt, arg...) do {			\
	if (debug >= level) 					\
	    dev_info(dev,fmt, ## arg); } while (0)


#define BQ2589X_STATUS_PLUGIN		0x0001
#define BQ2589X_STATUS_PG			0x0002  // precharging
#define	BQ2589X_STATUS_CHARGE_ENABLE 0x0004
#define BQ2589X_STATUS_FAULT		0x0008

#define BQ2589X_STATUS_EXIST		0x0100

char* bq2589x_vbus_type_str[]= {
   " BQ2589X_VBUS_NONE",
   " BQ2589X_VBUS_USB_SDP",
   " BQ2589X_VBUS_USB_CDP", /*CDP for bq25890, Adapter for bq25892*/
   " BQ2589X_VBUS_USB_DCP",
   " BQ2589X_VBUS_MAXC",
   " BQ2589X_VBUS_UNKNOWN",
   " BQ2589X_VBUS_NONSTAND",
   " BQ2589X_VBUS_OTG",
   " BQ2589X_VBUS_TYPE_NUM",
};



struct bq2589x *g_bq;
static struct pe_ctrl pe;
enum thermal_state thermal_s= NORMAL;
static int now_ichg = 2800; 
static DEFINE_MUTEX(bq2589x_i2c_lock);

struct bq2589x* bq2589x_control()
{
    return g_bq;
}
EXPORT_SYMBOL_GPL(bq2589x_control);
static int bq2589x_read_byte(struct bq2589x *bq, u8 *data, u8 reg)
{
	int ret;

	mutex_lock(&bq2589x_i2c_lock);
	ret = i2c_smbus_read_byte_data(bq->client, reg);
	if (ret < 0) {
		dev_err(bq->dev, "failed to read 0x%.2x\n", reg);
		mutex_unlock(&bq2589x_i2c_lock);
		return ret;
	}

	*data = (u8)ret;
	mutex_unlock(&bq2589x_i2c_lock);

	return 0;
}

int gbq2589x_read_byte(u8 reg, u8* data)
{
    if(g_bq)
       return bq2589x_read_byte(g_bq,data,reg);
    else{
        printk(KERN_ERR "g_bq is null !!\n");
        return -1;
    }

}
EXPORT_SYMBOL_GPL(gbq2589x_read_byte);


static int bq2589x_write_byte(struct bq2589x *bq, u8 reg, u8 data)
{
	int ret;
	mutex_lock(&bq2589x_i2c_lock);
	ret = i2c_smbus_write_byte_data(bq->client, reg, data);
	mutex_unlock(&bq2589x_i2c_lock);
	return ret;
}

int gbq2589x_write_byte(u8 reg, u8 data)
{
    if(g_bq)
       return bq2589x_write_byte(g_bq,reg,data);
    else{
        printk(KERN_ERR "g_bq is null !!\n");
        return -1;
    }

}
EXPORT_SYMBOL_GPL(gbq2589x_write_byte);

int gbq2589x_update_bits(u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	ret = gbq2589x_read_byte(reg, &tmp);
	if (ret)
		return ret;

	tmp &= ~mask;
	tmp |= data & mask;

	return gbq2589x_write_byte(reg, tmp);
}
EXPORT_SYMBOL_GPL(gbq2589x_update_bits);

static int bq2589x_update_bits(struct bq2589x *bq, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	ret = bq2589x_read_byte(bq, &tmp, reg);

	if (ret)
		return ret;

	tmp &= ~mask;
	tmp |= data & mask;

	return bq2589x_write_byte(bq, reg, tmp);
}


static enum bq2589x_vbus_type bq2589x_get_vbus_type(struct bq2589x *bq)
{
	u8 val = 0;
	int ret;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_0B);
	if (ret < 0)
		return 0;
	val &= BQ2589X_VBUS_STAT_MASK;
	val >>= BQ2589X_VBUS_STAT_SHIFT;

	return val;
}


static int bq2589x_enable_otg(struct bq2589x *bq)
{
	u8 val = BQ2589X_OTG_ENABLE << BQ2589X_OTG_CONFIG_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_03,
							   BQ2589X_OTG_CONFIG_MASK, val);

}

int bq2589x_disable_otg(struct bq2589x *bq)
{
	u8 val = BQ2589X_OTG_DISABLE << BQ2589X_OTG_CONFIG_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_03,
							   BQ2589X_OTG_CONFIG_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2589x_disable_otg);

int bq2589x_set_otg_volt(struct bq2589x *bq, int volt)
{
	u8 val = 0;

	if (volt < BQ2589X_BOOSTV_BASE)
		volt = BQ2589X_BOOSTV_BASE;
	if (volt > BQ2589X_BOOSTV_BASE + (BQ2589X_BOOSTV_MASK >> BQ2589X_BOOSTV_SHIFT) * BQ2589X_BOOSTV_LSB)
		volt = BQ2589X_BOOSTV_BASE + (BQ2589X_BOOSTV_MASK >> BQ2589X_BOOSTV_SHIFT) * BQ2589X_BOOSTV_LSB;

	val = ((volt - BQ2589X_BOOSTV_BASE) / BQ2589X_BOOSTV_LSB) << BQ2589X_BOOSTV_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_0A, BQ2589X_BOOSTV_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2589x_set_otg_volt);

int bq2589x_set_otg_current(struct bq2589x *bq, int curr)
{
	u8 temp;

	if (curr == 500)
		temp = BQ2589X_BOOST_LIM_500MA;
	else if (curr == 700)
		temp = BQ2589X_BOOST_LIM_700MA;
	else if (curr == 1100)
		temp = BQ2589X_BOOST_LIM_1100MA;
	else if (curr == 1600)
		temp = BQ2589X_BOOST_LIM_1600MA;
	else if (curr == 1800)
		temp = BQ2589X_BOOST_LIM_1800MA;
	else if (curr == 2100)
		temp = BQ2589X_BOOST_LIM_2100MA;
	else if (curr == 2400)
		temp = BQ2589X_BOOST_LIM_2400MA;
	else
		temp = BQ2589X_BOOST_LIM_1300MA;

	return bq2589x_update_bits(bq, BQ2589X_REG_0A, BQ2589X_BOOST_LIM_MASK, temp << BQ2589X_BOOST_LIM_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_otg_current);

static int bq2589x_enable_charger(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_CHG_ENABLE << BQ2589X_CHG_CONFIG_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_03, BQ2589X_CHG_CONFIG_MASK, val);
	if (ret == 0)
		bq->status |= BQ2589X_STATUS_CHARGE_ENABLE;
	return ret;
}

int bq2589x_disable_charger(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_CHG_DISABLE << BQ2589X_CHG_CONFIG_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_03, BQ2589X_CHG_CONFIG_MASK, val);
	if (ret == 0)
		bq->status &= ~BQ2589X_STATUS_CHARGE_ENABLE;
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_disable_charger);

int gbq2589x_disable_charger(bool disable)
{
    if(g_bq) {
        if (disable) {
            return bq2589x_disable_charger(g_bq);
        } else {
            return bq2589x_enable_charger(g_bq);
        }
    } else {
        printk(KERN_ERR "g_bq is null !!\n");
        return -1;
    }
}
EXPORT_SYMBOL_GPL(gbq2589x_disable_charger);

/* interfaces that can be called by other module */
int bq2589x_adc_start(struct bq2589x *bq, bool oneshot)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_02);
	if (ret < 0) {
		dev_err(bq->dev, "%s failed to read register 0x02:%d\n", __func__, ret);
		return ret;
	}

	if (((val & BQ2589X_CONV_RATE_MASK) >> BQ2589X_CONV_RATE_SHIFT) == BQ2589X_ADC_CONTINUE_ENABLE)
		return 0; /*is doing continuous scan*/
	if (oneshot)
		ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_START_MASK, BQ2589X_CONV_START << BQ2589X_CONV_START_SHIFT);
	else
		ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_RATE_MASK,  BQ2589X_ADC_CONTINUE_ENABLE << BQ2589X_CONV_RATE_SHIFT);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_adc_start);

int bq2589x_adc_stop(struct bq2589x *bq)
{
	return bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_RATE_MASK, BQ2589X_ADC_CONTINUE_DISABLE << BQ2589X_CONV_RATE_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_adc_stop);


int bq2589x_adc_read_battery_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_0E);
	if (ret < 0) {
		dev_err(bq->dev, "read battery voltage failed :%d\n", ret);
		return ret;
	} else{
		volt = BQ2589X_BATV_BASE + ((val & BQ2589X_BATV_MASK) >> BQ2589X_BATV_SHIFT) * BQ2589X_BATV_LSB ;
        printk("zzdts vol %d \n",volt);
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_battery_volt);


int bq2589x_adc_read_sys_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_0F);
	if (ret < 0) {
		dev_err(bq->dev, "read system voltage failed :%d\n", ret);
		return ret;
	} else{
		volt = BQ2589X_SYSV_BASE + ((val & BQ2589X_SYSV_MASK) >> BQ2589X_SYSV_SHIFT) * BQ2589X_SYSV_LSB ;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_sys_volt);

int bq2589x_adc_read_vbus_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_11);
	if (ret < 0) {
		dev_err(bq->dev, "read vbus voltage failed :%d\n", ret);
		return ret;
	} else{
		volt = BQ2589X_VBUSV_BASE + ((val & BQ2589X_VBUSV_MASK) >> BQ2589X_VBUSV_SHIFT) * BQ2589X_VBUSV_LSB ;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_vbus_volt);

int bq2589x_adc_read_temperature(struct bq2589x *bq)
{
	uint8_t val;
	int temp;
	int ret;
	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_10);
	if (ret < 0) {
		dev_err(bq->dev, "read temperature failed :%d\n", ret);
		return ret;
	} else{
		temp = BQ2589X_TSPCT_BASE + ((val & BQ2589X_TSPCT_MASK) >> BQ2589X_TSPCT_SHIFT) * BQ2589X_TSPCT_LSB ;
		return temp;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_temperature);

int bq2589x_adc_read_charge_current(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_12);
	if (ret < 0) {
		dev_err(bq->dev, "read charge current failed :%d\n", ret);
		return ret;
	} else{
		volt = (int)(BQ2589X_ICHGR_BASE + ((val & BQ2589X_ICHGR_MASK) >> BQ2589X_ICHGR_SHIFT) * BQ2589X_ICHGR_LSB) ;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_charge_current);

int bq2589x_set_chargecurrent(struct bq2589x *bq, int curr,char * source)
{
	u8 ichg;

    dev_err(bq->dev, "set charge current: %s -> %d\n", source,curr);
	ichg = (curr - BQ2589X_ICHG_BASE)/BQ2589X_ICHG_LSB;
    bq->curr_set=ichg;
	return bq2589x_update_bits(bq, BQ2589X_REG_04, BQ2589X_ICHG_MASK, ichg << BQ2589X_ICHG_SHIFT);

}
EXPORT_SYMBOL_GPL(bq2589x_set_chargecurrent);

int bq2589x_get_chargecurrent(struct bq2589x *bq)
{
    uint8_t value;
    int data;
    bq2589x_read_byte(bq, &value, BQ2589X_REG_04);

    value= (value&BQ2589X_ICHG_MASK) >>BQ2589X_ICHG_SHIFT;

    data=(int)value*BQ2589X_ICHG_LSB+BQ2589X_ICHG_BASE;


    return data;
}
EXPORT_SYMBOL_GPL(bq2589x_get_chargecurrent);


int bq2589x_read_chargecurrent(struct bq2589x *bq, uint32_t* curr)
{
    uint8_t value;
    uint32_t data;    
    bq2589x_read_byte(bq, &value, BQ2589X_REG_04);

    value= (value&BQ2589X_ICHG_MASK) >>BQ2589X_ICHG_SHIFT;

    data=value*BQ2589X_ICHG_LSB+BQ2589X_ICHG_BASE;
    if(curr)
        *curr=data;

    bq->curr_get=data;
    dprintk(1,bq->dev, "get charge current :%u\n", data);


}
int bq2589x_set_term_current(struct bq2589x *bq, int curr)
{
	u8 iterm;

	iterm = (curr - BQ2589X_ITERM_BASE) / BQ2589X_ITERM_LSB;

	return bq2589x_update_bits(bq, BQ2589X_REG_05, BQ2589X_ITERM_MASK, iterm << BQ2589X_ITERM_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_term_current);


int bq2589x_set_prechg_current(struct bq2589x *bq, int curr)
{
	u8 iprechg;

	iprechg = (curr - BQ2589X_IPRECHG_BASE) / BQ2589X_IPRECHG_LSB;

	return bq2589x_update_bits(bq, BQ2589X_REG_05, BQ2589X_IPRECHG_MASK, iprechg << BQ2589X_IPRECHG_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_prechg_current);

int bq2589x_set_chargevoltage(struct bq2589x *bq, int volt)
{
	u8 val;

		dprintk(1,bq->dev, "set charge voltage :%d\n", volt);
	val = (volt - BQ2589X_VREG_BASE)/BQ2589X_VREG_LSB;
	return bq2589x_update_bits(bq, BQ2589X_REG_06, BQ2589X_VREG_MASK, val << BQ2589X_VREG_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_chargevoltage);

int bq2589x_set_recharge_vol(struct bq2589x *bq, int volt)
{
	u8 val;

	dev_info(bq->dev, "set recharge voltage :%d\n", volt);
	val = (volt - 100)/100;
	return bq2589x_update_bits(bq, BQ2589X_REG_06, BQ2589X_VRECHG_MASK, val << BQ2589X_VRECHG_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_recharge_vol);


int bq2589x_set_input_volt_limit(struct bq2589x *bq, int volt)
{
	u8 val;
	val = (volt - BQ2589X_VINDPM_BASE) / BQ2589X_VINDPM_LSB;
	return bq2589x_update_bits(bq, BQ2589X_REG_0D, BQ2589X_VINDPM_MASK, val << BQ2589X_VINDPM_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_input_volt_limit);

int bq2589x_set_input_current_limit(struct bq2589x *bq, int curr)
{
	u8 val;

	val = (curr - BQ2589X_IINLIM_BASE) / BQ2589X_IINLIM_LSB;
	return bq2589x_update_bits(bq, BQ2589X_REG_00, BQ2589X_IINLIM_MASK, val << BQ2589X_IINLIM_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_input_current_limit);


int bq2589x_set_vindpm_offset(struct bq2589x *bq, int offset)
{
	u8 val;

	val = (offset - BQ2589X_VINDPMOS_BASE)/BQ2589X_VINDPMOS_LSB;
	return bq2589x_update_bits(bq, BQ2589X_REG_01, BQ2589X_VINDPMOS_MASK, val << BQ2589X_VINDPMOS_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_vindpm_offset);

int bq2589x_get_charging_status(struct bq2589x *bq)
{
	u8 val = 0;
	int ret;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_0B);
	if (ret < 0) {
		dev_err(bq->dev, "%s Failed to read register 0x0b:%d\n", __func__, ret);
		return ret;
	}
	val &= BQ2589X_CHRG_STAT_MASK;
	val >>= BQ2589X_CHRG_STAT_SHIFT;
	return val;
}
EXPORT_SYMBOL_GPL(bq2589x_get_charging_status);

void _bq2589x_set_otg(struct bq2589x *bq, int enable)
{
	int ret;

    dprintk(1,bq->dev,"%s  enable? %s \n",__func__,enable? "true":"false");
	if (enable) {
		ret = bq2589x_enable_otg(bq);
        usb_path_selecter(0); //set usb to host
		if (ret < 0) {
			dev_err(bq->dev, "%s:Failed to enable otg-%d\n", __func__, ret);
			return;
		}
	} else{
		ret = bq2589x_disable_otg(bq);
        usb_path_selecter(1); //set usb to charger
		if (ret < 0)
			dev_err(bq->dev, "%s:Failed to disable otg-%d\n", __func__, ret);
	}
}
EXPORT_SYMBOL_GPL(_bq2589x_set_otg);

void bq2589x_set_otg(int enable)
{

    if(g_bq){
        g_bq->otg_enable=enable;
		schedule_delayed_work(&g_bq->otg_work, 0);
    }else
         printk(KERN_ERR, "%s g_bq not init\n",__func__);

}
EXPORT_SYMBOL_GPL(bq2589x_set_otg);

int bq2589x_set_watchdog_timer(struct bq2589x *bq, u8 timeout)
{
	return bq2589x_update_bits(bq, BQ2589X_REG_07, BQ2589X_WDT_MASK, (u8)((timeout - BQ2589X_WDT_BASE) / BQ2589X_WDT_LSB) << BQ2589X_WDT_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_watchdog_timer);

int bq2589x_disable_watchdog_timer(struct bq2589x *bq)
{
	u8 val = BQ2589X_WDT_DISABLE << BQ2589X_WDT_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07, BQ2589X_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_disable_watchdog_timer);

int bq2589x_reset_watchdog_timer(struct bq2589x *bq)
{
	u8 val = BQ2589X_WDT_RESET << BQ2589X_WDT_RESET_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_03, BQ2589X_WDT_RESET_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_reset_watchdog_timer);

int bq2589x_force_dpdm(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_FORCE_DPDM << BQ2589X_FORCE_DPDM_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_FORCE_DPDM_MASK, val);
	if (ret)
		return ret;

	msleep(20);/*TODO: how much time needed to finish dpdm detect?*/
	return 0;

}
EXPORT_SYMBOL_GPL(bq2589x_force_dpdm);

int bq2589x_disable_hvdcp(struct bq2589x *bq, bool disable)
{
	int ret;
	u8 val;

        dprintk(0, bq->dev,"BQ25890 %s:  status === %d\n", __func__, disable);

        if (disable)
            val = BQ2589X_HVDCP_DISABLE << BQ2589X_HVDCPEN_SHIFT;
        else
            val = BQ2589X_HVDCP_ENABLE << BQ2589X_HVDCPEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_HVDCPEN_MASK, val);
	if (ret) {
                printk(KERN_ERR, "%s failed!!!\n",__func__);
		return ret;
        }

        msleep(20);
	return 0;

}
EXPORT_SYMBOL_GPL(bq2589x_disable_hvdcp);

int bq2589x_reset_chip(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_RESET << BQ2589X_RESET_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_14, BQ2589X_RESET_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_reset_chip);

int bq2589x_enter_ship_mode(struct bq2589x *bq)
{
	int ret;
    dev_err(bq->dev, "%s\n", __func__);
	u8 val = BQ2589X_BATFET_OFF << BQ2589X_BATFET_DIS_SHIFT;

    /* val |= BQ2589X_BATFET_OFF_DELAY << BQ2589X_BATFET_OFF_DELAY_SHIFT; */

	ret = bq2589x_update_bits(bq, BQ2589X_REG_09, BQ2589X_BATFET_DIS_MASK, val);
	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_enter_ship_mode);

int bq2589x_enter_hiz_mode(struct bq2589x *bq)
{
	u8 val = BQ2589X_HIZ_ENABLE << BQ2589X_ENHIZ_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_00, BQ2589X_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2589x_enter_hiz_mode);

int bq2589x_exit_hiz_mode(struct bq2589x *bq)
{

	u8 val = BQ2589X_HIZ_DISABLE << BQ2589X_ENHIZ_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_00, BQ2589X_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2589x_exit_hiz_mode);

int bq2589x_get_hiz_mode(struct bq2589x *bq, u8 *state)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_00);
	if (ret)
		return ret;
	*state = (val & BQ2589X_ENHIZ_MASK) >> BQ2589X_ENHIZ_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(bq2589x_get_hiz_mode);


int bq2589x_pumpx_enable(struct bq2589x *bq, int enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2589X_PUMPX_ENABLE << BQ2589X_EN_PUMPX_SHIFT;
	else
		val = BQ2589X_PUMPX_DISABLE << BQ2589X_EN_PUMPX_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_04, BQ2589X_EN_PUMPX_MASK, val);

	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_pumpx_enable);

int bq2589x_pumpx_increase_volt(struct bq2589x *bq)
{
	u8 val;
	int ret;

	val = BQ2589X_PUMPX_UP << BQ2589X_PUMPX_UP_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_09, BQ2589X_PUMPX_UP_MASK, val);

	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_pumpx_increase_volt);

int bq2589x_pumpx_increase_volt_done(struct bq2589x *bq)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_09);
	if (ret)
		return ret;

	if (val & BQ2589X_PUMPX_UP_MASK)
		return 1;   /* not finished*/
	else
		return 0;   /* pumpx up finished*/

}
EXPORT_SYMBOL_GPL(bq2589x_pumpx_increase_volt_done);

int bq2589x_pumpx_decrease_volt(struct bq2589x *bq)
{
	u8 val;
	int ret;

	val = BQ2589X_PUMPX_DOWN << BQ2589X_PUMPX_DOWN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_09, BQ2589X_PUMPX_DOWN_MASK, val);

	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_pumpx_decrease_volt);

int bq2589x_pumpx_decrease_volt_done(struct bq2589x *bq)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_09);
	if (ret)
		return ret;

	if (val & BQ2589X_PUMPX_DOWN_MASK)
		return 1;   /* not finished*/
	else
		return 0;   /* pumpx down finished*/

}
EXPORT_SYMBOL_GPL(bq2589x_pumpx_decrease_volt_done);

int bq2589x_force_ico(struct bq2589x *bq)
{
	u8 val;
	int ret;

	val = BQ2589X_FORCE_ICO << BQ2589X_FORCE_ICO_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_09, BQ2589X_FORCE_ICO_MASK, val);

	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_force_ico);

int bq2589x_check_force_ico_done(struct bq2589x *bq)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_14);
	if (ret)
		return ret;

	if (val & BQ2589X_ICO_OPTIMIZED_MASK)
		return 1;  /*finished*/
	else
		return 0;   /* in progress*/
}
EXPORT_SYMBOL_GPL(bq2589x_check_force_ico_done);

int bq2589x_enable_term(struct bq2589x* bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2589X_TERM_ENABLE << BQ2589X_EN_TERM_SHIFT;
	else
		val = BQ2589X_TERM_DISABLE << BQ2589X_EN_TERM_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_07, BQ2589X_EN_TERM_MASK, val);

	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_enable_term);

int bq2589x_enable_auto_dpdm(struct bq2589x* bq, bool enable)
{
	u8 val;
	int ret;
	
	if (enable)
		val = BQ2589X_AUTO_DPDM_ENABLE << BQ2589X_AUTO_DPDM_EN_SHIFT;
	else
		val = BQ2589X_AUTO_DPDM_DISABLE << BQ2589X_AUTO_DPDM_EN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_AUTO_DPDM_EN_MASK, val);

	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_enable_auto_dpdm);

int bq2589x_use_absolute_vindpm(struct bq2589x* bq, bool enable)
{
	u8 val;
	int ret;
	
	if (enable)
		val = BQ2589X_FORCE_VINDPM_ENABLE << BQ2589X_FORCE_VINDPM_SHIFT;
	else
		val = BQ2589X_FORCE_VINDPM_DISABLE << BQ2589X_FORCE_VINDPM_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_0D, BQ2589X_FORCE_VINDPM_MASK, val);

	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_use_absolute_vindpm);

int bq2589x_enable_ico(struct bq2589x* bq, bool enable)
{
	u8 val;
	int ret;
	
	if (enable)
		val = BQ2589X_ICO_ENABLE << BQ2589X_ICOEN_SHIFT;
	else
		val = BQ2589X_ICO_DISABLE << BQ2589X_ICOEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_ICOEN_MASK, val);

	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_enable_ico);


int bq2589x_read_idpm_limit(struct bq2589x *bq)
{
	uint8_t val;
	int curr;
	int ret;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_13);
	if (ret < 0) {
		dev_err(bq->dev, "read vbus voltage failed :%d\n", ret);
		return ret;
	} else{
		curr = BQ2589X_IDPM_LIM_BASE + ((val & BQ2589X_IDPM_LIM_MASK) >> BQ2589X_IDPM_LIM_SHIFT) * BQ2589X_IDPM_LIM_LSB ;
		return curr;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_read_idpm_limit);

bool bq2589x_is_charge_done(struct bq2589x *bq)
{
	int ret;
	u8 val;

	ret = bq2589x_read_byte(bq, &val, BQ2589X_REG_0B);
	if (ret < 0) {
		dev_err(bq->dev, "%s:read REG0B failed :%d\n", __func__, ret);
		return false;
	}
	val &= BQ2589X_CHRG_STAT_MASK;
	val >>= BQ2589X_CHRG_STAT_SHIFT;

	return (val == BQ2589X_CHRG_STAT_CHGDONE);
}
EXPORT_SYMBOL_GPL(bq2589x_is_charge_done);

static int bq2589x_init_device(struct bq2589x *bq)
{
	int ret;

    /*common initialization*/

	bq2589x_disable_watchdog_timer(bq);

	bq2589x_enable_auto_dpdm(bq, bq->cfg.enable_auto_dpdm);
	bq2589x_enable_term(bq, bq->cfg.enable_term);
	bq2589x_enable_ico(bq, bq->cfg.enable_ico);
	/*force use absolute vindpm if auto_dpdm not enabled*/
	if (!bq->cfg.enable_auto_dpdm)
		bq->cfg.use_absolute_vindpm = true;
	bq2589x_use_absolute_vindpm(bq, bq->cfg.use_absolute_vindpm);


	ret = bq2589x_set_vindpm_offset(bq, 600);
	if (ret < 0) {
		dev_err(bq->dev, "%s:Failed to set vindpm offset:%d\n", __func__, ret);
		return ret;
	}

	ret = bq2589x_set_term_current(bq, bq->cfg.term_current);
	if (ret < 0) {
		dev_err(bq->dev, "%s:Failed to set termination current:%d\n", __func__, ret);
		return ret;
	}

	ret = bq2589x_set_chargevoltage(bq, bq->cfg.charge_voltage);
	if (ret < 0) {
		dev_err(bq->dev, "%s:Failed to set charge voltage:%d\n", __func__, ret);
		return ret;
	}
#if 0
	ret = bq2589x_set_recharge_vol(bq, 200);
	if (ret < 0) {
		dev_err(bq->dev, "%s:Failed to set recharge vol:%d\n", __func__, ret);
		return ret;
	}
#endif

	ret = bq2589x_set_chargecurrent(bq, bq->cfg.charge_current,"init_to_cfg.charge_current");
	if (ret < 0) {
		dev_err(bq->dev, "%s:Failed to set charge current:%d\n", __func__, ret);
		return ret;
	}

	ret = bq2589x_enable_charger(bq);
	if (ret < 0) {
		dev_err(bq->dev, "%s:Failed to enable charger:%d\n", __func__, ret);
		return ret;
	}

	bq2589x_adc_start(bq, false);

	ret = bq2589x_pumpx_enable(bq, 1);
	if (ret) {
		dev_err(bq->dev, "%s:Failed to enable pumpx:%d\n", __func__, ret);
		return ret;
	}

	bq2589x_set_watchdog_timer(bq, 160);

	return ret;
}


static int bq2589x_charge_status(struct bq2589x *bq)
{
	u8 val = 0;

	bq2589x_read_byte(bq, &val, BQ2589X_REG_0B);
	val &= BQ2589X_CHRG_STAT_MASK;
	val >>= BQ2589X_CHRG_STAT_SHIFT;
	switch (val) {
	case BQ2589X_CHRG_STAT_FASTCHG:
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	case BQ2589X_CHRG_STAT_PRECHG:
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	case BQ2589X_CHRG_STAT_CHGDONE:
	case BQ2589X_CHRG_STAT_IDLE:
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	default:
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}
}

bool bq2589x_is_usbcharge(struct bq2589x* bq)
{

	u8 type = bq2589x_get_vbus_type(bq);
    switch(type){
        case BQ2589X_VBUS_USB_SDP:
        case BQ2589X_VBUS_USB_CDP:
        case BQ2589X_VBUS_USB_DCP:
        case BQ2589X_VBUS_MAXC:
        case BQ2589X_VBUS_UNKNOWN:
        case BQ2589X_VBUS_NONSTAND:
            return true;
        default:
            return false;
    }
}

void bq2589x_shutdown()
{

    if(bq2589x_is_usbcharge(g_bq)){

		printk(KERN_ERR "[BQ2589X] reboot to charge!\n");
	    arch_reset(0,"charge");
		printk(KERN_ERR "[BQ2589X] warning!!! arch can't ,reboot, maybe some error happend!\n");

        mdelay(50);
        /* printk(KERN_ERR "[BQ2589X] warning!!! pmu can't power-off, maybe some error happend!\n"); */
    } else{

		printk(KERN_ERR "[BQ2589X] shutdown!\n");
        bq2589x_enter_ship_mode(g_bq);
    }

}
EXPORT_SYMBOL_GPL(bq2589x_shutdown);

static enum power_supply_property bq2589x_charger_props[] = {
        POWER_SUPPLY_PROP_CURRENT_NOW,
        POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_TYPE, /* Charger status output */
	POWER_SUPPLY_PROP_ONLINE, /* External power source */
        POWER_SUPPLY_PROP_CHARGE_CURRENT,
        POWER_SUPPLY_PROP_CHARGE_AVG,
    POWER_SUPPLY_PROP_CHARGE_USB_TYPE, /*charge type */
};


static int bq2589x_usb_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{

	struct bq2589x *bq = container_of(psy, struct bq2589x, usb);
	u8 type = bq2589x_get_vbus_type(bq);

	switch (psp) {
        case POWER_SUPPLY_PROP_CURRENT_NOW:
                val->intval = bq2589x_adc_read_charge_current(bq);
                break;
        case POWER_SUPPLY_PROP_VOLTAGE_NOW:
                val->intval = bq2589x_adc_read_battery_volt(bq);
                break;
	case POWER_SUPPLY_PROP_ONLINE:
		if(thermal_s <= LOW_TEMP_STOP_CHARGING ||thermal_s >= HIGH_TEMP_STOP_CHARGING){
			val->intval = 0;
		}else if (type == BQ2589X_VBUS_USB_SDP || type == BQ2589X_VBUS_USB_DCP 
            ||type == BQ2589X_VBUS_USB_CDP|| type == BQ2589X_VBUS_MAXC  
            || type == BQ2589X_VBUS_UNKNOWN)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = bq2589x_charge_status(bq);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CURRENT:
		val->intval = bq2589x_adc_read_vbus_volt(bq);
		break;
	case POWER_SUPPLY_PROP_CHARGE_AVG:
		val->intval = bq2589x_get_chargecurrent(bq);
		break;
	case POWER_SUPPLY_PROP_CHARGE_USB_TYPE:
		val->intval = type;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bq2589x_wall_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{

	struct bq2589x *bq = container_of(psy, struct bq2589x, wall);
	u8 type = bq2589x_get_vbus_type(bq);

	switch (psp) {
        case POWER_SUPPLY_PROP_CURRENT_NOW:
                val->intval = bq2589x_adc_read_charge_current(bq);
                break;
        case POWER_SUPPLY_PROP_VOLTAGE_NOW:
                val->intval = bq2589x_adc_read_battery_volt(bq);
                break;
	case POWER_SUPPLY_PROP_ONLINE:
		if(thermal_s <= LOW_TEMP_STOP_CHARGING ||thermal_s >= HIGH_TEMP_STOP_CHARGING){
			val->intval = 0;
		}else if (type == BQ2589X_VBUS_MAXC || type == BQ2589X_VBUS_UNKNOWN || type == BQ2589X_VBUS_NONSTAND)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = bq2589x_charge_status(bq);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CURRENT:
		val->intval = bq2589x_adc_read_vbus_volt(bq);
		break;
	case POWER_SUPPLY_PROP_CHARGE_AVG:
		val->intval = bq2589x_get_chargecurrent(bq);
		break;
	case POWER_SUPPLY_PROP_CHARGE_USB_TYPE:
		val->intval = type;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bq2589x_psy_register(struct bq2589x *bq)
{
	int ret;

	bq->usb.name = "usb";
	bq->usb.type = POWER_SUPPLY_TYPE_USB;
	bq->usb.properties = bq2589x_charger_props;
	bq->usb.num_properties = ARRAY_SIZE(bq2589x_charger_props);
	bq->usb.get_property = bq2589x_usb_get_property;
	bq->usb.external_power_changed = NULL;

	ret = power_supply_register(bq->dev, &bq->usb);
	if (ret < 0) {
		dev_err(bq->dev, "%s:failed to register usb psy:%d\n", __func__, ret);
		return ret;
	}

	bq->wall.name = "ac";
	bq->wall.type = POWER_SUPPLY_TYPE_MAINS;
	bq->wall.properties = bq2589x_charger_props;
	bq->wall.num_properties = ARRAY_SIZE(bq2589x_charger_props);
	bq->wall.get_property = bq2589x_wall_get_property;
	bq->wall.external_power_changed = NULL;

	ret = power_supply_register(bq->dev, &bq->wall);
	if (ret < 0) {
		dev_err(bq->dev, "%s:failed to register wall psy:%d\n", __func__, ret);
		goto fail_1;
	}

	return 0;

fail_1:
	power_supply_unregister(&bq->usb);

	return ret;
}

static void bq2589x_psy_unregister(struct bq2589x *bq)
{
	power_supply_unregister(&bq->usb);
	power_supply_unregister(&bq->wall);
}

static ssize_t bq2589x_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret ;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "Charger 1");
	for (addr = 0x0; addr <= 0x14; addr++) {
		ret = bq2589x_read_byte(g_bq, &val, addr);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,"Reg[0x%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static DEVICE_ATTR(registers, S_IRUGO, bq2589x_show_registers, NULL);

static struct attribute *bq2589x_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group bq2589x_attr_group = {
	.attrs = bq2589x_attributes,
};


#if 0
static int bq2589x_parse_dt(struct device *dev, struct bq2589x *bq)
{
	int ret;
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32(np, "ti,bq2589x,vbus-volt-high-level", &pe.high_volt_level);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "ti,bq2589x,vbus-volt-low-level", &pe.low_volt_level);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "ti,bq2589x,vbat-min-volt-to-tuneup", &pe.vbat_min_volt);
	if (ret)
		return ret;

	bq->cfg.enable_auto_dpdm = of_property_read_bool(np, "ti,bq2589x,enable-auto-dpdm");
	bq->cfg.enable_term = of_property_read_bool(np, "ti,bq2589x,enable-termination");
	bq->cfg.enable_ico = of_property_read_bool(np, "ti,bq2589x,enable-ico");
	bq->cfg.use_absolute_vindpm = of_property_read_bool(np, "ti,bq2589x,use-absolute-vindpm");

	ret = of_property_read_u32(np, "ti,bq2589x,charge-voltage",&bq->cfg.charge_voltage);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "ti,bq2589x,charge-current",&bq->cfg.charge_current);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "ti,bq2589x,term-current",&bq->cfg.term_current);
	if (ret)
		return ret;

	return 0;
}
#endif
static void bq2589x_init_platform_data(struct bq2589x *bq)
{
    pe.high_volt_level = bq->platform_data->high_volt_level;

    pe.low_volt_level= bq->platform_data->low_volt_level;

    pe.vbat_min_volt= bq->platform_data->vbat_min_volt;

    bq->cfg.enable_ico          = bq->platform_data->enable_ico;
    bq->cfg.enable_term         = bq->platform_data->enable_term;
    bq->cfg.enable_auto_dpdm    = bq->platform_data->enable_auto_dpdm;
    bq->cfg.use_absolute_vindpm = bq->platform_data->use_absolute_vindpm;

    bq->cfg.charge_voltage      = bq->platform_data->vreg;
    bq->cfg.charge_current      = bq->platform_data->ichg;
    bq->cfg.term_current        = bq->platform_data->iterm;



}

static int bq2589x_detect_device(struct bq2589x *bq)
{
	int ret;
	u8 data;

	ret = bq2589x_read_byte(bq, &data, BQ2589X_REG_14);
	if (ret == 0) {
		bq->part_no = (data & BQ2589X_PN_MASK) >> BQ2589X_PN_SHIFT;
		bq->revision = (data & BQ2589X_DEV_REV_MASK) >> BQ2589X_DEV_REV_SHIFT;
	}

	return ret;
}

static int bq2589x_read_batt_temp(struct bq2589x *bq)
	{
		union power_supply_propval ret = {0,};
	
		if (!bq->batt_psy) 
			bq->batt_psy = power_supply_get_by_name("battery");
	
		if (bq->batt_psy) {
			bq->batt_psy->get_property(bq->batt_psy,POWER_SUPPLY_PROP_TEMP,&ret);
			return ret.intval;
		} else {
			return 250;
		}
	}

static int bq2589x_read_batt_rsoc(struct bq2589x *bq)
{
	union power_supply_propval ret = {0,};

	if (!bq->batt_psy) 
		bq->batt_psy = power_supply_get_by_name("battery");

	if (bq->batt_psy) {
		bq->batt_psy->get_property(bq->batt_psy,POWER_SUPPLY_PROP_CAPACITY,&ret);
		return ret.intval;
	} else {
		return 50;
	}
}

static void bq2589x_adjust_absolute_vindpm(struct bq2589x *bq)
{
	u16 vbus_volt;
	u16 vindpm_volt;
	int ret;

	ret = bq2589x_disable_charger(bq);	
	if (ret < 0) {
		dev_err(bq->dev,"%s:failed to disable charger\n",__func__);
		/*return;*/
	}
	/* wait for new adc data */
	msleep(1000);
	vbus_volt = bq2589x_adc_read_vbus_volt(bq);
	ret = bq2589x_enable_charger(bq);
	if (ret < 0) {
		dev_err(bq->dev, "%s:failed to enable charger\n",__func__);
		return;
	}

    //<6000MV ? vin  = vbus -0.6V : vin=vbus - 1.2V
	if (vbus_volt < 6000)
		vindpm_volt = vbus_volt - 600;
	else
		vindpm_volt = vbus_volt - 1200;
	ret = bq2589x_set_input_volt_limit(bq, vindpm_volt);
	if (ret < 0)
		dev_err(bq->dev, "%s:Set absolute vindpm threshold %d Failed:%d\n", __func__, vindpm_volt, ret);
	else
		dprintk(1,bq->dev, "%s:Set absolute vindpm threshold %d successfully\n", __func__, vindpm_volt);

}

// reconfig charge state  and parameters
void bq2589x_reconfig()
{

    if(g_bq){
        cancel_work_sync(&g_bq->irq_work);
        cancel_work_sync(&g_bq->adapter_in_work);
        cancel_work_sync(&g_bq->adapter_out_work);
        cancel_delayed_work_sync(&g_bq->monitor_work);
        cancel_delayed_work_sync(&g_bq->ico_work);
        cancel_delayed_work_sync(&g_bq->check_pe_tuneup_work);
        cancel_delayed_work_sync(&g_bq->pe_volt_tune_work);

        //restart work
        schedule_work(&g_bq->irq_work);/*in case of adapter has been in when power off*/
    }
}
EXPORT_SYMBOL_GPL(bq2589x_reconfig);

extern bool dock_is_intert();
#define OTHER_ADAPTER_MA  1200
#define LIMIT_MA          1800
static void bq2589x_adapter_in_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, adapter_in_work);
	int ret;
    int cc;
        int current_ma;
	printk("qiuguang %s\n",__func__);
        /*
         * add for dock in
         * when dock is intert(DK1/DK5), set fast charging current to 1200mA 
         */
        if (dock_is_intert())
            current_ma = OTHER_ADAPTER_MA;
        else
            current_ma = bq->cfg.charge_current;

		now_ichg = current_ma;
        dprintk(0,bq->dev, "%s: current_ma is %d, vbus_type %s  line is %d\n", __func__, current_ma,bq2589x_vbus_type_str[bq->vbus_type], __LINE__);

	if (bq->vbus_type == BQ2589X_VBUS_MAXC) {
		dprintk(1,bq->dev, "%s:HVDCP or Maxcharge adapter plugged in\n", __func__);

		ret = bq2589x_set_chargecurrent(bq, current_ma,"HVDCP or MAXC");
		if (ret < 0) 
			dev_err(bq->dev, "%s:Failed to set charge current:%d\n", __func__, ret);
		else
			dprintk(1,bq->dev, "%s: Set charge current to %dmA successfully\n",__func__,current_ma);

                bq2589x_set_input_current_limit(bq, LIMIT_MA);
		schedule_delayed_work(&bq->ico_work, 0);
	} else if (bq->vbus_type == BQ2589X_VBUS_USB_DCP) {/* DCP, let's check if it is PE adapter*/
		dprintk(1,bq->dev, "%s:usb dcp adapter plugged in\n", __func__);

		ret = bq2589x_set_chargecurrent(bq, current_ma,"USB_DCP");
		if (ret < 0) 
			dev_err(bq->dev, "%s:Failed to set charge current:%d\n", __func__, ret);
		else
			dprintk(1,bq->dev, "%s: Set charge current to %dmA successfully\n",__func__,current_ma);

                bq2589x_set_input_current_limit(bq, LIMIT_MA);
		schedule_delayed_work(&bq->check_pe_tuneup_work, 0);
	} else if (bq->vbus_type == BQ2589X_VBUS_USB_SDP || bq->vbus_type == BQ2589X_VBUS_UNKNOWN) {
		if (bq->vbus_type == BQ2589X_VBUS_USB_SDP){
                        usb_path_selecter(0);// connected to host
			dprintk(1,bq->dev, "%s:host SDP plugged in\n", __func__);
                }
		else
			dprintk(1,bq->dev, "%s:unknown adapter plugged in\n", __func__);

                if (bq->vbus_type == BQ2589X_VBUS_USB_SDP){
		        ret = bq2589x_set_chargecurrent(bq, 500,"USB_SDP");
						now_ichg = 500;
                        bq2589x_set_input_current_limit(bq, 500);
                        cc=500;
                } else {

	                    bq2589x_set_vindpm_offset(bq, 600);
                        bq2589x_set_input_current_limit(bq, LIMIT_MA);
                        ret = bq2589x_set_chargecurrent(bq, 1200,"USB_UNKNOWN");
						now_ichg = 1200;
                        schedule_delayed_work(&bq->ico_work, 0);
                        cc=1200;
                }
		if (ret < 0) 
			dev_err(bq->dev, "%s:Failed to set charge current:%d\n", __func__, ret);
		else
			dprintk(1,bq->dev, "%s: Set charge current to %dmA successfully\n",__func__,cc);
	}
	else {	
                bq2589x_set_input_current_limit(bq, LIMIT_MA);
		dprintk(1,bq->dev, "%s:other adapter plugged in,vbus_type is %d\n", __func__, bq->vbus_type);
		ret = bq2589x_set_chargecurrent(bq, OTHER_ADAPTER_MA,"OTHER_ADPTER");
		now_ichg = OTHER_ADAPTER_MA;
		if (ret < 0) 
			dev_err(bq->dev, "%s:Failed to set charge current:%d\n", __func__, ret);
		else
			dprintk(1,bq->dev, "%s: Set charge current to %dmA successfully\n",__func__, OTHER_ADAPTER_MA);
		schedule_delayed_work(&bq->ico_work, 0);
	}

        bq2589x_read_chargecurrent(bq,NULL);
	if (bq->cfg.use_absolute_vindpm)
		bq2589x_adjust_absolute_vindpm(bq);

		printk("now_ichg = %d \n",now_ichg);
	schedule_delayed_work(&bq->monitor_work, 0);
}

static void bq2589x_adapter_out_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, adapter_out_work);
	int ret;

        //wake up screen, when adapter is out
        rk28_send_wakeup_key();

	ret = bq2589x_set_input_volt_limit(bq, 4400);
	if (ret < 0)
		dev_err(bq->dev,"%s:reset vindpm threshold to 4400 failed:%d\n",__func__,ret);
	else
		dprintk(1,bq->dev,"%s:reset vindpm threshold to 4400 successfully\n",__func__);

	cancel_delayed_work_sync(&bq->monitor_work);
}

static void bq2589x_otg_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, otg_work.work);

        _bq2589x_set_otg(bq, bq->otg_enable);

}

static void bq2589x_ico_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, ico_work.work);
	int ret;
	int idpm;
	u8 status;
	static bool ico_issued;

	if (!ico_issued) {
		ret = bq2589x_force_ico(bq);
		if (ret < 0) {
			schedule_delayed_work(&bq->ico_work, HZ); /* retry 1 second later*/
			dprintk(1,bq->dev, "%s:ICO command issued failed:%d\n", __func__, ret);
		} else {
			ico_issued = true;
			schedule_delayed_work(&bq->ico_work, 3 * HZ);
			dprintk(1,bq->dev, "%s:ICO command issued successfully\n", __func__);
		}
	} else {
		ico_issued = false;
		ret = bq2589x_check_force_ico_done(bq);
		if (ret) {/*ico done*/
			ret = bq2589x_read_byte(bq, &status, BQ2589X_REG_13);
			if (ret == 0) {
				idpm = ((status & BQ2589X_IDPM_LIM_MASK) >> BQ2589X_IDPM_LIM_SHIFT) * BQ2589X_IDPM_LIM_LSB + BQ2589X_IDPM_LIM_BASE;
				dprintk(1,bq->dev, "%s:ICO done, result is:%d mA\n", __func__, idpm);
			}
		}
	}
}

static void bq2589x_check_pe_tuneup_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, check_pe_tuneup_work.work);

	if (!pe.enable) {
		schedule_delayed_work(&bq->ico_work, 0);
		return;
	}

	bq->vbat_volt = bq2589x_adc_read_battery_volt(bq);
	bq->rsoc = bq2589x_read_batt_rsoc(bq); 

	if (bq->vbat_volt > pe.vbat_min_volt && bq->rsoc < 95) {
		dprintk(1,bq->dev, "%s:trying to tune up vbus voltage\n", __func__);
		pe.target_volt = pe.high_volt_level;
		pe.tune_up_volt = true;
		pe.tune_down_volt = false;
		pe.tune_done = false;
		pe.tune_count = 0;
		pe.tune_fail = false;
		schedule_delayed_work(&bq->pe_volt_tune_work, 0);
	} else if (bq->rsoc >= 95) {
		schedule_delayed_work(&bq->ico_work, 0);
	} else {
		/* wait battery voltage up enough to check again */
		schedule_delayed_work(&bq->check_pe_tuneup_work, 2*HZ);
	}
}

static void bq2589x_tune_volt_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, pe_volt_tune_work.work);
	int ret;
	static bool pumpx_cmd_issued;

	bq->vbus_volt = bq2589x_adc_read_vbus_volt(bq);

	dprintk(1,bq->dev, "%s:vbus voltage:%d, Tune Target Volt:%d\n", __func__, bq->vbus_volt, pe.target_volt);

	if ((pe.tune_up_volt && bq->vbus_volt > pe.target_volt) ||
	    (pe.tune_down_volt && bq->vbus_volt < pe.target_volt)) {
		dprintk(1,bq->dev, "%s:voltage tune successfully\n", __func__);
		pe.tune_done = true;
		bq2589x_adjust_absolute_vindpm(bq);
		if (pe.tune_up_volt)
			schedule_delayed_work(&bq->ico_work, 0);
		return;
	}

	if (pe.tune_count > 10) {
		dprintk(1,bq->dev, "%s:voltage tune failed,reach max retry count\n", __func__);
		pe.tune_fail = true;
		bq2589x_adjust_absolute_vindpm(bq);

		if (pe.tune_up_volt)
			schedule_delayed_work(&bq->ico_work, 0);
		return;
	}

	if (!pumpx_cmd_issued) {
		if (pe.tune_up_volt)
			ret = bq2589x_pumpx_increase_volt(bq);
		else if (pe.tune_down_volt)
			ret =  bq2589x_pumpx_decrease_volt(bq);
		if (ret) {
			schedule_delayed_work(&bq->pe_volt_tune_work, HZ);
		} else {
			dprintk(1,bq->dev, "%s:pumpx command issued.\n", __func__);
			pumpx_cmd_issued = true;
			pe.tune_count++;
			schedule_delayed_work(&bq->pe_volt_tune_work, 3*HZ);
		}
	} else {
		if (pe.tune_up_volt)
			ret = bq2589x_pumpx_increase_volt_done(bq);
		else if (pe.tune_down_volt)
			ret = bq2589x_pumpx_decrease_volt_done(bq);
		if (ret == 0) {
			dprintk(1,bq->dev, "%s:pumpx command finishedd!\n", __func__);
			bq2589x_adjust_absolute_vindpm(bq);
			pumpx_cmd_issued = 0;
		}
		schedule_delayed_work(&bq->pe_volt_tune_work, HZ);
	}
}

void thermal_contrller(struct bq2589x *bq){

    int target_current;
	if(bq->temperature >= 550){
		target_current = 700;		
	}else if(bq->temperature >= 520){
		target_current = 700 + 70 * (550-bq->temperature);
	}else{
		target_current = now_ichg;
	}
		printk("target_current:%d,temperature:%d\n",target_current,bq->temperature);
		bq2589x_set_chargecurrent(bq,target_current,"null");
	switch (thermal_s) {
		case LOW_SHUTDOWN:
			bq2589x_disable_charger(bq);
			if(bq->temperature >= -190){
				thermal_s = LOW_TEMP_STOP_CHARGING;
			}
			break;		
		case LOW_TEMP_STOP_CHARGING:
			bq2589x_disable_charger(bq);
			if(bq->temperature >= 10){
				thermal_s = LOW_TEMP_LIMIT_CURRENT;
			}else if(bq->temperature <=-200){
				thermal_s = LOW_SHUTDOWN;
				pm_power_off();
			}
			break;

		case LOW_TEMP_LIMIT_CURRENT:
			bq2589x_enable_charger(bq);
			if(bq->temperature < 0){
				thermal_s = LOW_TEMP_STOP_CHARGING;
			}else if(bq->temperature > 150)
			{
				thermal_s = NORMAL;
			}
			break;
		case NORMAL:
			bq2589x_enable_charger(bq);
			if(bq->temperature < 150){
				thermal_s = LOW_TEMP_LIMIT_CURRENT;
			}else if(bq->temperature > 510)
			{
				thermal_s = HIGH_TEMP_LIMIT_CURRENT;
			}
			break;
		case HIGH_TEMP_LIMIT_CURRENT:
			
			bq2589x_enable_charger(bq);
			if(bq->temperature < 480){
				thermal_s = NORMAL;
			}else if(bq->temperature > 560)
			{
				thermal_s = HIGH_TEMP_STOP_CHARGING;
			}
			break;
		case HIGH_TEMP_STOP_CHARGING:
			bq2589x_disable_charger(bq);
			if(bq->temperature < 530){
				thermal_s = HIGH_TEMP_LIMIT_CURRENT;
			}else if(bq->temperature > 660)
			{
				thermal_s = HIGH_SHUTDOWN;;
			}
			break;
		case HIGH_SHUTDOWN:
			bq2589x_disable_charger(bq);
			if(bq->temperature < 650){
				thermal_s = HIGH_TEMP_STOP_CHARGING;
			}
			break;
		default:
			break;
		}
	printk("temperature = %d  chg_current = %d,thermal = %d \n",bq->temperature,
			bq2589x_adc_read_charge_current(bq),thermal_s);
}
extern dwc_vbus_status();
/*
 * Battery-longevity charge limit.
 *
 * When set, charging is stopped once the battery reaches charge_limit% and
 * resumed only after it falls CHARGE_LIMIT_HYST% below, so the Li-ion pack is
 * spared from sitting at a constant high float voltage (the main driver of
 * calendar ageing). 0 = off (charge to full, stock behaviour).
 *
 *   echo 80 > /sys/module/bq2589x_charger/parameters/charge_limit   # cap at 80%
 *   echo 0  > /sys/module/bq2589x_charger/parameters/charge_limit   # disable, charge to full
 */
#define CHARGE_LIMIT_MIN   50
#define CHARGE_LIMIT_HYST  3
static int charge_limit;          /* 0 = off; 50..99 = stop-charge SOC% */
static bool g_chg_stopped;        /* charger currently held off by us (limit or full) */

static int charge_limit_set(const char *val, const struct kernel_param *kp)
{
	int l, rv = kstrtoint(val, 0, &l);
	if (rv)
		return rv;
	if (l != 0 && (l < CHARGE_LIMIT_MIN || l > 99))
		return -EINVAL;          /* commit only after validation */
	charge_limit = l;
	if (g_bq)                        /* re-evaluate now rather than up to 10s later */
		schedule_delayed_work(&g_bq->monitor_work, 0);
	return 0;
}
static const struct kernel_param_ops charge_limit_ops = {
	.set = charge_limit_set,
	.get = param_get_int,
};
module_param_cb(charge_limit, &charge_limit_ops, &charge_limit, 0644);
MODULE_PARM_DESC(charge_limit,
	"Stop charging at this battery % (0=off, 50-99); spares the pack from full-float wear");

static void bq2589x_monitor_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, monitor_work.work);
	u8 status = 0;
	int ret;
	int chg_current;
        int usb_status = dwc_vbus_status();
        static bool usb_flag;

        /* if (usb_status == 1 && !usb_flag) { */
            /* bq2589x_force_dpdm(bq); */
            /* schedule_work(&bq->adapter_in_work); */
            /* usb_flag = true; */
        /* } */

	bq2589x_reset_watchdog_timer(bq);

	bq->rsoc = bq2589x_read_batt_rsoc(bq);
	bq->temperature = bq2589x_read_batt_temp(bq);
	bq->vbus_volt = bq2589x_adc_read_vbus_volt(bq);
	bq->vbat_volt = bq2589x_adc_read_battery_volt(bq);
	chg_current = bq2589x_adc_read_charge_current(bq);
    bq2589x_read_chargecurrent(bq,NULL);
	thermal_contrller(bq);
	dprintk(0,bq->dev, "%s:vbus volt:%d,vbat volt:%d,charge current:%d target: %d\n", __func__,bq->vbus_volt,bq->vbat_volt,chg_current,bq->curr_get);

	ret = bq2589x_read_byte(bq, &status, BQ2589X_REG_13);
	if (ret == 0 && (status & BQ2589X_VDPM_STAT_MASK))
		dprintk(1,bq->dev, "%s:VINDPM occurred\n", __func__);
	if (ret == 0 && (status & BQ2589X_IDPM_STAT_MASK))
		dprintk(1,bq->dev, "%s:IINDPM occurred\n", __func__);
		
	if (bq->vbus_type == BQ2589X_VBUS_USB_DCP && bq->vbus_volt > pe.high_volt_level &&
	    bq->rsoc > 95 && !pe.tune_down_volt) {
		pe.tune_down_volt = true;
		pe.tune_up_volt = false;
		pe.target_volt = pe.low_volt_level;
		pe.tune_done = false;
		pe.tune_count = 0;
		pe.tune_fail = false;
		schedule_delayed_work(&bq->pe_volt_tune_work, 0);
	}

        if (charge_limit >= CHARGE_LIMIT_MIN && charge_limit < 100) {
            /* battery-longevity cap: hold charging in a hysteresis band below the limit */
            if (bq->rsoc >= charge_limit && !g_chg_stopped) {
                dprintk(0,bq->dev, "%s: rsoc %d%% reached charge_limit %d%%, stopping charge\n", __func__, bq->rsoc, charge_limit);
                if (bq2589x_disable_charger(bq) == 0)
                    g_chg_stopped = true;
                else
                    dev_err(bq->dev,"%s:failed to disable charger\n",__func__);
            } else if (bq->rsoc <= charge_limit - CHARGE_LIMIT_HYST && g_chg_stopped) {
                dprintk(0,bq->dev, "%s: rsoc %d%% fell below charge_limit-%d, resuming charge\n", __func__, bq->rsoc, CHARGE_LIMIT_HYST);
                if (bq2589x_enable_charger(bq) == 0)
                    g_chg_stopped = false;
            }
        } else {
            /* limit off: stock full-charge termination, and resume if the cap was just cleared */
            if (g_chg_stopped && bq->rsoc < 100) {
                dprintk(0,bq->dev, "%s: charge_limit cleared, resuming charge\n", __func__);
                if (bq2589x_enable_charger(bq) == 0)
                    g_chg_stopped = false;
            } else if (bq->rsoc == 100 && chg_current <= 85 /*500*/ && !g_chg_stopped) {
                dprintk(0,bq->dev, "%s:  charger  done,  and disable  charger!\n", __func__);
                if (bq2589x_disable_charger(bq) == 0)
                    g_chg_stopped = true;
                else
                    dev_err(bq->dev,"%s:failed to disable charger\n",__func__);
            }
        }

	/* read temperature,or any other check if need to decrease charge current*/

	schedule_delayed_work(&bq->monitor_work, 10 * HZ);
}

static void  bq2589x_vindpm_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, vindpm_work);
        int ret;

        if (dock_is_intert()) {
            dprintk(0, bq->dev, "%s: dock is intert !\n", __func__);
            //schedule_work(&bq->adapter_in_work);
        } else {
            dprintk(0, bq->dev, "%s: dock is not intert !\n", __func__);
            usb_path_selecter(1);
            bq2589x_disable_hvdcp(bq, false);
            bq2589x_force_dpdm(bq);
            msleep(2000);
            schedule_work(&bq->adapter_in_work);
        }
}


static void bq2589x_charger_irq_workfunc(struct work_struct *work)
{
	struct bq2589x *bq = container_of(work, struct bq2589x, irq_work);
	u8 status = 0;
	u8 fault = 0;
	u8 charge_status = 0;
	int ret;
        static bool vindpm_flag;


    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	msleep(500);

    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	/* Read STATUS and FAULT registers */
	ret = bq2589x_read_byte(bq, &status, BQ2589X_REG_0B);
	if (ret)
		return;

    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	ret = bq2589x_read_byte(bq, &fault, BQ2589X_REG_0C);
	if (ret)
		return;
	
    bq->vbus_type_old=bq->vbus_type;
	bq->vbus_type = (status & BQ2589X_VBUS_STAT_MASK) >> BQ2589X_VBUS_STAT_SHIFT;
    dprintk(0,bq->dev,"BQ25890 %s %d status 0x%x  vbus_type  %s",__func__,__LINE__,status,bq2589x_vbus_type_str[bq->vbus_type]);

	if (((bq->vbus_type == BQ2589X_VBUS_NONE) || (bq->vbus_type == BQ2589X_VBUS_OTG)) && (bq->status & BQ2589X_STATUS_PLUGIN)) {
		dprintk(1,bq->dev, "%s:adapter removed\n", __func__);
		bq->status &= ~BQ2589X_STATUS_PLUGIN;
		schedule_work(&bq->adapter_out_work);

                usb_path_selecter(0);// connected to soc //charger 
                bq2589x_disable_hvdcp(bq, true);
	} else if (bq->vbus_type != BQ2589X_VBUS_NONE && (bq->vbus_type != BQ2589X_VBUS_OTG) && !(bq->status & BQ2589X_STATUS_PLUGIN)) {
		dprintk(0,bq->dev, "%s:adapter plugged in\n", __func__);
		bq->status |= BQ2589X_STATUS_PLUGIN;
                //usb_path_selecter(0);
                bq2589x_disable_hvdcp(bq, true);

                if (!vindpm_flag) {
                    schedule_delayed_work(&bq->vindpm_work, msecs_to_jiffies(7000));
                    vindpm_flag = true;
                } else {
                    schedule_delayed_work(&bq->vindpm_work, msecs_to_jiffies(2000));
                }
		schedule_work(&bq->adapter_in_work);
        // following enable VBUS_MAXC to 2800mA
	}else if ((bq->vbus_type != BQ2589X_VBUS_NONE) && (bq->vbus_type != BQ2589X_VBUS_OTG) 
              && (bq->status & BQ2589X_STATUS_PLUGIN) && (bq->vbus_type_old!=bq->vbus_type)) {
		dprintk(0,bq->dev, "%s:adapter plugged in and vbus_type have changed %s -> %s\n", __func__,bq2589x_vbus_type_str[bq->vbus_type_old],bq2589x_vbus_type_str[bq->vbus_type]);
		schedule_work(&bq->adapter_in_work);

    }

    dev_dbg(bq->dev,"BQ25890 %s %d status 0x%x",__func__,__LINE__,status);
	if ((status & BQ2589X_PG_STAT_MASK) && !(bq->status & BQ2589X_STATUS_PG))
		bq->status |= BQ2589X_STATUS_PG;
	else if (!(status & BQ2589X_PG_STAT_MASK) && (bq->status & BQ2589X_STATUS_PG))
		bq->status &= ~BQ2589X_STATUS_PG;

	if (fault && !(bq->status & BQ2589X_STATUS_FAULT))
		bq->status |= BQ2589X_STATUS_FAULT;
	else if (!fault && (bq->status & BQ2589X_STATUS_FAULT))
		bq->status &= ~BQ2589X_STATUS_FAULT;

    dev_dbg(bq->dev,"BQ25890 %s %d status 0x%x",__func__,__LINE__,status);
	charge_status = (status & BQ2589X_CHRG_STAT_MASK) >> BQ2589X_CHRG_STAT_SHIFT;
	if (charge_status == BQ2589X_CHRG_STAT_IDLE)
		dprintk(1,bq->dev, "%s:not charging\n", __func__);
	else if (charge_status == BQ2589X_CHRG_STAT_PRECHG)
		dprintk(1,bq->dev, "%s:precharging\n", __func__);
	else if (charge_status == BQ2589X_CHRG_STAT_FASTCHG)
		dprintk(1,bq->dev, "%s:fast charging\n", __func__);
	else if (charge_status == BQ2589X_CHRG_STAT_CHGDONE)
		dprintk(1,bq->dev, "%s:charge done!\n", __func__);
	
	if (fault)
		dprintk(1,bq->dev, "%s:charge fault:%02x\n", __func__,fault);
}


static irqreturn_t bq2589x_charger_interrupt(int irq, void *data)
{
	struct bq2589x *bq = data;

    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	schedule_work(&bq->irq_work);
	return IRQ_HANDLED;
}


static int bq2589x_charger_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct bq2589x *bq;
	int irqn;

	int ret;

	bq = devm_kzalloc(&client->dev, sizeof(struct bq2589x), GFP_KERNEL);
	if (!bq) {
		dev_err(&client->dev, "%s: out of memory\n", __func__);
		return -ENOMEM;
	}

    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	bq->dev = &client->dev;
	bq->client = client;
	i2c_set_clientdata(client, bq);
    bq->platform_data = client->dev.platform_data;

	ret = bq2589x_detect_device(bq);
	if (!ret && bq->part_no == BQ25890) {
		bq->status |= BQ2589X_STATUS_EXIST;
		dprintk(1,bq->dev, "%s: charger device bq25890 detected, revision:%d\n", __func__, bq->revision);
	} else {
		dprintk(1,bq->dev, "%s: no bq25890 charger device found:%d\n", __func__, ret);
		return -ENODEV;
	}

    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	bq->batt_psy = power_supply_get_by_name("battery");

	g_bq = bq;

	/* if (client->dev.of_node) */
		/* bq2589x_parse_dt(&client->dev, bq); */
    bq2589x_init_platform_data(bq);


	ret = bq2589x_init_device(bq);
	if (ret) {
		dev_err(bq->dev, "device init failure: %d\n", ret);
		goto err_0;
	}
    bq->irq_gpio=client->irq;
	ret = gpio_request(client->irq, "bq2589x irq pin");
	if (ret) {
		dev_err(bq->dev, "%s: %d gpio request failed\n", __func__, client->irq);
		goto err_0;
	}
	gpio_direction_input(client->irq);

	irqn = gpio_to_irq(client->irq);
	if (irqn < 0) {
		dev_err(bq->dev, "%s:%d gpio_to_irq failed\n", __func__, irqn);
		ret = irqn;
		goto err_1;
	}
	client->irq = irqn;

	ret = bq2589x_psy_register(bq);
	if (ret)
		goto err_0;

	INIT_WORK(&bq->irq_work, bq2589x_charger_irq_workfunc);
	INIT_WORK(&bq->adapter_in_work, bq2589x_adapter_in_workfunc);
	INIT_WORK(&bq->adapter_out_work, bq2589x_adapter_out_workfunc);
	INIT_DELAYED_WORK(&bq->monitor_work, bq2589x_monitor_workfunc);
	INIT_DELAYED_WORK(&bq->ico_work, bq2589x_ico_workfunc);
	INIT_DELAYED_WORK(&bq->otg_work, bq2589x_otg_workfunc);
	INIT_DELAYED_WORK(&bq->pe_volt_tune_work, bq2589x_tune_volt_workfunc);
	INIT_DELAYED_WORK(&bq->check_pe_tuneup_work, bq2589x_check_pe_tuneup_workfunc);
	INIT_DELAYED_WORK(&bq->vindpm_work, bq2589x_vindpm_workfunc);


    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	ret = sysfs_create_group(&bq->dev->kobj, &bq2589x_attr_group);
	if (ret) {
		dev_err(bq->dev, "failed to register sysfs. err: %d\n", ret);
		goto err_irq;
	}

	ret = request_irq(client->irq, bq2589x_charger_interrupt, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "bq2589x_charger1_irq", bq);
	if (ret) {
		dev_err(bq->dev, "%s:Request IRQ %d failed: %d\n", __func__, client->irq, ret);
		goto err_irq;
	} else {
		dprintk(1,bq->dev, "%s:irq = %d\n", __func__, client->irq);
	}

	pe.enable = false;
	/* schedule_work(&bq->irq_work);[>in case of adapter has been in when power off<] */

    // force to detect usb status when system up
    bq2589x_force_dpdm(bq);
    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	return 0;

err_irq:
	cancel_work_sync(&bq->irq_work);
	cancel_work_sync(&bq->adapter_in_work);
	cancel_work_sync(&bq->adapter_out_work);
	cancel_delayed_work_sync(&bq->monitor_work);
	cancel_delayed_work_sync(&bq->ico_work);
	cancel_delayed_work_sync(&bq->check_pe_tuneup_work);
	cancel_delayed_work_sync(&bq->pe_volt_tune_work);
	cancel_delayed_work_sync(&bq->vindpm_work);
err_1:
	gpio_free(bq->irq_gpio);
err_0:
	g_bq = NULL;
    dev_dbg(bq->dev,"BQ25890 %s %d ",__func__,__LINE__);
	return ret;
}

static void bq2589x_charger_shutdown(struct i2c_client *client)
{
	struct bq2589x *bq = i2c_get_clientdata(client);

	dprintk(1,bq->dev, "%s: shutdown\n", __func__);
        usb_path_selecter(0);
        mdelay(1);

	bq2589x_psy_unregister(bq);

	sysfs_remove_group(&bq->dev->kobj, &bq2589x_attr_group);
	cancel_work_sync(&bq->irq_work);
	cancel_work_sync(&bq->adapter_in_work);
	cancel_work_sync(&bq->adapter_out_work);
	cancel_delayed_work_sync(&bq->monitor_work);
	cancel_delayed_work_sync(&bq->ico_work);
	cancel_delayed_work_sync(&bq->check_pe_tuneup_work);
	cancel_delayed_work_sync(&bq->pe_volt_tune_work);
	cancel_delayed_work_sync(&bq->vindpm_work);

	/* free_irq(bq->client->irq, NULL); */
	/* gpio_free(bq->irq_gpio); */
	/* g_bq = NULL; */
}

static struct of_device_id bq2589x_charger_match_table[] = {
	{.compatible = "ti,bq2589x-1",},
	{},
};


static const struct i2c_device_id bq2589x_charger_id[] = {
	{ "bq2589x-1", BQ25890 },
	{},
};

MODULE_DEVICE_TABLE(i2c, bq2589x_charger_id);

static struct i2c_driver bq2589x_charger= {
	.driver		= {
		.name	= "bq2589x-1",
		.of_match_table = bq2589x_charger_match_table,
	},
	.id_table	= bq2589x_charger_id,

	.probe		= bq2589x_charger_probe,
	.shutdown   = bq2589x_charger_shutdown,
};

static int __init bq2589x_mod_init(void)
{
    int ret;
    printk("bq2589x_charger mod init \n");
    ret=i2c_add_driver(&bq2589x_charger);
    return ret;
}

static void __exit bq2589x_mod_exit(void)
{

    printk(KERN_ERR "bq2589x_charger mod exit\n");
        i2c_del_driver(&bq2589x_charger);

}
subsys_initcall(bq2589x_mod_init);
module_exit(bq2589x_mod_exit);
/* module_i2c_driver(bq2589x_charger_driver); */

MODULE_DESCRIPTION("TI BQ2589x Charger Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments");
