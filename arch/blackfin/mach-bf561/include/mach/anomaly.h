/*
 * DO NOT EDIT THIS FILE
 * This file is under version control at
 *   svn://sources.blackfin.uclinux.org/toolchain/trunk/proc-defs/header-frags/
 * and can be replaced with that version at any time
 * DO NOT EDIT THIS FILE
 *
 * Copyright 2004-2011 Analog Devices Inc.
 * Licensed under the 3-clause BSD license.
 */

/* This file should be up to date with:
 *  - Revision S, 05/23/2011; ADSP-BF561 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* We do not support 0.1, 0.2, or 0.4 silicon - sorry */
#if __SILICON_REVISION__ < 3 || __SILICON_REVISION__ == 4
# error will not work on BF561 silicon version 0.0, 0.1, 0.2, or 0.4
#endif

/* Multi-Issue Instruction with dsp32shiftimm in slot1 and P-reg Store in slot2 Not Supported */
#define ANOMALY_05000074 (1)
/* UART Line Status Register (UART_LSR) Bits Are Not Updated at the Same Time */
#define ANOMALY_05000099 (__SILICON_REVISION__ < 5)
/* TESTSET Instructions Restricted to 32-Bit Aligned Memory Locations */
#define ANOMALY_05000120 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* SIGNBITS Instruction Not Functional under Certain Conditions */
#define ANOMALY_05000127 (1)
/* IMDMA S1/D1 Channel May Stall */
#define ANOMALY_05000149 (1)
/* Timers in PWM-Out Mode with PPI GP Receive (Input) Mode with 0 Frame Syncs */
#define ANOMALY_05000156 (__SILICON_REVISION__ < 4)
/* PPI Data Lengths between 8 and 16 Do Not Zero Out Upper Bits */
#define ANOMALY_05000166 (1)
/* Turning SPORTs on while External Frame Sync Is Active May Corrupt Data */
#define ANOMALY_05000167 (1)
/* Undefined Behavior when Power-Up Sequence Is Issued to SDRAM during Auto-Refresh */
#define ANOMALY_05000168 (__SILICON_REVISION__ < 5)
/* DATA CPLB Page Miss Can Result in Lost Write-Through Data Cache Writes */
#define ANOMALY_05000169 (__SILICON_REVISION__ < 5)
/* Boot-ROM Modifies SICA_IWRx Wakeup Registers */
#define ANOMALY_05000171 (__SILICON_REVISION__ < 5)
/* Cache Fill Buffer Data lost */
#define ANOMALY_05000174 (__SILICON_REVISION__ < 5)
/* Overlapping Sequencer and Memory Stalls */
#define ANOMALY_05000175 (__SILICON_REVISION__ < 5)
/* Overflow Bit Asserted when Multiplication of -1 by -1 Followed by Accumulator Saturation */
#define ANOMALY_05000176 (__SILICON_REVISION__ < 5)
/* PPI_COUNT Cannot Be Programmed to 0 in General Purpose TX or RX Modes */
#define ANOMALY_05000179 (__SILICON_REVISION__ < 5)
/* PPI_DELAY Not Functional in PPI Modes with 0 Frame Syncs */
#define ANOMALY_05000180 (1)
/* Disabling the PPI Resets the PPI Configuration Registers */
#define ANOMALY_05000181 (__SILICON_REVISION__ < 5)
/* Internal Memory DMA Does Not Operate at Full Speed */
#define ANOMALY_05000182 (1)
/* Timer Pin Limitations for PPI TX Modes with External Frame Syncs */
#define ANOMALY_05000184 (__SILICON_REVISION__ < 5)
/* Early PPI Transmit when FS1 Asserts before FS2 in TX Mode with 2 External Frame Syncs */
#define ANOMALY_05000185 (__SILICON_REVISION__ < 5)
/* Upper PPI Pins Driven when PPI Packing Enabled and Data Length >8 Bits */
#define ANOMALY_05000186 (__SILICON_REVISION__ < 5)
/* IMDMA Corrupted Data after a Halt */
#define ANOMALY_05000187 (1)
/* IMDMA Restrictions on Descriptor and Buffer Placement in Memory */
#define ANOMALY_05000188 (__SILICON_REVISION__ < 5)
/* False Protection Exceptions when Speculative Fetch Is Cancelled */
#define ANOMALY_05000189 (__SILICON_REVISION__ < 5)
/* PPI Not Functional at Core Voltage < 1Volt */
#define ANOMALY_05000190 (1)
/* False I/O Pin Interrupts on Edge-Sensitive Inputs When Polarity Setting Is Changed */
#define ANOMALY_05000193 (__SILICON_REVISION__ < 5)
/* Restarting SPORT in Specific Modes May Cause Data Corruption */
#define ANOMALY_05000194 (__SILICON_REVISION__ < 5)
/* Failing MMR Accesses when Preceding Memory Read Stalls */
#define ANOMALY_05000198 (__SILICON_REVISION__ < 5)
/* Current DMA Address Shows Wrong Value During Carry Fix */
#define ANOMALY_05000199 (__SILICON_REVISION__ < 5)
/* SPORT TFS and DT Are Incorrectly Driven During Inactive Channels in Certain Conditions */
#define ANOMALY_05000200 (__SILICON_REVISION__ < 5)
/* Possible Infinite Stall with Specific Dual-DAG Situation */
#define ANOMALY_05000202 (__SILICON_REVISION__ < 5)
/* Incorrect Data Read with Writethrough "Allocate Cache Lines on Reads Only" Cache Mode */
#define ANOMALY_05000204 (__SILICON_REVISION__ < 5)
/* Specific Sequence that Can Cause DMA Error or DMA Stopping */
#define ANOMALY_05000205 (__SILICON_REVISION__ < 5)
/* Recovery from "Brown-Out" Condition */
#define ANOMALY_05000207 (__SILICON_REVISION__ < 5)
/* VSTAT Status Bit in PLL_STAT Register Is Not Functional */
#define ANOMALY_05000208 (1)
/* Speed Path in Computational Unit Affects Certain Instructions */
#define ANOMALY_05000209 (__SILICON_REVISION__ < 5)
/* UART TX Interrupt Masked Erroneously */
#define ANOMALY_05000215 (__SILICON_REVISION__ < 5)
/* NMI Event at Boot Time Results in Unpredictable State */
#define ANOMALY_05000219 (__SILICON_REVISION__ < 5)
/* Data Corruption/Core Hang with L2/L3 Configured in Writeback Cache Mode */
#define ANOMALY_05000220 (__SILICON_REVISION__ < 4)
/* Incorrect Pulse-Width of UART Start Bit */
#define ANOMALY_05000225 (__SILICON_REVISION__ < 5)
/* Scratchpad Memory Bank Reads May Return Incorrect Data */
#define ANOMALY_05000227 (__SILICON_REVISION__ < 5)
/* UART Receiver is Less Robust Against Baudrate Differences in Certain Conditions */
#define ANOMALY_05000230 (__SILICON_REVISION__ < 5)
/* UART STB Bit Incorrectly Affects Receiver Setting */
#define ANOMALY_05000231 (__SILICON_REVISION__ < 5)
/* SPORT Data Transmit Lines Are Incorrectly Driven in Multichannel Mode */
#define ANOMALY_05000232 (__SILICON_REVISION__ < 5)
/* DF Bit in PLL_CTL Register Does Not Respond to Hardware Reset */
#define ANOMALY_05000242 (__SILICON_REVISION__ < 5)
/* If I-Cache Is On, CSYNC/SSYNC/IDLE Around Change of Control Causes Failures */
#define ANOMALY_05000244 (__SILICON_REVISION__ < 5)
/* False Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (__SILICON_REVISION__ < 5)
/* TESTSET Operation Forces Stall on the Other Core */
#define ANOMALY_05000248 (__SILICON_REVISION__ < 5)
/* Incorrect Bit Shift of Data Word in Multichannel (TDM) Mode in Certain Conditions */
#define ANOMALY_05000250 (__SILICON_REVISION__ > 2 && __SILICON_REVISION__ < 5)
/* Exception Not Generated for MMR Accesses in Reserved Region */
#define ANOMALY_05000251 (__SILICON_REVISION__ < 5)
/* Maximum External Clock Speed for Timers */
#define ANOMALY_05000253 (__SILICON_REVISION__ < 5)
/* Incorrect Timer Pulse Width in Single-Shot PWM_OUT Mode with External Clock */
#define ANOMALY_05000254 (__SILICON_REVISION__ > 3)
/* Interrupt/Exception During Short Hardware Loop May Cause Bad Instruction Fetches */
/* Tempoary work around for kgdb bug 6333 in SMP kernel. It looks coreb hangs in exception
 * without handling anomaly 05000257 properly on bf561 v0.5. This work around may change
 * after the behavior and the root cause are confirmed with hardware team.
 */
#define ANOMALY_05000257 (__SILICON_REVISION__ < 5 || (__SILICON_REVISION__ == 5 && CONFIG_SMP))
/* Instruction Cache Is Corrupted When Bits 9 and 12 of the ICPLB Data Registers Differ */
#define ANOMALY_05000258 (__SILICON_REVISION__ < 5)
/* ICPLB_STATUS MMR Register May Be Corrupted */
#define ANOMALY_05000260 (__SILICON_REVISION__ < 5)
/* DCPLB_FAULT_ADDR MMR Register May Be Corrupted */
#define ANOMALY_05000261 (__SILICON_REVISION__ < 5)
/* Stores To Data Cache May Be Lost */
#define ANOMALY_05000262 (__SILICON_REVISION__ < 5)
/* Hardware Loop Corrupted When Taking an ICPLB Exception */
#define ANOMALY_05000263 (__SILICON_REVISION__ < 5)
/* CSYNC/SSYNC/IDLE Causes Infinite Stall in Penultimate Instruction in Hardware Loop */
#define ANOMALY_05000264 (__SILICON_REVISION__ < 5)
/* Sensitivity To Noise with Slow Input Edge Rates on External SPORT TX and RX Clocks */
#define ANOMALY_05000265 (__SILICON_REVISION__ < 5)
/* IMDMA Destination IRQ Status Must Be Read Prior to Using IMDMA */
#define ANOMALY_05000266 (__SILICON_REVISION__ > 3)
/* IMDMA May Corrupt Data under Certain Conditions */
#define ANOMALY_05000267 (1)
/* High I/O Activity Causes Output Voltage of Internal Voltage Regulator (Vddint) to Increase */
#define ANOMALY_05000269 (1)
/* High I/O Activity Causes Output Voltage of Internal Voltage Regulator (Vddint) to Decrease */
#define ANOMALY_05000270 (1)
/* Certain Data Cache Writethrough Modes Fail for Vddint <= 0.9V */
#define ANOMALY_05000272 (1)
/* Data Cache Write Back to External Synchronous Memory May Be Lost */
#define ANOMALY_05000274 (1)
/* PPI Timing and Sampling Information Updates */
#define ANOMALY_05000275 (__SILICON_REVISION__ > 2)
/* Timing Requirements Change for External Frame Sync PPI Modes with Non-Zero PPI_DELAY */
#define ANOMALY_05000276 (__SILICON_REVISION__ < 5)
/* Writes to an I/O Data Register One SCLK Cycle after an Edge Is Detected May Clear Interrupt */
#define ANOMALY_05000277 (__SILICON_REVISION__ < 5)
/* Disabling Peripherals with DMA Running May Cause DMA System Instability */
#define ANOMALY_05000278 (__SILICON_REVISION__ < 5)
/* False Hardware Error when ISR Context Is Not Restored */
/* Temporarily walk around for bug 5423 till this issue is confirmed by
 * official anomaly document. It looks 05000281 still exists on bf561
 * v0.5.
 */
#define ANOMALY_05000281 (__SILICON_REVISION__ <= 5)
/* System MMR Write Is Stalled Indefinitely when Killed in a Particular Stage */
#define ANOMALY_05000283 (1)
/* Reads Will Receive Incorrect Data under Certain Conditions */
#define ANOMALY_05000287 (__SILICON_REVISION__ < 5)
/* SPORTs May Receive Bad Data If FIFOs Fill Up */
#define ANOMALY_05000288 (__SILICON_REVISION__ < 5)
/* Memory-To-Memory DMA Source/Destination Descriptors Must Be in Same Memory Space */
#define ANOMALY_05000301 (1)
/* SSYNCs after Writes to DMA MMR Registers May Not Be Handled Correctly */
#define ANOMALY_05000302 (1)
/* SPORT_HYS Bit in PLL_CTL Register Is Not Functional */
#define ANOMALY_05000305 (__SILICON_REVISION__ < 5)
/* SCKELOW Bit Does Not Maintain State Through Hibernate */
#define ANOMALY_05000307 (__SILICON_REVISION__ < 5)
/* False Hardware Errors Caused by Fetches at the Boundary of Reserved Memory */
#define ANOMALY_05000310 (1)
/* Errors when SSYNC, CSYNC, or Loads to LT, LB and LC Registers Are Interrupted */
#define ANOMALY_05000312 (1)
/* PPI Is Level-Sensitive on First Transfer In Single Frame Sync Modes */
#define ANOMALY_05000313 (1)
/* Killed System MMR Write Completes Erroneously on Next System MMR Access */
#define ANOMALY_05000315 (1)
/* PF2 Output Remains Asserted after SPI Master Boot */
#define ANOMALY_05000320 (__SILICON_REVISION__ > 3)
/* Erroneous GPIO Flag Pin Operations under Specific Sequences */
#define ANOMALY_05000323 (1)
/* SPORT Secondary Receive Channel Not Functional when Word Length >16 Bits */
#define ANOMALY_05000326 (__SILICON_REVISION__ > 3)
/* 24-Bit SPI Boot Mode Is Not Functional */
#define ANOMALY_05000331 (__SILICON_REVISION__ < 5)
/* Slave SPI Boot Mode Is Not Functional */
#define ANOMALY_05000332 (__SILICON_REVISION__ < 5)
/* Flag Data Register Writes One SCLK Cycle after Edge Is Detected May Clear Interrupt Status */
#define ANOMALY_05000333 (__SILICON_REVISION__ < 5)
/* ALT_TIMING Bit in PLL_CTL Register Is Not Functional */
#define ANOMALY_05000339 (__SILICON_REVISION__ < 5)
/* Memory DMA FIFO Causes Throughput Degradation on Writes to External Memory */
#define ANOMALY_05000343 (__SILICON_REVISION__ < 5)
/* Serial Port (SPORT) Multichannel Transmit Failure when Channel 0 Is Disabled */
#define ANOMALY_05000357 (1)
/* Conflicting Column Address Widths Causes SDRAM Errors */
#define ANOMALY_05000362 (1)
/* UART Break Signal Issues */
#define ANOMALY_05000363 (__SILICON_REVISION__ < 5)
/* PPI Underflow Error Goes Undetected in ITU-R 656 Mode */
#define ANOMALY_05000366 (1)
/* Possible RETS Register Corruption when Subroutine Is under 5 Cycles in Duration */
#define ANOMALY_05000371 (1)
/* Level-Sensitive External GPIO Wakeups May Cause Indefinite Stall */
#define ANOMALY_05000403 (1)
/* TESTSET Instruction Causes Data Corruption with Writeback Data Cache Enabled */
#define ANOMALY_05000412 (1)
/* Speculative Fetches Can Cause Undesired External FIFO Operations */
#define ANOMALY_05000416 (1)
/* Multichannel SPORT Channel Misalignment Under Specific Configuration */
#define ANOMALY_05000425 (1)
/* Speculative Fetches of Indirect-Pointer Instructions Can Cause False Hardware Errors */
#define ANOMALY_05000426 (1)
/* Lost/Corrupted L2/L3 Memory Write after Speculative L2 Memory Read by Core B */
#define ANOMALY_05000428 (__SILICON_REVISION__ > 3)
/* IFLUSH Instruction at End of Hardware Loop Causes Infinite Stall */
#define ANOMALY_05000443 (1)
/* SCKELOW Feature Is Not Functional */
#define ANOMALY_05000458 (1)
/* False Hardware Error when RETI Points to Invalid Memory */
#define ANOMALY_05000461 (1)
/* Synchronization Problem at Startup May Cause SPORT Transmit Channels to Misalign */
#define ANOMALY_05000462 (1)
/* Boot Failure When SDRAM Control Signals Toggle Coming Out Of Reset */
#define ANOMALY_05000471 (1)
/* Interrupted SPORT Receive Data Register Read Results In Underflow when SLEN > 15 */
#define ANOMALY_05000473 (1)
/* Possible Lockup Condition when Modifying PLL from External Memory */
#define ANOMALY_05000475 (1)
/* TESTSET Instruction Cannot Be Interrupted */
#define ANOMALY_05000477 (1)
/* Reads of ITEST_COMMAND and ITEST_DATA Registers Cause Cache Corruption */
#define ANOMALY_05000481 (1)
/* PLL May Latch Incorrect Values Coming Out of Reset */
#define ANOMALY_05000489 (1)
/* Instruction Memory Stalls Can Cause IFLUSH to Fail */
#define ANOMALY_05000491 (1)
/* EXCPT Instruction May Be Lost If NMI Happens Simultaneously */
#define ANOMALY_05000494 (1)
/* RXS Bit in SPI_STAT May Become Stuck In RX DMA Modes */
#define ANOMALY_05000501 (1)

/*
 * These anomalies have been "phased" out of analog.com anomaly sheets and are
 * here to show running on older silicon just isn't feasible.
 */

/* Trace Buffers May Contain Errors in Emulation Mode and/or Exception, NMI, Reset Handlers */
#define ANOMALY_05000116 (__SILICON_REVISION__ < 3)
/* Erroneous Exception when Enabling Cache */
#define ANOMALY_05000125 (__SILICON_REVISION__ < 3)
/* Two bits in the Watchpoint Status Register (WPSTAT) are swapped */
#define ANOMALY_05000134 (__SILICON_REVISION__ < 3)
/* Enable wires from the Data Watchpoint Address Control Register (WPDACTL) are swapped */
#define ANOMALY_05000135 (__SILICON_REVISION__ < 3)
/* Stall in multi-unit DMA operations */
#define ANOMALY_05000136 (__SILICON_REVISION__ < 3)
/* Allowing the SPORT RX FIFO to fill will cause an overflow */
#define ANOMALY_05000140 (__SILICON_REVISION__ < 3)
/* Infinite Stall may occur with a particular sequence of consecutive dual dag events */
#define ANOMALY_05000141 (__SILICON_REVISION__ < 3)
/* Interrupts may be lost when a programmable input flag is configured to be edge sensitive */
#define ANOMALY_05000142 (__SILICON_REVISION__ < 3)
/* DMA and TESTSET conflict when both are accessing external memory */
#define ANOMALY_05000144 (__SILICON_REVISION__ < 3)
/* In PWM_OUT mode, you must enable the PPI block to generate a waveform from PPI_CLK */
#define ANOMALY_05000145 (__SILICON_REVISION__ < 3)
/* MDMA may lose the first few words of a descriptor chain */
#define ANOMALY_05000146 (__SILICON_REVISION__ < 3)
/* Source MDMA descriptor may stop with a DMA Error near beginning of descriptor fetch */
#define ANOMALY_05000147 (__SILICON_REVISION__ < 3)
/* DMA engine may lose data due to incorrect handshaking */
#define ANOMALY_05000150 (__SILICON_REVISION__ < 3)
/* DMA stalls when all three controllers read data from the same source */
#define ANOMALY_05000151 (__SILICON_REVISION__ < 3)
/* Execution stall when executing in L2 and doing external accesses */
#define ANOMALY_05000152 (__SILICON_REVISION__ < 3)
/* Frame Delay in SPORT Multichannel Mode */
#define ANOMALY_05000153 (__SILICON_REVISION__ < 3)
/* SPORT TFS signal stays active in multichannel mode outside of valid channels */
#define ANOMALY_05000154 (__SILICON_REVISION__ < 3)
/* Killed 32-Bit MMR Write Leads to Next System MMR Access Thinking It Should Be 32-Bit */
#define ANOMALY_05000157 (__SILICON_REVISION__ < 3)
/* DMA Lock-up at CCLK to SCLK ratios of 4:1, 2:1, or 1:1 */
#define ANOMALY_05000159 (__SILICON_REVISION__ < 3)
/* A read from external memory may return a wrong value with data cache enabled */
#define ANOMALY_05000160 (__SILICON_REVISION__ < 3)
/* Data Cache Fill data can be corrupted after/during Instruction DMA if certain core stalls exist */
#define ANOMALY_05000161 (__SILICON_REVISION__ < 3)
/* DMEM_CONTROL<12> is not set on Reset */
#define ANOMALY_05000162 (__SILICON_REVISION__ < 3)
/* SPORT Transmit Data Is Not Gated by External Frame Sync in Certain Conditions */
#define ANOMALY_05000163 (__SILICON_REVISION__ < 3)
/* DSPID register values incorrect */
#define ANOMALY_05000172 (__SILICON_REVISION__ < 3)
/* DMA vs Core accesses to external memory */
#define ANOMALY_05000173 (__SILICON_REVISION__ < 3)
/* PPI does not invert the Driving PPICLK edge in Transmit Modes */
#define ANOMALY_05000191 (__SILICON_REVISION__ < 3)
/* SSYNC Stalls Processor when Executed from Non-Cacheable Memory */
#define ANOMALY_05000402 (__SILICON_REVISION__ == 4)

/* Anomalies that don't exist on this proc */
#define ANOMALY_05000119 (0)
#define ANOMALY_05000158 (0)
#define ANOMALY_05000183 (0)
#define ANOMALY_05000233 (0)
#define ANOMALY_05000234 (0)
#define ANOMALY_05000273 (0)
#define ANOMALY_05000311 (0)
#define ANOMALY_05000353 (1)
#define ANOMALY_05000364 (0)
#define ANOMALY_05000380 (0)
#define ANOMALY_05000383 (0)
#define ANOMALY_05000386 (1)
#define ANOMALY_05000389 (0)
#define ANOMALY_05000400 (0)
#define ANOMALY_05000430 (0)
#define ANOMALY_05000432 (0)
#define ANOMALY_05000435 (0)
#define ANOMALY_05000440 (0)
#define ANOMALY_05000447 (0)
#define ANOMALY_05000448 (0)
#define ANOMALY_05000456 (0)
#define ANOMALY_05000450 (0)
#define ANOMALY_05000465 (0)
#define ANOMALY_05000467 (0)
#define ANOMALY_05000474 (0)
#define ANOMALY_05000480 (0)
#define ANOMALY_05000485 (0)

#endif
