/* bnx2x_ethtool.c: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2011 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/crc32.h>


#include "bnx2x.h"
#include "bnx2x_cmn.h"
#include "bnx2x_dump.h"
#include "bnx2x_init.h"
#include "bnx2x_sp.h"

/* Note: in the format strings below %s is replaced by the queue-name which is
 * either its index or 'fcoe' for the fcoe queue. Make sure the format string
 * length does not exceed ETH_GSTRING_LEN - MAX_QUEUE_NAME_LEN + 2
 */
#define MAX_QUEUE_NAME_LEN	4
static const struct {
	long offset;
	int size;
	char string[ETH_GSTRING_LEN];
} bnx2x_q_stats_arr[] = {
/* 1 */	{ Q_STATS_OFFSET32(total_bytes_received_hi), 8, "[%s]: rx_bytes" },
	{ Q_STATS_OFFSET32(total_unicast_packets_received_hi),
						8, "[%s]: rx_ucast_packets" },
	{ Q_STATS_OFFSET32(total_multicast_packets_received_hi),
						8, "[%s]: rx_mcast_packets" },
	{ Q_STATS_OFFSET32(total_broadcast_packets_received_hi),
						8, "[%s]: rx_bcast_packets" },
	{ Q_STATS_OFFSET32(no_buff_discard_hi),	8, "[%s]: rx_discards" },
	{ Q_STATS_OFFSET32(rx_err_discard_pkt),
					 4, "[%s]: rx_phy_ip_err_discards"},
	{ Q_STATS_OFFSET32(rx_skb_alloc_failed),
					 4, "[%s]: rx_skb_alloc_discard" },
	{ Q_STATS_OFFSET32(hw_csum_err), 4, "[%s]: rx_csum_offload_errors" },

	{ Q_STATS_OFFSET32(total_bytes_transmitted_hi),	8, "[%s]: tx_bytes" },
/* 10 */{ Q_STATS_OFFSET32(total_unicast_packets_transmitted_hi),
						8, "[%s]: tx_ucast_packets" },
	{ Q_STATS_OFFSET32(total_multicast_packets_transmitted_hi),
						8, "[%s]: tx_mcast_packets" },
	{ Q_STATS_OFFSET32(total_broadcast_packets_transmitted_hi),
						8, "[%s]: tx_bcast_packets" },
	{ Q_STATS_OFFSET32(total_tpa_aggregations_hi),
						8, "[%s]: tpa_aggregations" },
	{ Q_STATS_OFFSET32(total_tpa_aggregated_frames_hi),
					8, "[%s]: tpa_aggregated_frames"},
	{ Q_STATS_OFFSET32(total_tpa_bytes_hi),	8, "[%s]: tpa_bytes"}
};

#define BNX2X_NUM_Q_STATS ARRAY_SIZE(bnx2x_q_stats_arr)

static const struct {
	long offset;
	int size;
	u32 flags;
#define STATS_FLAGS_PORT		1
#define STATS_FLAGS_FUNC		2
#define STATS_FLAGS_BOTH		(STATS_FLAGS_FUNC | STATS_FLAGS_PORT)
	char string[ETH_GSTRING_LEN];
} bnx2x_stats_arr[] = {
/* 1 */	{ STATS_OFFSET32(total_bytes_received_hi),
				8, STATS_FLAGS_BOTH, "rx_bytes" },
	{ STATS_OFFSET32(error_bytes_received_hi),
				8, STATS_FLAGS_BOTH, "rx_error_bytes" },
	{ STATS_OFFSET32(total_unicast_packets_received_hi),
				8, STATS_FLAGS_BOTH, "rx_ucast_packets" },
	{ STATS_OFFSET32(total_multicast_packets_received_hi),
				8, STATS_FLAGS_BOTH, "rx_mcast_packets" },
	{ STATS_OFFSET32(total_broadcast_packets_received_hi),
				8, STATS_FLAGS_BOTH, "rx_bcast_packets" },
	{ STATS_OFFSET32(rx_stat_dot3statsfcserrors_hi),
				8, STATS_FLAGS_PORT, "rx_crc_errors" },
	{ STATS_OFFSET32(rx_stat_dot3statsalignmenterrors_hi),
				8, STATS_FLAGS_PORT, "rx_align_errors" },
	{ STATS_OFFSET32(rx_stat_etherstatsundersizepkts_hi),
				8, STATS_FLAGS_PORT, "rx_undersize_packets" },
	{ STATS_OFFSET32(etherstatsoverrsizepkts_hi),
				8, STATS_FLAGS_PORT, "rx_oversize_packets" },
/* 10 */{ STATS_OFFSET32(rx_stat_etherstatsfragments_hi),
				8, STATS_FLAGS_PORT, "rx_fragments" },
	{ STATS_OFFSET32(rx_stat_etherstatsjabbers_hi),
				8, STATS_FLAGS_PORT, "rx_jabbers" },
	{ STATS_OFFSET32(no_buff_discard_hi),
				8, STATS_FLAGS_BOTH, "rx_discards" },
	{ STATS_OFFSET32(mac_filter_discard),
				4, STATS_FLAGS_PORT, "rx_filtered_packets" },
	{ STATS_OFFSET32(mf_tag_discard),
				4, STATS_FLAGS_PORT, "rx_mf_tag_discard" },
	{ STATS_OFFSET32(pfc_frames_received_hi),
				8, STATS_FLAGS_PORT, "pfc_frames_received" },
	{ STATS_OFFSET32(pfc_frames_sent_hi),
				8, STATS_FLAGS_PORT, "pfc_frames_sent" },
	{ STATS_OFFSET32(brb_drop_hi),
				8, STATS_FLAGS_PORT, "rx_brb_discard" },
	{ STATS_OFFSET32(brb_truncate_hi),
				8, STATS_FLAGS_PORT, "rx_brb_truncate" },
	{ STATS_OFFSET32(pause_frames_received_hi),
				8, STATS_FLAGS_PORT, "rx_pause_frames" },
	{ STATS_OFFSET32(rx_stat_maccontrolframesreceived_hi),
				8, STATS_FLAGS_PORT, "rx_mac_ctrl_frames" },
	{ STATS_OFFSET32(nig_timer_max),
			4, STATS_FLAGS_PORT, "rx_constant_pause_events" },
/* 20 */{ STATS_OFFSET32(rx_err_discard_pkt),
				4, STATS_FLAGS_BOTH, "rx_phy_ip_err_discards"},
	{ STATS_OFFSET32(rx_skb_alloc_failed),
				4, STATS_FLAGS_BOTH, "rx_skb_alloc_discard" },
	{ STATS_OFFSET32(hw_csum_err),
				4, STATS_FLAGS_BOTH, "rx_csum_offload_errors" },

	{ STATS_OFFSET32(total_bytes_transmitted_hi),
				8, STATS_FLAGS_BOTH, "tx_bytes" },
	{ STATS_OFFSET32(tx_stat_ifhcoutbadoctets_hi),
				8, STATS_FLAGS_PORT, "tx_error_bytes" },
	{ STATS_OFFSET32(total_unicast_packets_transmitted_hi),
				8, STATS_FLAGS_BOTH, "tx_ucast_packets" },
	{ STATS_OFFSET32(total_multicast_packets_transmitted_hi),
				8, STATS_FLAGS_BOTH, "tx_mcast_packets" },
	{ STATS_OFFSET32(total_broadcast_packets_transmitted_hi),
				8, STATS_FLAGS_BOTH, "tx_bcast_packets" },
	{ STATS_OFFSET32(tx_stat_dot3statsinternalmactransmiterrors_hi),
				8, STATS_FLAGS_PORT, "tx_mac_errors" },
	{ STATS_OFFSET32(rx_stat_dot3statscarriersenseerrors_hi),
				8, STATS_FLAGS_PORT, "tx_carrier_errors" },
/* 30 */{ STATS_OFFSET32(tx_stat_dot3statssinglecollisionframes_hi),
				8, STATS_FLAGS_PORT, "tx_single_collisions" },
	{ STATS_OFFSET32(tx_stat_dot3statsmultiplecollisionframes_hi),
				8, STATS_FLAGS_PORT, "tx_multi_collisions" },
	{ STATS_OFFSET32(tx_stat_dot3statsdeferredtransmissions_hi),
				8, STATS_FLAGS_PORT, "tx_deferred" },
	{ STATS_OFFSET32(tx_stat_dot3statsexcessivecollisions_hi),
				8, STATS_FLAGS_PORT, "tx_excess_collisions" },
	{ STATS_OFFSET32(tx_stat_dot3statslatecollisions_hi),
				8, STATS_FLAGS_PORT, "tx_late_collisions" },
	{ STATS_OFFSET32(tx_stat_etherstatscollisions_hi),
				8, STATS_FLAGS_PORT, "tx_total_collisions" },
	{ STATS_OFFSET32(tx_stat_etherstatspkts64octets_hi),
				8, STATS_FLAGS_PORT, "tx_64_byte_packets" },
	{ STATS_OFFSET32(tx_stat_etherstatspkts65octetsto127octets_hi),
			8, STATS_FLAGS_PORT, "tx_65_to_127_byte_packets" },
	{ STATS_OFFSET32(tx_stat_etherstatspkts128octetsto255octets_hi),
			8, STATS_FLAGS_PORT, "tx_128_to_255_byte_packets" },
	{ STATS_OFFSET32(tx_stat_etherstatspkts256octetsto511octets_hi),
			8, STATS_FLAGS_PORT, "tx_256_to_511_byte_packets" },
/* 40 */{ STATS_OFFSET32(tx_stat_etherstatspkts512octetsto1023octets_hi),
			8, STATS_FLAGS_PORT, "tx_512_to_1023_byte_packets" },
	{ STATS_OFFSET32(etherstatspkts1024octetsto1522octets_hi),
			8, STATS_FLAGS_PORT, "tx_1024_to_1522_byte_packets" },
	{ STATS_OFFSET32(etherstatspktsover1522octets_hi),
			8, STATS_FLAGS_PORT, "tx_1523_to_9022_byte_packets" },
	{ STATS_OFFSET32(pause_frames_sent_hi),
				8, STATS_FLAGS_PORT, "tx_pause_frames" },
	{ STATS_OFFSET32(total_tpa_aggregations_hi),
			8, STATS_FLAGS_FUNC, "tpa_aggregations" },
	{ STATS_OFFSET32(total_tpa_aggregated_frames_hi),
			8, STATS_FLAGS_FUNC, "tpa_aggregated_frames"},
	{ STATS_OFFSET32(total_tpa_bytes_hi),
			8, STATS_FLAGS_FUNC, "tpa_bytes"}
};

#define BNX2X_NUM_STATS		ARRAY_SIZE(bnx2x_stats_arr)
static int bnx2x_get_port_type(struct bnx2x *bp)
{
	int port_type;
	u32 phy_idx = bnx2x_get_cur_phy_idx(bp);
	switch (bp->link_params.phy[phy_idx].media_type) {
	case ETH_PHY_SFP_FIBER:
	case ETH_PHY_XFP_FIBER:
	case ETH_PHY_KR:
	case ETH_PHY_CX4:
		port_type = PORT_FIBRE;
		break;
	case ETH_PHY_DA_TWINAX:
		port_type = PORT_DA;
		break;
	case ETH_PHY_BASE_T:
		port_type = PORT_TP;
		break;
	case ETH_PHY_NOT_PRESENT:
		port_type = PORT_NONE;
		break;
	case ETH_PHY_UNSPECIFIED:
	default:
		port_type = PORT_OTHER;
		break;
	}
	return port_type;
}

static int bnx2x_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2x *bp = netdev_priv(dev);
	int cfg_idx = bnx2x_get_link_cfg_idx(bp);

	/* Dual Media boards present all available port types */
	cmd->supported = bp->port.supported[cfg_idx] |
		(bp->port.supported[cfg_idx ^ 1] &
		 (SUPPORTED_TP | SUPPORTED_FIBRE));
	cmd->advertising = bp->port.advertising[cfg_idx];

	if ((bp->state == BNX2X_STATE_OPEN) &&
	    !(bp->flags & MF_FUNC_DIS) &&
	    (bp->link_vars.link_up)) {
		ethtool_cmd_speed_set(cmd, bp->link_vars.line_speed);
		cmd->duplex = bp->link_vars.duplex;
	} else {
		ethtool_cmd_speed_set(
			cmd, bp->link_params.req_line_speed[cfg_idx]);
		cmd->duplex = bp->link_params.req_duplex[cfg_idx];
	}

	if (IS_MF(bp))
		ethtool_cmd_speed_set(cmd, bnx2x_get_mf_speed(bp));

	cmd->port = bnx2x_get_port_type(bp);

	cmd->phy_address = bp->mdio.prtad;
	cmd->transceiver = XCVR_INTERNAL;

	if (bp->link_params.req_line_speed[cfg_idx] == SPEED_AUTO_NEG)
		cmd->autoneg = AUTONEG_ENABLE;
	else
		cmd->autoneg = AUTONEG_DISABLE;

	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;

	DP(NETIF_MSG_LINK, "ethtool_cmd: cmd %d\n"
	   "  supported 0x%x  advertising 0x%x  speed %u\n"
	   "  duplex %d  port %d  phy_address %d  transceiver %d\n"
	   "  autoneg %d  maxtxpkt %d  maxrxpkt %d\n",
	   cmd->cmd, cmd->supported, cmd->advertising,
	   ethtool_cmd_speed(cmd),
	   cmd->duplex, cmd->port, cmd->phy_address, cmd->transceiver,
	   cmd->autoneg, cmd->maxtxpkt, cmd->maxrxpkt);

	return 0;
}

static int bnx2x_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 advertising, cfg_idx, old_multi_phy_config, new_multi_phy_config;
	u32 speed;

	if (IS_MF_SD(bp))
		return 0;

	DP(NETIF_MSG_LINK, "ethtool_cmd: cmd %d\n"
	   "  supported 0x%x  advertising 0x%x  speed %u\n"
	   "  duplex %d  port %d  phy_address %d  transceiver %d\n"
	   "  autoneg %d  maxtxpkt %d  maxrxpkt %d\n",
	   cmd->cmd, cmd->supported, cmd->advertising,
	   ethtool_cmd_speed(cmd),
	   cmd->duplex, cmd->port, cmd->phy_address, cmd->transceiver,
	   cmd->autoneg, cmd->maxtxpkt, cmd->maxrxpkt);

	speed = ethtool_cmd_speed(cmd);

	if (IS_MF_SI(bp)) {
		u32 part;
		u32 line_speed = bp->link_vars.line_speed;

		/* use 10G if no link detected */
		if (!line_speed)
			line_speed = 10000;

		if (bp->common.bc_ver < REQ_BC_VER_4_SET_MF_BW) {
			BNX2X_DEV_INFO("To set speed BC %X or higher "
				       "is required, please upgrade BC\n",
				       REQ_BC_VER_4_SET_MF_BW);
			return -EINVAL;
		}

		part = (speed * 100) / line_speed;

		if (line_speed < speed || !part) {
			BNX2X_DEV_INFO("Speed setting should be in a range "
				       "from 1%% to 100%% "
				       "of actual line speed\n");
			return -EINVAL;
		}

		if (bp->state != BNX2X_STATE_OPEN)
			/* store value for following "load" */
			bp->pending_max = part;
		else
			bnx2x_update_max_mf_config(bp, part);

		return 0;
	}

	cfg_idx = bnx2x_get_link_cfg_idx(bp);
	old_multi_phy_config = bp->link_params.multi_phy_config;
	switch (cmd->port) {
	case PORT_TP:
		if (bp->port.supported[cfg_idx] & SUPPORTED_TP)
			break; /* no port change */

		if (!(bp->port.supported[0] & SUPPORTED_TP ||
		      bp->port.supported[1] & SUPPORTED_TP)) {
			DP(NETIF_MSG_LINK, "Unsupported port type\n");
			return -EINVAL;
		}
		bp->link_params.multi_phy_config &=
			~PORT_HW_CFG_PHY_SELECTION_MASK;
		if (bp->link_params.multi_phy_config &
		    PORT_HW_CFG_PHY_SWAPPED_ENABLED)
			bp->link_params.multi_phy_config |=
			PORT_HW_CFG_PHY_SELECTION_SECOND_PHY;
		else
			bp->link_params.multi_phy_config |=
			PORT_HW_CFG_PHY_SELECTION_FIRST_PHY;
		break;
	case PORT_FIBRE:
	case PORT_DA:
		if (bp->port.supported[cfg_idx] & SUPPORTED_FIBRE)
			break; /* no port change */

		if (!(bp->port.supported[0] & SUPPORTED_FIBRE ||
		      bp->port.supported[1] & SUPPORTED_FIBRE)) {
			DP(NETIF_MSG_LINK, "Unsupported port type\n");
			return -EINVAL;
		}
		bp->link_params.multi_phy_config &=
			~PORT_HW_CFG_PHY_SELECTION_MASK;
		if (bp->link_params.multi_phy_config &
		    PORT_HW_CFG_PHY_SWAPPED_ENABLED)
			bp->link_params.multi_phy_config |=
			PORT_HW_CFG_PHY_SELECTION_FIRST_PHY;
		else
			bp->link_params.multi_phy_config |=
			PORT_HW_CFG_PHY_SELECTION_SECOND_PHY;
		break;
	default:
		DP(NETIF_MSG_LINK, "Unsupported port type\n");
		return -EINVAL;
	}
	/* Save new config in case command complete successully */
	new_multi_phy_config = bp->link_params.multi_phy_config;
	/* Get the new cfg_idx */
	cfg_idx = bnx2x_get_link_cfg_idx(bp);
	/* Restore old config in case command failed */
	bp->link_params.multi_phy_config = old_multi_phy_config;
	DP(NETIF_MSG_LINK, "cfg_idx = %x\n", cfg_idx);

	if (cmd->autoneg == AUTONEG_ENABLE) {
		u32 an_supported_speed = bp->port.supported[cfg_idx];
		if (bp->link_params.phy[EXT_PHY1].type ==
		    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84833)
			an_supported_speed |= (SUPPORTED_100baseT_Half |
					       SUPPORTED_100baseT_Full);
		if (!(bp->port.supported[cfg_idx] & SUPPORTED_Autoneg)) {
			DP(NETIF_MSG_LINK, "Autoneg not supported\n");
			return -EINVAL;
		}

		/* advertise the requested speed and duplex if supported */
		if (cmd->advertising & ~an_supported_speed) {
			DP(NETIF_MSG_LINK, "Advertisement parameters "
					   "are not supported\n");
			return -EINVAL;
		}

		bp->link_params.req_line_speed[cfg_idx] = SPEED_AUTO_NEG;
		bp->link_params.req_duplex[cfg_idx] = cmd->duplex;
		bp->port.advertising[cfg_idx] = (ADVERTISED_Autoneg |
					 cmd->advertising);
		if (cmd->advertising) {

			bp->link_params.speed_cap_mask[cfg_idx] = 0;
			if (cmd->advertising & ADVERTISED_10baseT_Half) {
				bp->link_params.speed_cap_mask[cfg_idx] |=
				PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF;
			}
			if (cmd->advertising & ADVERTISED_10baseT_Full)
				bp->link_params.speed_cap_mask[cfg_idx] |=
				PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL;

			if (cmd->advertising & ADVERTISED_100baseT_Full)
				bp->link_params.speed_cap_mask[cfg_idx] |=
				PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL;

			if (cmd->advertising & ADVERTISED_100baseT_Half) {
				bp->link_params.speed_cap_mask[cfg_idx] |=
				     PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF;
			}
			if (cmd->advertising & ADVERTISED_1000baseT_Half) {
				bp->link_params.speed_cap_mask[cfg_idx] |=
					PORT_HW_CFG_SPEED_CAPABILITY_D0_1G;
			}
			if (cmd->advertising & (ADVERTISED_1000baseT_Full |
						ADVERTISED_1000baseKX_Full))
				bp->link_params.speed_cap_mask[cfg_idx] |=
					PORT_HW_CFG_SPEED_CAPABILITY_D0_1G;

			if (cmd->advertising & (ADVERTISED_10000baseT_Full |
						ADVERTISED_10000baseKX4_Full |
						ADVERTISED_10000baseKR_Full))
				bp->link_params.speed_cap_mask[cfg_idx] |=
					PORT_HW_CFG_SPEED_CAPABILITY_D0_10G;
		}
	} else { /* forced speed */
		/* advertise the requested speed and duplex if supported */
		switch (speed) {
		case SPEED_10:
			if (cmd->duplex == DUPLEX_FULL) {
				if (!(bp->port.supported[cfg_idx] &
				      SUPPORTED_10baseT_Full)) {
					DP(NETIF_MSG_LINK,
					   "10M full not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_10baseT_Full |
					       ADVERTISED_TP);
			} else {
				if (!(bp->port.supported[cfg_idx] &
				      SUPPORTED_10baseT_Half)) {
					DP(NETIF_MSG_LINK,
					   "10M half not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_10baseT_Half |
					       ADVERTISED_TP);
			}
			break;

		case SPEED_100:
			if (cmd->duplex == DUPLEX_FULL) {
				if (!(bp->port.supported[cfg_idx] &
						SUPPORTED_100baseT_Full)) {
					DP(NETIF_MSG_LINK,
					   "100M full not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_100baseT_Full |
					       ADVERTISED_TP);
			} else {
				if (!(bp->port.supported[cfg_idx] &
						SUPPORTED_100baseT_Half)) {
					DP(NETIF_MSG_LINK,
					   "100M half not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_100baseT_Half |
					       ADVERTISED_TP);
			}
			break;

		case SPEED_1000:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(NETIF_MSG_LINK, "1G half not supported\n");
				return -EINVAL;
			}

			if (!(bp->port.supported[cfg_idx] &
			      SUPPORTED_1000baseT_Full)) {
				DP(NETIF_MSG_LINK, "1G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_1000baseT_Full |
				       ADVERTISED_TP);
			break;

		case SPEED_2500:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(NETIF_MSG_LINK,
				   "2.5G half not supported\n");
				return -EINVAL;
			}

			if (!(bp->port.supported[cfg_idx]
			      & SUPPORTED_2500baseX_Full)) {
				DP(NETIF_MSG_LINK,
				   "2.5G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_2500baseX_Full |
				       ADVERTISED_TP);
			break;

		case SPEED_10000:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(NETIF_MSG_LINK, "10G half not supported\n");
				return -EINVAL;
			}

			if (!(bp->port.supported[cfg_idx]
			      & SUPPORTED_10000baseT_Full)) {
				DP(NETIF_MSG_LINK, "10G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_10000baseT_Full |
				       ADVERTISED_FIBRE);
			break;

		default:
			DP(NETIF_MSG_LINK, "Unsupported speed %u\n", speed);
			return -EINVAL;
		}

		bp->link_params.req_line_speed[cfg_idx] = speed;
		bp->link_params.req_duplex[cfg_idx] = cmd->duplex;
		bp->port.advertising[cfg_idx] = advertising;
	}

	DP(NETIF_MSG_LINK, "req_line_speed %d\n"
	   "  req_duplex %d  advertising 0x%x\n",
	   bp->link_params.req_line_speed[cfg_idx],
	   bp->link_params.req_duplex[cfg_idx],
	   bp->port.advertising[cfg_idx]);

	/* Set new config */
	bp->link_params.multi_phy_config = new_multi_phy_config;
	if (netif_running(dev)) {
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);
		bnx2x_link_set(bp);
	}

	return 0;
}

#define IS_E1_ONLINE(info)	(((info) & RI_E1_ONLINE) == RI_E1_ONLINE)
#define IS_E1H_ONLINE(info)	(((info) & RI_E1H_ONLINE) == RI_E1H_ONLINE)
#define IS_E2_ONLINE(info)	(((info) & RI_E2_ONLINE) == RI_E2_ONLINE)
#define IS_E3_ONLINE(info)	(((info) & RI_E3_ONLINE) == RI_E3_ONLINE)
#define IS_E3B0_ONLINE(info)	(((info) & RI_E3B0_ONLINE) == RI_E3B0_ONLINE)

static inline bool bnx2x_is_reg_online(struct bnx2x *bp,
				       const struct reg_addr *reg_info)
{
	if (CHIP_IS_E1(bp))
		return IS_E1_ONLINE(reg_info->info);
	else if (CHIP_IS_E1H(bp))
		return IS_E1H_ONLINE(reg_info->info);
	else if (CHIP_IS_E2(bp))
		return IS_E2_ONLINE(reg_info->info);
	else if (CHIP_IS_E3A0(bp))
		return IS_E3_ONLINE(reg_info->info);
	else if (CHIP_IS_E3B0(bp))
		return IS_E3B0_ONLINE(reg_info->info);
	else
		return false;
}

/******* Paged registers info selectors ********/
static inline const u32 *__bnx2x_get_page_addr_ar(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return page_vals_e2;
	else if (CHIP_IS_E3(bp))
		return page_vals_e3;
	else
		return NULL;
}

static inline u32 __bnx2x_get_page_reg_num(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return PAGE_MODE_VALUES_E2;
	else if (CHIP_IS_E3(bp))
		return PAGE_MODE_VALUES_E3;
	else
		return 0;
}

static inline const u32 *__bnx2x_get_page_write_ar(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return page_write_regs_e2;
	else if (CHIP_IS_E3(bp))
		return page_write_regs_e3;
	else
		return NULL;
}

static inline u32 __bnx2x_get_page_write_num(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return PAGE_WRITE_REGS_E2;
	else if (CHIP_IS_E3(bp))
		return PAGE_WRITE_REGS_E3;
	else
		return 0;
}

static inline const struct reg_addr *__bnx2x_get_page_read_ar(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return page_read_regs_e2;
	else if (CHIP_IS_E3(bp))
		return page_read_regs_e3;
	else
		return NULL;
}

static inline u32 __bnx2x_get_page_read_num(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return PAGE_READ_REGS_E2;
	else if (CHIP_IS_E3(bp))
		return PAGE_READ_REGS_E3;
	else
		return 0;
}

static inline int __bnx2x_get_regs_len(struct bnx2x *bp)
{
	int num_pages = __bnx2x_get_page_reg_num(bp);
	int page_write_num = __bnx2x_get_page_write_num(bp);
	const struct reg_addr *page_read_addr = __bnx2x_get_page_read_ar(bp);
	int page_read_num = __bnx2x_get_page_read_num(bp);
	int regdump_len = 0;
	int i, j, k;

	for (i = 0; i < REGS_COUNT; i++)
		if (bnx2x_is_reg_online(bp, &reg_addrs[i]))
			regdump_len += reg_addrs[i].size;

	for (i = 0; i < num_pages; i++)
		for (j = 0; j < page_write_num; j++)
			for (k = 0; k < page_read_num; k++)
				if (bnx2x_is_reg_online(bp, &page_read_addr[k]))
					regdump_len += page_read_addr[k].size;

	return regdump_len;
}

static int bnx2x_get_regs_len(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	int regdump_len = 0;

	regdump_len = __bnx2x_get_regs_len(bp);
	regdump_len *= 4;
	regdump_len += sizeof(struct dump_hdr);

	return regdump_len;
}

/**
 * bnx2x_read_pages_regs - read "paged" registers
 *
 * @bp		device handle
 * @p		output buffer
 *
 * Reads "paged" memories: memories that may only be read by first writing to a
 * specific address ("write address") and then reading from a specific address
 * ("read address"). There may be more than one write address per "page" and
 * more than one read address per write address.
 */
static inline void bnx2x_read_pages_regs(struct bnx2x *bp, u32 *p)
{
	u32 i, j, k, n;
	/* addresses of the paged registers */
	const u32 *page_addr = __bnx2x_get_page_addr_ar(bp);
	/* number of paged registers */
	int num_pages = __bnx2x_get_page_reg_num(bp);
	/* write addresses */
	const u32 *write_addr = __bnx2x_get_page_write_ar(bp);
	/* number of write addresses */
	int write_num = __bnx2x_get_page_write_num(bp);
	/* read addresses info */
	const struct reg_addr *read_addr = __bnx2x_get_page_read_ar(bp);
	/* number of read addresses */
	int read_num = __bnx2x_get_page_read_num(bp);

	for (i = 0; i < num_pages; i++) {
		for (j = 0; j < write_num; j++) {
			REG_WR(bp, write_addr[j], page_addr[i]);
			for (k = 0; k < read_num; k++)
				if (bnx2x_is_reg_online(bp, &read_addr[k]))
					for (n = 0; n <
					      read_addr[k].size; n++)
						*p++ = REG_RD(bp,
						       read_addr[k].addr + n*4);
		}
	}
}

static inline void __bnx2x_get_regs(struct bnx2x *bp, u32 *p)
{
	u32 i, j;

	/* Read the regular registers */
	for (i = 0; i < REGS_COUNT; i++)
		if (bnx2x_is_reg_online(bp, &reg_addrs[i]))
			for (j = 0; j < reg_addrs[i].size; j++)
				*p++ = REG_RD(bp, reg_addrs[i].addr + j*4);

	/* Read "paged" registes */
	bnx2x_read_pages_regs(bp, p);
}

static void bnx2x_get_regs(struct net_device *dev,
			   struct ethtool_regs *regs, void *_p)
{
	u32 *p = _p;
	struct bnx2x *bp = netdev_priv(dev);
	struct dump_hdr dump_hdr = {0};

	regs->version = 0;
	memset(p, 0, regs->len);

	if (!netif_running(bp->dev))
		return;

	/* Disable parity attentions as long as following dump may
	 * cause false alarms by reading never written registers. We
	 * will re-enable parity attentions right after the dump.
	 */
	bnx2x_disable_blocks_parity(bp);

	dump_hdr.hdr_size = (sizeof(struct dump_hdr) / 4) - 1;
	dump_hdr.dump_sign = dump_sign_all;
	dump_hdr.xstorm_waitp = REG_RD(bp, XSTORM_WAITP_ADDR);
	dump_hdr.tstorm_waitp = REG_RD(bp, TSTORM_WAITP_ADDR);
	dump_hdr.ustorm_waitp = REG_RD(bp, USTORM_WAITP_ADDR);
	dump_hdr.cstorm_waitp = REG_RD(bp, CSTORM_WAITP_ADDR);

	if (CHIP_IS_E1(bp))
		dump_hdr.info = RI_E1_ONLINE;
	else if (CHIP_IS_E1H(bp))
		dump_hdr.info = RI_E1H_ONLINE;
	else if (!CHIP_IS_E1x(bp))
		dump_hdr.info = RI_E2_ONLINE |
		(BP_PATH(bp) ? RI_PATH1_DUMP : RI_PATH0_DUMP);

	memcpy(p, &dump_hdr, sizeof(struct dump_hdr));
	p += dump_hdr.hdr_size + 1;

	/* Actually read the registers */
	__bnx2x_get_regs(bp, p);

	/* Re-enable parity attentions */
	bnx2x_clear_blocks_parity(bp);
	bnx2x_enable_blocks_parity(bp);
}

static void bnx2x_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *info)
{
	struct bnx2x *bp = netdev_priv(dev);
	u8 phy_fw_ver[PHY_FW_VER_LEN];

	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));

	phy_fw_ver[0] = '\0';
	if (bp->port.pmf) {
		bnx2x_acquire_phy_lock(bp);
		bnx2x_get_ext_phy_fw_version(&bp->link_params,
					     (bp->state != BNX2X_STATE_CLOSED),
					     phy_fw_ver, PHY_FW_VER_LEN);
		bnx2x_release_phy_lock(bp);
	}

	strlcpy(info->fw_version, bp->fw_ver, sizeof(info->fw_version));
	snprintf(info->fw_version + strlen(bp->fw_ver), 32 - strlen(bp->fw_ver),
		 "bc %d.%d.%d%s%s",
		 (bp->common.bc_ver & 0xff0000) >> 16,
		 (bp->common.bc_ver & 0xff00) >> 8,
		 (bp->common.bc_ver & 0xff),
		 ((phy_fw_ver[0] != '\0') ? " phy " : ""), phy_fw_ver);
	strlcpy(info->bus_info, pci_name(bp->pdev), sizeof(info->bus_info));
	info->n_stats = BNX2X_NUM_STATS;
	info->testinfo_len = BNX2X_NUM_TESTS;
	info->eedump_len = bp->common.flash_size;
	info->regdump_len = bnx2x_get_regs_len(dev);
}

static void bnx2x_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->flags & NO_WOL_FLAG) {
		wol->supported = 0;
		wol->wolopts = 0;
	} else {
		wol->supported = WAKE_MAGIC;
		if (bp->wol)
			wol->wolopts = WAKE_MAGIC;
		else
			wol->wolopts = 0;
	}
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int bnx2x_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGIC) {
		if (bp->flags & NO_WOL_FLAG)
			return -EINVAL;

		bp->wol = 1;
	} else
		bp->wol = 0;

	return 0;
}

static u32 bnx2x_get_msglevel(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	return bp->msg_enable;
}

static void bnx2x_set_msglevel(struct net_device *dev, u32 level)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (capable(CAP_NET_ADMIN)) {
		/* dump MCP trace */
		if (level & BNX2X_MSG_MCP)
			bnx2x_fw_dump_lvl(bp, KERN_INFO);
		bp->msg_enable = level;
	}
}

static int bnx2x_nway_reset(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (!bp->port.pmf)
		return 0;

	if (netif_running(dev)) {
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);
		bnx2x_link_set(bp);
	}

	return 0;
}

static u32 bnx2x_get_link(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->flags & MF_FUNC_DIS || (bp->state != BNX2X_STATE_OPEN))
		return 0;

	return bp->link_vars.link_up;
}

static int bnx2x_get_eeprom_len(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	return bp->common.flash_size;
}

static int bnx2x_acquire_nvram_lock(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int count, i;
	u32 val = 0;

	/* adjust timeout for emulation/FPGA */
	count = BNX2X_NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* request access to nvram interface */
	REG_WR(bp, MCP_REG_MCPR_NVM_SW_ARB,
	       (MCPR_NVM_SW_ARB_ARB_REQ_SET1 << port));

	for (i = 0; i < count*10; i++) {
		val = REG_RD(bp, MCP_REG_MCPR_NVM_SW_ARB);
		if (val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port))
			break;

		udelay(5);
	}

	if (!(val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port))) {
		DP(BNX2X_MSG_NVM, "cannot get access to nvram interface\n");
		return -EBUSY;
	}

	return 0;
}

static int bnx2x_release_nvram_lock(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int count, i;
	u32 val = 0;

	/* adjust timeout for emulation/FPGA */
	count = BNX2X_NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* relinquish nvram interface */
	REG_WR(bp, MCP_REG_MCPR_NVM_SW_ARB,
	       (MCPR_NVM_SW_ARB_ARB_REQ_CLR1 << port));

	for (i = 0; i < count*10; i++) {
		val = REG_RD(bp, MCP_REG_MCPR_NVM_SW_ARB);
		if (!(val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port)))
			break;

		udelay(5);
	}

	if (val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port)) {
		DP(BNX2X_MSG_NVM, "cannot free access to nvram interface\n");
		return -EBUSY;
	}

	return 0;
}

static void bnx2x_enable_nvram_access(struct bnx2x *bp)
{
	u32 val;

	val = REG_RD(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE);

	/* enable both bits, even on read */
	REG_WR(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE,
	       (val | MCPR_NVM_ACCESS_ENABLE_EN |
		      MCPR_NVM_ACCESS_ENABLE_WR_EN));
}

static void bnx2x_disable_nvram_access(struct bnx2x *bp)
{
	u32 val;

	val = REG_RD(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE);

	/* disable both bits, even after read */
	REG_WR(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE,
	       (val & ~(MCPR_NVM_ACCESS_ENABLE_EN |
			MCPR_NVM_ACCESS_ENABLE_WR_EN)));
}

static int bnx2x_nvram_read_dword(struct bnx2x *bp, u32 offset, __be32 *ret_val,
				  u32 cmd_flags)
{
	int count, i, rc;
	u32 val;

	/* build the command word */
	cmd_flags |= MCPR_NVM_COMMAND_DOIT;

	/* need to clear DONE bit separately */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, MCPR_NVM_COMMAND_DONE);

	/* address of the NVRAM to read from */
	REG_WR(bp, MCP_REG_MCPR_NVM_ADDR,
	       (offset & MCPR_NVM_ADDR_NVM_ADDR_VALUE));

	/* issue a read command */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, cmd_flags);

	/* adjust timeout for emulation/FPGA */
	count = BNX2X_NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* wait for completion */
	*ret_val = 0;
	rc = -EBUSY;
	for (i = 0; i < count; i++) {
		udelay(5);
		val = REG_RD(bp, MCP_REG_MCPR_NVM_COMMAND);

		if (val & MCPR_NVM_COMMAND_DONE) {
			val = REG_RD(bp, MCP_REG_MCPR_NVM_READ);
			/* we read nvram data in cpu order
			 * but ethtool sees it as an array of bytes
			 * converting to big-endian will do the work */
			*ret_val = cpu_to_be32(val);
			rc = 0;
			break;
		}
	}

	return rc;
}

static int bnx2x_nvram_read(struct bnx2x *bp, u32 offset, u8 *ret_buf,
			    int buf_size)
{
	int rc;
	u32 cmd_flags;
	__be32 val;

	if ((offset & 0x03) || (buf_size & 0x03) || (buf_size == 0)) {
		DP(BNX2X_MSG_NVM,
		   "Invalid parameter: offset 0x%x  buf_size 0x%x\n",
		   offset, buf_size);
		return -EINVAL;
	}

	if (offset + buf_size > bp->common.flash_size) {
		DP(BNX2X_MSG_NVM, "Invalid parameter: offset (0x%x) +"
				  " buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->common.flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	/* read the first word(s) */
	cmd_flags = MCPR_NVM_COMMAND_FIRST;
	while ((buf_size > sizeof(u32)) && (rc == 0)) {
		rc = bnx2x_nvram_read_dword(bp, offset, &val, cmd_flags);
		memcpy(ret_buf, &val, 4);

		/* advance to the next dword */
		offset += sizeof(u32);
		ret_buf += sizeof(u32);
		buf_size -= sizeof(u32);
		cmd_flags = 0;
	}

	if (rc == 0) {
		cmd_flags |= MCPR_NVM_COMMAND_LAST;
		rc = bnx2x_nvram_read_dword(bp, offset, &val, cmd_flags);
		memcpy(ret_buf, &val, 4);
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_get_eeprom(struct net_device *dev,
			    struct ethtool_eeprom *eeprom, u8 *eebuf)
{
	struct bnx2x *bp = netdev_priv(dev);
	int rc;

	if (!netif_running(dev))
		return -EAGAIN;

	DP(BNX2X_MSG_NVM, "ethtool_eeprom: cmd %d\n"
	   "  magic 0x%x  offset 0x%x (%d)  len 0x%x (%d)\n",
	   eeprom->cmd, eeprom->magic, eeprom->offset, eeprom->offset,
	   eeprom->len, eeprom->len);

	/* parameters already validated in ethtool_get_eeprom */

	rc = bnx2x_nvram_read(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int bnx2x_nvram_write_dword(struct bnx2x *bp, u32 offset, u32 val,
				   u32 cmd_flags)
{
	int count, i, rc;

	/* build the command word */
	cmd_flags |= MCPR_NVM_COMMAND_DOIT | MCPR_NVM_COMMAND_WR;

	/* need to clear DONE bit separately */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, MCPR_NVM_COMMAND_DONE);

	/* write the data */
	REG_WR(bp, MCP_REG_MCPR_NVM_WRITE, val);

	/* address of the NVRAM to write to */
	REG_WR(bp, MCP_REG_MCPR_NVM_ADDR,
	       (offset & MCPR_NVM_ADDR_NVM_ADDR_VALUE));

	/* issue the write command */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, cmd_flags);

	/* adjust timeout for emulation/FPGA */
	count = BNX2X_NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* wait for completion */
	rc = -EBUSY;
	for (i = 0; i < count; i++) {
		udelay(5);
		val = REG_RD(bp, MCP_REG_MCPR_NVM_COMMAND);
		if (val & MCPR_NVM_COMMAND_DONE) {
			rc = 0;
			break;
		}
	}

	return rc;
}

#define BYTE_OFFSET(offset)		(8 * (offset & 0x03))

static int bnx2x_nvram_write1(struct bnx2x *bp, u32 offset, u8 *data_buf,
			      int buf_size)
{
	int rc;
	u32 cmd_flags;
	u32 align_offset;
	__be32 val;

	if (offset + buf_size > bp->common.flash_size) {
		DP(BNX2X_MSG_NVM, "Invalid parameter: offset (0x%x) +"
				  " buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->common.flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	cmd_flags = (MCPR_NVM_COMMAND_FIRST | MCPR_NVM_COMMAND_LAST);
	align_offset = (offset & ~0x03);
	rc = bnx2x_nvram_read_dword(bp, align_offset, &val, cmd_flags);

	if (rc == 0) {
		val &= ~(0xff << BYTE_OFFSET(offset));
		val |= (*data_buf << BYTE_OFFSET(offset));

		/* nvram data is returned as an array of bytes
		 * convert it back to cpu order */
		val = be32_to_cpu(val);

		rc = bnx2x_nvram_write_dword(bp, align_offset, val,
					     cmd_flags);
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_nvram_write(struct bnx2x *bp, u32 offset, u8 *data_buf,
			     int buf_size)
{
	int rc;
	u32 cmd_flags;
	u32 val;
	u32 written_so_far;

	if (buf_size == 1)	/* ethtool */
		return bnx2x_nvram_write1(bp, offset, data_buf, buf_size);

	if ((offset & 0x03) || (buf_size & 0x03) || (buf_size == 0)) {
		DP(BNX2X_MSG_NVM,
		   "Invalid parameter: offset 0x%x  buf_size 0x%x\n",
		   offset, buf_size);
		return -EINVAL;
	}

	if (offset + buf_size > bp->common.flash_size) {
		DP(BNX2X_MSG_NVM, "Invalid parameter: offset (0x%x) +"
				  " buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->common.flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	written_so_far = 0;
	cmd_flags = MCPR_NVM_COMMAND_FIRST;
	while ((written_so_far < buf_size) && (rc == 0)) {
		if (written_so_far == (buf_size - sizeof(u32)))
			cmd_flags |= MCPR_NVM_COMMAND_LAST;
		else if (((offset + 4) % BNX2X_NVRAM_PAGE_SIZE) == 0)
			cmd_flags |= MCPR_NVM_COMMAND_LAST;
		else if ((offset % BNX2X_NVRAM_PAGE_SIZE) == 0)
			cmd_flags |= MCPR_NVM_COMMAND_FIRST;

		memcpy(&val, data_buf, 4);

		rc = bnx2x_nvram_write_dword(bp, offset, val, cmd_flags);

		/* advance to the next dword */
		offset += sizeof(u32);
		data_buf += sizeof(u32);
		written_so_far += sizeof(u32);
		cmd_flags = 0;
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_set_eeprom(struct net_device *dev,
			    struct ethtool_eeprom *eeprom, u8 *eebuf)
{
	struct bnx2x *bp = netdev_priv(dev);
	int port = BP_PORT(bp);
	int rc = 0;
	u32 ext_phy_config;
	if (!netif_running(dev))
		return -EAGAIN;

	DP(BNX2X_MSG_NVM, "ethtool_eeprom: cmd %d\n"
	   "  magic 0x%x  offset 0x%x (%d)  len 0x%x (%d)\n",
	   eeprom->cmd, eeprom->magic, eeprom->offset, eeprom->offset,
	   eeprom->len, eeprom->len);

	/* parameters already validated in ethtool_set_eeprom */

	/* PHY eeprom can be accessed only by the PMF */
	if ((eeprom->magic >= 0x50485900) && (eeprom->magic <= 0x504859FF) &&
	    !bp->port.pmf)
		return -EINVAL;

	ext_phy_config =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].external_phy_config);

	if (eeprom->magic == 0x50485950) {
		/* 'PHYP' (0x50485950): prepare phy for FW upgrade */
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);

		bnx2x_acquire_phy_lock(bp);
		rc |= bnx2x_link_reset(&bp->link_params,
				       &bp->link_vars, 0);
		if (XGXS_EXT_PHY_TYPE(ext_phy_config) ==
					PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101)
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_0,
				       MISC_REGISTERS_GPIO_HIGH, port);
		bnx2x_release_phy_lock(bp);
		bnx2x_link_report(bp);

	} else if (eeprom->magic == 0x50485952) {
		/* 'PHYR' (0x50485952): re-init link after FW upgrade */
		if (bp->state == BNX2X_STATE_OPEN) {
			bnx2x_acquire_phy_lock(bp);
			rc |= bnx2x_link_reset(&bp->link_params,
					       &bp->link_vars, 1);

			rc |= bnx2x_phy_init(&bp->link_params,
					     &bp->link_vars);
			bnx2x_release_phy_lock(bp);
			bnx2x_calc_fc_adv(bp);
		}
	} else if (eeprom->magic == 0x53985943) {
		/* 'PHYC' (0x53985943): PHY FW upgrade completed */
		if (XGXS_EXT_PHY_TYPE(ext_phy_config) ==
				       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101) {

			/* DSP Remove Download Mode */
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_0,
				       MISC_REGISTERS_GPIO_LOW, port);

			bnx2x_acquire_phy_lock(bp);

			bnx2x_sfx7101_sp_sw_reset(bp,
						&bp->link_params.phy[EXT_PHY1]);

			/* wait 0.5 sec to allow it to run */
			msleep(500);
			bnx2x_ext_phy_hw_reset(bp, port);
			msleep(500);
			bnx2x_release_phy_lock(bp);
		}
	} else
		rc = bnx2x_nvram_write(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int bnx2x_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal)
{
	struct bnx2x *bp = netdev_priv(dev);

	memset(coal, 0, sizeof(struct ethtool_coalesce));

	coal->rx_coalesce_usecs = bp->rx_ticks;
	coal->tx_coalesce_usecs = bp->tx_ticks;

	return 0;
}

static int bnx2x_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal)
{
	struct bnx2x *bp = netdev_priv(dev);

	bp->rx_ticks = (u16)coal->rx_coalesce_usecs;
	if (bp->rx_ticks > BNX2X_MAX_COALESCE_TOUT)
		bp->rx_ticks = BNX2X_MAX_COALESCE_TOUT;

	bp->tx_ticks = (u16)coal->tx_coalesce_usecs;
	if (bp->tx_ticks > BNX2X_MAX_COALESCE_TOUT)
		bp->tx_ticks = BNX2X_MAX_COALESCE_TOUT;

	if (netif_running(dev))
		bnx2x_update_coalesce(bp);

	return 0;
}

static void bnx2x_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *ering)
{
	struct bnx2x *bp = netdev_priv(dev);

	ering->rx_max_pending = MAX_RX_AVAIL;

	if (bp->rx_ring_size)
		ering->rx_pending = bp->rx_ring_size;
	else
		ering->rx_pending = MAX_RX_AVAIL;

	ering->tx_max_pending = MAX_TX_AVAIL;
	ering->tx_pending = bp->tx_ring_size;
}

static int bnx2x_set_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ering)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->recovery_state != BNX2X_RECOVERY_DONE) {
		pr_err("Handling parity error recovery. Try again later\n");
		return -EAGAIN;
	}

	if ((ering->rx_pending > MAX_RX_AVAIL) ||
	    (ering->rx_pending < (bp->disable_tpa ? MIN_RX_SIZE_NONTPA :
						    MIN_RX_SIZE_TPA)) ||
	    (ering->tx_pending > MAX_TX_AVAIL) ||
	    (ering->tx_pending <= MAX_SKB_FRAGS + 4))
		return -EINVAL;

	bp->rx_ring_size = ering->rx_pending;
	bp->tx_ring_size = ering->tx_pending;

	return bnx2x_reload_if_running(dev);
}

static void bnx2x_get_pauseparam(struct net_device *dev,
				 struct ethtool_pauseparam *epause)
{
	struct bnx2x *bp = netdev_priv(dev);
	int cfg_idx = bnx2x_get_link_cfg_idx(bp);
	epause->autoneg = (bp->link_params.req_flow_ctrl[cfg_idx] ==
			   BNX2X_FLOW_CTRL_AUTO);

	epause->rx_pause = ((bp->link_vars.flow_ctrl & BNX2X_FLOW_CTRL_RX) ==
			    BNX2X_FLOW_CTRL_RX);
	epause->tx_pause = ((bp->link_vars.flow_ctrl & BNX2X_FLOW_CTRL_TX) ==
			    BNX2X_FLOW_CTRL_TX);

	DP(NETIF_MSG_LINK, "ethtool_pauseparam: cmd %d\n"
	   "  autoneg %d  rx_pause %d  tx_pause %d\n",
	   epause->cmd, epause->autoneg, epause->rx_pause, epause->tx_pause);
}

static int bnx2x_set_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 cfg_idx = bnx2x_get_link_cfg_idx(bp);
	if (IS_MF(bp))
		return 0;

	DP(NETIF_MSG_LINK, "ethtool_pauseparam: cmd %d\n"
	   "  autoneg %d  rx_pause %d  tx_pause %d\n",
	   epause->cmd, epause->autoneg, epause->rx_pause, epause->tx_pause);

	bp->link_params.req_flow_ctrl[cfg_idx] = BNX2X_FLOW_CTRL_AUTO;

	if (epause->rx_pause)
		bp->link_params.req_flow_ctrl[cfg_idx] |= BNX2X_FLOW_CTRL_RX;

	if (epause->tx_pause)
		bp->link_params.req_flow_ctrl[cfg_idx] |= BNX2X_FLOW_CTRL_TX;

	if (bp->link_params.req_flow_ctrl[cfg_idx] == BNX2X_FLOW_CTRL_AUTO)
		bp->link_params.req_flow_ctrl[cfg_idx] = BNX2X_FLOW_CTRL_NONE;

	if (epause->autoneg) {
		if (!(bp->port.supported[cfg_idx] & SUPPORTED_Autoneg)) {
			DP(NETIF_MSG_LINK, "autoneg not supported\n");
			return -EINVAL;
		}

		if (bp->link_params.req_line_speed[cfg_idx] == SPEED_AUTO_NEG) {
			bp->link_params.req_flow_ctrl[cfg_idx] =
				BNX2X_FLOW_CTRL_AUTO;
		}
	}

	DP(NETIF_MSG_LINK,
	   "req_flow_ctrl 0x%x\n", bp->link_params.req_flow_ctrl[cfg_idx]);

	if (netif_running(dev)) {
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);
		bnx2x_link_set(bp);
	}

	return 0;
}

static const struct {
	char string[ETH_GSTRING_LEN];
} bnx2x_tests_str_arr[BNX2X_NUM_TESTS] = {
	{ "register_test (offline)" },
	{ "memory_test (offline)" },
	{ "loopback_test (offline)" },
	{ "nvram_test (online)" },
	{ "interrupt_test (online)" },
	{ "link_test (online)" },
	{ "idle check (online)" }
};

enum {
	BNX2X_CHIP_E1_OFST = 0,
	BNX2X_CHIP_E1H_OFST,
	BNX2X_CHIP_E2_OFST,
	BNX2X_CHIP_E3_OFST,
	BNX2X_CHIP_E3B0_OFST,
	BNX2X_CHIP_MAX_OFST
};

#define BNX2X_CHIP_MASK_E1	(1 << BNX2X_CHIP_E1_OFST)
#define BNX2X_CHIP_MASK_E1H	(1 << BNX2X_CHIP_E1H_OFST)
#define BNX2X_CHIP_MASK_E2	(1 << BNX2X_CHIP_E2_OFST)
#define BNX2X_CHIP_MASK_E3	(1 << BNX2X_CHIP_E3_OFST)
#define BNX2X_CHIP_MASK_E3B0	(1 << BNX2X_CHIP_E3B0_OFST)

#define BNX2X_CHIP_MASK_ALL	((1 << BNX2X_CHIP_MAX_OFST) - 1)
#define BNX2X_CHIP_MASK_E1X	(BNX2X_CHIP_MASK_E1 | BNX2X_CHIP_MASK_E1H)

static int bnx2x_test_registers(struct bnx2x *bp)
{
	int idx, i, rc = -ENODEV;
	u32 wr_val = 0, hw;
	int port = BP_PORT(bp);
	static const struct {
		u32 hw;
		u32 offset0;
		u32 offset1;
		u32 mask;
	} reg_tbl[] = {
/* 0 */		{ BNX2X_CHIP_MASK_ALL,
			BRB1_REG_PAUSE_LOW_THRESHOLD_0,	4, 0x000003ff },
		{ BNX2X_CHIP_MASK_ALL,
			DORQ_REG_DB_ADDR0,		4, 0xffffffff },
		{ BNX2X_CHIP_MASK_E1X,
			HC_REG_AGG_INT_0,		4, 0x000003ff },
		{ BNX2X_CHIP_MASK_ALL,
			PBF_REG_MAC_IF0_ENABLE,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2 | BNX2X_CHIP_MASK_E3,
			PBF_REG_P0_INIT_CRD,		4, 0x000007ff },
		{ BNX2X_CHIP_MASK_E3B0,
			PBF_REG_INIT_CRD_Q0,		4, 0x000007ff },
		{ BNX2X_CHIP_MASK_ALL,
			PRS_REG_CID_PORT_0,		4, 0x00ffffff },
		{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_PSWRQ_CDU0_L2P,	4, 0x000fffff },
		{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_RQ_CDU0_EFIRST_MEM_ADDR, 8, 0x0003ffff },
		{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_PSWRQ_TM0_L2P,		4, 0x000fffff },
/* 10 */	{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_RQ_USDM0_EFIRST_MEM_ADDR, 8, 0x0003ffff },
		{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_PSWRQ_TSDM0_L2P,	4, 0x000fffff },
		{ BNX2X_CHIP_MASK_ALL,
			QM_REG_CONNNUM_0,		4, 0x000fffff },
		{ BNX2X_CHIP_MASK_ALL,
			TM_REG_LIN0_MAX_ACTIVE_CID,	4, 0x0003ffff },
		{ BNX2X_CHIP_MASK_ALL,
			SRC_REG_KEYRSS0_0,		40, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			SRC_REG_KEYRSS0_7,		40, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			XCM_REG_WU_DA_SET_TMR_CNT_FLG_CMD00, 4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			XCM_REG_WU_DA_CNT_CMD00,	4, 0x00000003 },
		{ BNX2X_CHIP_MASK_ALL,
			XCM_REG_GLB_DEL_ACK_MAX_CNT_0,	4, 0x000000ff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_T_BIT,		4, 0x00000001 },
/* 20 */	{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_EMAC0_IN_EN,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_BMAC0_IN_EN,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_XCM0_OUT_EN,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_BRB0_OUT_EN,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_XCM_MASK,		4, 0x00000007 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_ACPI_PAT_6_LEN,	68, 0x000000ff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_ACPI_PAT_0_CRC,	68, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_DEST_MAC_0_0,	160, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_DEST_IP_0_1,	160, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_IPV4_IPV6_0,	160, 0x00000001 },
/* 30 */	{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_DEST_UDP_0,	160, 0x0000ffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_DEST_TCP_0,	160, 0x0000ffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_VLAN_ID_0,	160, 0x00000fff },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_XGXS_SERDES0_MODE_SEL,	4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0, 4, 0x00000001},
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_STATUS_INTERRUPT_PORT0,	4, 0x07ffffff },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_XGXS0_CTRL_EXTREMOTEMDIOST, 24, 0x00000001 },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_SERDES0_CTRL_PHY_ADDR,	16, 0x0000001f },

		{ BNX2X_CHIP_MASK_ALL, 0xffffffff, 0, 0x00000000 }
	};

	if (!netif_running(bp->dev))
		return rc;

	if (CHIP_IS_E1(bp))
		hw = BNX2X_CHIP_MASK_E1;
	else if (CHIP_IS_E1H(bp))
		hw = BNX2X_CHIP_MASK_E1H;
	else if (CHIP_IS_E2(bp))
		hw = BNX2X_CHIP_MASK_E2;
	else if (CHIP_IS_E3B0(bp))
		hw = BNX2X_CHIP_MASK_E3B0;
	else /* e3 A0 */
		hw = BNX2X_CHIP_MASK_E3;

	/* Repeat the test twice:
	   First by writing 0x00000000, second by writing 0xffffffff */
	for (idx = 0; idx < 2; idx++) {

		switch (idx) {
		case 0:
			wr_val = 0;
			break;
		case 1:
			wr_val = 0xffffffff;
			break;
		}

		for (i = 0; reg_tbl[i].offset0 != 0xffffffff; i++) {
			u32 offset, mask, save_val, val;
			if (!(hw & reg_tbl[i].hw))
				continue;

			offset = reg_tbl[i].offset0 + port*reg_tbl[i].offset1;
			mask = reg_tbl[i].mask;

			save_val = REG_RD(bp, offset);

			REG_WR(bp, offset, wr_val & mask);

			val = REG_RD(bp, offset);

			/* Restore the original register's value */
			REG_WR(bp, offset, save_val);

			/* verify value is as expected */
			if ((val & mask) != (wr_val & mask)) {
				DP(NETIF_MSG_HW,
				   "offset 0x%x: val 0x%x != 0x%x mask 0x%x\n",
				   offset, val, wr_val, mask);
				goto test_reg_exit;
			}
		}
	}

	rc = 0;

test_reg_exit:
	return rc;
}

static int bnx2x_test_memory(struct bnx2x *bp)
{
	int i, j, rc = -ENODEV;
	u32 val, index;
	static const struct {
		u32 offset;
		int size;
	} mem_tbl[] = {
		{ CCM_REG_XX_DESCR_TABLE,   CCM_REG_XX_DESCR_TABLE_SIZE },
		{ CFC_REG_ACTIVITY_COUNTER, CFC_REG_ACTIVITY_COUNTER_SIZE },
		{ CFC_REG_LINK_LIST,        CFC_REG_LINK_LIST_SIZE },
		{ DMAE_REG_CMD_MEM,         DMAE_REG_CMD_MEM_SIZE },
		{ TCM_REG_XX_DESCR_TABLE,   TCM_REG_XX_DESCR_TABLE_SIZE },
		{ UCM_REG_XX_DESCR_TABLE,   UCM_REG_XX_DESCR_TABLE_SIZE },
		{ XCM_REG_XX_DESCR_TABLE,   XCM_REG_XX_DESCR_TABLE_SIZE },

		{ 0xffffffff, 0 }
	};

	static const struct {
		char *name;
		u32 offset;
		u32 hw_mask[BNX2X_CHIP_MAX_OFST];
	} prty_tbl[] = {
		{ "CCM_PRTY_STS",  CCM_REG_CCM_PRTY_STS,
			{0x3ffc0, 0,   0, 0} },
		{ "CFC_PRTY_STS",  CFC_REG_CFC_PRTY_STS,
			{0x2,     0x2, 0, 0} },
		{ "DMAE_PRTY_STS", DMAE_REG_DMAE_PRTY_STS,
			{0,       0,   0, 0} },
		{ "TCM_PRTY_STS",  TCM_REG_TCM_PRTY_STS,
			{0x3ffc0, 0,   0, 0} },
		{ "UCM_PRTY_STS",  UCM_REG_UCM_PRTY_STS,
			{0x3ffc0, 0,   0, 0} },
		{ "XCM_PRTY_STS",  XCM_REG_XCM_PRTY_STS,
			{0x3ffc1, 0,   0, 0} },

		{ NULL, 0xffffffff, {0, 0, 0, 0} }
	};

	if (!netif_running(bp->dev))
		return rc;

	if (CHIP_IS_E1(bp))
		index = BNX2X_CHIP_E1_OFST;
	else if (CHIP_IS_E1H(bp))
		index = BNX2X_CHIP_E1H_OFST;
	else if (CHIP_IS_E2(bp))
		index = BNX2X_CHIP_E2_OFST;
	else /* e3 */
		index = BNX2X_CHIP_E3_OFST;

	/* pre-Check the parity status */
	for (i = 0; prty_tbl[i].offset != 0xffffffff; i++) {
		val = REG_RD(bp, prty_tbl[i].offset);
		if (val & ~(prty_tbl[i].hw_mask[index])) {
			DP(NETIF_MSG_HW,
			   "%s is 0x%x\n", prty_tbl[i].name, val);
			goto test_mem_exit;
		}
	}

	/* Go through all the memories */
	for (i = 0; mem_tbl[i].offset != 0xffffffff; i++)
		for (j = 0; j < mem_tbl[i].size; j++)
			REG_RD(bp, mem_tbl[i].offset + j*4);

	/* Check the parity status */
	for (i = 0; prty_tbl[i].offset != 0xffffffff; i++) {
		val = REG_RD(bp, prty_tbl[i].offset);
		if (val & ~(prty_tbl[i].hw_mask[index])) {
			DP(NETIF_MSG_HW,
			   "%s is 0x%x\n", prty_tbl[i].name, val);
			goto test_mem_exit;
		}
	}

	rc = 0;

test_mem_exit:
	return rc;
}

static void bnx2x_wait_for_link(struct bnx2x *bp, u8 link_up, u8 is_serdes)
{
	int cnt = 1400;

	if (link_up) {
		while (bnx2x_link_test(bp, is_serdes) && cnt--)
			msleep(20);

		if (cnt <= 0 && bnx2x_link_test(bp, is_serdes))
			DP(NETIF_MSG_LINK, "Timeout waiting for link up\n");
	}
}

static int bnx2x_run_loopback(struct bnx2x *bp, int loopback_mode)
{
	unsigned int pkt_size, num_pkts, i;
	struct sk_buff *skb;
	unsigned char *packet;
	struct bnx2x_fastpath *fp_rx = &bp->fp[0];
	struct bnx2x_fastpath *fp_tx = &bp->fp[0];
	struct bnx2x_fp_txdata *txdata = &fp_tx->txdata[0];
	u16 tx_start_idx, tx_idx;
	u16 rx_start_idx, rx_idx;
	u16 pkt_prod, bd_prod;
	struct sw_tx_bd *tx_buf;
	struct eth_tx_start_bd *tx_start_bd;
	struct eth_tx_parse_bd_e1x  *pbd_e1x = NULL;
	struct eth_tx_parse_bd_e2  *pbd_e2 = NULL;
	dma_addr_t mapping;
	union eth_rx_cqe *cqe;
	u8 cqe_fp_flags, cqe_fp_type;
	struct sw_rx_bd *rx_buf;
	u16 len;
	int rc = -ENODEV;
	u8 *data;
	struct netdev_queue *txq = netdev_get_tx_queue(bp->dev, txdata->txq_index);

	/* check the loopback mode */
	switch (loopback_mode) {
	case BNX2X_PHY_LOOPBACK:
		if (bp->link_params.loopback_mode != LOOPBACK_XGXS)
			return -EINVAL;
		break;
	case BNX2X_MAC_LOOPBACK:
		if (CHIP_IS_E3(bp)) {
			int cfg_idx = bnx2x_get_link_cfg_idx(bp);
			if (bp->port.supported[cfg_idx] &
			    (SUPPORTED_10000baseT_Full |
			     SUPPORTED_20000baseMLD2_Full |
			     SUPPORTED_20000baseKR2_Full))
				bp->link_params.loopback_mode = LOOPBACK_XMAC;
			else
				bp->link_params.loopback_mode = LOOPBACK_UMAC;
		} else
			bp->link_params.loopback_mode = LOOPBACK_BMAC;

		bnx2x_phy_init(&bp->link_params, &bp->link_vars);
		break;
	default:
		return -EINVAL;
	}

	/* prepare the loopback packet */
	pkt_size = (((bp->dev->mtu < ETH_MAX_PACKET_SIZE) ?
		     bp->dev->mtu : ETH_MAX_PACKET_SIZE) + ETH_HLEN);
	skb = netdev_alloc_skb(bp->dev, fp_rx->rx_buf_size);
	if (!skb) {
		rc = -ENOMEM;
		goto test_loopback_exit;
	}
	packet = skb_put(skb, pkt_size);
	memcpy(packet, bp->dev->dev_addr, ETH_ALEN);
	memset(packet + ETH_ALEN, 0, ETH_ALEN);
	memset(packet + 2*ETH_ALEN, 0x77, (ETH_HLEN - 2*ETH_ALEN));
	for (i = ETH_HLEN; i < pkt_size; i++)
		packet[i] = (unsigned char) (i & 0xff);
	mapping = dma_map_single(&bp->pdev->dev, skb->data,
				 skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&bp->pdev->dev, mapping))) {
		rc = -ENOMEM;
		dev_kfree_skb(skb);
		BNX2X_ERR("Unable to map SKB\n");
		goto test_loopback_exit;
	}

	/* send the loopback packet */
	num_pkts = 0;
	tx_start_idx = le16_to_cpu(*txdata->tx_cons_sb);
	rx_start_idx = le16_to_cpu(*fp_rx->rx_cons_sb);

	netdev_tx_sent_queue(txq, skb->len);

	pkt_prod = txdata->tx_pkt_prod++;
	tx_buf = &txdata->tx_buf_ring[TX_BD(pkt_prod)];
	tx_buf->first_bd = txdata->tx_bd_prod;
	tx_buf->skb = skb;
	tx_buf->flags = 0;

	bd_prod = TX_BD(txdata->tx_bd_prod);
	tx_start_bd = &txdata->tx_desc_ring[bd_prod].start_bd;
	tx_start_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
	tx_start_bd->addr_lo = cpu_to_le32(U64_LO(mapping));
	tx_start_bd->nbd = cpu_to_le16(2); /* start + pbd */
	tx_start_bd->nbytes = cpu_to_le16(skb_headlen(skb));
	tx_start_bd->vlan_or_ethertype = cpu_to_le16(pkt_prod);
	tx_start_bd->bd_flags.as_bitfield = ETH_TX_BD_FLAGS_START_BD;
	SET_FLAG(tx_start_bd->general_data,
		 ETH_TX_START_BD_ETH_ADDR_TYPE,
		 UNICAST_ADDRESS);
	SET_FLAG(tx_start_bd->general_data,
		 ETH_TX_START_BD_HDR_NBDS,
		 1);

	/* turn on parsing and get a BD */
	bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));

	pbd_e1x = &txdata->tx_desc_ring[bd_prod].parse_bd_e1x;
	pbd_e2 = &txdata->tx_desc_ring[bd_prod].parse_bd_e2;

	memset(pbd_e2, 0, sizeof(struct eth_tx_parse_bd_e2));
	memset(pbd_e1x, 0, sizeof(struct eth_tx_parse_bd_e1x));

	wmb();

	txdata->tx_db.data.prod += 2;
	barrier();
	DOORBELL(bp, txdata->cid, txdata->tx_db.raw);

	mmiowb();
	barrier();

	num_pkts++;
	txdata->tx_bd_prod += 2; /* start + pbd */

	udelay(100);

	tx_idx = le16_to_cpu(*txdata->tx_cons_sb);
	if (tx_idx != tx_start_idx + num_pkts)
		goto test_loopback_exit;

	/* Unlike HC IGU won't generate an interrupt for status block
	 * updates that have been performed while interrupts were
	 * disabled.
	 */
	if (bp->common.int_block == INT_BLOCK_IGU) {
		/* Disable local BHes to prevent a dead-lock situation between
		 * sch_direct_xmit() and bnx2x_run_loopback() (calling
		 * bnx2x_tx_int()), as both are taking netif_tx_lock().
		 */
		local_bh_disable();
		bnx2x_tx_int(bp, txdata);
		local_bh_enable();
	}

	rx_idx = le16_to_cpu(*fp_rx->rx_cons_sb);
	if (rx_idx != rx_start_idx + num_pkts)
		goto test_loopback_exit;

	cqe = &fp_rx->rx_comp_ring[RCQ_BD(fp_rx->rx_comp_cons)];
	cqe_fp_flags = cqe->fast_path_cqe.type_error_flags;
	cqe_fp_type = cqe_fp_flags & ETH_FAST_PATH_RX_CQE_TYPE;
	if (!CQE_TYPE_FAST(cqe_fp_type) || (cqe_fp_flags & ETH_RX_ERROR_FALGS))
		goto test_loopback_rx_exit;

	len = le16_to_cpu(cqe->fast_path_cqe.pkt_len);
	if (len != pkt_size)
		goto test_loopback_rx_exit;

	rx_buf = &fp_rx->rx_buf_ring[RX_BD(fp_rx->rx_bd_cons)];
	dma_sync_single_for_cpu(&bp->pdev->dev,
				   dma_unmap_addr(rx_buf, mapping),
				   fp_rx->rx_buf_size, DMA_FROM_DEVICE);
	data = rx_buf->data + NET_SKB_PAD + cqe->fast_path_cqe.placement_offset;
	for (i = ETH_HLEN; i < pkt_size; i++)
		if (*(data + i) != (unsigned char) (i & 0xff))
			goto test_loopback_rx_exit;

	rc = 0;

test_loopback_rx_exit:

	fp_rx->rx_bd_cons = NEXT_RX_IDX(fp_rx->rx_bd_cons);
	fp_rx->rx_bd_prod = NEXT_RX_IDX(fp_rx->rx_bd_prod);
	fp_rx->rx_comp_cons = NEXT_RCQ_IDX(fp_rx->rx_comp_cons);
	fp_rx->rx_comp_prod = NEXT_RCQ_IDX(fp_rx->rx_comp_prod);

	/* Update producers */
	bnx2x_update_rx_prod(bp, fp_rx, fp_rx->rx_bd_prod, fp_rx->rx_comp_prod,
			     fp_rx->rx_sge_prod);

test_loopback_exit:
	bp->link_params.loopback_mode = LOOPBACK_NONE;

	return rc;
}

static int bnx2x_test_loopback(struct bnx2x *bp)
{
	int rc = 0, res;

	if (BP_NOMCP(bp))
		return rc;

	if (!netif_running(bp->dev))
		return BNX2X_LOOPBACK_FAILED;

	bnx2x_netif_stop(bp, 1);
	bnx2x_acquire_phy_lock(bp);

	res = bnx2x_run_loopback(bp, BNX2X_PHY_LOOPBACK);
	if (res) {
		DP(NETIF_MSG_PROBE, "  PHY loopback failed  (res %d)\n", res);
		rc |= BNX2X_PHY_LOOPBACK_FAILED;
	}

	res = bnx2x_run_loopback(bp, BNX2X_MAC_LOOPBACK);
	if (res) {
		DP(NETIF_MSG_PROBE, "  MAC loopback failed  (res %d)\n", res);
		rc |= BNX2X_MAC_LOOPBACK_FAILED;
	}

	bnx2x_release_phy_lock(bp);
	bnx2x_netif_start(bp);

	return rc;
}

#define CRC32_RESIDUAL			0xdebb20e3

static int bnx2x_test_nvram(struct bnx2x *bp)
{
	static const struct {
		int offset;
		int size;
	} nvram_tbl[] = {
		{     0,  0x14 }, /* bootstrap */
		{  0x14,  0xec }, /* dir */
		{ 0x100, 0x350 }, /* manuf_info */
		{ 0x450,  0xf0 }, /* feature_info */
		{ 0x640,  0x64 }, /* upgrade_key_info */
		{ 0x708,  0x70 }, /* manuf_key_info */
		{     0,     0 }
	};
	__be32 buf[0x350 / 4];
	u8 *data = (u8 *)buf;
	int i, rc;
	u32 magic, crc;

	if (BP_NOMCP(bp))
		return 0;

	rc = bnx2x_nvram_read(bp, 0, data, 4);
	if (rc) {
		DP(NETIF_MSG_PROBE, "magic value read (rc %d)\n", rc);
		goto test_nvram_exit;
	}

	magic = be32_to_cpu(buf[0]);
	if (magic != 0x669955aa) {
		DP(NETIF_MSG_PROBE, "magic value (0x%08x)\n", magic);
		rc = -ENODEV;
		goto test_nvram_exit;
	}

	for (i = 0; nvram_tbl[i].size; i++) {

		rc = bnx2x_nvram_read(bp, nvram_tbl[i].offset, data,
				      nvram_tbl[i].size);
		if (rc) {
			DP(NETIF_MSG_PROBE,
			   "nvram_tbl[%d] read data (rc %d)\n", i, rc);
			goto test_nvram_exit;
		}

		crc = ether_crc_le(nvram_tbl[i].size, data);
		if (crc != CRC32_RESIDUAL) {
			DP(NETIF_MSG_PROBE,
			   "nvram_tbl[%d] crc value (0x%08x)\n", i, crc);
			rc = -ENODEV;
			goto test_nvram_exit;
		}
	}

test_nvram_exit:
	return rc;
}

/* Send an EMPTY ramrod on the first queue */
static int bnx2x_test_intr(struct bnx2x *bp)
{
	struct bnx2x_queue_state_params params = {0};

	if (!netif_running(bp->dev))
		return -ENODEV;

	params.q_obj = &bp->fp->q_obj;
	params.cmd = BNX2X_Q_CMD_EMPTY;

	__set_bit(RAMROD_COMP_WAIT, &params.ramrod_flags);

	return bnx2x_queue_state_change(bp, &params);
}

static void bnx2x_self_test(struct net_device *dev,
			    struct ethtool_test *etest, u64 *buf)
{
	struct bnx2x *bp = netdev_priv(dev);
	u8 is_serdes;
	if (bp->recovery_state != BNX2X_RECOVERY_DONE) {
		pr_err("Handling parity error recovery. Try again later\n");
		etest->flags |= ETH_TEST_FL_FAILED;
		return;
	}

	memset(buf, 0, sizeof(u64) * BNX2X_NUM_TESTS);

	if (!netif_running(dev))
		return;

	/* offline tests are not supported in MF mode */
	if (IS_MF(bp))
		etest->flags &= ~ETH_TEST_FL_OFFLINE;
	is_serdes = (bp->link_vars.link_status & LINK_STATUS_SERDES_LINK) > 0;

	if (etest->flags & ETH_TEST_FL_OFFLINE) {
		int port = BP_PORT(bp);
		u32 val;
		u8 link_up;

		/* save current value of input enable for TX port IF */
		val = REG_RD(bp, NIG_REG_EGRESS_UMP0_IN_EN + port*4);
		/* disable input for TX port IF */
		REG_WR(bp, NIG_REG_EGRESS_UMP0_IN_EN + port*4, 0);

		link_up = bp->link_vars.link_up;

		bnx2x_nic_unload(bp, UNLOAD_NORMAL);
		bnx2x_nic_load(bp, LOAD_DIAG);
		/* wait until link state is restored */
		bnx2x_wait_for_link(bp, 1, is_serdes);

		if (bnx2x_test_registers(bp) != 0) {
			buf[0] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
		if (bnx2x_test_memory(bp) != 0) {
			buf[1] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}

		buf[2] = bnx2x_test_loopback(bp);
		if (buf[2] != 0)
			etest->flags |= ETH_TEST_FL_FAILED;

		bnx2x_nic_unload(bp, UNLOAD_NORMAL);

		/* restore input for TX port IF */
		REG_WR(bp, NIG_REG_EGRESS_UMP0_IN_EN + port*4, val);

		bnx2x_nic_load(bp, LOAD_NORMAL);
		/* wait until link state is restored */
		bnx2x_wait_for_link(bp, link_up, is_serdes);
	}
	if (bnx2x_test_nvram(bp) != 0) {
		buf[3] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}
	if (bnx2x_test_intr(bp) != 0) {
		buf[4] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (bnx2x_link_test(bp, is_serdes) != 0) {
		buf[5] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

#ifdef BNX2X_EXTRA_DEBUG
	bnx2x_panic_dump(bp);
#endif
}

#define IS_PORT_STAT(i) \
	((bnx2x_stats_arr[i].flags & STATS_FLAGS_BOTH) == STATS_FLAGS_PORT)
#define IS_FUNC_STAT(i)		(bnx2x_stats_arr[i].flags & STATS_FLAGS_FUNC)
#define IS_MF_MODE_STAT(bp) \
			(IS_MF(bp) && !(bp->msg_enable & BNX2X_MSG_STATS))

/* ethtool statistics are displayed for all regular ethernet queues and the
 * fcoe L2 queue if not disabled
 */
static inline int bnx2x_num_stat_queues(struct bnx2x *bp)
{
	return BNX2X_NUM_ETH_QUEUES(bp);
}

static int bnx2x_get_sset_count(struct net_device *dev, int stringset)
{
	struct bnx2x *bp = netdev_priv(dev);
	int i, num_stats;

	switch (stringset) {
	case ETH_SS_STATS:
		if (is_multi(bp)) {
			num_stats = bnx2x_num_stat_queues(bp) *
						BNX2X_NUM_Q_STATS;
		} else
			num_stats = 0;
		if (IS_MF_MODE_STAT(bp)) {
			for (i = 0; i < BNX2X_NUM_STATS; i++)
				if (IS_FUNC_STAT(i))
					num_stats++;
		} else
			num_stats += BNX2X_NUM_STATS;

		return num_stats;

	case ETH_SS_TEST:
		return BNX2X_NUM_TESTS;

	default:
		return -EINVAL;
	}
}

static void bnx2x_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	struct bnx2x *bp = netdev_priv(dev);
	int i, j, k;
	char queue_name[MAX_QUEUE_NAME_LEN+1];

	switch (stringset) {
	case ETH_SS_STATS:
		k = 0;
		if (is_multi(bp)) {
			for_each_eth_queue(bp, i) {
				memset(queue_name, 0, sizeof(queue_name));
				sprintf(queue_name, "%d", i);
				for (j = 0; j < BNX2X_NUM_Q_STATS; j++)
					snprintf(buf + (k + j)*ETH_GSTRING_LEN,
						ETH_GSTRING_LEN,
						bnx2x_q_stats_arr[j].string,
						queue_name);
				k += BNX2X_NUM_Q_STATS;
			}
		}


		for (i = 0, j = 0; i < BNX2X_NUM_STATS; i++) {
			if (IS_MF_MODE_STAT(bp) && IS_PORT_STAT(i))
				continue;
			strcpy(buf + (k + j)*ETH_GSTRING_LEN,
				   bnx2x_stats_arr[i].string);
			j++;
		}

		break;

	case ETH_SS_TEST:
		memcpy(buf, bnx2x_tests_str_arr, sizeof(bnx2x_tests_str_arr));
		break;
	}
}

static void bnx2x_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *buf)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 *hw_stats, *offset;
	int i, j, k = 0;

	if (is_multi(bp)) {
		for_each_eth_queue(bp, i) {
			hw_stats = (u32 *)&bp->fp[i].eth_q_stats;
			for (j = 0; j < BNX2X_NUM_Q_STATS; j++) {
				if (bnx2x_q_stats_arr[j].size == 0) {
					/* skip this counter */
					buf[k + j] = 0;
					continue;
				}
				offset = (hw_stats +
					  bnx2x_q_stats_arr[j].offset);
				if (bnx2x_q_stats_arr[j].size == 4) {
					/* 4-byte counter */
					buf[k + j] = (u64) *offset;
					continue;
				}
				/* 8-byte counter */
				buf[k + j] = HILO_U64(*offset, *(offset + 1));
			}
			k += BNX2X_NUM_Q_STATS;
		}
	}

	hw_stats = (u32 *)&bp->eth_stats;
	for (i = 0, j = 0; i < BNX2X_NUM_STATS; i++) {
		if (IS_MF_MODE_STAT(bp) && IS_PORT_STAT(i))
			continue;
		if (bnx2x_stats_arr[i].size == 0) {
			/* skip this counter */
			buf[k + j] = 0;
			j++;
			continue;
		}
		offset = (hw_stats + bnx2x_stats_arr[i].offset);
		if (bnx2x_stats_arr[i].size == 4) {
			/* 4-byte counter */
			buf[k + j] = (u64) *offset;
			j++;
			continue;
		}
		/* 8-byte counter */
		buf[k + j] = HILO_U64(*offset, *(offset + 1));
		j++;
	}
}

static int bnx2x_set_phys_id(struct net_device *dev,
			     enum ethtool_phys_id_state state)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (!netif_running(dev))
		return -EAGAIN;

	if (!bp->port.pmf)
		return -EOPNOTSUPP;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */

	case ETHTOOL_ID_ON:
		bnx2x_set_led(&bp->link_params, &bp->link_vars,
			      LED_MODE_ON, SPEED_1000);
		break;

	case ETHTOOL_ID_OFF:
		bnx2x_set_led(&bp->link_params, &bp->link_vars,
			      LED_MODE_FRONT_PANEL_OFF, 0);

		break;

	case ETHTOOL_ID_INACTIVE:
		bnx2x_set_led(&bp->link_params, &bp->link_vars,
			      LED_MODE_OPER,
			      bp->link_vars.line_speed);
	}

	return 0;
}

static int bnx2x_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			   u32 *rules __always_unused)
{
	struct bnx2x *bp = netdev_priv(dev);

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = BNX2X_NUM_ETH_QUEUES(bp);
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static u32 bnx2x_get_rxfh_indir_size(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	return (bp->multi_mode == ETH_RSS_MODE_DISABLED ?
		0 : T_ETH_INDIRECTION_TABLE_SIZE);
}

static int bnx2x_get_rxfh_indir(struct net_device *dev, u32 *indir)
{
	struct bnx2x *bp = netdev_priv(dev);
	u8 ind_table[T_ETH_INDIRECTION_TABLE_SIZE] = {0};
	size_t i;

	/* Get the current configuration of the RSS indirection table */
	bnx2x_get_rss_ind_table(&bp->rss_conf_obj, ind_table);

	/*
	 * We can't use a memcpy() as an internal storage of an
	 * indirection table is a u8 array while indir->ring_index
	 * points to an array of u32.
	 *
	 * Indirection table contains the FW Client IDs, so we need to
	 * align the returned table to the Client ID of the leading RSS
	 * queue.
	 */
	for (i = 0; i < T_ETH_INDIRECTION_TABLE_SIZE; i++)
		indir[i] = ind_table[i] - bp->fp->cl_id;

	return 0;
}

static int bnx2x_set_rxfh_indir(struct net_device *dev, const u32 *indir)
{
	struct bnx2x *bp = netdev_priv(dev);
	size_t i;
	u8 ind_table[T_ETH_INDIRECTION_TABLE_SIZE] = {0};

	for (i = 0; i < T_ETH_INDIRECTION_TABLE_SIZE; i++) {
		/*
		 * The same as in bnx2x_get_rxfh_indir: we can't use a memcpy()
		 * as an internal storage of an indirection table is a u8 array
		 * while indir->ring_index points to an array of u32.
		 *
		 * Indirection table contains the FW Client IDs, so we need to
		 * align the received table to the Client ID of the leading RSS
		 * queue
		 */
		ind_table[i] = indir[i] + bp->fp->cl_id;
	}

	return bnx2x_config_rss_pf(bp, ind_table, false);
}

static const struct ethtool_ops bnx2x_ethtool_ops = {
	.get_settings		= bnx2x_get_settings,
	.set_settings		= bnx2x_set_settings,
	.get_drvinfo		= bnx2x_get_drvinfo,
	.get_regs_len		= bnx2x_get_regs_len,
	.get_regs		= bnx2x_get_regs,
	.get_wol		= bnx2x_get_wol,
	.set_wol		= bnx2x_set_wol,
	.get_msglevel		= bnx2x_get_msglevel,
	.set_msglevel		= bnx2x_set_msglevel,
	.nway_reset		= bnx2x_nway_reset,
	.get_link		= bnx2x_get_link,
	.get_eeprom_len		= bnx2x_get_eeprom_len,
	.get_eeprom		= bnx2x_get_eeprom,
	.set_eeprom		= bnx2x_set_eeprom,
	.get_coalesce		= bnx2x_get_coalesce,
	.set_coalesce		= bnx2x_set_coalesce,
	.get_ringparam		= bnx2x_get_ringparam,
	.set_ringparam		= bnx2x_set_ringparam,
	.get_pauseparam		= bnx2x_get_pauseparam,
	.set_pauseparam		= bnx2x_set_pauseparam,
	.self_test		= bnx2x_self_test,
	.get_sset_count		= bnx2x_get_sset_count,
	.get_strings		= bnx2x_get_strings,
	.set_phys_id		= bnx2x_set_phys_id,
	.get_ethtool_stats	= bnx2x_get_ethtool_stats,
	.get_rxnfc		= bnx2x_get_rxnfc,
	.get_rxfh_indir_size	= bnx2x_get_rxfh_indir_size,
	.get_rxfh_indir		= bnx2x_get_rxfh_indir,
	.set_rxfh_indir		= bnx2x_set_rxfh_indir,
};

void bnx2x_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &bnx2x_ethtool_ops);
}
