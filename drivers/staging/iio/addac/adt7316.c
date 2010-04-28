/*
 * ADT7316 digital temperature sensor driver supporting ADT7316/7/8
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/rtc.h>

#include "../iio.h"
#include "../sysfs.h"
#include "adt7316.h"

/*
 * ADT7316 registers definition
 */
#define ADT7316_INT_STAT1               0x0
#define ADT7316_INT_STAT2               0x1
#define ADT7316_LSB_IN_TEMP_VDD         0x3
#define ADT7316_LSB_IN_TEMP_MASK	0x3
#define ADT7316_LSB_VDD_MASK		0xC
#define ADT7316_LSB_VDD_OFFSET		2
#define ADT7316_LSB_EX_TEMP             0x4
#define ADT7316_LSB_EX_TEMP_MASK	0x3
#define ADT7316_AD_MSB_DATA_BASE        0x6
#define ADT7316_AD_MSB_DATA_REGS        3
#define ADT7316_MSB_VDD                 0x6
#define ADT7316_MSB_IN_TEMP             0x7
#define ADT7316_MSB_EX_TEMP             0x8
#define ADT7316_DA_DATA_BASE		0x10
#define ADT7316_DA_MSB_DATA_REGS	4
#define ADT7316_LSB_DAC_A		0x10
#define ADT7316_MSB_DAC_A		0x11
#define ADT7316_LSB_DAC_B		0x12
#define ADT7316_MSB_DAC_B		0x13
#define ADT7316_LSB_DAC_C		0x14
#define ADT7316_MSB_DAC_C		0x15
#define ADT7316_LSB_DAC_D		0x16
#define ADT7316_MSB_DAC_D		0x17
#define ADT7316_CONFIG1			0x18
#define ADT7316_CONFIG2			0x19
#define ADT7316_CONFIG3			0x1A
#define ADT7316_LDAC_CONFIG		0x1B
#define ADT7316_DAC_CONFIG		0x1C
#define ADT7316_INT_MASK1		0x1D
#define ADT7316_INT_MASK2		0x1E
#define ADT7316_IN_TEMP_OFFSET		0x1F
#define ADT7316_EX_TEMP_OFFSET		0x20
#define ADT7316_IN_ANALOG_TEMP_OFFSET	0x21
#define ADT7316_EX_ANALOG_TEMP_OFFSET	0x22
#define ADT7316_VDD_HIGH		0x23
#define ADT7316_VDD_LOW			0x24
#define ADT7316_IN_TEMP_HIGH		0x25
#define ADT7316_IN_TEMP_LOW		0x26
#define ADT7316_EX_TEMP_HIGH		0x27
#define ADT7316_EX_TEMP_LOW		0x28
#define ADT7316_DEVICE_ID		0x4D
#define ADT7316_MANUFACTURE_ID		0x4E
#define ADT7316_DEVICE_REV		0x4F
#define ADT7316_SPI_LOCK_STAT		0x7F

/*
 * ADT7316 config1
 */
#define ADT7316_EN			0x1
#define ADT7316_INT_EN			0x20
#define ADT7316_INT_POLARITY		0x40
#define ADT7316_PD			0x80

/*
 * ADT7316 config2
 */
#define ADT7316_AD_SINGLE_CH_MASK	0x3
#define ADT7316_AD_SINGLE_CH_VDD	0
#define ADT7316_AD_SINGLE_CH_IN		1
#define ADT7316_AD_SINGLE_CH_EX		2
#define ADT7316_AD_SINGLE_CH_MODE	0x10
#define ADT7316_DISABLE_AVERAGING	0x20
#define ADT7316_EN_SMBUS_TIMEOUT	0x40
#define ADT7316_RESET			0x80

/*
 * ADT7316 config3
 */
#define ADT7316_ADCLK_22_5		0x1
#define ADT7316_DA_HIGH_RESOLUTION	0x2
#define ADT7316_DA_EN_VIA_DAC_LDCA	0x4
#define ADT7316_EN_IN_TEMP_PROP_DACA	0x20
#define ADT7316_EN_EX_TEMP_PROP_DACB	0x40

/*
 * ADT7316 DAC config
 */
#define ADT7316_DA_2VREF_CH_MASK	0xF
#define ADT7316_DA_EN_MODE_MASK		0x30
#define ADT7316_DA_EN_MODE_SINGLE	0x00
#define ADT7316_DA_EN_MODE_AB_CD	0x10
#define ADT7316_DA_EN_MODE_ABCD		0x20
#define ADT7316_DA_EN_MODE_LDAC		0x30
#define ADT7316_VREF_BYPASS_DAC_AB	0x40
#define ADT7316_VREF_BYPASS_DAC_CD	0x80

/*
 * ADT7316 LDAC config
 */
#define ADT7316_LDAC_EN_DA_MASK		0xF
#define ADT7316_IN_VREF			0x10

/*
 * ADT7316 INT_MASK2
 */
#define ADT7316_INT_MASK2_VDD		0x10

/*
 * ADT7316 value masks
 */
#define ADT7316_VALUE_MASK		0xfff
#define ADT7316_T_VALUE_SIGN		0x400
#define ADT7316_T_VALUE_FLOAT_OFFSET	2
#define ADT7316_T_VALUE_FLOAT_MASK	0x2

/*
 * struct adt7316_chip_info - chip specifc information
 */

struct adt7316_chip_info {
	const char		*name;
	struct iio_dev		*indio_dev;
	struct iio_work_cont	work_cont_thresh;
	s64			last_timestamp;
	struct adt7316_bus	bus;
	u16			ldac_pin;
	u8			config1;
	u8			config2;
	u8			config3;
	u8			dac_config;	/* DAC config */
	u8			ldac_config;	/* LDAC config */
	u8			dac_bits;
	u8			int_mask;
};

/*
 * Logic interrupt mask for user application to enable
 * interrupts.
 */
#define ADT7316_IN_TEMP_HIGH_INT_MASK	0x1
#define ADT7316_IN_TEMP_LOW_INT_MASK	0x2
#define ADT7316_EX_TEMP_HIGH_INT_MASK	0x4
#define ADT7316_EX_TEMP_LOW_INT_MASK	0x8
#define ADT7316_EX_TEMP_FAULT_INT_MASK	0x10
#define ADT7316_VDD_INT_MASK		0x20
#define ADT7316_TEMP_INT_MASK		0x1F

/*
 * struct adt7316_chip_info - chip specifc information
 */

struct adt7316_limit_regs {
	u16	data_high;
	u16	data_low;
};

static ssize_t adt7316_show_enabled(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", !!(chip->config1 & ADT7316_EN));
}

static ssize_t _adt7316_store_enabled(struct adt7316_chip_info *chip,
		int enable)
{
	u8 config1;
	int ret;

	if (enable)
		config1 = chip->config1 | ADT7316_EN;
	else
		config1 = chip->config1 & ~ADT7316_EN;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, config1);
	if (ret)
		return -EIO;

	chip->config1 = config1;

	return ret;

}

static ssize_t adt7316_store_enabled(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	int enable;

	if (!memcmp(buf, "1", 1))
		enable = 1;
	else
		enable = 0;

	if (_adt7316_store_enabled(chip, enable) < 0)
		return -EIO;
	else
		return len;
}

static IIO_DEVICE_ATTR(enabled, S_IRUGO | S_IWUSR,
		adt7316_show_enabled,
		adt7316_store_enabled,
		0);

static ssize_t adt7316_show_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	if (chip->config2 & ADT7316_AD_SINGLE_CH_MODE)
		return sprintf(buf, "single_channel\n");
	else
		return sprintf(buf, "round_robin\n");
}

static ssize_t adt7316_store_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config2;
	int ret;

	config2 = chip->config2 & (~ADT7316_AD_SINGLE_CH_MODE);
	if (!memcmp(buf, "single_channel", 14))
		config2 |= ADT7316_AD_SINGLE_CH_MODE;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	chip->config2 = config2;

	return len;
}

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		adt7316_show_mode,
		adt7316_store_mode,
		0);

static ssize_t adt7316_show_all_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "single_channel\nround_robin\n");
}

static IIO_DEVICE_ATTR(all_modes, S_IRUGO, adt7316_show_all_modes, NULL, 0);

static ssize_t adt7316_show_ad_channel(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	if (!(chip->config2 & ADT7316_AD_SINGLE_CH_MODE))
		return -EPERM;

	switch (chip->config2 & ADT7316_AD_SINGLE_CH_MASK) {
	case ADT7316_AD_SINGLE_CH_VDD:
		return sprintf(buf, "0 - VDD\n");
	case ADT7316_AD_SINGLE_CH_IN:
		return sprintf(buf, "1 - Internal Temperature\n");
	case ADT7316_AD_SINGLE_CH_EX:
		return sprintf(buf, "2 - External Temperature\n");
	default:
		return sprintf(buf, "N/A\n");
	};
}

static ssize_t adt7316_store_ad_channel(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config2;
	unsigned long data = 0;
	int ret;

	if (!(chip->config2 & ADT7316_AD_SINGLE_CH_MODE))
		return -EPERM;

	ret = strict_strtoul(buf, 10, &data);
	if (ret || data > 2)
		return -EINVAL;

	config2 = chip->config2 & (~ADT7316_AD_SINGLE_CH_MASK);

	config2 |= data;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	chip->config2 = config2;

	return len;
}

static IIO_DEVICE_ATTR(ad_channel, S_IRUGO | S_IWUSR,
		adt7316_show_ad_channel,
		adt7316_store_ad_channel,
		0);

static ssize_t adt7316_show_all_ad_channels(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	if (!(chip->config2 & ADT7316_AD_SINGLE_CH_MODE))
		return -EPERM;

	return sprintf(buf, "0 - VDD\n1 - Internal Temperature\n"
				"2 - External Temperature\n");
}

static IIO_DEVICE_ATTR(all_ad_channels, S_IRUGO,
		adt7316_show_all_ad_channels, NULL, 0);

static ssize_t adt7316_show_disable_averaging(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n",
		!!(chip->config2 & ADT7316_DISABLE_AVERAGING));
}

static ssize_t adt7316_store_disable_averaging(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config2;
	int ret;

	config2 = chip->config2 & (~ADT7316_DISABLE_AVERAGING);
	if (!memcmp(buf, "1", 1))
		config2 |= ADT7316_DISABLE_AVERAGING;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	chip->config2 = config2;

	return len;
}

static IIO_DEVICE_ATTR(disable_averaging, S_IRUGO | S_IWUSR,
		adt7316_show_disable_averaging,
		adt7316_store_disable_averaging,
		0);

static ssize_t adt7316_show_enable_smbus_timeout(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n",
		!!(chip->config2 & ADT7316_EN_SMBUS_TIMEOUT));
}

static ssize_t adt7316_store_enable_smbus_timeout(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config2;
	int ret;

	config2 = chip->config2 & (~ADT7316_EN_SMBUS_TIMEOUT);
	if (!memcmp(buf, "1", 1))
		config2 |= ADT7316_EN_SMBUS_TIMEOUT;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	chip->config2 = config2;

	return len;
}

static IIO_DEVICE_ATTR(enable_smbus_timeout, S_IRUGO | S_IWUSR,
		adt7316_show_enable_smbus_timeout,
		adt7316_store_enable_smbus_timeout,
		0);


static ssize_t adt7316_store_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config2;
	int ret;

	config2 = chip->config2 | ADT7316_RESET;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	return len;
}

static IIO_DEVICE_ATTR(reset, S_IWUSR,
		NULL,
		adt7316_store_reset,
		0);

static ssize_t adt7316_show_powerdown(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", !!(chip->config1 & ADT7316_PD));
}

static ssize_t adt7316_store_powerdown(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config1;
	int ret;

	config1 = chip->config1 & (~ADT7316_PD);
	if (!memcmp(buf, "1", 1))
		config1 |= ADT7316_PD;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, config1);
	if (ret)
		return -EIO;

	chip->config1 = config1;

	return len;
}

static IIO_DEVICE_ATTR(powerdown, S_IRUGO | S_IWUSR,
		adt7316_show_powerdown,
		adt7316_store_powerdown,
		0);

static ssize_t adt7316_show_fast_ad_clock(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", !!(chip->config3 & ADT7316_ADCLK_22_5));
}

static ssize_t adt7316_store_fast_ad_clock(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config3;
	int ret;

	config3 = chip->config3 & (~ADT7316_ADCLK_22_5);
	if (!memcmp(buf, "1", 1))
		config3 |= ADT7316_ADCLK_22_5;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(fast_ad_clock, S_IRUGO | S_IWUSR,
		adt7316_show_fast_ad_clock,
		adt7316_store_fast_ad_clock,
		0);

static ssize_t adt7316_show_da_high_resolution(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	if (chip->config3 & ADT7316_DA_HIGH_RESOLUTION) {
		if (!strcmp(chip->name, "adt7316"))
			return sprintf(buf, "1 (12 bits)\n");
		else if (!strcmp(chip->name, "adt7317"))
			return sprintf(buf, "1 (10 bits)\n");
	}

	return sprintf(buf, "0 (8 bits)\n");
}

static ssize_t adt7316_store_da_high_resolution(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config3;
	int ret;

	chip->dac_bits = 8;

	if (!memcmp(buf, "1", 1)) {
		config3 = chip->config3 | ADT7316_DA_HIGH_RESOLUTION;
		if (!strcmp(chip->name, "adt7316"))
			chip->dac_bits = 12;
		else if (!strcmp(chip->name, "adt7317"))
			chip->dac_bits = 10;
	} else
		config3 = chip->config3 & (~ADT7316_DA_HIGH_RESOLUTION);

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(da_high_resolution, S_IRUGO | S_IWUSR,
		adt7316_show_da_high_resolution,
		adt7316_store_da_high_resolution,
		0);

static ssize_t adt7316_show_enable_prop_DACA(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n",
		!!(chip->config3 & ADT7316_EN_IN_TEMP_PROP_DACA));
}

static ssize_t adt7316_store_enable_prop_DACA(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config3;
	int ret;

	config3 = chip->config3 & (~ADT7316_EN_IN_TEMP_PROP_DACA);
	if (!memcmp(buf, "1", 1))
		config3 |= ADT7316_EN_IN_TEMP_PROP_DACA;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(enable_proportion_DACA, S_IRUGO | S_IWUSR,
		adt7316_show_enable_prop_DACA,
		adt7316_store_enable_prop_DACA,
		0);

static ssize_t adt7316_show_enable_prop_DACB(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n",
		!!(chip->config3 & ADT7316_EN_EX_TEMP_PROP_DACB));
}

static ssize_t adt7316_store_enable_prop_DACB(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config3;
	int ret;

	config3 = chip->config3 & (~ADT7316_EN_EX_TEMP_PROP_DACB);
	if (!memcmp(buf, "1", 1))
		config3 |= ADT7316_EN_EX_TEMP_PROP_DACB;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(enable_proportion_DACB, S_IRUGO | S_IWUSR,
		adt7316_show_enable_prop_DACB,
		adt7316_store_enable_prop_DACB,
		0);

static ssize_t adt7316_show_DAC_2Vref_ch_mask(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%x\n",
		chip->dac_config & ADT7316_DA_2VREF_CH_MASK);
}

static ssize_t adt7316_store_DAC_2Vref_ch_mask(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 dac_config;
	unsigned long data = 0;
	int ret;

	ret = strict_strtoul(buf, 16, &data);
	if (ret || data > ADT7316_DA_2VREF_CH_MASK)
		return -EINVAL;

	dac_config = chip->dac_config & (~ADT7316_DA_2VREF_CH_MASK);
	dac_config |= data;

	ret = chip->bus.write(chip->bus.client, ADT7316_DAC_CONFIG, dac_config);
	if (ret)
		return -EIO;

	chip->dac_config = dac_config;

	return len;
}

static IIO_DEVICE_ATTR(DAC_2Vref_channels_mask, S_IRUGO | S_IWUSR,
		adt7316_show_DAC_2Vref_ch_mask,
		adt7316_store_DAC_2Vref_ch_mask,
		0);

static ssize_t adt7316_show_DAC_update_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	if (!(chip->config3 & ADT7316_DA_EN_VIA_DAC_LDCA))
		return sprintf(buf, "manual\n");
	else {
		switch (chip->dac_config & ADT7316_DA_EN_MODE_MASK) {
		case ADT7316_DA_EN_MODE_SINGLE:
			return sprintf(buf, "0 - auto at any MSB DAC writing\n");
		case ADT7316_DA_EN_MODE_AB_CD:
			return sprintf(buf, "1 - auto at MSB DAC AB and CD writing\n");
		case ADT7316_DA_EN_MODE_ABCD:
			return sprintf(buf, "2 - auto at MSB DAC ABCD writing\n");
		default: /* ADT7316_DA_EN_MODE_LDAC */
			return sprintf(buf, "3 - manual\n");
		};
	}
}

static ssize_t adt7316_store_DAC_update_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 dac_config;
	unsigned long data;
	int ret;

	if (!(chip->config3 & ADT7316_DA_EN_VIA_DAC_LDCA))
		return -EPERM;

	ret = strict_strtoul(buf, 10, &data);
	if (ret || data > ADT7316_DA_EN_MODE_MASK)
		return -EINVAL;

	dac_config = chip->dac_config & (~ADT7316_DA_EN_MODE_MASK);
	dac_config |= data;

	ret = chip->bus.write(chip->bus.client, ADT7316_DAC_CONFIG, dac_config);
	if (ret)
		return -EIO;

	chip->dac_config = dac_config;

	return len;
}

static IIO_DEVICE_ATTR(DAC_update_mode, S_IRUGO | S_IWUSR,
		adt7316_show_DAC_update_mode,
		adt7316_store_DAC_update_mode,
		0);

static ssize_t adt7316_show_all_DAC_update_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	if (chip->config3 & ADT7316_DA_EN_VIA_DAC_LDCA)
		return sprintf(buf, "0 - auto at any MSB DAC writing\n"
				"1 - auto at MSB DAC AB and CD writing\n"
				"2 - auto at MSB DAC ABCD writing\n"
				"3 - manual\n");
	else
		return sprintf(buf, "manual\n");
}

static IIO_DEVICE_ATTR(all_DAC_update_modes, S_IRUGO,
		adt7316_show_all_DAC_update_modes, NULL, 0);


static ssize_t adt7316_store_update_DAC(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 ldac_config;
	unsigned long data;
	int ret;

	if (chip->config3 & ADT7316_DA_EN_VIA_DAC_LDCA) {
		if ((chip->dac_config & ADT7316_DA_EN_MODE_MASK) !=
			ADT7316_DA_EN_MODE_LDAC)
			return -EPERM;

		ret = strict_strtoul(buf, 16, &data);
		if (ret || data > ADT7316_LDAC_EN_DA_MASK)
			return -EINVAL;

		ldac_config = chip->ldac_config & (~ADT7316_LDAC_EN_DA_MASK);
		ldac_config |= data;

		ret = chip->bus.write(chip->bus.client, ADT7316_LDAC_CONFIG,
			ldac_config);
		if (ret)
			return -EIO;
	} else {
		gpio_set_value(chip->ldac_pin, 0);
		gpio_set_value(chip->ldac_pin, 1);
	}

	return len;
}

static IIO_DEVICE_ATTR(update_DAC, S_IRUGO | S_IWUSR,
		NULL,
		adt7316_store_update_DAC,
		0);

static ssize_t adt7316_show_DA_AB_Vref_bypass(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n",
		!!(chip->dac_config & ADT7316_VREF_BYPASS_DAC_AB));
}

static ssize_t adt7316_store_DA_AB_Vref_bypass(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 dac_config;
	int ret;

	dac_config = chip->dac_config & (~ADT7316_VREF_BYPASS_DAC_AB);
	if (!memcmp(buf, "1", 1))
		dac_config |= ADT7316_VREF_BYPASS_DAC_AB;

	ret = chip->bus.write(chip->bus.client, ADT7316_DAC_CONFIG, dac_config);
	if (ret)
		return -EIO;

	chip->dac_config = dac_config;

	return len;
}

static IIO_DEVICE_ATTR(DA_AB_Vref_bypass, S_IRUGO | S_IWUSR,
		adt7316_show_DA_AB_Vref_bypass,
		adt7316_store_DA_AB_Vref_bypass,
		0);

static ssize_t adt7316_show_DA_CD_Vref_bypass(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n",
		!!(chip->dac_config & ADT7316_VREF_BYPASS_DAC_CD));
}

static ssize_t adt7316_store_DA_CD_Vref_bypass(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 dac_config;
	int ret;

	dac_config = chip->dac_config & (~ADT7316_VREF_BYPASS_DAC_CD);
	if (!memcmp(buf, "1", 1))
		dac_config |= ADT7316_VREF_BYPASS_DAC_CD;

	ret = chip->bus.write(chip->bus.client, ADT7316_DAC_CONFIG, dac_config);
	if (ret)
		return -EIO;

	chip->dac_config = dac_config;

	return len;
}

static IIO_DEVICE_ATTR(DA_CD_Vref_bypass, S_IRUGO | S_IWUSR,
		adt7316_show_DA_CD_Vref_bypass,
		adt7316_store_DA_CD_Vref_bypass,
		0);

static ssize_t adt7316_show_internal_Vref(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n",
		!!(chip->dac_config & ADT7316_IN_VREF));
}

static ssize_t adt7316_store_internal_Vref(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 ldac_config;
	int ret;

	ldac_config = chip->ldac_config & (~ADT7316_IN_VREF);
	if (!memcmp(buf, "1", 1))
		ldac_config |= ADT7316_IN_VREF;

	ret = chip->bus.write(chip->bus.client, ADT7316_LDAC_CONFIG, ldac_config);
	if (ret)
		return -EIO;

	chip->ldac_config = ldac_config;

	return len;
}

static IIO_DEVICE_ATTR(internal_Vref, S_IRUGO | S_IWUSR,
		adt7316_show_internal_Vref,
		adt7316_store_internal_Vref,
		0);

static ssize_t adt7316_show_temp(struct adt7316_chip_info *chip,
		int channel, char *buf)
{
	u16 data;
	u8 msb, lsb;
	char sign = ' ';
	int ret;

	if ((chip->config2 & ADT7316_AD_SINGLE_CH_MODE) &&
		channel != (chip->config2 & ADT7316_AD_SINGLE_CH_MASK))
		return -EPERM;

	switch (channel) {
	case ADT7316_AD_SINGLE_CH_IN:
		ret = chip->bus.read(chip->bus.client,
			ADT7316_LSB_IN_TEMP_VDD, &lsb);
		if (ret)
			return -EIO;

		ret = chip->bus.read(chip->bus.client,
			ADT7316_AD_MSB_DATA_BASE + channel, &msb);
		if (ret)
			return -EIO;

		data = msb << ADT7316_T_VALUE_FLOAT_OFFSET;
		data |= lsb & ADT7316_LSB_IN_TEMP_MASK;
		break;
	case ADT7316_AD_SINGLE_CH_EX:
		ret = chip->bus.read(chip->bus.client,
			ADT7316_LSB_EX_TEMP, &lsb);
		if (ret)
			return -EIO;

		ret = chip->bus.read(chip->bus.client,
			ADT7316_AD_MSB_DATA_BASE + channel, &msb);
		if (ret)
			return -EIO;

		data = msb << ADT7316_T_VALUE_FLOAT_OFFSET;
		data |= lsb & ADT7316_LSB_EX_TEMP_MASK;
		break;
	default: /* ADT7316_AD_SINGLE_CH_VDD */
		ret = chip->bus.read(chip->bus.client,
			ADT7316_LSB_IN_TEMP_VDD, &lsb);
		if (ret)
			return -EIO;

		ret = chip->bus.read(chip->bus.client,

			ADT7316_AD_MSB_DATA_BASE + channel, &msb);
		if (ret)
			return -EIO;

		data = msb << ADT7316_T_VALUE_FLOAT_OFFSET;
		data |= (lsb & ADT7316_LSB_VDD_MASK) >> ADT7316_LSB_VDD_OFFSET;
		return sprintf(buf, "%d\n", data);
	};

	if (data & ADT7316_T_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (ADT7316_T_VALUE_SIGN << 1) - data;
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.2d\n", sign,
		(data >> ADT7316_T_VALUE_FLOAT_OFFSET),
		(data & ADT7316_T_VALUE_FLOAT_MASK) * 25);
}

static ssize_t adt7316_show_VDD(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_temp(chip, ADT7316_AD_SINGLE_CH_VDD, buf);
}
static IIO_DEVICE_ATTR(vdd, S_IRUGO, adt7316_show_VDD, NULL, 0);

static ssize_t adt7316_show_in_temp(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_temp(chip, ADT7316_AD_SINGLE_CH_IN, buf);
}

static IIO_DEVICE_ATTR(in_temp, S_IRUGO, adt7316_show_in_temp, NULL, 0);

static ssize_t adt7316_show_ex_temp(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_temp(chip, ADT7316_AD_SINGLE_CH_EX, buf);
}

static IIO_DEVICE_ATTR(ex_temp, S_IRUGO, adt7316_show_ex_temp, NULL, 0);

static ssize_t adt7316_show_temp_offset(struct adt7316_chip_info *chip,
		int offset_addr, char *buf)
{
	int data;
	u8 val;
	int ret;

	ret = chip->bus.read(chip->bus.client, offset_addr, &val);
	if (ret)
		return -EIO;

	data = (int)val;
	if (val & 0x80)
		data -= 256;

	return sprintf(buf, "%d\n", data);
}

static ssize_t adt7316_store_temp_offset(struct adt7316_chip_info *chip,
		int offset_addr, const char *buf, size_t len)
{
	long data;
	u8 val;
	int ret;

	ret = strict_strtol(buf, 10, &data);
	if (ret || data > 127 || data < -128)
		return -EINVAL;

	if (data < 0)
		data += 256;

	val = (u8)data;

	ret = chip->bus.write(chip->bus.client, offset_addr, val);
	if (ret)
		return -EIO;

	return len;
}

static ssize_t adt7316_show_in_temp_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_temp_offset(chip, ADT7316_IN_TEMP_OFFSET, buf);
}

static ssize_t adt7316_store_in_temp_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_store_temp_offset(chip, ADT7316_IN_TEMP_OFFSET, buf, len);
}

static IIO_DEVICE_ATTR(in_temp_offset, S_IRUGO | S_IWUSR,
		adt7316_show_in_temp_offset,
		adt7316_store_in_temp_offset, 0);

static ssize_t adt7316_show_ex_temp_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_temp_offset(chip, ADT7316_EX_TEMP_OFFSET, buf);
}

static ssize_t adt7316_store_ex_temp_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_store_temp_offset(chip, ADT7316_EX_TEMP_OFFSET, buf, len);
}

static IIO_DEVICE_ATTR(ex_temp_offset, S_IRUGO | S_IWUSR,
		adt7316_show_ex_temp_offset,
		adt7316_store_ex_temp_offset, 0);

static ssize_t adt7316_show_in_analog_temp_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_temp_offset(chip,
			ADT7316_IN_ANALOG_TEMP_OFFSET, buf);
}

static ssize_t adt7316_store_in_analog_temp_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_store_temp_offset(chip,
			ADT7316_IN_ANALOG_TEMP_OFFSET, buf, len);
}

static IIO_DEVICE_ATTR(in_analog_temp_offset, S_IRUGO | S_IWUSR,
		adt7316_show_in_analog_temp_offset,
		adt7316_store_in_analog_temp_offset, 0);

static ssize_t adt7316_show_ex_analog_temp_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_temp_offset(chip,
			ADT7316_EX_ANALOG_TEMP_OFFSET, buf);
}

static ssize_t adt7316_store_ex_analog_temp_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_store_temp_offset(chip,
			ADT7316_EX_ANALOG_TEMP_OFFSET, buf, len);
}

static IIO_DEVICE_ATTR(ex_analog_temp_offset, S_IRUGO | S_IWUSR,
		adt7316_show_ex_analog_temp_offset,
		adt7316_store_ex_analog_temp_offset, 0);

static ssize_t adt7316_show_DAC(struct adt7316_chip_info *chip,
		int channel, char *buf)
{
	u16 data;
	u8 msb, lsb, offset;
	int ret;

	if (channel >= ADT7316_DA_MSB_DATA_REGS ||
		(channel == 0 &&
		(chip->config3 & ADT7316_EN_IN_TEMP_PROP_DACA)) ||
		(channel == 1 &&
		(chip->config3 & ADT7316_EN_EX_TEMP_PROP_DACB)))
		return -EPERM;

	offset = chip->dac_bits - 8;

	if (chip->dac_bits > 8) {
		ret = chip->bus.read(chip->bus.client,
			ADT7316_DA_DATA_BASE + channel * 2, &lsb);
		if (ret)
			return -EIO;
	}

	ret = chip->bus.read(chip->bus.client,
		ADT7316_DA_DATA_BASE + 1 + channel * 2, &msb);
	if (ret)
		return -EIO;

	data = (msb << offset) + (lsb & ((1 << offset) - 1));

	return sprintf(buf, "%d\n", data);
}

static ssize_t adt7316_store_DAC(struct adt7316_chip_info *chip,
		int channel, const char *buf, size_t len)
{
	u8 msb, lsb, offset;
	unsigned long data;
	int ret;

	if (channel >= ADT7316_DA_MSB_DATA_REGS ||
		(channel == 0 &&
		(chip->config3 & ADT7316_EN_IN_TEMP_PROP_DACA)) ||
		(channel == 1 &&
		(chip->config3 & ADT7316_EN_EX_TEMP_PROP_DACB)))
		return -EPERM;

	offset = chip->dac_bits - 8;

	ret = strict_strtoul(buf, 10, &data);
	if (ret || data >= (1 << chip->dac_bits))
		return -EINVAL;

	if (chip->dac_bits > 8) {
		lsb = data & (1 << offset);
		ret = chip->bus.write(chip->bus.client,
			ADT7316_DA_DATA_BASE + channel * 2, lsb);
		if (ret)
			return -EIO;
	}

	msb = data >> offset;
	ret = chip->bus.write(chip->bus.client,
		ADT7316_DA_DATA_BASE + 1 + channel * 2, msb);
	if (ret)
		return -EIO;

	return len;
}

static ssize_t adt7316_show_DAC_A(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_DAC(chip, 0, buf);
}

static ssize_t adt7316_store_DAC_A(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_store_DAC(chip, 0, buf, len);
}

static IIO_DEVICE_ATTR(DAC_A, S_IRUGO | S_IWUSR, adt7316_show_DAC_A,
		adt7316_store_DAC_A, 0);

static ssize_t adt7316_show_DAC_B(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_DAC(chip, 1, buf);
}

static ssize_t adt7316_store_DAC_B(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_store_DAC(chip, 1, buf, len);
}

static IIO_DEVICE_ATTR(DAC_B, S_IRUGO | S_IWUSR, adt7316_show_DAC_B,
		adt7316_store_DAC_B, 0);

static ssize_t adt7316_show_DAC_C(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_DAC(chip, 2, buf);
}

static ssize_t adt7316_store_DAC_C(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_store_DAC(chip, 2, buf, len);
}

static IIO_DEVICE_ATTR(DAC_C, S_IRUGO | S_IWUSR, adt7316_show_DAC_C,
		adt7316_store_DAC_C, 0);

static ssize_t adt7316_show_DAC_D(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_show_DAC(chip, 3, buf);
}

static ssize_t adt7316_store_DAC_D(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return adt7316_store_DAC(chip, 3, buf, len);
}

static IIO_DEVICE_ATTR(DAC_D, S_IRUGO | S_IWUSR, adt7316_show_DAC_D,
		adt7316_store_DAC_D, 0);

static ssize_t adt7316_show_device_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 id;
	int ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_DEVICE_ID, &id);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", id);
}

static IIO_DEVICE_ATTR(device_id, S_IRUGO, adt7316_show_device_id, NULL, 0);

static ssize_t adt7316_show_manufactorer_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 id;
	int ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_MANUFACTURE_ID, &id);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", id);
}

static IIO_DEVICE_ATTR(manufactorer_id, S_IRUGO,
		adt7316_show_manufactorer_id, NULL, 0);

static ssize_t adt7316_show_device_rev(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 rev;
	int ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_DEVICE_REV, &rev);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", rev);
}

static IIO_DEVICE_ATTR(device_rev, S_IRUGO, adt7316_show_device_rev, NULL, 0);

static ssize_t adt7316_show_bus_type(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 stat;
	int ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_SPI_LOCK_STAT, &stat);
	if (ret)
		return -EIO;

	if (stat)
		return sprintf(buf, "spi\n");
	else
		return sprintf(buf, "i2c\n");
}

static IIO_DEVICE_ATTR(bus_type, S_IRUGO, adt7316_show_bus_type, NULL, 0);

static ssize_t adt7316_show_name(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%s\n", chip->name);
}

static IIO_DEVICE_ATTR(name, S_IRUGO, adt7316_show_name, NULL, 0);

static struct attribute *adt7316_attributes[] = {
	&iio_dev_attr_all_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_dev_attr_enabled.dev_attr.attr,
	&iio_dev_attr_ad_channel.dev_attr.attr,
	&iio_dev_attr_all_ad_channels.dev_attr.attr,
	&iio_dev_attr_disable_averaging.dev_attr.attr,
	&iio_dev_attr_enable_smbus_timeout.dev_attr.attr,
	&iio_dev_attr_powerdown.dev_attr.attr,
	&iio_dev_attr_fast_ad_clock.dev_attr.attr,
	&iio_dev_attr_da_high_resolution.dev_attr.attr,
	&iio_dev_attr_enable_proportion_DACA.dev_attr.attr,
	&iio_dev_attr_enable_proportion_DACB.dev_attr.attr,
	&iio_dev_attr_DAC_2Vref_channels_mask.dev_attr.attr,
	&iio_dev_attr_DAC_update_mode.dev_attr.attr,
	&iio_dev_attr_all_DAC_update_modes.dev_attr.attr,
	&iio_dev_attr_update_DAC.dev_attr.attr,
	&iio_dev_attr_DA_AB_Vref_bypass.dev_attr.attr,
	&iio_dev_attr_DA_CD_Vref_bypass.dev_attr.attr,
	&iio_dev_attr_internal_Vref.dev_attr.attr,
	&iio_dev_attr_vdd.dev_attr.attr,
	&iio_dev_attr_in_temp.dev_attr.attr,
	&iio_dev_attr_ex_temp.dev_attr.attr,
	&iio_dev_attr_in_temp_offset.dev_attr.attr,
	&iio_dev_attr_ex_temp_offset.dev_attr.attr,
	&iio_dev_attr_in_analog_temp_offset.dev_attr.attr,
	&iio_dev_attr_ex_analog_temp_offset.dev_attr.attr,
	&iio_dev_attr_DAC_A.dev_attr.attr,
	&iio_dev_attr_DAC_B.dev_attr.attr,
	&iio_dev_attr_DAC_C.dev_attr.attr,
	&iio_dev_attr_DAC_D.dev_attr.attr,
	&iio_dev_attr_device_id.dev_attr.attr,
	&iio_dev_attr_manufactorer_id.dev_attr.attr,
	&iio_dev_attr_device_rev.dev_attr.attr,
	&iio_dev_attr_bus_type.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group adt7316_attribute_group = {
	.attrs = adt7316_attributes,
};

/*
 * temperature bound events
 */

#define IIO_EVENT_CODE_ADT7316_IN_TEMP_HIGH    (IIO_EVENT_CODE_DEVICE_SPECIFIC + 1)
#define IIO_EVENT_CODE_ADT7316_IN_TEMP_LOW    (IIO_EVENT_CODE_DEVICE_SPECIFIC + 2)
#define IIO_EVENT_CODE_ADT7316_EX_TEMP_HIGH    (IIO_EVENT_CODE_DEVICE_SPECIFIC + 3)
#define IIO_EVENT_CODE_ADT7316_EX_TEMP_LOW    (IIO_EVENT_CODE_DEVICE_SPECIFIC + 4)
#define IIO_EVENT_CODE_ADT7316_EX_TEMP_FAULT   (IIO_EVENT_CODE_DEVICE_SPECIFIC + 5)
#define IIO_EVENT_CODE_ADT7316_VDD	      (IIO_EVENT_CODE_DEVICE_SPECIFIC + 6)

static void adt7316_interrupt_bh(struct work_struct *work_s)
{
	struct iio_work_cont *wc = to_iio_work_cont_no_check(work_s);
	struct adt7316_chip_info *chip = wc->st;
	u8 stat1, stat2;
	int i, ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_INT_STAT1, &stat1);
	if (!ret) {
		for (i = 0; i < 5; i++) {
			if (stat1 & (1 << i))
				iio_push_event(chip->indio_dev, 0,
					IIO_EVENT_CODE_ADT7316_IN_TEMP_HIGH + i,
					chip->last_timestamp);
		}
	}

	ret = chip->bus.read(chip->bus.client, ADT7316_INT_STAT2, &stat2);
	if (!ret) {
		if (stat2 & ADT7316_INT_MASK2_VDD)
			iio_push_event(chip->indio_dev, 0,
				IIO_EVENT_CODE_ADT7316_VDD,
				chip->last_timestamp);
	}

	enable_irq(chip->bus.irq);
}

static int adt7316_interrupt(struct iio_dev *dev_info,
		int index,
		s64 timestamp,
		int no_test)
{
	struct adt7316_chip_info *chip = dev_info->dev_data;

	chip->last_timestamp = timestamp;
	schedule_work(&chip->work_cont_thresh.ws_nocheck);

	return 0;
}

IIO_EVENT_SH(adt7316, &adt7316_interrupt);

/*
 * Show mask of enabled interrupts in Hex.
 */
static ssize_t adt7316_show_int_mask(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%x\n", chip->int_mask);
}

/*
 * Set 1 to the mask in Hex to enabled interrupts.
 */
static ssize_t adt7316_set_int_mask(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;
	u8 mask;

	ret = strict_strtoul(buf, 16, &data);
	if (ret || data >= ADT7316_VDD_INT_MASK + 1)
		return -EINVAL;

	if (data & ADT7316_VDD_INT_MASK)
		mask = 0;			/* enable vdd int */
	else
		mask = ADT7316_INT_MASK2_VDD;	/* disable vdd int */

	ret = chip->bus.write(chip->bus.client, ADT7316_INT_MASK2, mask);
	if (!ret) {
		chip->int_mask &= ~ADT7316_VDD_INT_MASK;
		chip->int_mask |= data & ADT7316_VDD_INT_MASK;
	}

	if (data & ADT7316_TEMP_INT_MASK)
		/* mask in reg is opposite, set 1 to disable */
		mask = (~data) & ADT7316_TEMP_INT_MASK;
	ret = chip->bus.write(chip->bus.client, ADT7316_INT_MASK1, mask);
	if (!ret) {
		chip->int_mask &= ~ADT7316_TEMP_INT_MASK;
		chip->int_mask |= data & ADT7316_TEMP_INT_MASK;
	}

	return len;
}
static inline ssize_t adt7316_show_temp_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 val;
	int data;
	int ret;

	ret = chip->bus.read(chip->bus.client, bound_reg, &val);
	if (ret)
		return -EIO;

	data = (int)val;

	if (data & 0x80)
		data -= 256;

	return sprintf(buf, "%d\n", data);
}

static inline ssize_t adt7316_set_temp_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	long data;
	u8 val;
	int ret;

	ret = strict_strtol(buf, 10, &data);

	if (ret || data > 127 || data < -128)
		return -EINVAL;

	if (data < 0)
		data += 256;

	val = (u8)data;

	ret = chip->bus.write(chip->bus.client, bound_reg, val);
	if (ret)
		return -EIO;

	return len;
}

static ssize_t adt7316_show_in_temp_high(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7316_show_temp_bound(dev, attr,
			ADT7316_IN_TEMP_HIGH, buf);
}

static inline ssize_t adt7316_set_in_temp_high(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7316_set_temp_bound(dev, attr,
			ADT7316_IN_TEMP_HIGH, buf, len);
}

static ssize_t adt7316_show_in_temp_low(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7316_show_temp_bound(dev, attr,
			ADT7316_IN_TEMP_LOW, buf);
}

static inline ssize_t adt7316_set_in_temp_low(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7316_set_temp_bound(dev, attr,
			ADT7316_IN_TEMP_LOW, buf, len);
}

static ssize_t adt7316_show_ex_temp_high(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7316_show_temp_bound(dev, attr,
			ADT7316_EX_TEMP_HIGH, buf);
}

static inline ssize_t adt7316_set_ex_temp_high(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7316_set_temp_bound(dev, attr,
			ADT7316_EX_TEMP_HIGH, buf, len);
}

static ssize_t adt7316_show_ex_temp_low(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7316_show_temp_bound(dev, attr,
			ADT7316_EX_TEMP_LOW, buf);
}

static inline ssize_t adt7316_set_ex_temp_low(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7316_set_temp_bound(dev, attr,
			ADT7316_EX_TEMP_LOW, buf, len);
}

static ssize_t adt7316_show_int_enabled(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", !!(chip->config1 & ADT7316_INT_EN));
}

static ssize_t adt7316_set_int_enabled(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	u8 config1;
	int ret;

	config1 = chip->config1 & (~ADT7316_INT_EN);
	if (!memcmp(buf, "1", 1))
		config1 |= ADT7316_INT_EN;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, config1);
	if (ret)
		return -EIO;

	chip->config1 = config1;

	return len;
}


IIO_EVENT_ATTR_SH(int_mask, iio_event_adt7316,
		adt7316_show_int_mask, adt7316_set_int_mask, 0);
IIO_EVENT_ATTR_SH(in_temp_high, iio_event_adt7316,
		adt7316_show_in_temp_high, adt7316_set_in_temp_high, 0);
IIO_EVENT_ATTR_SH(in_temp_low, iio_event_adt7316,
		adt7316_show_in_temp_low, adt7316_set_in_temp_low, 0);
IIO_EVENT_ATTR_SH(ex_temp_high, iio_event_adt7316,
		adt7316_show_ex_temp_high, adt7316_set_ex_temp_high, 0);
IIO_EVENT_ATTR_SH(ex_temp_low, iio_event_adt7316,
		adt7316_show_ex_temp_low, adt7316_set_ex_temp_low, 0);
IIO_EVENT_ATTR_SH(int_enabled, iio_event_adt7316,
		adt7316_show_int_enabled, adt7316_set_int_enabled, 0);

static struct attribute *adt7316_event_attributes[] = {
	&iio_event_attr_int_mask.dev_attr.attr,
	&iio_event_attr_in_temp_high.dev_attr.attr,
	&iio_event_attr_in_temp_low.dev_attr.attr,
	&iio_event_attr_ex_temp_high.dev_attr.attr,
	&iio_event_attr_ex_temp_low.dev_attr.attr,
	&iio_event_attr_int_enabled.dev_attr.attr,
	NULL,
};

static struct attribute_group adt7316_event_attribute_group = {
	.attrs = adt7316_event_attributes,
};

#ifdef CONFIG_PM
int adt7316_disable(struct device *dev)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return _adt7316_store_enabled(chip, 0);
}
EXPORT_SYMBOL(adt7316_disable);

int adt7316_enable(struct device *dev)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;

	return _adt7316_store_enabled(chip, 1);
}
EXPORT_SYMBOL(adt7316_enable);
#endif

/*
 * device probe and remove
 */
int __devinit adt7316_probe(struct device *dev, struct adt7316_bus *bus,
		const char *name)
{
	struct adt7316_chip_info *chip;
	unsigned short *adt7316_platform_data = dev->platform_data;
	int ret = 0;

	chip = kzalloc(sizeof(struct adt7316_chip_info), GFP_KERNEL);

	if (chip == NULL)
		return -ENOMEM;

	/* this is only used for device removal purposes */
	dev_set_drvdata(dev, chip);

	chip->bus = *bus;
	chip->name = name;
	chip->ldac_pin = adt7316_platform_data[1];
	chip->config3 = ADT7316_DA_EN_VIA_DAC_LDCA;
	chip->int_mask = ADT7316_TEMP_INT_MASK | ADT7316_VDD_INT_MASK;

	chip->indio_dev = iio_allocate_device();
	if (chip->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_chip;
	}

	chip->indio_dev->dev.parent = dev;
	chip->indio_dev->attrs = &adt7316_attribute_group;
	chip->indio_dev->event_attrs = &adt7316_event_attribute_group;
	chip->indio_dev->dev_data = (void *)chip;
	chip->indio_dev->driver_module = THIS_MODULE;
	chip->indio_dev->num_interrupt_lines = 1;
	chip->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(chip->indio_dev);
	if (ret)
		goto error_free_dev;

	if (chip->bus.irq > 0) {
		iio_init_work_cont(&chip->work_cont_thresh,
				adt7316_interrupt_bh,
				adt7316_interrupt_bh,
				0,
				0,
				chip);

		if (adt7316_platform_data[0])
			chip->bus.irq_flags = adt7316_platform_data[0];

		ret = iio_register_interrupt_line(chip->bus.irq,
				chip->indio_dev,
				0,
				chip->bus.irq_flags,
				chip->name);
		if (ret)
			goto error_unreg_dev;

		/*
		 * The event handler list element refer to iio_event_adt7316.
		 * All event attributes bind to the same event handler.
		 * So, only register event handler once.
		 */
		iio_add_event_to_list(&iio_event_adt7316,
				&chip->indio_dev->interrupts[0]->ev_list);

		if (chip->bus.irq_flags & IRQF_TRIGGER_HIGH)
			chip->config1 |= ADT7316_INT_POLARITY;
	}

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, chip->config1);
	if (ret) {
		ret = -EIO;
		goto error_unreg_irq;
	}

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, chip->config3);
	if (ret) {
		ret = -EIO;
		goto error_unreg_irq;
	}

	dev_info(dev, "%s temperature sensor, ADC and DAC registered.\n",
			chip->name);

	return 0;

error_unreg_irq:
	iio_unregister_interrupt_line(chip->indio_dev, 0);
error_unreg_dev:
	iio_device_unregister(chip->indio_dev);
error_free_dev:
	iio_free_device(chip->indio_dev);
error_free_chip:
	kfree(chip);

	return ret;
}
EXPORT_SYMBOL(adt7316_probe);

int __devexit adt7316_remove(struct device *dev)
{

	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = dev_info->dev_data;
	struct iio_dev *indio_dev = chip->indio_dev;

	dev_set_drvdata(dev, NULL);
	if (chip->bus.irq)
		iio_unregister_interrupt_line(indio_dev, 0);
	iio_device_unregister(indio_dev);
	iio_free_device(chip->indio_dev);
	kfree(chip);

	return 0;
}
EXPORT_SYMBOL(adt7316_remove);

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADT7316/7/8 digital"
			" temperature sensor, ADC and DAC driver");
MODULE_LICENSE("GPL v2");