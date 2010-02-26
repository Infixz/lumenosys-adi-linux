#include "../sysfs.h"

/* metering ic types of attribute */

#define IIO_DEV_ATTR_CURRENT_A_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(current_a_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_B_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(current_b_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_C_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(current_c_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VOLT_A_OFFSET(_mode, _show, _store, _addr)      \
	IIO_DEVICE_ATTR(volt_a_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VOLT_B_OFFSET(_mode, _show, _store, _addr)      \
	IIO_DEVICE_ATTR(volt_b_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VOLT_C_OFFSET(_mode, _show, _store, _addr)      \
	IIO_DEVICE_ATTR(volt_c_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_A_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_a_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_B_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_b_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_C_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_c_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_A_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_a_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_B_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_b_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_C_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_c_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_A_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(current_a_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_B_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(current_b_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_C_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(current_c_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APPARENT_POWER_A_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(apparent_power_a_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APPARENT_POWER_B_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(apparent_power_b_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APPARENT_POWER_C_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(apparent_power_c_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_A_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_a_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_B_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_b_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_C_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_c_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_A_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_a_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_B_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_b_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_C_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_c_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_A(_show, _addr)			\
	IIO_DEVICE_ATTR(current_a, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CURRENT_B(_show, _addr)			\
	IIO_DEVICE_ATTR(current_b, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CURRENT_C(_show, _addr)			\
	IIO_DEVICE_ATTR(current_c, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VOLT_A(_show, _addr)			\
	IIO_DEVICE_ATTR(volt_a, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VOLT_B(_show, _addr)			\
	IIO_DEVICE_ATTR(volt_b, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VOLT_C(_show, _addr)			\
	IIO_DEVICE_ATTR(volt_c, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_AENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(aenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_RAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(raenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(laenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(vaenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LVAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(lvaenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_RVAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(rvaenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LVARENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(lvarenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_IOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(ios, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_PHCAL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(phcal, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(apos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_WGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(wgain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_WDIV(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(wdiv, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CFNUM(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cfnum, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CFDEN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cfden, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_IRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(irms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vrms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VAGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vagain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VADIV(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vadiv, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_LINECYC(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(linecyc, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_SAGCYC(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(sagcyc, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_SAGLVL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(saglvl, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_IPKLVL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(ipklvl, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VPKLVL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vpklvl, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_IPEAK(_show, _addr)			\
	IIO_DEVICE_ATTR(ipeak, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_RIPEAK(_show, _addr)			\
	IIO_DEVICE_ATTR(ripeak, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VPEAK(_show, _addr)			\
	IIO_DEVICE_ATTR(vpeak, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_RVPEAK(_show, _addr)			\
	IIO_DEVICE_ATTR(rvpeak, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VPERIOD(_show, _addr)			\
	IIO_DEVICE_ATTR(vperiod, S_IRUGO, _show, NULL, _addr)

/* active energy register, AENERGY, is more than half full */
#define IIO_EVENT_ATTR_AENERGY_HALF_FULL(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(aenergy_half_full, _evlist, _show, _store, _mask)

/* a SAG on the line voltage */
#define IIO_EVENT_ATTR_LINE_VOLT_SAG(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(line_volt_sag, _evlist, _show, _store, _mask)

/*
 * Indicates the end of energy accumulation over an integer number
 * of half line cycles
 */
#define IIO_EVENT_ATTR_CYCEND(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(cycend, _evlist, _show, _store, _mask)

/* on the rising and falling edge of the the voltage waveform */
#define IIO_EVENT_ATTR_ZERO_CROSS(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(zero_cross, _evlist, _show, _store, _mask)

/* the active energy register has overflowed */
#define IIO_EVENT_ATTR_AENERGY_OVERFLOW(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(aenergy_overflow, _evlist, _show, _store, _mask)

/* the apparent energy register has overflowed */
#define IIO_EVENT_ATTR_VAENERGY_OVERFLOW(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(vaenergy_overflow, _evlist, _show, _store, _mask)

/* the active energy register, VAENERGY, is more than half full */
#define IIO_EVENT_ATTR_VAENERGY_HALF_FULL(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(vaenergy_half_full, _evlist, _show, _store, _mask)

/* the power has gone from negative to positive */
#define IIO_EVENT_ATTR_PPOS(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(ppos, _evlist, _show, _store, _mask)

/* the power has gone from positive to negative */
#define IIO_EVENT_ATTR_PNEG(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(pneg, _evlist, _show, _store, _mask)

/* waveform sample from Channel 1 has exceeded the IPKLVL value */
#define IIO_EVENT_ATTR_IPKLVL_EXC(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(ipklvl_exc, _evlist, _show, _store, _mask)

/* waveform sample from Channel 2 has exceeded the VPKLVL value */
#define IIO_EVENT_ATTR_VPKLVL_EXC(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(vpklvl_exc, _evlist, _show, _store, _mask)
