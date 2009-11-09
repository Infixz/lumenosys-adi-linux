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
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

#include <asm/portmux.h>
#include "bfin-can.h"

#define DRV_NAME "bfin_can"

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

static int bfin_can_set_bittiming(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
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

	CAN_WRITE_CTRL(priv, OFFSET_CLOCK, clk);
	CAN_WRITE_CTRL(priv, OFFSET_TIMING, timing);

	dev_info(dev->dev.parent,
			"setting can bitrate:%d brp:%d prop_seg:%d phase_seg1:%d phase_seg2:%d\n",
			bt->bitrate, bt->brp, bt->prop_seg, bt->phase_seg1, bt->phase_seg2);

	return 0;
}

static void bfin_can_set_reset_mode(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	int i;

	dev_dbg(dev->dev.parent, "%s enter\n", __func__);

	/* disable interrupts */
	CAN_WRITE_CTRL(priv, OFFSET_MBIM1, 0);
	CAN_WRITE_CTRL(priv, OFFSET_MBIM2, 0);
	CAN_WRITE_CTRL(priv, OFFSET_GIM, 0);

	/* reset can and enter configuration mode */
	CAN_WRITE_CTRL(priv, OFFSET_CONTROL, SRS | CCR);
	SSYNC();
	CAN_WRITE_CTRL(priv, OFFSET_CONTROL, CCR);
	SSYNC();
	while (!(CAN_READ_CTRL(priv, OFFSET_CONTROL) & CCA))
		continue;

	/*
	 * All mailbox configurations are marked as inactive
	 * by writing to CAN Mailbox Configuration Registers 1 and 2
	 * For all bits: 0 - Mailbox disabled, 1 - Mailbox enabled
	 */
	CAN_WRITE_CTRL(priv, OFFSET_MC1, 0);
	CAN_WRITE_CTRL(priv, OFFSET_MC2, 0);

	/* Set Mailbox Direction */
	CAN_WRITE_CTRL(priv, OFFSET_MD1, 0xFFFF);  /* mailbox 1-16 are RX */
	CAN_WRITE_CTRL(priv, OFFSET_MD2, 0);       /* mailbox 17-32 are TX */

	/* RECEIVE_STD_CHL */
	for (i = 0; i < 2; i++) {
		CAN_WRITE_OID(priv, RECEIVE_STD_CHL + i, 0);
		CAN_WRITE_ID0(priv, RECEIVE_STD_CHL, 0);
		CAN_WRITE_DLC(priv, RECEIVE_STD_CHL + i, 0);
		CAN_WRITE_AMH(priv, RECEIVE_STD_CHL + i, 0x1FFF);
		CAN_WRITE_AML(priv, RECEIVE_STD_CHL + i, 0xFFFF);
	}

	/* RECEIVE_EXT_CHL */
	for (i = 0; i < 2; i++) {
		CAN_WRITE_XOID(priv, RECEIVE_EXT_CHL + i, 0);
		CAN_WRITE_DLC(priv, RECEIVE_EXT_CHL + i, 0);
		CAN_WRITE_AMH(priv, RECEIVE_EXT_CHL + i, 0x1FFF);
		CAN_WRITE_AML(priv, RECEIVE_EXT_CHL + i, 0xFFFF);
	}

	CAN_WRITE_CTRL(priv, OFFSET_MC2, 1 << (TRANSMIT_CHL - 16));
	CAN_WRITE_CTRL(priv, OFFSET_MC1, (1 << RECEIVE_STD_CHL) + (1 << RECEIVE_EXT_CHL));
	SSYNC();

	priv->can.state = CAN_STATE_STOPPED;
}

static void bfin_can_set_normal_mode(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);

	dev_dbg(dev->dev.parent, "%s enter\n", __func__);

	/* leave configuration mode */
	CAN_WRITE_CTRL(priv, OFFSET_CONTROL, CAN_READ_CTRL(priv, OFFSET_CONTROL) & ~CCR);
	while (CAN_READ_CTRL(priv, OFFSET_STATUS) & CCA)
		continue;

	/* clear _All_  tx and rx interrupts */
	CAN_WRITE_CTRL(priv, OFFSET_MBTIF1, 0xFFFF);
	CAN_WRITE_CTRL(priv, OFFSET_MBTIF2, 0xFFFF);
	CAN_WRITE_CTRL(priv, OFFSET_MBRIF1, 0xFFFF);
	CAN_WRITE_CTRL(priv, OFFSET_MBRIF2, 0xFFFF);
	/* clear global interrupt status register */
	CAN_WRITE_CTRL(priv, OFFSET_GIS, 0x7FF); /* overwrites with '1' */

	/* Initialize Interrupts
	 * - set bits in the mailbox interrupt mask register
	 * - global interrupt mask
	 */

	CAN_WRITE_CTRL(priv, OFFSET_MBIM1, (1 << RECEIVE_STD_CHL) + (1 << RECEIVE_EXT_CHL));
	CAN_WRITE_CTRL(priv, OFFSET_MBIM2, 1 << (TRANSMIT_CHL - 16));

	CAN_WRITE_CTRL(priv, OFFSET_GIM, EPIM | BOIM | RMLIM);
	SSYNC();
}


static void bfin_can_start(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);

	dev_dbg(dev->dev.parent, "%s enter\n", __func__);

	/* leave reset mode */
	if (priv->can.state != CAN_STATE_STOPPED)
		bfin_can_set_reset_mode(dev);

	/* leave reset mode */
	bfin_can_set_normal_mode(dev);
}

static int bfin_can_set_mode(struct net_device *dev, enum can_mode mode)
{
	dev_dbg(dev->dev.parent, "%s enter\n", __func__);

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

/*
 * transmit a CAN message
 * message layout in the sk_buff should be like this:
 * xx xx xx xx	 ff	 ll   00 11 22 33 44 55 66 77
 * [  can-id ] [flags] [len] [can data (up to 8 bytes]
 */
static int bfin_can_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf = (struct can_frame *)skb->data;
	uint8_t dlc;
	canid_t id;

	dev_dbg(dev->dev.parent, "%s enter\n", __func__);

	netif_stop_queue(dev);

	dlc = cf->can_dlc;
	id = cf->can_id;

	/* fill id and data length code */
	if (id & CAN_EFF_FLAG) {
		if (id & CAN_RTR_FLAG)
			CAN_WRITE_XOID_RTR(priv, TRANSMIT_CHL, id);
		else
			CAN_WRITE_XOID(priv, TRANSMIT_CHL, id);
	} else {
		if (id & CAN_RTR_FLAG)
			CAN_WRITE_OID_RTR(priv, TRANSMIT_CHL, id);
		else
			CAN_WRITE_OID(priv, TRANSMIT_CHL, id);
	}

	BFIN_CAN_WRITE_MSG(priv, TRANSMIT_CHL, cf->data, dlc);

	CAN_WRITE_DLC(priv, TRANSMIT_CHL, dlc);

	stats->tx_bytes += dlc;
	dev->trans_start = jiffies;

	can_put_echo_skb(skb, dev, 0);

	/* set transmit request */
	CAN_WRITE_CTRL(priv, OFFSET_TRS2, 1 << (TRANSMIT_CHL - 16));
	return 0;
}

/* Our watchdog timed out. Called by the up layer */
static void bfin_can_timeout(struct net_device *dev)
{
	dev_dbg(dev->dev.parent, "%s enter\n", __func__);

	/* We can accept TX packets again */
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static void bfin_can_rx(struct net_device *dev, uint16_t isrc)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	canid_t id;
	uint8_t dlc;
	int obj;

	dev_dbg(dev->dev.parent, "%s enter\n", __func__);

	skb = dev_alloc_skb(sizeof(struct can_frame));
	if (skb == NULL)
		return;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_CAN);

	/* get id and data length code */
	if (isrc & (1 << RECEIVE_EXT_CHL)) {
		/* extended frame format (EFF) */
		id = CAN_READ_XOID(priv, RECEIVE_EXT_CHL);
		id |= CAN_EFF_FLAG;
		obj = RECEIVE_EXT_CHL;
	} else {
		/* standard frame format (SFF) */
		id = CAN_READ_OID(priv, RECEIVE_STD_CHL);
		obj = RECEIVE_STD_CHL;
	}
	if (CAN_READ_ID1(priv, obj) & RTR)
		id |= CAN_RTR_FLAG;
	dlc = CAN_READ_DLC(priv, obj);

	cf = (struct can_frame *)skb_put(skb, sizeof(*cf));
	memset(cf, 0, sizeof(struct can_frame));
	cf->can_id = id;
	cf->can_dlc = dlc;

	BFIN_CAN_READ_MSG(priv, obj, cf->data, dlc);

	netif_rx(skb);

	dev->last_rx = jiffies;
	stats->rx_packets++;
	stats->rx_bytes += dlc;
}

static int bfin_can_err(struct net_device *dev, uint16_t isrc, uint16_t status)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	enum can_state state = priv->can.state;

	dev_dbg(dev->dev.parent, "%s enter\n", __func__);

	skb = dev_alloc_skb(sizeof(struct can_frame));
	if (skb == NULL)
		return -ENOMEM;
	skb->dev = dev;
	skb->protocol = htons(ETH_P_CAN);
	cf = (struct can_frame *)skb_put(skb, sizeof(*cf));
	memset(cf, 0, sizeof(struct can_frame));
	cf->can_id = CAN_ERR_FLAG;
	cf->can_dlc = CAN_ERR_DLC;

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
		uint16_t cec = CAN_READ_CTRL(priv, OFFSET_CEC);
		uint8_t rxerr = cec;
		uint8_t txerr = cec >> 8;
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

	dev->last_rx = jiffies;
	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;

	return 0;
}

irqreturn_t bfin_can_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	uint16_t status, isrc;

	dev_dbg(dev->dev.parent, "%s enter, irq num:%d\n", __func__, irq);

	if ((irq == priv->tx_irq) && CAN_READ_CTRL(priv, OFFSET_MBTIF2)) {
		/* transmission complete interrupt */
		CAN_WRITE_CTRL(priv, OFFSET_MBTIF2, 0xFFFF);
		stats->tx_packets++;
		can_get_echo_skb(dev, 0);
		netif_wake_queue(dev);
	} else if ((irq == priv->rx_irq) && CAN_READ_CTRL(priv, OFFSET_MBRIF1)) {
		/* receive interrupt */
		isrc = CAN_READ_CTRL(priv, OFFSET_MBRIF1);
		CAN_WRITE_CTRL(priv, OFFSET_MBRIF1, 0xFFFF);
		bfin_can_rx(dev, isrc);
	} else if ((irq == priv->err_irq) && CAN_READ_CTRL(priv, OFFSET_GIS)) {
		/* error interrupt */
		isrc = CAN_READ_CTRL(priv, OFFSET_GIS);
		status = CAN_READ_CTRL(priv, OFFSET_ESR);
		CAN_WRITE_CTRL(priv, OFFSET_GIS, 0x7FF);
		bfin_can_err(dev, isrc, status);
	}

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

void free_bfin_candev(struct net_device *dev)
{
	free_candev(dev);
}

static const struct net_device_ops bfin_can_netdev_ops = {
	.ndo_open               = bfin_can_open,
	.ndo_stop               = bfin_can_close,
	.ndo_start_xmit         = bfin_can_start_xmit,
	.ndo_tx_timeout         = bfin_can_timeout,
};

int register_bfin_candev(struct net_device *dev)
{
	dev->flags |= IFF_ECHO;	/* we support local echo */
	dev->netdev_ops = &bfin_can_netdev_ops;

	bfin_can_set_reset_mode(dev);

	return register_candev(dev);
}

void unregister_bfin_candev(struct net_device *dev)
{
	bfin_can_set_reset_mode(dev);
	unregister_candev(dev);
}

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
		err = -ENODEV;
		goto exit;
	}

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rx_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	tx_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	err_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 2);
	if (!res_mem || !rx_irq || !tx_irq || !err_irq) {
		err = -ENODEV;
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
	priv->membase = res_mem->start;
	priv->rx_irq = rx_irq->start;
	priv->tx_irq = tx_irq->start;
	priv->err_irq = err_irq->start;
	priv->pin_list = pdata;
	priv->can.clock.freq = get_sclk();

	dev_set_drvdata(&pdev->dev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_bfin_candev(dev);
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
	free_bfin_candev(dev);
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

	unregister_bfin_candev(dev);
	dev_set_drvdata(&pdev->dev, NULL);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	free_irq(priv->rx_irq, dev);
	free_irq(priv->tx_irq, dev);
	free_irq(priv->err_irq, dev);
	peripheral_free_list(priv->pin_list);

	free_bfin_candev(dev);
	return 0;
}

static struct platform_driver bfin_can_driver = {
	.probe = bfin_can_probe,
	.remove = __devexit_p(bfin_can_remove),
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
