
/**
 * simeth.c
 *
 * This is the main driver file for the SIMulated ETHernet driver.
 * This is based on IVSHMEM during initial phase, later phases include
 * dma from host application and other enhancements, once basic simeth's up.
 *
 * The module can be inserted and used inside a VM while
 * host shared (IVSHMEM) memory shared acts as resource used
 * with a ethernet NIC engine in a software application on host
 *
 * Author:   ChetaN KS (chetan.kumarsanga@gmail.com)
 */

#include <linux/types.h>
#include <linux/pci-aspm.h>
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

#define SIMETH_DEF_MSG_EN (NETIF_MSG_DRV | \
		NETIF_MSG_PROBE | NETIF_MSG_LINK | \
		NETIF_MSG_IFDOWN | NETIF_MSG_IFUP | \
		NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR | \
		NETIF_MSG_HW)

static struct {
	uint32_t msg_enable;
} debugm = {(uint32_t)(-1)};
/*allow user/others to rd/wr debug control*/
module_param_named(debugm, debugm.msg_enable, int, 0660);
MODULE_PARM_DESC (debugm, "Debug level (0=none,...,16=all)");

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


static inline void _simeth_clean_adapter (simeth_adapter_t *adapter);
static void _simeth_release_qs (simeth_adapter_t *adapter);
static int _simeth_alloc_qs (simeth_adapter_t *adapter);
static void __used _simeth_irq_enable (simeth_adapter_t *adapter);
static void _simeth_irq_disable (simeth_adapter_t *adapter);
static int _simeth_setup_adapter (simeth_adapter_t *adapter);

static inline void _simeth_clean_adapter (simeth_adapter_t *adapter)
{
	_simeth_release_qs (adapter);
}

static void simeth_remove (struct pci_dev *pcidev)
{
	struct net_device *netdev = pci_get_drvdata (pcidev);
	simeth_adapter_t *adapter = netdev_priv (netdev);

	simeth_info (drv, "%s\n", __func__);

	netif_napi_del (&adapter->napi);
    unregister_netdev (netdev);
	simeth_release (iounmap, adapter->ioaddr);
	_simeth_clean_adapter (adapter);
    free_netdev (netdev);

	pci_release_regions (pcidev);
	pci_clear_master (pcidev);
	pci_disable_device (pcidev);
}

static int simeth_napi_rxpoll (struct napi_struct *napi, int budget)
{
	int ret = 0;
	simeth_adapter_t *adapter = container_of(napi, simeth_adapter_t, napi);

	simeth_dbg ("%s\n", __func__);
	return ret;
}

int (*simeth_xmit_mac_fn) (struct sk_buff *skb);

static int simeth_ndo_open (struct net_device	*netdev)
{
	simeth_adapter_t *adapter = netdev_priv (netdev);
	simeth_info (probe, "%s\n", __func__);
	napi_enable (&adapter->napi);
    return 0;
}

static int simeth_ndo_stop (struct net_device *netdev)
{
	int ret = 0;
	simeth_adapter_t *adapter = netdev_priv (netdev);
	simeth_info (drv, "%s\n", __func__);
	napi_disable (&adapter->napi);
    return ret;
}

static netdev_tx_t simeth_ndo_start_xmit (struct sk_buff *skb, struct net_device *netdev)
{
	int ret = 0, txst = 0;
	simeth_adapter_t *adapter = netdev_priv (netdev);
	struct simeth_pcpustats *cpstats = &adapter->cpstats;

    if (!skb) return (netdev_tx_t)0;

	simeth_info (drv, "%s\n", __func__);

#if XMIT_IS_REAL
	if (simeth_xmit_mac_fn) {
		ret = simeth_xmit_mac_fn (skb);
		if (ret < 0) {/*check for -1/-2,etc err cases*/
			simeth_err (tx_err, "in simeth_xmit_mac, ret: %d\n", ret);
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
			default: simeth_err (tx_err, "%s txst: %d\n", __func__, txst);
					 break;
		}
	}

	dev_kfree_skb (skb);

    return ret;
}

static void simeth_ndo_get_stats64 (struct net_device *netdev, struct rtnl_link_stats64 *showstats)
{
	uint32_t start;
	simeth_adapter_t *adapter = netdev_priv (netdev);

	/*This log should be seen in dmesg with level 8 on printk in proc -TODO*/
	simeth_dbg ("%s\n", __func__);

	do {
		start = u64_stats_fetch_begin_irq (&adapter->cpstats.rx_stats.syncp);
		showstats->rx_packets = adapter->cpstats.rx_stats.packets;
		showstats->rx_bytes = adapter->cpstats.rx_stats.bytes;
	} while (u64_stats_fetch_retry_irq (&adapter->cpstats.rx_stats.syncp, start));

	do {
		start = u64_stats_fetch_begin_irq (&adapter->cpstats.tx_stats.syncp);
		showstats->tx_packets = adapter->cpstats.tx_stats.packets;
		showstats->tx_bytes = adapter->cpstats.tx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&adapter->cpstats.tx_stats.syncp, start));

	showstats->rx_dropped   = netdev->stats.rx_dropped;
	showstats->tx_dropped   = netdev->stats.tx_dropped;
	showstats->rx_length_errors = netdev->stats.rx_length_errors;
	showstats->rx_errors    = netdev->stats.rx_errors;
	showstats->rx_crc_errors    = netdev->stats.rx_crc_errors;
	showstats->rx_fifo_errors   = netdev->stats.rx_fifo_errors;
	showstats->rx_missed_errors = netdev->stats.rx_missed_errors;
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

static inline uint8_t simeth_mac_byte (int i)
{
	static uint8_t _dummy_simeth_mac[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	/* TODO - Prep a hw register set to allow read actual MAC
	 * and remove the dummy eth mac stuff! */
	return _dummy_simeth_mac[i];
}

static void _setup_ethtool_ops (struct net_device *netdev)
{
	simeth_adapter_t *adapter = netdev_priv (netdev);
	simeth_info (probe, "%s TODO\n", __func__); /*TODO - setup ethtool ops*/
}

static void _simeth_release_qs (simeth_adapter_t *adapter)
{
	simeth_release (kfree, adapter->txq);
	simeth_release (kfree, adapter->rxq);
}

static int _simeth_alloc_qs (simeth_adapter_t *adapter)
{
	adapter->txq = kcalloc (adapter->n_txqs, 
			sizeof (simeth_txq_t), GFP_KERNEL);
	if (!adapter->txq) {
		simeth_err (probe, "kcalloc (adapter->txq) failed\n");
		return -ENOMEM;
	}

	adapter->rxq = kcalloc (adapter->n_rxqs, 
			sizeof (simeth_rxq_t), GFP_KERNEL);
	if (!adapter->rxq) {
		simeth_err (probe, "kcalloc (adapter->rxq) failed\n");
		simeth_release (kfree, adapter->txq);
		return -ENOMEM;
	}

	return 0;
}

static void __used _simeth_irq_enable (simeth_adapter_t *adapter)
{
	/* TODO - reset mask bit or do something similar
	 * to enable interrupt generation on this device */
	/*writel and then readl*/
}

static void _simeth_irq_disable (simeth_adapter_t *adapter)
{
	/* TODO - set mask bit or do something similar
	 * to disable interrupt generation on this device */
	/*writel and then readl*/
	/*synchronize_irq (adapter->pcidev->irq);*/
}

static int _simeth_setup_adapter (simeth_adapter_t *adapter)
{
	int ret = 0;

	adapter->rx_buflen = MAX_ETH_VLAN_SZ;

	adapter->n_txqs = 1;
	adapter->n_rxqs = 1;

	ret = _simeth_alloc_qs (adapter);
	if (ret) return ret;

	_simeth_irq_disable (adapter);
	return ret;
}

static int simeth_probe (struct pci_dev *pcidev, const struct pci_device_id *id)
{
	int ret = 0;
	int i = 0;
	struct net_device *netdev = NULL;
	simeth_adapter_t *adapter = NULL;
	const unsigned int nic_bar_idx = (unsigned int)(id->driver_data);
	void __iomem *ioaddr;

	if (netif_msg_drv(&debugm)) {
		pr_info ("Probing %s Ethernet driver, Version %s\n", \
				MODULENAME, SIMETH_VERSION);
	}

    netdev = alloc_etherdev (sizeof (*adapter));
    if (!netdev) {
        pr_err ("Failed alloc-ether-simeth-dev\n");
        return -ENOMEM;
    }

	SET_NETDEV_DEV (netdev, &pcidev->dev);

	netdev->netdev_ops = &simeth_netdev_ops;
	adapter = netdev_priv (netdev);
	adapter->netdev = netdev;
	adapter->pcidev = pcidev;
	adapter->msg_enable = netif_msg_init (debugm.msg_enable, SIMETH_DEF_MSG_EN);

	/* If aspm needs to be disabled, now is the time before enabling device!
	 * Some devices may be unable to handle aspm power states like l0s, l1 
	 * properly and may effect the functionality of driver or system */
	pci_disable_link_state (pcidev, \
			PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1 | PCIE_LINK_STATE_CLKPM);

	/* PCI setup */
	ret = pci_enable_device (pcidev);
	if (ret < 0) {
		simeth_crit (probe, "ERROR simeth-enable-device, ret: %d\n", ret);
		goto do_free_netdev;
	} else {
		simeth_info (probe, "simeth-dev-irq: %d\n", pcidev->irq);
	}

	if (pci_resource_len (pcidev, nic_bar_idx) < SIMETH_BAR_SZ) {
		simeth_emerg (probe, "Required bar2 size: %llu\nDetected bar2 size: %llu\n", \
				(uint64_t)SIMETH_BAR_SZ, \
				(uint64_t)pci_resource_len (pcidev, nic_bar_idx));
		ret = -EIO;
		goto do_dis_dev;
	}

	if (pci_set_mwi (pcidev) < 0)
		simeth_warn (probe, "pci_set_msi: unable to set MWI\n");
	if (!pci_is_pcie (pcidev))
		simeth_notice (probe, "This's not PCIe!\n");

	ret = pci_request_regions (pcidev, MODULENAME);
	if (ret < 0) {
		simeth_err (probe, "simeth-request-regions, ret: %d\n", ret);
		goto do_dis_dev;
	}

	/* pci-dma-mask-settings, even this's simeth just try
	 * setting dma-mask and discard errors for time being */
	ret = pci_set_dma_mask (pcidev, DMA_BIT_MASK (64));
	if (ret) {
		simeth_warn (probe, "error pci_set_dma_mask-64: %d", ret);
		ret = pci_set_dma_mask (pcidev, DMA_BIT_MASK (32));
		if (ret) {
			simeth_warn (probe, "error pci_set_dma_mask-32: %d", ret);
			/*goto do_dis_dev;*/
		}
	}

	/* ioremap here */
	ioaddr = ioremap (pci_resource_start(pcidev, 2), \
			pci_resource_len (pcidev, 2));
	if (!ioaddr) {
		simeth_err (probe, "Error ioremap-simethnet\n");
		ret = -1;
		goto do_rel_regions;
	}

	/* set bus-mastering for the device */
	pci_set_master (pcidev);

	ret = pci_save_state (pcidev);
	if (ret < 0) {
		simeth_err (probe, "Saving pci state\n");
		goto do_clear_master;
	}

	adapter->ioaddr = ioaddr;

	/* MAC Address */
	for (i = 0; i < ETH_ALEN; i++) {
		netdev->dev_addr[i] = (unsigned char)simeth_mac_byte (i);
	}

	netdev->features |= NETIF_F_RXCSUM |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;

	_setup_ethtool_ops (netdev);

	netif_napi_add (netdev, &adapter->napi, simeth_napi_rxpoll, SIMETH_NAPI_WEIGHT);

	/*netdev->watchdog_timeo = 5*HZ; TODO - add tx-timeout handler*/

	pci_set_drvdata (pcidev, netdev);

	ret = _simeth_setup_adapter (adapter);
    if (ret < 0) {
        simeth_crit (probe, "Failed adapter_setup: %d\n", ret);
		goto do_napi_del;
    }

    ret = register_netdev (netdev);
    if (ret < 0) {
        simeth_crit (probe, "Failed register-netdev, ret: %d\n", ret);
		goto do_clean_adapter;
    }

	simeth_info (probe, "simeth setup done!");

	return 0;

do_clean_adapter:
	_simeth_clean_adapter (adapter);
do_napi_del:
	netif_napi_del (&adapter->napi);
do_clear_master:
	pci_clear_master (pcidev);
/*do_iounmap:*/
	iounmap (adapter->ioaddr);
do_rel_regions:
	pci_release_regions (pcidev);
do_dis_dev:
	pci_disable_device (pcidev);
do_free_netdev:
	free_netdev (netdev);

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

	pr_info ("%s\n", __func__);

	ret = pci_register_driver (&simeth_drv);
	if (ret < 0) {
		pr_crit ("ERROR pcic-drv-registration for simeth\n");
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

