#ifndef __PLAT_GPIO_H
#define __PLAT_GPIO_H

#include <linux/types.h>

//定义GPIO相关寄存器偏移地址
#define GPIO_SWPORT_DR		0x00
#define GPIO_SWPORT_DDR		0x04
#define GPIO_INTEN		0x30
#define GPIO_INTMASK		0x34
#define GPIO_INTTYPE_LEVEL	0x38
#define GPIO_INT_POLARITY	0x3c
#define GPIO_INT_STATUS		0x40
#define GPIO_INT_RAWSTATUS	0x44
#define GPIO_DEBOUNCE		0x48
#define GPIO_PORTS_EOI		0x4c
#define GPIO_EXT_PORT		0x50
#define GPIO_LS_SYNC		0x60

#define NUM_GROUP			32

#define PIN_BASE			NR_GIC_IRQS

#define CPU_TOTOL_GPIO_NUM		NR_GPIO_IRQS

#define SPI_FPGA_EXPANDER_BASE		(PIN_BASE + CPU_TOTOL_GPIO_NUM)

#if defined(CONFIG_IOEXTEND_TCA6424)
#define TCA6424_TOTOL_GPIO_NUM		24
#else
#define TCA6424_TOTOL_GPIO_NUM		0
#endif
#define TCA6424_GPIO_EXPANDER_BASE	(SPI_FPGA_EXPANDER_BASE + CONFIG_SPI_FPGA_GPIO_NUM)

#if defined(CONFIG_GPIO_WM831X)
#define WM831X_TOTOL_GPIO_NUM		12
#else
#define WM831X_TOTOL_GPIO_NUM		0
#endif
#define WM831X_GPIO_EXPANDER_BASE	(TCA6424_GPIO_EXPANDER_BASE + TCA6424_TOTOL_GPIO_NUM)

#if defined(CONFIG_GPIO_WM8994)
#define CONFIG_GPIO_WM8994_NUM		11
#else
#define CONFIG_GPIO_WM8994_NUM		0
#endif
#define WM8994_GPIO_EXPANDER_BASE	(WM831X_GPIO_EXPANDER_BASE + WM831X_TOTOL_GPIO_NUM)

#if defined(CONFIG_GPIO_TPS65910)
#define CONFIG_GPIO_TPS65910_NUM	9
#else
#define CONFIG_GPIO_TPS65910_NUM	0
#endif
#define TPS65910_GPIO_EXPANDER_BASE	(WM8994_GPIO_EXPANDER_BASE + CONFIG_GPIO_WM8994_NUM)

//定义GPIO的PIN口最大数目。CONFIG_SPI_FPGA_GPIO_NUM表示FPGA的PIN脚数。
#define ARCH_NR_GPIOS  (PIN_BASE + CPU_TOTOL_GPIO_NUM + TCA6424_TOTOL_GPIO_NUM + WM831X_TOTOL_GPIO_NUM + CONFIG_SPI_FPGA_GPIO_NUM + CONFIG_GPIO_WM8994_NUM + CONFIG_GPIO_TPS65910_NUM)

#define INVALID_GPIO        	-1

#if defined(CONFIG_SPI_FPGA_GPIO)
#define FPGA_PIO0_00 (SPI_FPGA_EXPANDER_BASE + 0*8 + 0)
#define FPGA_PIO0_01 (SPI_FPGA_EXPANDER_BASE + 0*8 + 1)
#define FPGA_PIO0_02 (SPI_FPGA_EXPANDER_BASE + 0*8 + 2)
#define FPGA_PIO0_03 (SPI_FPGA_EXPANDER_BASE + 0*8 + 3)
#define FPGA_PIO0_04 (SPI_FPGA_EXPANDER_BASE + 0*8 + 4)
#define FPGA_PIO0_05 (SPI_FPGA_EXPANDER_BASE + 0*8 + 5)
#define FPGA_PIO0_06 (SPI_FPGA_EXPANDER_BASE + 0*8 + 6)
#define FPGA_PIO0_07 (SPI_FPGA_EXPANDER_BASE + 0*8 + 7)

#define FPGA_PIO0_08 (SPI_FPGA_EXPANDER_BASE + 1*8 + 0)
#define FPGA_PIO0_09 (SPI_FPGA_EXPANDER_BASE + 1*8 + 1)
#define FPGA_PIO0_10 (SPI_FPGA_EXPANDER_BASE + 1*8 + 2)
#define FPGA_PIO0_11 (SPI_FPGA_EXPANDER_BASE + 1*8 + 3)
#define FPGA_PIO0_12 (SPI_FPGA_EXPANDER_BASE + 1*8 + 4)
#define FPGA_PIO0_13 (SPI_FPGA_EXPANDER_BASE + 1*8 + 5)
#define FPGA_PIO0_14 (SPI_FPGA_EXPANDER_BASE + 1*8 + 6)
#define FPGA_PIO0_15 (SPI_FPGA_EXPANDER_BASE + 1*8 + 7)

#define FPGA_PIO1_00 (SPI_FPGA_EXPANDER_BASE + 2*8 + 0)
#define FPGA_PIO1_01 (SPI_FPGA_EXPANDER_BASE + 2*8 + 1)
#define FPGA_PIO1_02 (SPI_FPGA_EXPANDER_BASE + 2*8 + 2)
#define FPGA_PIO1_03 (SPI_FPGA_EXPANDER_BASE + 2*8 + 3)
#define FPGA_PIO1_04 (SPI_FPGA_EXPANDER_BASE + 2*8 + 4)
#define FPGA_PIO1_05 (SPI_FPGA_EXPANDER_BASE + 2*8 + 5)
#define FPGA_PIO1_06 (SPI_FPGA_EXPANDER_BASE + 2*8 + 6)
#define FPGA_PIO1_07 (SPI_FPGA_EXPANDER_BASE + 2*8 + 7)

#define FPGA_PIO1_08 (SPI_FPGA_EXPANDER_BASE + 3*8 + 0)
#define FPGA_PIO1_09 (SPI_FPGA_EXPANDER_BASE + 3*8 + 1)
#define FPGA_PIO1_10 (SPI_FPGA_EXPANDER_BASE + 3*8 + 2)
#define FPGA_PIO1_11 (SPI_FPGA_EXPANDER_BASE + 3*8 + 3)
#define FPGA_PIO1_12 (SPI_FPGA_EXPANDER_BASE + 3*8 + 4)
#define FPGA_PIO1_13 (SPI_FPGA_EXPANDER_BASE + 3*8 + 5)
#define FPGA_PIO1_14 (SPI_FPGA_EXPANDER_BASE + 3*8 + 6)
#define FPGA_PIO1_15 (SPI_FPGA_EXPANDER_BASE + 3*8 + 7)

#define FPGA_PIO2_00 (SPI_FPGA_EXPANDER_BASE + 4*8 + 0)
#define FPGA_PIO2_01 (SPI_FPGA_EXPANDER_BASE + 4*8 + 1)
#define FPGA_PIO2_02 (SPI_FPGA_EXPANDER_BASE + 4*8 + 2)
#define FPGA_PIO2_03 (SPI_FPGA_EXPANDER_BASE + 4*8 + 3)
#define FPGA_PIO2_04 (SPI_FPGA_EXPANDER_BASE + 4*8 + 4)
#define FPGA_PIO2_05 (SPI_FPGA_EXPANDER_BASE + 4*8 + 5)
#define FPGA_PIO2_06 (SPI_FPGA_EXPANDER_BASE + 4*8 + 6)
#define FPGA_PIO2_07 (SPI_FPGA_EXPANDER_BASE + 4*8 + 7)

#define FPGA_PIO2_08 (SPI_FPGA_EXPANDER_BASE + 5*8 + 0)
#define FPGA_PIO2_09 (SPI_FPGA_EXPANDER_BASE + 5*8 + 1)
#define FPGA_PIO2_10 (SPI_FPGA_EXPANDER_BASE + 5*8 + 2)
#define FPGA_PIO2_11 (SPI_FPGA_EXPANDER_BASE + 5*8 + 3)
#define FPGA_PIO2_12 (SPI_FPGA_EXPANDER_BASE + 5*8 + 4)
#define FPGA_PIO2_13 (SPI_FPGA_EXPANDER_BASE + 5*8 + 5)
#define FPGA_PIO2_14 (SPI_FPGA_EXPANDER_BASE + 5*8 + 6)
#define FPGA_PIO2_15 (SPI_FPGA_EXPANDER_BASE + 5*8 + 7)

#define FPGA_PIO3_00 (SPI_FPGA_EXPANDER_BASE + 6*8 + 0)
#define FPGA_PIO3_01 (SPI_FPGA_EXPANDER_BASE + 6*8 + 1)
#define FPGA_PIO3_02 (SPI_FPGA_EXPANDER_BASE + 6*8 + 2)
#define FPGA_PIO3_03 (SPI_FPGA_EXPANDER_BASE + 6*8 + 3)
#define FPGA_PIO3_04 (SPI_FPGA_EXPANDER_BASE + 6*8 + 4)
#define FPGA_PIO3_05 (SPI_FPGA_EXPANDER_BASE + 6*8 + 5)
#define FPGA_PIO3_06 (SPI_FPGA_EXPANDER_BASE + 6*8 + 6)
#define FPGA_PIO3_07 (SPI_FPGA_EXPANDER_BASE + 6*8 + 7)

#define FPGA_PIO3_08 (SPI_FPGA_EXPANDER_BASE + 7*8 + 0)
#define FPGA_PIO3_09 (SPI_FPGA_EXPANDER_BASE + 7*8 + 1)
#define FPGA_PIO3_10 (SPI_FPGA_EXPANDER_BASE + 7*8 + 2)
#define FPGA_PIO3_11 (SPI_FPGA_EXPANDER_BASE + 7*8 + 3)
#define FPGA_PIO3_12 (SPI_FPGA_EXPANDER_BASE + 7*8 + 4)
#define FPGA_PIO3_13 (SPI_FPGA_EXPANDER_BASE + 7*8 + 5)
#define FPGA_PIO3_14 (SPI_FPGA_EXPANDER_BASE + 7*8 + 6)
#define FPGA_PIO3_15 (SPI_FPGA_EXPANDER_BASE + 7*8 + 7)

#define FPGA_PIO4_00 (SPI_FPGA_EXPANDER_BASE + 8*8 + 0)
#define FPGA_PIO4_01 (SPI_FPGA_EXPANDER_BASE + 8*8 + 1)
#define FPGA_PIO4_02 (SPI_FPGA_EXPANDER_BASE + 8*8 + 2)
#define FPGA_PIO4_03 (SPI_FPGA_EXPANDER_BASE + 8*8 + 3)
#define FPGA_PIO4_04 (SPI_FPGA_EXPANDER_BASE + 8*8 + 4)
#define FPGA_PIO4_05 (SPI_FPGA_EXPANDER_BASE + 8*8 + 5)
#define FPGA_PIO4_06 (SPI_FPGA_EXPANDER_BASE + 8*8 + 6)
#define FPGA_PIO4_07 (SPI_FPGA_EXPANDER_BASE + 8*8 + 7)

#define FPGA_PIO4_08 (SPI_FPGA_EXPANDER_BASE + 9*8 + 0)
#define FPGA_PIO4_09 (SPI_FPGA_EXPANDER_BASE + 9*8 + 1)
#define FPGA_PIO4_10 (SPI_FPGA_EXPANDER_BASE + 9*8 + 2)
#define FPGA_PIO4_11 (SPI_FPGA_EXPANDER_BASE + 9*8 + 3)
#define FPGA_PIO4_12 (SPI_FPGA_EXPANDER_BASE + 9*8 + 4)
#define FPGA_PIO4_13 (SPI_FPGA_EXPANDER_BASE + 9*8 + 5)
#define FPGA_PIO4_14 (SPI_FPGA_EXPANDER_BASE + 9*8 + 6)
#define FPGA_PIO4_15 (SPI_FPGA_EXPANDER_BASE + 9*8 + 7)

#define FPGA_PIO5_00 (SPI_FPGA_EXPANDER_BASE + 10*8 + 0)
#define FPGA_PIO5_01 (SPI_FPGA_EXPANDER_BASE + 10*8 + 1)
#define FPGA_PIO5_02 (SPI_FPGA_EXPANDER_BASE + 10*8 + 2)
#define FPGA_PIO5_03 (SPI_FPGA_EXPANDER_BASE + 10*8 + 3)
#define FPGA_PIO5_04 (SPI_FPGA_EXPANDER_BASE + 10*8 + 4)
#define FPGA_PIO5_05 (SPI_FPGA_EXPANDER_BASE + 10*8 + 5)
#define FPGA_PIO5_06 (SPI_FPGA_EXPANDER_BASE + 10*8 + 6)
#define FPGA_PIO5_07 (SPI_FPGA_EXPANDER_BASE + 10*8 + 7)

#define FPGA_PIO5_08 (SPI_FPGA_EXPANDER_BASE + 11*8 + 0)
#define FPGA_PIO5_09 (SPI_FPGA_EXPANDER_BASE + 11*8 + 1)
#define FPGA_PIO5_10 (SPI_FPGA_EXPANDER_BASE + 11*8 + 2)
#define FPGA_PIO5_11 (SPI_FPGA_EXPANDER_BASE + 11*8 + 3)
#define FPGA_PIO5_12 (SPI_FPGA_EXPANDER_BASE + 11*8 + 4)
#define FPGA_PIO5_13 (SPI_FPGA_EXPANDER_BASE + 11*8 + 5)
#define FPGA_PIO5_14 (SPI_FPGA_EXPANDER_BASE + 11*8 + 6)
#define FPGA_PIO5_15 (SPI_FPGA_EXPANDER_BASE + 11*8 + 7)
#endif

#if defined(CONFIG_IOEXTEND_TCA6424)
#define TCA6424_P00 (TCA6424_GPIO_EXPANDER_BASE + 0*8 + 0)
#define TCA6424_P01 (TCA6424_GPIO_EXPANDER_BASE + 0*8 + 1)
#define TCA6424_P02 (TCA6424_GPIO_EXPANDER_BASE + 0*8 + 2)
#define TCA6424_P03 (TCA6424_GPIO_EXPANDER_BASE + 0*8 + 3)
#define TCA6424_P04 (TCA6424_GPIO_EXPANDER_BASE + 0*8 + 4)
#define TCA6424_P05 (TCA6424_GPIO_EXPANDER_BASE + 0*8 + 5)
#define TCA6424_P06 (TCA6424_GPIO_EXPANDER_BASE + 0*8 + 6)
#define TCA6424_P07 (TCA6424_GPIO_EXPANDER_BASE + 0*8 + 7)

#define TCA6424_P10 (TCA6424_GPIO_EXPANDER_BASE + 1*8 + 0)
#define TCA6424_P11 (TCA6424_GPIO_EXPANDER_BASE + 1*8 + 1)
#define TCA6424_P12 (TCA6424_GPIO_EXPANDER_BASE + 1*8 + 2)
#define TCA6424_P13 (TCA6424_GPIO_EXPANDER_BASE + 1*8 + 3)
#define TCA6424_P14 (TCA6424_GPIO_EXPANDER_BASE + 1*8 + 4)
#define TCA6424_P15 (TCA6424_GPIO_EXPANDER_BASE + 1*8 + 5)
#define TCA6424_P16 (TCA6424_GPIO_EXPANDER_BASE + 1*8 + 6)
#define TCA6424_P17 (TCA6424_GPIO_EXPANDER_BASE + 1*8 + 7)

#define TCA6424_P20 (TCA6424_GPIO_EXPANDER_BASE + 2*8 + 0)
#define TCA6424_P21 (TCA6424_GPIO_EXPANDER_BASE + 2*8 + 1)
#define TCA6424_P22 (TCA6424_GPIO_EXPANDER_BASE + 2*8 + 2)
#define TCA6424_P23 (TCA6424_GPIO_EXPANDER_BASE + 2*8 + 3)
#define TCA6424_P24 (TCA6424_GPIO_EXPANDER_BASE + 2*8 + 4)
#define TCA6424_P25 (TCA6424_GPIO_EXPANDER_BASE + 2*8 + 5)
#define TCA6424_P26 (TCA6424_GPIO_EXPANDER_BASE + 2*8 + 6)
#define TCA6424_P27 (TCA6424_GPIO_EXPANDER_BASE + 2*8 + 7)
#endif

#if defined(CONFIG_GPIO_WM831X)
#define WM831X_P01 (WM831X_GPIO_EXPANDER_BASE + 0*8 + 0)
#define WM831X_P02 (WM831X_GPIO_EXPANDER_BASE + 0*8 + 1)
#define WM831X_P03 (WM831X_GPIO_EXPANDER_BASE + 0*8 + 2)
#define WM831X_P04 (WM831X_GPIO_EXPANDER_BASE + 0*8 + 3)
#define WM831X_P05 (WM831X_GPIO_EXPANDER_BASE + 0*8 + 4)
#define WM831X_P06 (WM831X_GPIO_EXPANDER_BASE + 0*8 + 5)
#define WM831X_P07 (WM831X_GPIO_EXPANDER_BASE + 0*8 + 6)
#define WM831X_P08 (WM831X_GPIO_EXPANDER_BASE + 0*8 + 7)

#define WM831X_P09 (WM831X_GPIO_EXPANDER_BASE + 1*8 + 0)
#define WM831X_P10 (WM831X_GPIO_EXPANDER_BASE + 1*8 + 1)
#define WM831X_P11 (WM831X_GPIO_EXPANDER_BASE + 1*8 + 2)
#define WM831X_P12 (WM831X_GPIO_EXPANDER_BASE + 1*8 + 3)
#endif

/*
 * tp_int = <bank><goff><off><driving force><wake_en><irq_flags><reserve>
 * tp_rst = <bank><goff><off><driving force><active_low><pull_mode><reserve>
 * gpio = RKXX_PIN(bank)_P(goff)(off)
 * e.g.  bank=2, goff=A, off=3 ==>gpio is RKXX_PIN2_PA3
 */
enum {
        PULL_MODE_NONE = 0,
        PULL_MODE_DISABLE,
        PULL_MODE_ENABLE,
};
struct irq_config{
        unsigned int off:4,  //bit[3:0]
                     goff:4,
                     bank:4,
                     driving_force:4, 
                     wake_en:4,
                     irq_flags:4,
                     reserve:8;
};
struct gpio_config{
        unsigned int off:4, //bit[3:0]
                     goff:4,
                     bank:4,
                     driving_force:4,
                     active_low:4,
                     pull_mode:4, 
                     reserve:8;
};
struct port_config {
        union{
                struct irq_config irq;
                struct gpio_config io;
                unsigned int v;
        };
        int gpio;
};
static inline struct port_config get_port_config(unsigned int value)
{
        struct port_config port;

        port.v = value;
        if(value == 0xffffffff)
                port.gpio = INVALID_GPIO;
        else
                port.gpio = PIN_BASE + port.io.bank * 32 + (port.io.goff - 0x0A) * 8 + port.io.off;

        return port;
}
void gpio_set_iomux(int gpio);
int port_output_init(unsigned int value, int on, char *name);
void port_output_on(unsigned int value);
void port_output_off(unsigned int value);
int port_input_init(unsigned int value, char *name);
int port_get_value(unsigned int value);
void port_deinit(unsigned int value);

typedef enum eGPIOPinLevel
{
	GPIO_LOW=0,
	GPIO_HIGH
}eGPIOPinLevel_t;

typedef enum eGPIOPinDirection
{
	GPIO_IN=0,
	GPIO_OUT
}eGPIOPinDirection_t;

typedef enum GPIOPullType {
	PullDisable = 0,
	PullEnable = 1,
	GPIONormal = PullDisable,
	GPIOPullUp = 1,
	GPIOPullDown = 2,
}eGPIOPullType_t;

typedef enum GPIOIntType {
	GPIOLevelLow=0,
	GPIOLevelHigh,	 
	GPIOEdgelFalling,
	GPIOEdgelRising
}eGPIOIntType_t;

static inline bool gpio_is_valid(int number)
{
	return number >= PIN_BASE && number < ARCH_NR_GPIOS;
}

static inline bool get_gpio_base_and_nrgpios(unsigned* gpio_base,unsigned* gpio_nr)
{
   *gpio_base=PIN_BASE;
   *gpio_nr=ARCH_NR_GPIOS;
	return 0;
}
#define gpio_is_valid gpio_is_valid

extern void __init rk30_gpio_init(void);

static inline int gpio_to_irq(unsigned gpio)
{
	return gpio - PIN_BASE + NR_GIC_IRQS;
}

static inline int irq_to_gpio(unsigned irq)
{
	return irq - NR_GIC_IRQS + PIN_BASE;
}

#include <asm/errno.h>
#include <asm-generic/gpio.h>		/* cansleep wrappers */

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep

#endif
