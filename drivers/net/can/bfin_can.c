/*
 * Blackfin On-Chip CAN Driver
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

#include <asm/portmux.h>

#define DRV_NAME "bfin_can"
#define BFIN_CAN_TIMEOUT 100

/*
 * transmit and receive channels
 */
#define TRANSMIT_CHL		24
#define RECEIVE_STD_CHL 	0
#define RECEIVE_EXT_CHL 	4
#define RECEIVE_RTR_CHL 	8
#define RECEIVE_EXT_RTR_CHL 	12
#define MAX_CHL_NUMBER          32

/*
 * bfin can registers layout
 */
struct bfin_can_mask_regs {
	u16 aml;
	u16 dummy1;
	u16 amh;
	u16 dummy2;
};

struct bfin_can_channel_regs {
	u16 data[8];
	u16 dlc;
	u16 dummy1;
	u16 tsv;
	u16 dummy2;
	u16 id0;
	u16 dummy3;
	u16 id1;
	u16 dummy4;
};

struct bfin_can_regs {
	/*
	 * global control and status registers
	 */
	u16 mc1;           /* offset 0 */
	u16 dummy1;
	u16 md1;           /* offset 4 */
	u16 rsv1[13];
	u16 mbtif1;        /* offset 0x20 */
	u16 dummy2;
	u16 mbrif1;        /* offset 0x24 */
	u16 dummy3;
	u16 mbim1;         /* offset 0x28 */
	u16 rsv2[11];
	u16 mc2;          /* offset 0x40 */
	u16 dummy4;
	u16 md2;          /* offset 0x44 */
	u16 dummy5;
	u16 trs2;         /* offset 0x48 */
	u16 rsv3[11];
	u16 mbtif2;       /* offset 0x60 */
	u16 dummy6;
	u16 mbrif2;       /* offset 0x64 */
	u16 dummy7;
	u16 mbim2;        /* offset 0x68 */
	u16 rsv4[11];
	u16 clk;          /* offset 0x80 */
	u16 dummy8;
	u16 timing;       /* offset 0x84 */
	u16 rsv5[3];
	u16 status;       /* offset 0x8c */
	u16 dummy9;
	u16 cec;          /* offset 0x90 */
	u16 dummy10;
	u16 gis;          /* offset 0x94 */
	u16 dummy11;
	u16 gim;          /* offset 0x98 */
	u16 rsv6[3];
	u16 ctrl;         /* offset 0xa0 */
	u16 dummy12;
	u16 intr;         /* offset 0xa4 */
	u16 rsv7[7];
	u16 esr;          /* offset 0xb4 */
	u16 rsv8[37];

	/*
	 * channel(mailbox) mask and message registers
	 */
	struct bfin_can_mask_regs    msk[MAX_CHL_NUMBER]; /* offset 0x100 */
	struct bfin_can_channel_regs chl[MAX_CHL_NUMBER]; /* offset 0x200 */
};

/*
 * bfin can private data
 */
struct bfin_can_priv {
	struct can_priv can;	/* must be the first member */
	struct net_device *dev;
	void __iomem *membase;
	int rx_irq;
	int tx_irq;
	int err_irq;
	unsigned short *pin_list;
};

/*
 * bfin can timing parameters
 */
static struct can_bittiming_const bfin_can_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	/* Although the BRP field can be set to any value, it is recommended
	 * that the value be greater than or equal to 4, as restrictions
	 * apply to the bit timing configuration when BRP is less than 4.
	 */
	.brp_min = 4,
	.brp_max = 1024,
	.brp_inc = 1,
};

/*
 * inline functions to read/write ID, data length and payload of CAN frame
 */
static inline void bfin_can_write_oid(struct bfin_can_priv *priv, int channel, canid_t id)
{
	struct bfin_can_regs *reg = priv->membase;

	writew((id << 2) + AME, &reg->chl[channel].id1);
}

static inline void bfin_can_write_oid_rtr(struct bfin_can_priv *priv, int channel, canid_t id)
{
	struct bfin_can_regs *reg = priv->membase;

	writew((id << 2) + AME + RTR, &reg->chl[channel].id1);
}

static inline canid_t bfin_can_read_oid(struct bfin_can_priv *priv, int channel)
{
	struct bfin_can_regs *reg = priv->membase;

	return (readw(&reg->chl[channel].id1) & 0x1ffc) >> 2;
}

static inline void bfin_can_write_xoid(struct bfin_can_priv *priv, int channel, canid_t id)
{
	struct bfin_can_regs *reg = priv->membase;

	writew(id, &reg->chl[channel].id0);
	writew(((id & 0x1FFF0000) >> 16) + IDE + AME, &reg->chl[channel].id1);
}

static inline void bfin_can_write_xoid_rtr(struct bfin_can_priv *priv, int channel, canid_t id)
{
	struct bfin_can_regs *reg = priv->membase;

	writew(id, &reg->chl[channel].id0);
	writew(((id & 0x1FFF0000) >> 16) + IDE + AME + RTR, &reg->chl[channel].id1);
}

static inline canid_t bfin_can_read_xoid(struct bfin_can_priv *priv, int channel)
{
	struct bfin_can_regs *reg = priv->membase;

	return ((readw(&reg->chl[channel].id1) & 0x1FFF) << 16) + readw(&reg->chl[channel].id0);
}

static inline void bfin_can_write_dlc(struct bfin_can_priv *priv, int channel, u8 dlc)
{
	struct bfin_can_regs *reg = priv->membase;

	writew(dlc, &reg->chl[channel].dlc);
}

static inline u8 bfin_can_read_dlc(struct bfin_can_priv *priv, int channel)
{
	struct bfin_can_regs *reg = priv->membase;

	return readw(&reg->chl[channel].dlc);
}

static inline void bfin_can_write_data(struct bfin_can_priv *priv, int channel, u8 *data, u8 dlc)
{
	struct bfin_can_regs *reg = priv->membase;
	int i;
	u16 val;

	for (i = 0; i < 8; i += 2) {
		val = ((7 - i) < dlc ? (data[7 - i]) : 0) +
			((6 - i) < dlc ? (data[6 - i] << 8) : 0);
		writew(val, &reg->chl[channel].data[i]);
	}
}

static inline void bfin_can_read_data(struct bfin_can_priv *priv, int channel, u8 *data, u8 dlc)
{
	struct bfin_can_regs *reg = priv->membase;
	int i;
	u16 val;

	for (i = 0; i < 8; i += 2) {
		val = readw(&reg->chl[channel].data[i]);
		data[7 - i] = (7 - i) < dlc ? val : 0;
		data[6 - i] = (6 - i) < dlc ? (val >> 8) : 0;
	}
}

static int bfin_can_set_bittiming(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs *reg = priv->membase;
	struct can_bittiming *bt = &priv->can.bittiming;
	u16 clk, timing;

	clk = bt->brp - 1;
	timing = ((bt->sjw - 1) << 8) | (bt->prop_seg + bt->phase_seg1 - 1) |
		((bt->phase_seg2 - 1) << 4);

	/*
	 * If the SAM bit is set, the input signal is oversampled three times at the SCLK rate.
	 */
	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		timing |= SAM;

	writew(clk, &reg->clk);
	writew(timing, &reg->timing);

	dev_info(dev->dev.parent, "setting CLOCK=0x%04x TIMING=0x%04x\n", clk, timing);
	return 0;
}

static void bfin_can_set_reset_mode(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs *reg = priv->membase;
	int timeout = BFIN_CAN_TIMEOUT;
	int i;

	/* disable interrupts */
	writew(0, &reg->mbim1);
	writew(0, &reg->mbim2);
	writew(0, &reg->gim);

	/* reset can and enter configuration mode */
	writew(SRS | CCR, &reg->ctrl);
	SSYNC();
	writew(CCR, &reg->ctrl);
	SSYNC();
	while (!(readw(&reg->ctrl) & CCA)) {
		udelay(10);
		if (--timeout == 0) {
			dev_err(dev->dev.parent, "fail to enter configuration mode\n");
			BUG();
		}
	}

	/*
	 * All mailbox configurations are marked as inactive
	 * by writing to CAN Mailbox Configuration Registers 1 and 2
	 * For all bits: 0 - Mailbox disabled, 1 - Mailbox enabled
	 */
	writew(0, &reg->mc1);
	writew(0, &reg->mc2);

	/* Set Mailbox Direction */
	writew(0xFFFF, &reg->md1);   /* mailbox 1-16 are RX */
	writew(0, &reg->md2);   /* mailbox 17-32 are TX */

	/* RECEIVE_STD_CHL */
	for (i = 0; i < 2; i++) {
		writew(0, &reg->chl[RECEIVE_STD_CHL + i].id0);
		writew(AME, &reg->chl[RECEIVE_STD_CHL + i].id1);
		writew(0, &reg->chl[RECEIVE_STD_CHL + i].dlc);
		writew(0x1FFF, &reg->msk[RECEIVE_STD_CHL + i].amh);
		writew(0xFFFF, &reg->msk[RECEIVE_STD_CHL + i].aml);
	}

	/* RECEIVE_EXT_CHL */
	for (i = 0; i < 2; i++) {
		writew(0, &reg->chl[RECEIVE_EXT_CHL + i].id0);
		writew(AME | IDE, &reg->chl[RECEIVE_EXT_CHL + i].id1);
		writew(0, &reg->chl[RECEIVE_EXT_CHL + i].dlc);
		writew(0x1FFF, &reg->msk[RECEIVE_EXT_CHL + i].amh);
		writew(0xFFFF, &reg->msk[RECEIVE_EXT_CHL + i].aml);
	}

	writew(BIT(TRANSMIT_CHL - 16), &reg->mc2);
	writew(BIT(RECEIVE_STD_CHL) + BIT(RECEIVE_EXT_CHL), &reg->mc1);
	SSYNC();

	priv->can.state = CAN_STATE_STOPPED;
}

static void bfin_can_set_normal_mode(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs *reg = priv->membase;
	int timeout = BFIN_CAN_TIMEOUT;

	/*
	 * leave configuration mode
	 */
	writew(readw(&reg->ctrl) & ~CCR, &reg->ctrl);

	while (readw(&reg->status) & CCA) {
		udelay(10);
		if (--timeout == 0) {
			dev_err(dev->dev.parent, "fail to leave configuration mode\n");
			BUG();
		}
	}

	/*
	 * clear _All_  tx and rx interrupts
	 */
	writew(0xFFFF, &reg->mbtif1);
	writew(0xFFFF, &reg->mbtif2);
	writew(0xFFFF, &reg->mbrif1);
	writew(0xFFFF, &reg->mbrif2);

	/*
	 * clear global interrupt status register
	 */
	writew(0x7FF, &reg->gis); /* overwrites with '1' */

	/*
	 * Initialize Interrupts
	 * - set bits in the mailbox interrupt mask register
	 * - global interrupt mask
	 */
	writew(BIT(RECEIVE_STD_CHL) + BIT(RECEIVE_EXT_CHL), &reg->mbim1);
	writew(BIT(TRANSMIT_CHL - 16), &reg->mbim2);

	writew(EPIM | BOIM | RMLIM, &reg->gim);
	SSYNC();
}

static void bfin_can_start(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);

	/* leave reset mode */
	if (priv->can.state != CAN_STATE_STOPPED)
		bfin_can_set_reset_mode(dev);

	/* leave reset mode */
	bfin_can_set_normal_mode(dev);
}

static int bfin_can_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		bfin_can_start(dev);
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int bfin_can_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs *reg = priv->membase;
	struct can_frame *cf = (struct can_frame *)skb->data;
	u8 dlc;
	canid_t id;

	netif_stop_queue(dev);

	dlc = cf->can_dlc;
	id = cf->can_id;

	/* fill id and data length code */
	if (id & CAN_EFF_FLAG) {
		if (id & CAN_RTR_FLAG)
			bfin_can_write_xoid_rtr(priv, TRANSMIT_CHL, id);
		else
			bfin_can_write_xoid(priv, TRANSMIT_CHL, id);
	} else {
		if (id & CAN_RTR_FLAG)
			bfin_can_write_oid_rtr(priv, TRANSMIT_CHL, id);
		else
			bfin_can_write_oid(priv, TRANSMIT_CHL, id);
	}

	bfin_can_write_data(priv, TRANSMIT_CHL, cf->data, dlc);

	bfin_can_write_dlc(priv, TRANSMIT_CHL, dlc);

	dev->trans_start = jiffies;

	can_put_echo_skb(skb, dev, 0);

	/* set transmit request */
	writew(BIT(TRANSMIT_CHL - 16), &reg->trs2);
	return 0;
}

static void bfin_can_rx(struct net_device *dev, u16 isrc)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct bfin_can_regs *reg = priv->membase;
	struct can_frame *cf;
	struct sk_buff *skb;
	canid_t id;
	u8 dlc;
	int obj;

	skb = alloc_can_skb(dev, &cf);
	if (skb == NULL)
		return;

	/* get id and data length code */
	if (isrc & BIT(RECEIVE_EXT_CHL)) {
		/* extended frame format (EFF) */
		id = bfin_can_read_xoid(priv, RECEIVE_EXT_CHL);
		id |= CAN_EFF_FLAG;
		obj = RECEIVE_EXT_CHL;
	} else {
		/* standard frame format (SFF) */
		id = bfin_can_read_oid(priv, RECEIVE_STD_CHL);
		obj = RECEIVE_STD_CHL;
	}
	if (readw(&reg->chl[obj].id1) & RTR)
		id |= CAN_RTR_FLAG;
	dlc = bfin_can_read_dlc(priv, obj);

	cf->can_id = id;
	cf->can_dlc = dlc;

	bfin_can_read_data(priv, obj, cf->data, dlc);

	netif_rx(skb);

	stats->rx_packets++;
	stats->rx_bytes += dlc;
}

static int bfin_can_err(struct net_device *dev, u16 isrc, u16 status)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs *reg = priv->membase;
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	enum can_state state = priv->can.state;

	skb = alloc_can_err_skb(dev, &cf);
	if (skb == NULL)
		return -ENOMEM;

	if (isrc & RMLIS) {
		/* data overrun interrupt */
		dev_dbg(dev->dev.parent, "data overrun interrupt\n");
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		stats->rx_over_errors++;
		stats->rx_errors++;
	}

	if (isrc & BOIS) {
		dev_dbg(dev->dev.parent, "bus-off mode interrupt\n");

		state = CAN_STATE_BUS_OFF;
		cf->can_id |= CAN_ERR_BUSOFF;
		can_bus_off(dev);
	}

	if (isrc & EPIS) {
		/* error passive interrupt */
		dev_dbg(dev->dev.parent, "error passive interrupt\n");
		state = CAN_STATE_ERROR_PASSIVE;
	}

	if ((isrc & EWTIS) || (isrc & EWRIS)) {
		dev_dbg(dev->dev.parent, "Error Warning Transmit/Receive Interrupt\n");
		state = CAN_STATE_ERROR_WARNING;
	}

	if (state != priv->can.state && (state == CAN_STATE_ERROR_WARNING ||
				state == CAN_STATE_ERROR_PASSIVE)) {
		u16 cec = readw(&reg->cec);
		u8 rxerr = cec;
		u8 txerr = cec >> 8;
		cf->can_id |= CAN_ERR_CRTL;
		if (state == CAN_STATE_ERROR_WARNING) {
			priv->can.can_stats.error_warning++;
			cf->data[1] = (txerr > rxerr) ?
				CAN_ERR_CRTL_TX_WARNING :
				CAN_ERR_CRTL_RX_WARNING;
		} else {
			priv->can.can_stats.error_passive++;
			cf->data[1] = (txerr > rxerr) ?
				CAN_ERR_CRTL_TX_PASSIVE :
				CAN_ERR_CRTL_RX_PASSIVE;
		}
	}

	if (status) {
		priv->can.can_stats.bus_error++;

		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

		if (status & BEF)
			cf->data[2] |= CAN_ERR_PROT_BIT;
		else if (status & FER)
			cf->data[2] |= CAN_ERR_PROT_FORM;
		else if (status & SER)
			cf->data[2] |= CAN_ERR_PROT_STUFF;
		else
			cf->data[2] |= CAN_ERR_PROT_UNSPEC;
	}

	priv->can.state = state;

	netif_rx(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;

	return 0;
}

irqreturn_t bfin_can_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs *reg = priv->membase;
	struct net_device_stats *stats = &dev->stats;
	u16 status, isrc;

	if ((irq == priv->tx_irq) && readw(&reg->mbtif2)) {
		/* transmission complete interrupt */
		writew(0xFFFF, &reg->mbtif2);
		stats->tx_packets++;
		stats->tx_bytes += bfin_can_read_dlc(priv, TRANSMIT_CHL);
		can_get_echo_skb(dev, 0);
		netif_wake_queue(dev);
	} else if ((irq == priv->rx_irq) && readw(&reg->mbrif1)) {
		/* receive interrupt */
		isrc = readw(&reg->mbrif1);
		writew(0xFFFF, &reg->mbrif1);
		bfin_can_rx(dev, isrc);
	} else if ((irq == priv->err_irq) && readw(&reg->gis)) {
		/* error interrupt */
		isrc = readw(&reg->gis);
		status = readw(&reg->esr);
		writew(0x7FF, &reg->gis);
		bfin_can_err(dev, isrc, status);
	} else
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static int bfin_can_open(struct net_device *dev)
{
	int err;

	/* set chip into reset mode */
	bfin_can_set_reset_mode(dev);

	/* common open */
	err = open_candev(dev);
	if (err)
		return err;

	/* init and start chi */
	bfin_can_start(dev);

	netif_start_queue(dev);

	return 0;
}

static int bfin_can_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	bfin_can_set_reset_mode(dev);

	close_candev(dev);

	return 0;
}

struct net_device *alloc_bfin_candev(void)
{
	struct net_device *dev;
	struct bfin_can_priv *priv;

	dev = alloc_candev(sizeof(*priv));
	if (!dev)
		return NULL;

	priv = netdev_priv(dev);

	priv->dev = dev;
	priv->can.bittiming_const = &bfin_can_bittiming_const;
	priv->can.do_set_bittiming = bfin_can_set_bittiming;
	priv->can.do_set_mode = bfin_can_set_mode;

	return dev;
}

static const struct net_device_ops bfin_can_netdev_ops = {
	.ndo_open               = bfin_can_open,
	.ndo_stop               = bfin_can_close,
	.ndo_start_xmit         = bfin_can_start_xmit,
};

static int __devinit bfin_can_probe(struct platform_device *pdev)
{
	int err;
	struct net_device *dev;
	struct bfin_can_priv *priv;
	struct resource *res_mem, *rx_irq, *tx_irq, *err_irq;
	unsigned short *pdata;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data provided!\n");
		err = -EINVAL;
		goto exit;
	}

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rx_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	tx_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	err_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 2);
	if (!res_mem || !rx_irq || !tx_irq || !err_irq) {
		err = -EINVAL;
		goto exit;
	}

	if (!request_mem_region(res_mem->start, resource_size(res_mem),
				dev_name(&pdev->dev))) {
		err = -EBUSY;
		goto exit;
	}

	/* request peripheral pins */
	err = peripheral_request_list(pdata, dev_name(&pdev->dev));
	if (err)
		goto exit_mem_release;

	dev = alloc_bfin_candev();
	if (!dev) {
		err = -ENOMEM;
		goto exit_peri_pin_free;
	}

	/* register interrupt handler */
	err = request_irq(rx_irq->start, &bfin_can_interrupt, 0,
			"bfin-can-rx", (void *)dev);
	if (err)
		goto exit_candev_free;
	err = request_irq(tx_irq->start, &bfin_can_interrupt, 0,
			"bfin-can-tx", (void *)dev);
	if (err)
		goto exit_rx_irq_free;
	err = request_irq(err_irq->start, &bfin_can_interrupt, 0,
			"bfin-can-err", (void *)dev);
	if (err)
		goto exit_tx_irq_free;

	priv = netdev_priv(dev);
	priv->membase = (void __iomem *)res_mem->start;
	priv->rx_irq = rx_irq->start;
	priv->tx_irq = tx_irq->start;
	priv->err_irq = err_irq->start;
	priv->pin_list = pdata;
	priv->can.clock.freq = get_sclk();

	dev_set_drvdata(&pdev->dev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	dev->flags |= IFF_ECHO;	/* we support local echo */
	dev->netdev_ops = &bfin_can_netdev_ops;

	bfin_can_set_reset_mode(dev);

	err = register_candev(dev);
	if (err) {
		dev_err(&pdev->dev, "registering failed (err=%d)\n", err);
		goto exit_err_irq_free;
	}

	dev_info(&pdev->dev, "%s device registered (reg_base=%p, rx_irq=%d, tx_irq=%d, err_irq=%d, sclk=%d)\n",
			DRV_NAME, (void *)priv->membase, priv->rx_irq, priv->tx_irq, priv->err_irq,
			priv->can.clock.freq);
	return 0;

exit_err_irq_free:
	free_irq(err_irq->start, dev);
exit_tx_irq_free:
	free_irq(tx_irq->start, dev);
exit_rx_irq_free:
	free_irq(rx_irq->start, dev);
exit_candev_free:
	free_candev(dev);
exit_peri_pin_free:
	peripheral_free_list(pdata);
exit_mem_release:
	release_mem_region(res_mem->start, resource_size(res_mem));
exit:
	return err;
}

static int __devexit bfin_can_remove(struct platform_device *pdev)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct resource *res;

	bfin_can_set_reset_mode(dev);

	unregister_candev(dev);

	dev_set_drvdata(&pdev->dev, NULL);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	free_irq(priv->rx_irq, dev);
	free_irq(priv->tx_irq, dev);
	free_irq(priv->err_irq, dev);
	peripheral_free_list(priv->pin_list);

	free_candev(dev);
	return 0;
}

#ifdef CONFIG_PM
static int bfin_can_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs *reg = priv->membase;
	int timeout = BFIN_CAN_TIMEOUT;

	if (netif_running(dev)) {
		/* enter sleep mode */
		writew(readw(&reg->ctrl) | SMR, &reg->ctrl);
		SSYNC();
		while (!(readw(&reg->intr) & SMACK)) {
			udelay(10);
			if (--timeout == 0) {
				dev_err(dev->dev.parent, "fail to enter sleep mode\n");
				BUG();
			}
		}
	}

	return 0;
}

static int bfin_can_resume(struct platform_device *pdev)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs *reg = priv->membase;

	if (netif_running(dev)) {
		/* leave sleep mode */
		writew(0, &reg->intr);
		SSYNC();
	}

	return 0;
}
#else
#define bfin_can_suspend NULL
#define bfin_can_resume NULL
#endif	/* CONFIG_PM */

static struct platform_driver bfin_can_driver = {
	.probe = bfin_can_probe,
	.remove = __devexit_p(bfin_can_remove),
	.suspend = bfin_can_suspend,
	.resume = bfin_can_resume,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init bfin_can_init(void)
{
	return platform_driver_register(&bfin_can_driver);
}

module_init(bfin_can_init);

static void __exit bfin_can_exit(void)
{
	platform_driver_unregister(&bfin_can_driver);
}

module_exit(bfin_can_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blackfin on-chip CAN netdevice driver");
