/*
 * File:         sound/soc/codec/ad73311.h
 * Based on:
 * Author:       Cliff Cai <cliff.cai@analog.com>
 *
 * Created:      Thur Sep 25, 2008
 * Description:  definitions for AD73311 registers
 *
 *
 * Modified:
 *               Copyright 2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __AD73311_H__
#define __AD73311_H__

#if defined(CONFIG_SND_SOC_AD73311)

#define AD_CONTROL	0x8000
#define AD_DATA		0x0000
#define AD_READ		0x4000
#define AD_WRITE	0x0000

/* Control register A */
#define CTRL_REG_A	(0 << 8)

#define REGA_MODE_PRO	0x00
#define REGA_MODE_DATA	0x01
#define REGA_MODE_MIXED	0x03
#define REGA_DLB		0x04
#define REGA_SLB		0x08
#define REGA_DEVC(x)		((x & 0x7) << 4)
#define REGA_RESET		0x80

/* Control register B */
#define CTRL_REG_B	(1 << 8)

#define REGB_DIRATE(x)	(x & 0x3)
#define REGB_SCDIV(x)	((x & 0x3) << 2)
#define REGB_MCDIV(x)	((x & 0x7) << 4)
#define REGB_CEE		(1 << 7)

/* Control register C */
#define CTRL_REG_C	(2 << 8)

#define REGC_PUDEV		(1 << 0)
#define REGC_PUADC		(1 << 3)
#define REGC_PUDAC		(1 << 4)
#define REGC_PUREF		(1 << 5)
#define REGC_REFUSE		(1 << 6)

/* Control register D */
#define CTRL_REG_D	(3 << 8)

#define REGD_IGS(x)		(x & 0x7)
#define REGD_RMOD		(1 << 3)
#define REGD_OGS(x)		((x & 0x7) << 4)
#define REGD_MUTE		(1 << 7)

/* Control register E */
#define CTRL_REG_E	(4 << 8)

#define REGE_DA(x)		(x & 0x1f)
#define REGE_IBYP		(1 << 5)

/* Control register F */
#define CTRL_REG_F	(5 << 8)

#define REGF_SEEN		(1 << 5)
#define REGF_INV		(1 << 6)
#define REGF_ALB		(1 << 7)

#elif defined(CONFIG_SND_SOC_AD74111)

#define AD_READ		0x0000
#define AD_WRITE	0x8000

/* Control register A */
#define CTRL_REG_A	(0 << 11)

#define REGA_REFAMP	(1 << 2)
#define REGA_REF	(1 << 3)
#define REGA_DAC	(1 << 4)
#define REGA_ADC	(1 << 5)
#define REGA_ADC_INPAMP (1 << 6)

/* Control register B */
#define CTRL_REG_B	(1 << 11)

#define REGB_FCLKDIV(x)	(x & 0x3)
#define REGB_SCLKDIV(x)	((x & 0x3) << 2)
#define REGB_TCLKDIV(x)	((x & 0x3) << 4)

/* Control register C */
#define CTRL_REG_C	(2 << 11)

#define REGC_ADC_HP		(1 << 0)
#define REGC_DAC_DEEMPH(x)	((x & 0x3) << 1)
#define REGC_LG_DELAY		(1 << 3)
#define REGC_WORD_WIDTH(x)	((x & 0x3) << 4)

/* Control register D */
#define CTRL_REG_D	(3 << 11)

#define REGD_MASTER		(1 << 0)
#define REGD_FDCLK		(1 << 1)
#define REGD_DSP_MODE		(1 << 2)
#define REGD_MIX_MODE		(1 << 3)
#define REGD_MFS		(1 << 9)

/* Control register E */
#define CTRL_REG_E	(4 << 11)

#define REGE_DAC_MUTE		(1 << 0)
#define REGE_ADC_MUTE		(1 << 1)
#define REGE_ADC_GAIN(x)	((x & 0x7) << 2)
#define REGE_ADC_PEAKEN		(1 << 5)

/* Control register F */
#define CTRL_REG_F	(5 << 11)
#define REGF_DAC_VOL(x)		(x & 0x3F)

/* Control register G */
#define CTRL_REG_F	(6 << 11)
#define REGF_DAC_VOL(x)		(x & 0x1FF)
#endif

extern struct snd_soc_dai ad73311_dai;
extern struct snd_soc_codec_device soc_codec_dev_ad73311;
#endif
