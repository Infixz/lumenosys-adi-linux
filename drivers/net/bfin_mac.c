/*
 * File:         drivers/net/bfin_mac.c
 * Based on:
 * Author:       Luke Yang <luke.yang@analog.com>
 *
 * Created:
 * Description:
 *
 * Rev:          $Id$
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software ;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ;  either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program ;  see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/crc32.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/mii.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>

#include <asm/irq.h>
#include <asm/blackfin.h>
#include <asm/delay.h>

#include "bfin_mac.h"

#define CARDNAME "bfin_mac"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luke Yang");
MODULE_DESCRIPTION("Blackfin MAC Driver");

#if defined(CONFIG_BFIN_MAC_USE_L1)
# define bfin_mac_alloc(dma_handle, size)  l1_data_sram_zalloc(size)
# define bfin_mac_free(dma_handle, ptr)    l1_data_sram_free(ptr)
#else
# define bfin_mac_alloc(dma_handle, size)  dma_alloc_coherent(NULL, size, dma_handle, GFP_DMA)
# define bfin_mac_free(dma_handle, ptr)    dma_free_coherent(NULL, sizeof(*ptr), ptr, dma_handle)
#endif

#define PKT_BUF_SZ 1580

static void desc_list_free(void);

/* pointers to maintain transmit list */
static struct net_dma_desc_tx *tx_list_head;
static struct net_dma_desc_tx *tx_list_tail;
static struct net_dma_desc_rx *rx_list_head;
static struct net_dma_desc_rx *rx_list_tail;
static struct net_dma_desc_rx *current_rx_ptr;
static struct net_dma_desc_tx *current_tx_ptr;
static struct net_dma_desc_tx *tx_desc;
static struct net_dma_desc_rx *rx_desc;

static int desc_list_init(void)
{
	struct net_dma_desc_tx *tmp_desc_tx;
	struct net_dma_desc_rx *tmp_desc_rx;
	int i;
	struct sk_buff *new_skb;
#if !defined(CONFIG_BFIN_MAC_USE_L1)
	dma_addr_t dma_handle;
#endif

	tx_desc =
	    bfin_mac_alloc(&dma_handle,
			   sizeof(struct net_dma_desc_tx) *
			   CONFIG_BFIN_TX_DESC_NUM);
	if (tx_desc == NULL)
		goto init_error;

	rx_desc =
	    bfin_mac_alloc(&dma_handle,
			   sizeof(struct net_dma_desc_rx) *
			   CONFIG_BFIN_RX_DESC_NUM);
	if (rx_desc == NULL)
		goto init_error;

	/* init tx_list */
	for (i = 0; i < CONFIG_BFIN_TX_DESC_NUM; i++) {

		tmp_desc_tx = tx_desc + i;

		if (i == 0) {
			tx_list_head = tmp_desc_tx;
			tx_list_tail = tmp_desc_tx;
		}

		tmp_desc_tx->desc_a.start_addr =
		    (unsigned long)tmp_desc_tx->packet;
		tmp_desc_tx->desc_a.x_count = 0;
		tmp_desc_tx->desc_a.config.b_DMA_EN = 0;	/* disabled */
		tmp_desc_tx->desc_a.config.b_WNR = 0;		/* read from memory */
		tmp_desc_tx->desc_a.config.b_WDSIZE = 2;	/* wordsize is 32 bits */
		tmp_desc_tx->desc_a.config.b_NDSIZE = 6;	/* 6 half words is desc size. */
		tmp_desc_tx->desc_a.config.b_FLOW = 7;		/* large desc flow */
		tmp_desc_tx->desc_a.next_dma_desc = &(tmp_desc_tx->desc_b);

		tmp_desc_tx->desc_b.start_addr =
		    (unsigned long)(&(tmp_desc_tx->status));
		tmp_desc_tx->desc_b.x_count = 0;
		tmp_desc_tx->desc_b.config.b_DMA_EN = 1;	/* enabled */
		tmp_desc_tx->desc_b.config.b_WNR = 1;		/* write to memory */
		tmp_desc_tx->desc_b.config.b_WDSIZE = 2;	/* wordsize is 32 bits */
		tmp_desc_tx->desc_b.config.b_DI_EN = 0;		/* disable interrupt */
		tmp_desc_tx->desc_b.config.b_NDSIZE = 6;
		tmp_desc_tx->desc_b.config.b_FLOW = 7;		/* stop mode */
		tmp_desc_tx->skb = NULL;
		tx_list_tail->desc_b.next_dma_desc = &(tmp_desc_tx->desc_a);
		tx_list_tail->next = tmp_desc_tx;

		tx_list_tail = tmp_desc_tx;
	}
	tx_list_tail->next = tx_list_head;	/* tx_list is a circle */
	tx_list_tail->desc_b.next_dma_desc = &(tx_list_head->desc_a);
	current_tx_ptr = tx_list_head;

	/* init rx_list */
	for (i = 0; i < CONFIG_BFIN_RX_DESC_NUM; i++) {

		tmp_desc_rx = rx_desc + i;

		if (i == 0) {
			rx_list_head = tmp_desc_rx;
			rx_list_tail = tmp_desc_rx;
		}

		/* allocat a new skb for next time receive */
		new_skb = dev_alloc_skb(PKT_BUF_SZ + 2);
		if (!new_skb) {
			printk(KERN_NOTICE CARDNAME
			       ": init: low on mem - packet dropped\n");
			goto init_error;
		}
		skb_reserve(new_skb, 2);
		tmp_desc_rx->skb = new_skb;
		/* since RXDWA is enabled */
		tmp_desc_rx->desc_a.start_addr =
		    (unsigned long)new_skb->data - 2;
		tmp_desc_rx->desc_a.x_count = 0;
		tmp_desc_rx->desc_a.config.b_DMA_EN = 1;	/* enabled */
		tmp_desc_rx->desc_a.config.b_WNR = 1;		/* Write to memory */
		tmp_desc_rx->desc_a.config.b_WDSIZE = 2;	/* wordsize is 32 bits */
		tmp_desc_rx->desc_a.config.b_NDSIZE = 6;	/* 6 half words is desc size. */
		tmp_desc_rx->desc_a.config.b_FLOW = 7;		/* large desc flow */
		tmp_desc_rx->desc_a.next_dma_desc = &(tmp_desc_rx->desc_b);

		tmp_desc_rx->desc_b.start_addr =
		    (unsigned long)(&(tmp_desc_rx->status));
		tmp_desc_rx->desc_b.x_count = 0;
		tmp_desc_rx->desc_b.config.b_DMA_EN = 1;	/* enabled */
		tmp_desc_rx->desc_b.config.b_WNR = 1;		/* Write to memory */
		tmp_desc_rx->desc_b.config.b_WDSIZE = 2;	/* wordsize is 32 bits */
		tmp_desc_rx->desc_b.config.b_NDSIZE = 6;
		tmp_desc_rx->desc_b.config.b_DI_EN = 1;		/* enable interrupt */
		tmp_desc_rx->desc_b.config.b_FLOW = 7;		/* large mode */
		rx_list_tail->desc_b.next_dma_desc = &(tmp_desc_rx->desc_a);

		rx_list_tail->next = tmp_desc_rx;
		rx_list_tail = tmp_desc_rx;
	}
	rx_list_tail->next = rx_list_head;	/* rx_list is a circle */
	rx_list_tail->desc_b.next_dma_desc = &(rx_list_head->desc_a);
	current_rx_ptr = rx_list_head;

	return 0;

      init_error:
	desc_list_free();
	printk(KERN_ERR CARDNAME ": kmalloc failed\n");
	return -ENOMEM;
}

static void desc_list_free(void)
{
	struct net_dma_desc_rx *tmp_desc_rx;
	struct net_dma_desc_tx *tmp_desc_tx;
	int i;
#if !defined(CONFIG_BFIN_MAC_USE_L1)
	dma_addr_t dma_handle = 0;
#endif

	if (tx_desc != NULL) {
		tmp_desc_tx = tx_list_head;
		for (i = 0; i < CONFIG_BFIN_TX_DESC_NUM; i++) {
			if (tmp_desc_tx != NULL) {
				if (tmp_desc_tx->skb) {
					dev_kfree_skb(tmp_desc_tx->skb);
					tmp_desc_tx->skb = NULL;
				}

			}
			tmp_desc_tx = tmp_desc_tx->next;
		}
		bfin_mac_free(dma_handle, tx_desc);
	}

	if (rx_desc != NULL) {
		tmp_desc_rx = rx_list_head;
		for (i = 0; i < CONFIG_BFIN_RX_DESC_NUM; i++) {
			if (tmp_desc_rx != NULL) {
				if (tmp_desc_rx->skb) {
					dev_kfree_skb(tmp_desc_rx->skb);
					tmp_desc_rx->skb = NULL;
				}
			}
			tmp_desc_rx = tmp_desc_rx->next;
		}
		bfin_mac_free(dma_handle, rx_desc);
	}
}

/*---PHY CONTROL AND CONFIGURATION-----------------------------------------*/

/* Set FER regs to MUX in Ethernet pins */
static void setup_pin_mux(void)
{
	unsigned int fer_val;

	/* 
	 * FER reg bug work-around 
	 * read it once
	 */
	fer_val = bfin_read_PORTH_FER();

#if defined(CONFIG_BFIN_MAC_RMII)
	fer_val = 0xC373;
#else
	fer_val = 0xffff;
#endif
	/* write it twice to the same value */

	bfin_write_PORTH_FER(fer_val);
	bfin_write_PORTH_FER(fer_val);
}

/* Wait until the previous MDC/MDIO transaction has completed */
static void poll_mdc_done(void)
{
	/* poll the STABUSY bit */
	while ((bfin_read_EMAC_STAADD()) & STABUSY) {
	};
}

/* Read an off-chip register in a PHY through the MDC/MDIO port */
static u16 read_phy_reg(u16 PHYAddr, u16 RegAddr)
{
	poll_mdc_done();
	bfin_write_EMAC_STAADD(SET_PHYAD(PHYAddr) | SET_REGAD(RegAddr) | STABUSY);	/* read mode */
	poll_mdc_done();

	return (u16) bfin_read_EMAC_STADAT();
}

/* Write an off-chip register in a PHY through the MDC/MDIO port */
static void raw_write_phy_reg(u16 PHYAddr, u16 RegAddr, u32 Data)
{
	bfin_write_EMAC_STADAT(Data);

	bfin_write_EMAC_STAADD(SET_PHYAD(PHYAddr) | SET_REGAD(RegAddr) | STAOP | STABUSY);	/* write mode */

	poll_mdc_done();
}

static void write_phy_reg(u16 PHYAddr, u16 RegAddr, u32 Data)
{
	poll_mdc_done();
	raw_write_phy_reg(PHYAddr, RegAddr, Data);
}

/* set up the phy */
static void bf537mac_setphy(struct net_device *dev)
{
	u16 phydat;
	u32 sysctl;
	struct bf537mac_local *lp = netdev_priv(dev);

	pr_debug("start settting up phy\n");

	/* Program PHY registers */
	phydat = 0;

	/* issue a reset */
	raw_write_phy_reg(lp->PhyAddr, PHYREG_MODECTL, 0x8000);

	/* wait half a second */
	udelay(500);

	phydat = read_phy_reg(lp->PhyAddr, PHYREG_MODECTL);

	/* advertise flow control supported */
	phydat = read_phy_reg(lp->PhyAddr, PHYREG_ANAR);
	phydat |= (1 << 10);
	write_phy_reg(lp->PhyAddr, PHYREG_ANAR, phydat);

	phydat = 0;
	if (lp->Negotiate) {
		phydat |= 0x1000;	/* enable auto negotiation */
	} else {
		if (lp->FullDuplex) {
			phydat |= (1 << 8);	/* full duplex */
		} else {
			phydat &= (~(1 << 8));	/* half duplex */
		}
		if (!lp->Port10) {
			phydat |= (1 << 13);	/* 100 Mbps */
		} else {
			phydat &= (~(1 << 13));	/* 10 Mbps */
		}
	}

	if (lp->Loopback) {
		phydat |= (1 << 14);	/* enable TX->RX loopback */
#if 0
		write_phy_reg(lp->PhyAddr, PHYREG_MODECTL, phydat);
#endif
	}

	write_phy_reg(lp->PhyAddr, PHYREG_MODECTL, phydat);
	udelay(500);

	phydat = read_phy_reg(lp->PhyAddr, PHYREG_MODECTL);
	/* check for SMSC PHY */
	if ((read_phy_reg(lp->PhyAddr, PHYREG_PHYID1) == 0x7)
	    && ((read_phy_reg(lp->PhyAddr, PHYREG_PHYID2) & 0xfff0) == 0xC0A0)) {
		/* we have SMSC PHY so reqest interrupt on link down condition */
		write_phy_reg(lp->PhyAddr, 30, 0x0ff);	/* enable interrupts */
		/* enable PHY_INT */
		sysctl = bfin_read_EMAC_SYSCTL();
		sysctl |= 0x1;
#if 0
		bfin_write_EMAC_SYSCTL(sysctl);
#endif
	}
}

/**************************************************************************/
void setup_system_regs(struct net_device *dev)
{
	int PHYADDR;
	unsigned short sysctl, phydat;
	u32 opmode;
	struct bf537mac_local *lp = netdev_priv(dev);
	int count = 0;

	PHYADDR = lp->PhyAddr;

	/* Enable PHY output */
	if (!(bfin_read_VR_CTL() & PHYCLKOE))
		bfin_write_VR_CTL(bfin_read_VR_CTL() | PHYCLKOE);

	/* MDC  = 2.5 MHz */
	sysctl = SET_MDCDIV(24);
	/* Odd word alignment for Receive Frame DMA word */
	/* Configure checksum support and rcve frame word alignment */
#if defined(BFIN_MAC_CSUM_OFFLOAD)
	sysctl |= RXDWA | RXCKS;
#else
	sysctl |= RXDWA;
#endif
	bfin_write_EMAC_SYSCTL(sysctl);
	/* auto negotiation on  */
	/* full duplex          */
	/* 100 Mbps             */
	phydat = PHY_ANEG_EN | PHY_DUPLEX | PHY_SPD_SET;
	write_phy_reg(PHYADDR, PHYREG_MODECTL, phydat);

	/* test if full duplex supported */
	do {
		msleep(100);
		phydat = read_phy_reg(PHYADDR, PHYREG_MODESTAT);
		if (count > 30) {
			printk(KERN_NOTICE CARDNAME
			       ": Link is down, please check your network connection\n");
			break;
		}
		count++;
	} while (!(phydat & 0x0004));

	phydat = read_phy_reg(PHYADDR, PHYREG_ANLPAR);

	if ((phydat & 0x0100) || (phydat & 0x0040)) {
		opmode = FDMODE;
	} else {
		opmode = 0;
		printk(KERN_INFO CARDNAME ": Network is set to half duplex\n");
	}

#if defined(CONFIG_BFIN_MAC_RMII)
	opmode |= RMII; /* For Now only 100MBit are supported */
#endif

	bfin_write_EMAC_OPMODE(opmode);

#if 0
	bfin_write_EMAC_MMC_CTL(RSTC | CROLL | MMCE);
#endif
	bfin_write_EMAC_MMC_CTL(RSTC | CROLL);

	/* Initialize the TX DMA channel registers */
	bfin_write_DMA2_X_COUNT(0);
	bfin_write_DMA2_X_MODIFY(4);
	bfin_write_DMA2_Y_COUNT(0);
	bfin_write_DMA2_Y_MODIFY(0);

	/* Initialize the RX DMA channel registers */
	bfin_write_DMA1_X_COUNT(0);
	bfin_write_DMA1_X_MODIFY(4);
	bfin_write_DMA1_Y_COUNT(0);
	bfin_write_DMA1_Y_MODIFY(0);
}

void setup_mac_addr(u8 * mac_addr)
{
	/* this depends on a little-endian machine */
	bfin_write_EMAC_ADDRLO(*(u32 *) & mac_addr[0]);
	bfin_write_EMAC_ADDRHI(*(u16 *) & mac_addr[4]);
}

static void adjust_tx_list(void)
{
	if (tx_list_head->status.status_word != 0
	    && current_tx_ptr != tx_list_head) {
		goto adjust_head;	/* released something, just return; */
	}

	/* if nothing released, check wait condition */
	/* current's next can not be the head, otherwise the dma will not stop as we want */
	if (current_tx_ptr->next->next == tx_list_head) {
		while (tx_list_head->status.status_word == 0) {
			udelay(10);
			if (tx_list_head->status.status_word != 0
			    || !(bfin_read_DMA2_IRQ_STATUS() & 0x08)) {
				goto adjust_head;
			}
		}
		if (tx_list_head->status.status_word != 0) {
			goto adjust_head;
		}
	}

	return;

      adjust_head:
	do {
		tx_list_head->desc_a.config.b_DMA_EN = 0;
		tx_list_head->status.status_word = 0;
		if (tx_list_head->skb) {
			dev_kfree_skb(tx_list_head->skb);
			tx_list_head->skb = NULL;
		} else {
			printk(KERN_ERR CARDNAME
			       ": no sk_buff in a transmitted frame!\n");
		}
		tx_list_head = tx_list_head->next;
	} while (tx_list_head->status.status_word != 0
		 && current_tx_ptr != tx_list_head);
	return;

}

static int bf537mac_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bf537mac_local *lp = netdev_priv(dev);
	unsigned int data;

	current_tx_ptr->skb = skb;
	/* Is skb->data always 16-bit aligned? Do we need to memcpy((char *)(tail->packet + 2),skb->data,len)? */
	if ((((unsigned int)(skb->data)) & 0x02) == 2) {
		/* move skb->data to current_tx_ptr payload */
		data = (unsigned int)(skb->data) - 2;
		*((unsigned short *)data) = (unsigned short)(skb->len);
		current_tx_ptr->desc_a.start_addr = (unsigned long)data;
		blackfin_dcache_flush_range(data, (data + (skb->len)) + 2);	/* this is important! */

	} else {
		*((unsigned short *)(current_tx_ptr->packet)) =
		    (unsigned short)(skb->len);
		memcpy((char *)(current_tx_ptr->packet + 2), skb->data,
		       (skb->len));
		current_tx_ptr->desc_a.start_addr =
		    (unsigned long)current_tx_ptr->packet;
		if (current_tx_ptr->status.status_word != 0)
			current_tx_ptr->status.status_word = 0;
		blackfin_dcache_flush_range((unsigned int)current_tx_ptr->
					    packet,
					    (unsigned int)(current_tx_ptr->
							   packet + skb->len) +
					    2);
	}

	current_tx_ptr->desc_a.config.b_DMA_EN = 1;	/* enable this packet's dma */

	if (bfin_read_DMA2_IRQ_STATUS() & 0x08) {	/* tx dma is running, just return */
		goto out;
	} else {
		/* tx dma is not running */
		bfin_write_DMA2_NEXT_DESC_PTR(&(current_tx_ptr->desc_a));
		/* dma enabled, read from memory, size is 6 */
		bfin_write_DMA2_CONFIG(*((unsigned short *)(&(current_tx_ptr->desc_a.config))));
		/* Turn on the EMAC tx */
		bfin_write_EMAC_OPMODE(bfin_read_EMAC_OPMODE() | TE);
	}

      out:
	adjust_tx_list();
	current_tx_ptr = current_tx_ptr->next;
	dev->trans_start = jiffies;
	lp->stats.tx_packets++;
	lp->stats.tx_bytes += (skb->len);
	return 0;
}

static void bf537mac_rx(struct net_device *dev)
{
	struct sk_buff *skb, *new_skb;
	struct bf537mac_local *lp = netdev_priv(dev);
	unsigned short len;

	/* allocat a new skb for next time receive */
	skb = current_rx_ptr->skb;
	new_skb = dev_alloc_skb(PKT_BUF_SZ + 2);
	if (!new_skb) {
		printk(KERN_NOTICE CARDNAME
		       ": rx: low on mem - packet dropped\n");
		lp->stats.rx_dropped++;
		goto out;
	}
	/* reserve 2 bytes for RXDWA padding */
	skb_reserve(new_skb, 2);
	current_rx_ptr->skb = new_skb;
	current_rx_ptr->desc_a.start_addr = (unsigned long)new_skb->data - 2;

#if 0
	int i;
	if (len >= 64) {
		for (i=0;i<len;i++) {
			printk("%.2x-",((unsigned char *)pkt)[i]);
			if (((i%8)==0) && (i!=0)) printk("\n");
		}
	printk("\n");
	}
#endif

	len = (unsigned short)((current_rx_ptr->status.status_word) & RX_FRLEN);
	skb_put(skb, len);
	blackfin_dcache_invalidate_range((unsigned long)skb->head,
					 (unsigned long)skb->tail);

	dev->last_rx = jiffies;
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
#if defined(BFIN_MAC_CSUM_OFFLOAD)
	skb->csum = current_rx_ptr->status.ip_payload_csum;
	skb->ip_summed = CHECKSUM_PARTIAL;
#endif

	netif_rx(skb);
	lp->stats.rx_packets++;
	lp->stats.rx_bytes += len;
	current_rx_ptr->status.status_word = 0x00000000;
	current_rx_ptr = current_rx_ptr->next;

      out:
	return;
}

/* interrupt routine to handle rx and error signal */
static irqreturn_t bf537mac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	int number = 0;

      get_one_packet:
	if (current_rx_ptr->status.status_word == 0) {	/* no more new packet received */
		if (number == 0) {
			if (current_rx_ptr->next->status.status_word != 0) {
				current_rx_ptr = current_rx_ptr->next;
				goto real_rx;
			}
		}
		bfin_write_DMA1_IRQ_STATUS(bfin_read_DMA1_IRQ_STATUS() |
					   DMA_DONE | DMA_ERR);
		return IRQ_HANDLED;
	}

      real_rx:
	bf537mac_rx(dev);
	number++;
	goto get_one_packet;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void bf537mac_poll(struct net_device *dev)
{
	disable_irq(IRQ_MAC_RX);
	bf537mac_interrupt(IRQ_MAC_RX, dev);
	enable_irq(IRQ_MAC_RX);
}
#endif				/* CONFIG_NET_POLL_CONTROLLER */

static void bf537mac_reset(void)
{
	unsigned int opmode;

	opmode = bfin_read_EMAC_OPMODE();
	opmode &= (~RE);
	opmode &= (~TE);
	/* Turn off the EMAC */
	bfin_write_EMAC_OPMODE(bfin_read_EMAC_OPMODE() & opmode);
}

/*
 * Enable Interrupts, Receive, and Transmit
 */
static int bf537mac_enable(struct net_device *dev)
{
	u32 opmode;

	pr_debug("%s: %s\n", dev->name, __FUNCTION__);

	/* Set RX DMA */
	bfin_write_DMA1_NEXT_DESC_PTR(&(rx_list_head->desc_a));
	bfin_write_DMA1_CONFIG(*
			       ((unsigned short
				 *)(&(rx_list_head->desc_a.config))));

	/* Wait MII done */
	poll_mdc_done();

	/* We enable only RX here */
	/* ASTP   : Enable Automatic Pad Stripping
	   PR     : Promiscuous Mode for test
	   PSF    : Receive frames with total length less than 64 bytes.
	   FDMODE : Full Duplex Mode
	   LB     : Internal Loopback for test
	   RE     : Receiver Enable */
	opmode = bfin_read_EMAC_OPMODE();
	if (opmode & FDMODE)
		opmode |= PSF;
	else
		opmode |= DRO | DC | PSF;
	opmode |= RE;

#if defined(CONFIG_BFIN_MAC_RMII)
	opmode |= RMII; /* For Now only 100MBit are supported */
#ifdef CONFIG_BF_REV_0_2
	opmode |= TE;
#endif
#endif
	/* Turn on the EMAC rx */
	bfin_write_EMAC_OPMODE(opmode);

	return 0;
}

/* Our watchdog timed out. Called by the networking layer */
static void bf537mac_timeout(struct net_device *dev)
{
	pr_debug("%s: %s\n", dev->name, __FUNCTION__);

	bf537mac_reset();

	/* reset tx queue */
	tx_list_tail = tx_list_head->next;

	bf537mac_enable(dev);

	/* We can accept TX packets again */
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */
static struct net_device_stats *bf537mac_query_statistics(struct net_device
							  *dev)
{
	struct bf537mac_local *lp = netdev_priv(dev);

	pr_debug("%s: %s\n", dev->name, __FUNCTION__);

	return &lp->stats;
}

/*
 * This routine will, depending on the values passed to it,
 * either make it accept multicast packets, go into
 * promiscuous mode (for TCPDUMP and cousins) or accept
 * a select set of multicast packets
 */
static void bf537mac_set_multicast_list(struct net_device *dev)
{
	u32 sysctl;

	if (dev->flags & IFF_PROMISC) {
		printk(KERN_INFO "%s: set to promisc mode\n", dev->name);
		sysctl = bfin_read_EMAC_OPMODE();
		sysctl |= RAF;
		bfin_write_EMAC_OPMODE(sysctl);
	} else if (dev->flags & IFF_ALLMULTI || dev->mc_count > 16) {
		/* accept all multicast */
		sysctl = bfin_read_EMAC_OPMODE();
		sysctl |= PAM;
		bfin_write_EMAC_OPMODE(sysctl);
	} else if (dev->mc_count) {
		/* set multicast */
	} else {
		/* clear promisc or multicast mode */
		sysctl = bfin_read_EMAC_OPMODE();
		sysctl &= ~(RAF | PAM);
		bfin_write_EMAC_OPMODE(sysctl);
	}
}

/*
 * this puts the device in an inactive state
 */
static void bf537mac_shutdown(struct net_device *dev)
{
	/* Turn off the EMAC */
	bfin_write_EMAC_OPMODE(0x00000000);
	/* Turn off the EMAC RX DMA */
	bfin_write_DMA1_CONFIG(0x0000);
	bfin_write_DMA2_CONFIG(0x0000);
}

/*
 * Open and Initialize the interface
 *
 * Set up everything, reset the card, etc..
 */
static int bf537mac_open(struct net_device *dev)
{
	pr_debug("%s: %s\n", dev->name, __FUNCTION__);

	/*
	 * Check that the address is valid.  If its not, refuse
	 * to bring the device up.  The user must specify an
	 * address using ifconfig eth0 hw ether xx:xx:xx:xx:xx:xx
	 */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		printk(KERN_WARNING CARDNAME ": no valid ethernet hw addr\n");
		return -EINVAL;
	}

	/* initial rx and tx list */
	desc_list_init();

	bf537mac_setphy(dev);
	setup_system_regs(dev);
	bf537mac_reset();
	bf537mac_enable(dev);

	pr_debug("hardware init finished\n");
	netif_start_queue(dev);
	netif_carrier_on(dev);

	return 0;
}

/*
 *
 * this makes the board clean up everything that it can
 * and not talk to the outside world.   Caused by
 * an 'ifconfig ethX down'
 */
static int bf537mac_close(struct net_device *dev)
{
	pr_debug("%s: %s\n", dev->name, __FUNCTION__);
	
	netif_stop_queue(dev);
	netif_carrier_off(dev);

	/* clear everything */
	bf537mac_shutdown(dev);

	/* free the rx/tx buffers */
	desc_list_free();

	return 0;
}

void get_bf537_ether_addr(char *addr)
{
	/* currently the mac addr is saved in flash */
	int flash_mac = 0x203f0000;
	*(u32 *)(&(addr[0])) = *(int *)flash_mac;
	flash_mac += 4;
	*(u16 *)(&(addr[4])) = (u16)*(int *)flash_mac;
}

static int __init bf537mac_probe(struct net_device *dev)
{
	struct bf537mac_local *lp = netdev_priv(dev);
	int retval;

	/* Grab the MAC address in the MAC */
	*(u32 *) (&(dev->dev_addr[0])) = bfin_read_EMAC_ADDRLO();
	*(u16 *) (&(dev->dev_addr[4])) = (u16) bfin_read_EMAC_ADDRHI();

/* probe mac */
	/*todo: how to proble? which is revision_register */
	bfin_write_EMAC_ADDRLO(0x12345678);
	if (bfin_read_EMAC_ADDRLO() != 0x12345678) {
		pr_debug("can't detect bf537 mac!\n");
		retval = -ENODEV;
		goto err_out;
	}

	/*Is it valid? (Did bootloader initialize it?) */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		/* Grab the MAC from the board somehow - this is done in the
		   arch/blackfin/boards/bf537/boardname.c */
		get_bf537_ether_addr(dev->dev_addr);
	}

	/* If still not valid, get a random one */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		random_ether_addr(dev->dev_addr);
	}

	setup_mac_addr(dev->dev_addr);

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);

	dev->open = bf537mac_open;
	dev->stop = bf537mac_close;
	dev->hard_start_xmit = bf537mac_hard_start_xmit;
	dev->tx_timeout = bf537mac_timeout;
	dev->get_stats = bf537mac_query_statistics;
	dev->set_multicast_list = bf537mac_set_multicast_list;
#if 0
	dev->ethtool_ops = &bf537mac_ethtool_ops;
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = bf537mac_poll;
#endif

	/* fill in some of the fields */
	lp->version = 1;
	lp->PhyAddr = 0x01;
	lp->CLKIN = 25;
	lp->FullDuplex = 0;
	lp->Negotiate = 1;
	lp->FlowControl = 0;
	spin_lock_init(&lp->lock);

	/* set the GPIO pins to Ethernet mode */
	setup_pin_mux();

	/* now, enable interrupts */
	/* register irq handler */
	if (request_irq
	    (IRQ_MAC_RX, bf537mac_interrupt, IRQF_DISABLED | IRQF_SHARED,
	     "BFIN537_MAC_RX", dev)) {
		printk(KERN_WARNING CARDNAME
		       ": Unable to attach BlackFin MAC RX interrupt\n");
		return -EBUSY;
	}

	/* Enable PHY output early */
	if (!(bfin_read_VR_CTL() & PHYCLKOE))
		bfin_write_VR_CTL(bfin_read_VR_CTL() | PHYCLKOE);

	retval = register_netdev(dev);
	if (retval == 0) {
		/* now, print out the card info, in a short format.. */
		printk(KERN_INFO "Blackfin mac net device registered\n");
	}

      err_out:
	return retval;
}

static int bfin_mac_probe(struct platform_device *pdev)
{
	struct net_device *ndev;

	ndev = alloc_etherdev(sizeof(struct bf537mac_local));

	if (!ndev) {
		printk(KERN_WARNING CARDNAME ": could not allocate device\n");
		return -ENOMEM;
	}

	if (bf537mac_probe(ndev) != 0) {
		platform_set_drvdata(pdev, NULL);
		free_netdev(ndev);
		printk(KERN_WARNING CARDNAME ": not found\n");
		return -ENODEV;
	}

	SET_MODULE_OWNER(ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	platform_set_drvdata(pdev, ndev);

	return 0;
}

static int bfin_mac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	unregister_netdev(ndev);

	free_irq(IRQ_MAC_RX, ndev);

	free_netdev(ndev);

	return 0;
}

static int bfin_mac_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int bfin_mac_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver bfin_mac_driver = {
	.probe = bfin_mac_probe,
	.remove = bfin_mac_remove,
	.resume = bfin_mac_resume,
	.suspend = bfin_mac_suspend,
	.driver = {
		   .name = CARDNAME,
		   },
};

static int __init bfin_mac_init(void)
{
	return platform_driver_register(&bfin_mac_driver);
}

module_init(bfin_mac_init);

static void __exit bfin_mac_cleanup(void)
{
	platform_driver_unregister(&bfin_mac_driver);
}

module_exit(bfin_mac_cleanup);
