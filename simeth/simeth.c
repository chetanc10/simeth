
/**
 * simeth.c
 *
 * This is a simulated ethernet driver based on IVSHMEM.
 * The module can be inserted and used inside a VM while
 * host memory shared acts a register and descriptor handler
 * with a ethernet NIC engine in a software application on host
 *
 * Author:   ChetaN KS (chetan.kumarsanga@gmail.com)
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/pci.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/netdev_features.h>
#include <linux/if_ether.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/pkt_sched.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/if_macvlan.h>
#include <linux/if_bridge.h>
#include <linux/prefetch.h>
#include <scsi/fc/fc_fcoe.h>
#include <net/udp_tunnel.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>

#include "simeth.h"
#include "simeth_nic.h"

MODULE_AUTHOR ("ChetaN KS <chetan.kumar@gmail.com>");
MODULE_DESCRIPTION ("SIMulated ETHernet driver using IVSHMEM on VM \
		demonstrating simulated NIC engine on host userspace");
MODULE_LICENSE ("GPL");
MODULE_VERSION (SIMETH_VERSION);

#define MODULENAME "simeth"
#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV|NETIF_MSG_PROBE|NETIF_MSG_LINK)
static int debug = -1;
module_param (debug, int, 0);
MODULE_PARM_DESC (debug, "Debug level (0=none,...,16=all)");

typedef enum simeth_dev_region {
	SIMETH_BAR_0 = 0,
	SIMETH_BAR_1 = 1,
	SIMETH_BAR_2 = 2,
	SIMETH_BAR_3 = 3,
	SIMETH_BAR_4 = 4,
	SIMETH_BAR_5 = 5,
} simeth_dev_region_t;

static struct pci_device_id simeth_dev_ids[] = {
	simeth_pci_dev_id (0x1af4, 0x1110, SIMETH_BAR_2),
	{0,} /* sentinel */
};

static void simeth_remove (struct pci_dev *pci_dev)
{
	struct net_device *ndev = pci_get_drvdata (pci_dev);
	simeth_priv_t *simeth_priv = netdev_priv (ndev);

	pr_info ("Removing simeth\n");

	netif_napi_del (&simeth_priv->napi);
    unregister_netdev (ndev);
    free_netdev (ndev);

	iounmap (simeth_priv->ioaddr);
	pci_release_regions (pci_dev);
	pci_clear_master (pci_dev);
	pci_disable_device (pci_dev);
}

static int simeth_poll (struct napi_struct *napi, int budget)
{
	int ret = 0;

	pr_info ("simeth_poll\n");
	return ret;
}

static int simeth_ndo_open (struct net_device	*ndev)
{
	struct simeth_priv *simeth_priv = netdev_priv (ndev);
	pr_info (" simeth_ndo_open\n");
	napi_enable (&simeth_priv->napi);
    return 0;
}

static int simeth_ndo_stop (struct net_device *ndev)
{
	int ret = 0;
	struct simeth_priv *simeth_priv = netdev_priv (ndev);
	pr_info (" simeth_ndo_stop\n");
	napi_disable (&simeth_priv->napi);
    return ret;
}

static netdev_tx_t simeth_ndo_start_xmit (struct sk_buff *skb, struct net_device *ndev)
{
	int ret = 0, txst = 0;
	struct simeth_pcpustats *cpstats =
		&((struct simeth_priv *)netdev_priv (ndev))->cpstats;

    if (!skb) return (netdev_tx_t)0;

#if XMIT_IS_REAL
	if (simeth_xmit_mac_fn) {
		ret = simeth_xmit_mac_fn (skb);
		if (ret < 0) {/*check for -1/-2,etc err cases*/
			pr_err ("in simeth_xmit_mac, ret: %d\n", ret);
			txst = ret;
			ret = NETDEV_TX_BUSY;
		}
	}
#endif

	/* We may have to see if we need a work-scheduling/sync
	 * In case we do, lot of changes follow this line -TODO */
	if (!ret) { /* tx success */
		cpstats->tx_stats.packets += 1;
		cpstats->tx_stats.bytes += skb->len;
	} else { /* tx failed */
		switch (txst) {
			case -1: cpstats->tx_stats.dropped += 1; break;
			case -2: cpstats->tx_stats.errors += 1; break;
			default: pr_err ("in simeth_xmit_mac.. txst: %d\n", txst); break;
		}
	}

	dev_kfree_skb (skb);

    return ret;
}

static void simeth_ndo_get_stats64 (struct net_device *ndev, struct rtnl_link_stats64 *showstats)
{
	uint32_t start;
	struct simeth_priv *simeth_priv = netdev_priv (ndev);

	pr_info ("simeth_get_stats64\n");

	do {
		start = u64_stats_fetch_begin_irq (&simeth_priv->cpstats.rx_stats.syncp);
		showstats->rx_packets = simeth_priv->cpstats.rx_stats.packets;
		showstats->rx_bytes = simeth_priv->cpstats.rx_stats.bytes;
	} while (u64_stats_fetch_retry_irq (&simeth_priv->cpstats.rx_stats.syncp, start));

	do {
		start = u64_stats_fetch_begin_irq (&simeth_priv->cpstats.tx_stats.syncp);
		showstats->tx_packets = simeth_priv->cpstats.tx_stats.packets;
		showstats->tx_bytes = simeth_priv->cpstats.tx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&simeth_priv->cpstats.tx_stats.syncp, start));

	showstats->rx_dropped   = ndev->stats.rx_dropped;
	showstats->tx_dropped   = ndev->stats.tx_dropped;
	showstats->rx_length_errors = ndev->stats.rx_length_errors;
	showstats->rx_errors    = ndev->stats.rx_errors;
	showstats->rx_crc_errors    = ndev->stats.rx_crc_errors;
	showstats->rx_fifo_errors   = ndev->stats.rx_fifo_errors;
	showstats->rx_missed_errors = ndev->stats.rx_missed_errors;
}

static const struct net_device_ops simeth_netdev_ops = {
	.ndo_open = simeth_ndo_open,
	.ndo_stop = simeth_ndo_stop,
	.ndo_get_stats64 = simeth_ndo_get_stats64,
	.ndo_start_xmit = simeth_ndo_start_xmit,
	/*.ndo_tx_timeout = simeth_ndo_tx_timeout,*/
	/*.ndo_validate_addr = simeth_ndo_validate_addr,*/
	/*.ndo_change_mtu = simeth_ndo_change_mtu,*/
	/*.ndo_set_mac_address = simeth_ndo_set_mac_address,*/
	/*.ndo_do_ioctl = simeth_ndo_do_ioctl,*/
	/*.ndo_set_rx_mode = simeth_ndo_set_rx_mode,*/
#ifdef CONFIG_NET_POLL_CONTROLLER
	/*.ndo_poll_controller = simeth_ndo_poll_controller,*/
#endif
};

static uint8_t _dummy_simeth_mac[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static inline uint8_t simeth_mac_byte (int i)
{
	/* TODO - Prep a hw register set to allow read actual MAC
	 * and remove the dummy eth mac stuff! */
	return _dummy_simeth_mac[i];
}

static int simeth_probe (struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	int ret = 0;
	int i = 0;
	struct net_device *ndev = NULL;
	simeth_priv_t *simeth_priv = NULL;
	const unsigned int nic_bar_idx = (unsigned int)(id->driver_data);
	void __iomem *ioaddr;

	pr_info ("Probing simeth\n");

	/* PCI setup */
	ret = pci_enable_device (pci_dev);
	if (ret < 0) {
		pr_err ("ERROR simeth-enable-device, ret: %d\n", ret);
		return -ENODEV;
	} else {
		pr_info ("simeth-dev-irq: %d\n", pci_dev->irq);
	}

	if (pci_resource_len (pci_dev, nic_bar_idx) < SIMETH_BAR_SZ) {
		pr_err ("Required bar2 size: %llu\nDetected bar2 size: %llu\n", \
				(uint64_t)SIMETH_BAR_SZ, \
				(uint64_t)pci_resource_len (pci_dev, nic_bar_idx));
		return -ENOMEM;
	}

	if (pci_set_mwi (pci_dev) < 0)
		pr_warn ("pci_set_msi: unable to set MWI\n");
	if (!pci_is_pcie (pci_dev))
		pr_alert ("This's not PCIe!\n");

	ret = pci_request_regions (pci_dev, MODULENAME);
	if (ret < 0) {
		pr_err ("simeth-request-regions, ret: %d\n", ret);
		goto do_dis_dev;
	}

	/* pci-dma-mask-set comes here -TODO */

	/* ioremap here */
	ioaddr = ioremap (pci_resource_start(pci_dev, 2), \
			pci_resource_len (pci_dev, 2));
	if (!ioaddr) {
		pr_err ("Error ioremap-simethnet\n");
		ret = -1;
		goto do_rel_regions;
	}

	pci_set_master (pci_dev);

	ret = pci_save_state (pci_dev);
	if (ret < 0) {
		pr_err ("Saving pci state\n");
		goto do_iounmap;
	}

    ndev = alloc_etherdev (sizeof (*simeth_priv));
    if (!ndev) {
        pr_err ("Failed alloc-ether-simeth-dev\n");
        goto do_iounmap;
    }

	SET_NETDEV_DEV (ndev, &pci_dev->dev);

	ndev->netdev_ops = &simeth_netdev_ops;
	simeth_priv = netdev_priv (ndev);
	simeth_priv->netdev = ndev;
	simeth_priv->pci_dev = pci_dev;
	simeth_priv->msg_enable = netif_msg_init (debug, DEFAULT_MSG_ENABLE); /*Use this!! -TODO*/

	/* any more net __inits? -TODO */

	simeth_priv->ioaddr = ioaddr;

	/* MAC Address */
	for (i = 0; i < ETH_ALEN; i++) {
		ndev->dev_addr[i] = (unsigned char)simeth_mac_byte (i);
	}

	netif_napi_add (ndev, &simeth_priv->napi, simeth_poll, SIMETH_NAPI_WEIGHT);

	ndev->features |= NETIF_F_RXCSUM |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;

    ret = register_netdev (ndev);
    if (ret < 0) {
        pr_err ("Failed register-netdev, ret: %d\n", ret);
		goto do_napi_del;
    }

	pci_set_drvdata (pci_dev, ndev);

	return 0;

do_napi_del:
	netif_napi_del (&simeth_priv->napi);
    unregister_netdev (ndev);
	free_netdev (ndev);
do_iounmap:
	if (simeth_priv && simeth_priv->ioaddr) {
		iounmap (simeth_priv->ioaddr);
	}
do_rel_regions:
	pci_release_regions (pci_dev);
do_dis_dev:
	pci_clear_master (pci_dev);
	pci_disable_device (pci_dev);

	return ret;
}


static struct pci_driver simeth_drv = {
	.name = MODULENAME,
	.probe = simeth_probe,
	.remove = simeth_remove,
	.id_table = simeth_dev_ids,
};

static int __init simeth_init_module (void)
{
	int ret = 0;

	pr_info ("in %s. Version: %s\n", __func__, SIMETH_VERSION);

	ret = pci_register_driver (&simeth_drv);
	if (ret < 0) {
		printk ("ERROR pcic-drv-registration\n");
	}

	return ret;
}

static void __exit simeth_exit_module (void)
{
	pr_info ("%s\n", __func__);
	pci_unregister_driver (&simeth_drv);
}

module_init (simeth_init_module);
module_exit (simeth_exit_module);

