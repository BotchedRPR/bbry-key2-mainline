/*
* linux/arch/arm/mach-omap1/board-sx1.c
*
* Modified from board-generic.c
*
* Support for the Siemens SX1 mobile phone.
*
* Original version : Vladimir Ananiev (Vovan888-at-gmail com)
*
* Maintainters : Vladimir Ananiev (aka Vovan888), Sergge
*		oslik.ru
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/errno.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/irda.h>
#include <mach/usb.h>
#include <mach/tc.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/mcbsp.h>
#include <mach/omap-alsa.h>
#include <mach/keypad.h>

/* Write to I2C device */
int sx1_i2c_write_byte(u8 devaddr, u8 regoffset, u8 value)
{
	struct i2c_adapter *adap;
	int err;
	struct i2c_msg msg[1];
	unsigned char data[2];

	adap = i2c_get_adapter(0);
	if (!adap)
		return -ENODEV;
	msg->addr = devaddr;	/* I2C address of chip */
	msg->flags = 0;
	msg->len = 2;
	msg->buf = data;
	data[0] = regoffset;	/* register num */
	data[1] = value;		/* register data */
	err = i2c_transfer(adap, msg, 1);
	i2c_put_adapter(adap);
	if (err >= 0)
		return 0;
	return err;
}

/* Read from I2C device */
int sx1_i2c_read_byte(u8 devaddr, u8 regoffset, u8 *value)
{
	struct i2c_adapter *adap;
	int err;
	struct i2c_msg msg[1];
	unsigned char data[2];

	adap = i2c_get_adapter(0);
	if (!adap)
		return -ENODEV;

	msg->addr = devaddr;	/* I2C address of chip */
	msg->flags = 0;
	msg->len = 1;
	msg->buf = data;
	data[0] = regoffset;	/* register num */
	err = i2c_transfer(adap, msg, 1);

	msg->addr = devaddr;	/* I2C address */
	msg->flags = I2C_M_RD;
	msg->len = 1;
	msg->buf = data;
	err = i2c_transfer(adap, msg, 1);
	*value = data[0];
	i2c_put_adapter(adap);

	if (err >= 0)
		return 0;
	return err;
}
/* set keyboard backlight intensity */
int sx1_setkeylight(u8 keylight)
{
	if (keylight > SOFIA_MAX_LIGHT_VAL)
		keylight = SOFIA_MAX_LIGHT_VAL;
	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_KEYLIGHT_REG, keylight);
}
/* get current keylight intensity */
int sx1_getkeylight(u8 * keylight)
{
	return sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_KEYLIGHT_REG, keylight);
}
/* set LCD backlight intensity */
int sx1_setbacklight(u8 backlight)
{
	if (backlight > SOFIA_MAX_LIGHT_VAL)
		backlight = SOFIA_MAX_LIGHT_VAL;
	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_BACKLIGHT_REG,
				  backlight);
}
/* get current LCD backlight intensity */
int sx1_getbacklight (u8 * backlight)
{
	return sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_BACKLIGHT_REG,
				 backlight);
}
/* set LCD backlight power on/off */
int sx1_setmmipower(u8 onoff)
{
	int err;
	u8 dat = 0;
	err = sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, &dat);
	if (err < 0)
		return err;
	if (onoff)
		dat |= SOFIA_MMILIGHT_POWER;
	else
		dat &= ~SOFIA_MMILIGHT_POWER;
	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, dat);
}

/* set USB power on/off */
int sx1_setusbpower(u8 onoff)
{
	int err;
	u8 dat = 0;
	err = sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, &dat);
	if (err < 0)
		return err;
	if (onoff)
		dat |= SOFIA_USB_POWER;
	else
		dat &= ~SOFIA_USB_POWER;
	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, dat);
}

EXPORT_SYMBOL(sx1_setkeylight);
EXPORT_SYMBOL(sx1_getkeylight);
EXPORT_SYMBOL(sx1_setbacklight);
EXPORT_SYMBOL(sx1_getbacklight);
EXPORT_SYMBOL(sx1_setmmipower);
EXPORT_SYMBOL(sx1_setusbpower);

/*----------- Keypad -------------------------*/

static int sx1_keymap[] = {
	KEY(5, 3, GROUP_0 | 117), /* camera Qt::Key_F17 */
	KEY(0, 4, GROUP_0 | 114), /* voice memo Qt::Key_F14 */
	KEY(1, 4, GROUP_2 | 114), /* voice memo */
	KEY(2, 4, GROUP_3 | 114), /* voice memo */
	KEY(0, 0, GROUP_1 | KEY_F12),	/* red button Qt::Key_Hangup */
	KEY(4, 3, GROUP_1 | KEY_LEFT),
	KEY(2, 3, GROUP_1 | KEY_DOWN),
	KEY(1, 3, GROUP_1 | KEY_RIGHT),
	KEY(0, 3, GROUP_1 | KEY_UP),
	KEY(3, 3, GROUP_1 | KEY_POWER), /* joystick press or Qt::Key_Select */
	KEY(5, 0, GROUP_1 | KEY_1),
	KEY(4, 0, GROUP_1 | KEY_2),
	KEY(3, 0, GROUP_1 | KEY_3),
	KEY(3, 4, GROUP_1 | KEY_4),
	KEY(4, 4, GROUP_1 | KEY_5),
	KEY(5, 4, GROUP_1 | KEY_KPASTERISK),/* "*" */
	KEY(4, 1, GROUP_1 | KEY_6),
	KEY(5, 1, GROUP_1 | KEY_7),
	KEY(3, 1, GROUP_1 | KEY_8),
	KEY(3, 2, GROUP_1 | KEY_9),
	KEY(5, 2, GROUP_1 | KEY_0),
	KEY(4, 2, GROUP_1 | 113),	/* # F13 Toggle input method Qt::Key_F13 */
	KEY(0, 1, GROUP_1 | KEY_F11),	/* green button Qt::Key_Call */
	KEY(1, 2, GROUP_1 | KEY_YEN),	/* left soft Qt::Key_Context1 */
	KEY(2, 2, GROUP_1 | KEY_F8),	/* right soft Qt::Key_Back */
	KEY(2, 1, GROUP_1 | KEY_LEFTSHIFT), /* shift */
	KEY(1, 1, GROUP_1 | KEY_BACKSPACE), /* C (clear) */
	KEY(0, 2, GROUP_1 | KEY_F7),	/* menu Qt::Key_Menu */
	0
};

static struct resource sx1_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_kp_platform_data sx1_kp_data = {
	.rows		= 6,
	.cols		= 6,
	.keymap	= sx1_keymap,
	.keymapsize = ARRAY_SIZE(sx1_keymap),
	.delay	= 80,
};

static struct platform_device sx1_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &sx1_kp_data,
	},
	.num_resources	= ARRAY_SIZE(sx1_kp_resources),
	.resource	= sx1_kp_resources,
};

/*----------- IRDA -------------------------*/

static struct omap_irda_config sx1_irda_data = {
	.transceiver_cap	= IR_SIRMODE,
	.rx_channel		= OMAP_DMA_UART3_RX,
	.tx_channel		= OMAP_DMA_UART3_TX,
	.dest_start		= UART3_THR,
	.src_start		= UART3_RHR,
	.tx_trigger		= 0,
	.rx_trigger		= 0,
};

static struct resource sx1_irda_resources[] = {
	[0] = {
		.start	= INT_UART3,
		.end	= INT_UART3,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 irda_dmamask = 0xffffffff;

static struct platform_device sx1_irda_device = {
	.name		= "omapirda",
	.id		= 0,
	.dev		= {
		.platform_data	= &sx1_irda_data,
		.dma_mask	= &irda_dmamask,
	},
	.num_resources	= ARRAY_SIZE(sx1_irda_resources),
	.resource	= sx1_irda_resources,
};

/*----------- McBSP & Sound -------------------------*/

/* Playback interface - McBSP1 */
static struct omap_mcbsp_reg_cfg mcbsp1_regs = {
	.spcr2	= XINTM(3),	/* SPCR2=30 */
	.spcr1	= RINTM(3),	/* SPCR1=30 */
	.rcr2	= 0,	/* RCR2 =00 */
	.rcr1	= RFRLEN1(1) | RWDLEN1(OMAP_MCBSP_WORD_16),	/* RCR1=140 */
	.xcr2	= 0,	/* XCR2 = 0 */
	.xcr1	= XFRLEN1(1) | XWDLEN1(OMAP_MCBSP_WORD_16),	/* XCR1 = 140 */
	.srgr1	= FWID(15) | CLKGDV(12),	/* SRGR1=0f0c */
	.srgr2	= FSGM | FPER(31),	/* SRGR2=101f */
	.pcr0	= FSXM | FSRM | CLKXM | CLKRM | FSXP | FSRP | CLKXP | CLKRP,
						/* PCR0 =0f0f */
};

static struct omap_alsa_codec_config sx1_alsa_config = {
	.name			= "SX1 EGold",
	.mcbsp_regs_alsa	= &mcbsp1_regs,
};

static struct platform_device sx1_mcbsp1_device = {
	.name	= "omap_alsa_mcbsp",
	.id	= 1,
	.dev = {
		.platform_data	= &sx1_alsa_config,
	},
};

/*----------- MTD -------------------------*/

static struct mtd_partition sx1_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
		.name		= "bootloader",
		.offset		= 0x01800000,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= 0,
	},
	/* kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M - 2 * SZ_128K,
		.mask_flags	= 0
	},
	/* file system */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct flash_platform_data sx1_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= sx1_partitions,
	.nr_parts	= ARRAY_SIZE(sx1_partitions),
};

#ifdef CONFIG_SX1_OLD_FLASH
/* MTD Intel StrataFlash - old flashes */
static struct resource sx1_old_flash_resource[] = {
	[0] = {
		.start	= OMAP_CS0_PHYS,	/* Physical */
		.end	= OMAP_CS0_PHYS + SZ_16M - 1,,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_CS1_PHYS,
		.end	= OMAP_CS1_PHYS + SZ_8M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device sx1_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &sx1_flash_data,
	},
	.num_resources	= 2,
	.resource	= &sx1_old_flash_resource,
};
#else
/* MTD Intel 4000 flash - new flashes */
static struct resource sx1_new_flash_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device sx1_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &sx1_flash_data,
	},
	.num_resources	= 1,
	.resource	= &sx1_new_flash_resource,
};
#endif

/*----------- USB -------------------------*/

static struct omap_usb_config sx1_usb_config __initdata = {
	.otg		= 0,
	.register_dev	= 1,
	.register_host	= 0,
	.hmc_mode	= 0,
	.pins[0]	= 2,
	.pins[1]	= 0,
	.pins[2]	= 0,
};

/*----------- LCD -------------------------*/

static struct platform_device sx1_lcd_device = {
	.name		= "lcd_sx1",
	.id		= -1,
};

static struct omap_lcd_config sx1_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

/*-----------------------------------------*/
static struct platform_device *sx1_devices[] __initdata = {
	&sx1_flash_device,
	&sx1_kp_device,
	&sx1_lcd_device,
	&sx1_mcbsp1_device,
	&sx1_irda_device,
};
/*-----------------------------------------*/

static struct omap_uart_config sx1_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel sx1_config[] __initdata = {
	{ OMAP_TAG_USB,	&sx1_usb_config },
	{ OMAP_TAG_LCD,	&sx1_lcd_config },
	{ OMAP_TAG_UART,	&sx1_uart_config },
};

/*-----------------------------------------*/

static void __init omap_sx1_init(void)
{
	platform_add_devices(sx1_devices, ARRAY_SIZE(sx1_devices));

	omap_board_config = sx1_config;
	omap_board_config_size = ARRAY_SIZE(sx1_config);
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);
	sx1_mmc_init();

	/* turn on USB power */
	/* sx1_setusbpower(1); cant do it here because i2c is not ready */
	omap_request_gpio(1);	/* A_IRDA_OFF */
	omap_request_gpio(11);	/* A_SWITCH */
	omap_request_gpio(15);	/* A_USB_ON */
	gpio_direction_output(1, 1);	/*A_IRDA_OFF = 1 */
	gpio_direction_output(11, 0);	/*A_SWITCH = 0 */
	gpio_direction_output(15, 0);	/*A_USB_ON = 0 */
}
/*----------------------------------------*/
static void __init omap_sx1_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
}
/*----------------------------------------*/

static void __init omap_sx1_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(SX1, "OMAP310 based Siemens SX1")
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= omap_sx1_map_io,
	.init_irq		= omap_sx1_init_irq,
	.init_machine	= omap_sx1_init,
	.timer		= &omap_timer,
MACHINE_END
