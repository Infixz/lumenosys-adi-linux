/*
 * AD7879/AD7889 touchscreen (SPI bus)
 *
 * Copyright (C) 2008-2010 Michael Hennerich, Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>	/* BUS_SPI */
#include <linux/spi/spi.h>

#include "ad7879.h"

#define AD7879_DEVID		0x7A	/* AD7879/AD7889 */

#define MAX_SPI_FREQ_HZ      5000000
#define AD7879_CMD_MAGIC     0xE000
#define AD7879_CMD_READ      (1 << 10)
#define AD7879_CMD(reg)      (AD7879_CMD_MAGIC | ((reg) & 0xF))
#define AD7879_WRITECMD(reg) (AD7879_CMD(reg))
#define AD7879_READCMD(reg)  (AD7879_CMD(reg) | AD7879_CMD_READ)

#ifdef CONFIG_PM
static int ad7879_spi_suspend(struct spi_device *spi, pm_message_t message)
{
	return ad7879_disable(&spi->dev);
}

static int ad7879_spi_resume(struct spi_device *spi)
{
	return ad7879_enable(&spi->dev);
}
#else
# define ad7879_spi_suspend NULL
# define ad7879_spi_resume  NULL
#endif

/*
 * ad7879_read/write are only used for initial setup and for sysfs controls.
 * The main traffic is done in ad7879_collect().
 */

static int ad7879_spi_xfer(void *spi, u16 cmd, u8 count, u16 *tx_buf, u16 *rx_buf)
{
	struct spi_message msg;
	struct spi_transfer *xfers;
	void *spi_data;
	u16 *command;
	u16 *_rx_buf = _rx_buf; /* shut gcc up */
	u8 idx;
	int ret;

	xfers = spi_data = kzalloc(sizeof(*xfers) * (count + 2), GFP_KERNEL);
	if (!spi_data)
		return -ENOMEM;

	spi_message_init(&msg);

	command = spi_data;
	command[0] = cmd;
	if (count == 1) {
		/* ad7879_spi_{read,write} gave us buf on stack */
		command[1] = *tx_buf;
		tx_buf = &command[1];
		_rx_buf = rx_buf;
		rx_buf = &command[2];
	}

	++xfers;
	xfers[0].tx_buf = command;
	xfers[0].len = 2;
	spi_message_add_tail(&xfers[0], &msg);
	++xfers;

	for (idx = 0; idx < count; ++idx) {
		if (rx_buf)
			xfers[idx].rx_buf = &rx_buf[idx];
		if (tx_buf)
			xfers[idx].tx_buf = &tx_buf[idx];
		xfers[idx].len = 2;
		spi_message_add_tail(&xfers[idx], &msg);
	}

	ret = spi_sync(spi, &msg);

	if (count == 1)
		_rx_buf[0] = command[2];

	kfree(spi_data);

	return ret;
}

static int ad7879_spi_multi_read(void *spi, u8 first_reg, u8 count, u16 *buf)
{
	return ad7879_spi_xfer(spi, AD7879_READCMD(first_reg), count, NULL, buf);
}

static int ad7879_spi_read(void *spi, u8 reg)
{
	u16 ret, dummy;
	return ad7879_spi_xfer(spi, AD7879_READCMD(reg), 1, &dummy, &ret) ? : ret;
}

static int ad7879_spi_write(void *spi, u8 reg, u16 val)
{
	u16 dummy;
	return ad7879_spi_xfer(spi, AD7879_WRITECMD(reg), 1, &val, &dummy);
}

static int __devinit ad7879_spi_probe(struct spi_device *spi)
{
	struct ad7879_bus_ops bops = {
		.bus_data = spi,
		.irq = spi->irq,
		.read = ad7879_spi_read,
		.multi_read = ad7879_spi_multi_read,
		.write = ad7879_spi_write,
	};

	/* don't exceed max specified SPI CLK frequency */
	if (spi->max_speed_hz > MAX_SPI_FREQ_HZ) {
		dev_err(&spi->dev, "SPI CLK %d Hz?\n", spi->max_speed_hz);
		return -EINVAL;
	}

	return ad7879_probe(&spi->dev, &bops, AD7879_DEVID, BUS_SPI);
}

static int __devexit ad7879_spi_remove(struct spi_device *spi)
{
	return ad7879_remove(&spi->dev);
}

static struct spi_driver ad7879_spi_driver = {
	.driver = {
		.name	= "ad7879",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ad7879_spi_probe,
	.remove		= __devexit_p(ad7879_spi_remove),
	.suspend	= ad7879_spi_suspend,
	.resume		= ad7879_spi_resume,
};

static int __init ad7879_spi_init(void)
{
	return spi_register_driver(&ad7879_spi_driver);
}
module_init(ad7879_spi_init);

static void __exit ad7879_spi_exit(void)
{
	spi_unregister_driver(&ad7879_spi_driver);
}
module_exit(ad7879_spi_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("AD7879(-1) touchscreen SPI bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ad7879");
