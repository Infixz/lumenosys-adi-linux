/*
 * File: include/asm-blackfin/mach-bf548/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2007 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file shoule be up to date with:
 *  - Revision B, April 6, 2007; ADSP-BF549 Silicon Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* Multi-Issue Instruction with dsp32shiftimm in slot1 and P-reg Store in slot 2 Not Supported */
#define ANOMALY_05000074 (1)
/* DMA_RUN Bit Is Not Valid after a Peripheral Receive Channel DMA Stops */
#define ANOMALY_05000119 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* Spurious Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (1)
/* Entering Hibernate State with RTC Seconds Interrupt Not Functional */
#define ANOMALY_05000255 (1)
/* Sensitivity To Noise with Slow Input Edge Rates on External SPORT TX and RX Clocks */
#define ANOMALY_05000265 (1)
/* Certain Data Cache Writethrough Modes Fail for Vddint <= 0.9V */
#define ANOMALY_05000272 (1)
/* False Hardware Errors Caused by Fetches at the Boundary of Reserved Memory */
#define ANOMALY_05000310 (1)
/* Errors When SSYNC, CSYNC, or Loads to LT, LB and LC Registers Are Interrupted */
#define ANOMALY_05000312 (1)
/* TWI Slave Boot Mode Is Not Functional */
#define ANOMALY_05000324 (1)
/* External FIFO Boot Mode Is Not Functional */
#define ANOMALY_05000325 (1)
/* Data Lost When Core and DMA Accesses Are Made to the USB FIFO Simultaneously */
#define ANOMALY_05000327 (1)
/* Incorrect Access of OTP_STATUS During otp_write() Function */
#define ANOMALY_05000328 (1)
/* Synchronous Burst Flash Boot Mode Is Not Functional */
#define ANOMALY_05000329 (1)
/* Host DMA Boot Mode Is Not Functional */
#define ANOMALY_05000330 (1)
/* Inadequate Timing Margins on DDR DQS to DQ and DQM Skew */
#define ANOMALY_05000334 (1)
/* Inadequate Rotary Debounce Logic Duration */
#define ANOMALY_05000335 (1)
/* Phantom Interrupt Occurs After First Configuration of Host DMA Port */
#define ANOMALY_05000336 (1)
/* Disallowed Configuration Prevents Subsequent Allowed Configuration on Host DMA Port */
#define ANOMALY_05000337 (1)
/* Slave-Mode SPI0 MISO Failure With CPHA = 0 */
#define ANOMALY_05000338 (1)

/* Anomalies that don't exist on this proc */
#define ANOMALY_05000125 (0)
#define ANOMALY_05000183 (0)
#define ANOMALY_05000198 (0)
#define ANOMALY_05000244 (0)
#define ANOMALY_05000263 (0)
#define ANOMALY_05000266 (0)
#define ANOMALY_05000273 (0)
#define ANOMALY_05000311 (0)

#endif
