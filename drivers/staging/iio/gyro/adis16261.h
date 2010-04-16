#ifndef SPI_ADIS16261_H_
#define SPI_ADIS16261_H_

#define ADIS16261_STARTUP_DELAY	220 /* ms */

#define ADIS16261_READ_REG(a)    a
#define ADIS16261_WRITE_REG(a) ((a) | 0x80)

#define ADIS16261_ENDURANCE  0x00 /* Flash memory write count */
#define ADIS16261_SUPPLY_OUT 0x02 /* Power supply measurement */
#define ADIS16261_GYRO_OUT   0x04 /* X-axis gyroscope output */
#define ADIS16261_AUX_ADC    0x0A /* analog input channel measurement */
#define ADIS16261_TEMP_OUT   0x0C /* internal temperature measurement */
#define ADIS16261_ANGL_OUT   0x0E /* angle displacement */
#define ADIS16261_GYRO_OFF   0x14 /* Calibration, offset/bias adjustment */
#define ADIS16261_GYRO_SCALE 0x16 /* Calibration, scale adjustment */
#define ADIS16261_ALM_MAG1   0x20 /* Alarm 1 magnitude/polarity setting */
#define ADIS16261_ALM_MAG2   0x22 /* Alarm 2 magnitude/polarity setting */
#define ADIS16261_ALM_SMPL1  0x24 /* Alarm 1 dynamic rate of change setting */
#define ADIS16261_ALM_SMPL2  0x26 /* Alarm 2 dynamic rate of change setting */
#define ADIS16261_ALM_CTRL   0x28 /* Alarm control */
#define ADIS16261_AUX_DAC    0x30 /* Auxiliary DAC data */
#define ADIS16261_GPIO_CTRL  0x32 /* Control, digital I/O line */
#define ADIS16261_MSC_CTRL   0x34 /* Control, data ready, self-test settings */
#define ADIS16261_SMPL_PRD   0x36 /* Control, internal sample rate */
#define ADIS16261_SENS_AVG   0x38 /* Control, dynamic range, filtering */
#define ADIS16261_SLP_CNT    0x3A /* Control, sleep mode initiation */
#define ADIS16261_DIAG_STAT  0x3C /* Diagnostic, error flags */
#define ADIS16261_GLOB_CMD   0x3E /* Control, global commands */

#define ADIS16261_ERROR_ACTIVE			(1<<14)
#define ADIS16261_NEW_DATA			(1<<14)

/* MSC_CTRL */
#define ADIS16261_MSC_CTRL_INT_SELF_TEST	(1<<10) /* Internal self-test enable */
#define ADIS16261_MSC_CTRL_NEG_SELF_TEST	(1<<9)
#define ADIS16261_MSC_CTRL_POS_SELF_TEST	(1<<8)
#define ADIS16261_MSC_CTRL_DATA_RDY_EN		(1<<2)
#define ADIS16261_MSC_CTRL_DATA_RDY_POL_HIGH	(1<<1)
#define ADIS16261_MSC_CTRL_DATA_RDY_DIO2	(1<<0)

/* SMPL_PRD */
#define ADIS16261_SMPL_PRD_TIME_BASE	(1<<7) /* Time base (tB): 0 = 1.953 ms, 1 = 60.54 ms */
#define ADIS16261_SMPL_PRD_DIV_MASK	0x7F

/* SLP_CNT */
#define ADIS16261_SLP_CNT_POWER_OFF     0x80

/* DIAG_STAT */
#define ADIS16261_DIAG_STAT_ALARM2	(1<<9)
#define ADIS16261_DIAG_STAT_ALARM1	(1<<8)
#define ADIS16261_DIAG_STAT_SELF_TEST	(1<<5)
#define ADIS16261_DIAG_STAT_OVERFLOW	(1<<4)
#define ADIS16261_DIAG_STAT_SPI_FAIL	(1<<3)
#define ADIS16261_DIAG_STAT_FLASH_UPT	(1<<2)
#define ADIS16261_DIAG_STAT_POWER_HIGH	(1<<1)
#define ADIS16261_DIAG_STAT_POWER_LOW	(1<<0)

/* GLOB_CMD */
#define ADIS16261_GLOB_CMD_SW_RESET	(1<<7)
#define ADIS16261_GLOB_CMD_FLASH_UPD	(1<<3)
#define ADIS16261_GLOB_CMD_DAC_LATCH	(1<<2)
#define ADIS16261_GLOB_CMD_FAC_CALIB	(1<<1)
#define ADIS16261_GLOB_CMD_AUTO_NULL	(1<<0)

#define ADIS16261_MAX_TX 24
#define ADIS16261_MAX_RX 24

#define ADIS16261_SPI_SLOW	(u32)(300 * 1000)
#define ADIS16261_SPI_BURST	(u32)(1000 * 1000)
#define ADIS16261_SPI_FAST	(u32)(2000 * 1000)

/**
 * struct adis16261_state - device instance specific data
 * @us:			actual spi_device
 * @work_trigger_to_ring: bh for triggered event handling
 * @work_cont_thresh: CLEAN
 * @inter:		used to check if new interrupt has been triggered
 * @last_timestamp:	passing timestamp from th to bh of interrupt handler
 * @indio_dev:		industrial I/O device structure
 * @trig:		data ready trigger registered with iio
 * @tx:			transmit buffer
 * @rx:			recieve buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16261_state {
	struct spi_device		*us;
	struct work_struct		work_trigger_to_ring;
	struct iio_work_cont		work_cont_thresh;
	s64				last_timestamp;
	struct iio_dev			*indio_dev;
	struct iio_trigger		*trig;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};

int adis16261_spi_write_reg_8(struct device *dev,
			      u8 reg_address,
			      u8 val);

int adis16261_spi_read_burst(struct device *dev, u8 *rx);

int adis16261_spi_read_sequence(struct device *dev,
				      u8 *tx, u8 *rx, int num);

int adis16261_set_irq(struct device *dev, bool enable);

int adis16261_reset(struct device *dev);

int adis16261_stop_device(struct device *dev);

int adis16261_check_status(struct device *dev);

#ifdef CONFIG_IIO_RING_BUFFER
/* At the moment triggers are only used for ring buffer
 * filling. This may change!
 */

enum adis16261_scan {
	ADIS16261_SCAN_SUPPLY,
	ADIS16261_SCAN_GYRO,
	ADIS16261_SCAN_TEMP,
	ADIS16261_SCAN_ADC_0,
};

void adis16261_remove_trigger(struct iio_dev *indio_dev);
int adis16261_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16261_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);


int adis16261_configure_ring(struct iio_dev *indio_dev);
void adis16261_unconfigure_ring(struct iio_dev *indio_dev);

int adis16261_initialize_ring(struct iio_ring_buffer *ring);
void adis16261_uninitialize_ring(struct iio_ring_buffer *ring);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void adis16261_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16261_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16261_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static int adis16261_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16261_unconfigure_ring(struct iio_dev *indio_dev)
{
}

static inline int adis16261_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}

static inline void adis16261_uninitialize_ring(struct iio_ring_buffer *ring)
{
}

#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* SPI_ADIS16261_H_ */