
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
#include "simeth_common.h"

MODULE_AUTHOR ("ChetaN KS <chetan.kumar@gmail.com>");
MODULE_DESCRIPTION ("SIMulated ETHernet driver using IVSHMEM on VM \
		demonstrating simulated NIC engine on host userspace");
MODULE_LICENSE ("GPL");
MODULE_VERSION (SIMETH_VER_0);

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
module_param_named (debugm, debugm.msg_enable, int, 0660);
MODULE_PARM_DESC (debugm, "Debug level (0=none,...,16=all)");

/*Module parameter for Tx descriptor count per Tx queue*/
static uint32_t g_n_txds = 64;
module_param_named (g_n_txds, g_n_txds, int, 0660);
MODULE_PARM_DESC (g_n_txds, "Per Queue Tx Descriptor count: 32-512, default 64; Must be aligned as per simeth.h");

/*Module parameter for Rx descriptor count per Rx queue*/
static uint32_t g_n_rxds = 64;
module_param_named (g_n_rxds, g_n_rxds, int, 0660);
MODULE_PARM_DESC (g_n_rxds, "Per Queue Rx Descriptor count: 32-512, default 64; Must be aligned as per simeth.h");

typedef enum simeth_dev_region {
	SIMETH_BAR_0 = 0,
	SIMETH_BAR_1 = 1,
	SIMETH_BAR_2 = 2,
	SIMETH_BAR_3 = 3,
	SIMETH_BAR_4 = 4,
	SIMETH_BAR_5 = 5,
} simeth_dev_region_t;

static const struct pci_device_id simeth_dev_ids[] = {
	simeth_pci_dev_id (0x1af4, 0x1110, SIMETH_BAR_2),
	{0,} /* sentinel */
};

int (*simeth_xmit_mac_fn) (struct sk_buff *skb);

#define _simeth_clean_txq(a, q) _simeth_clean_q (a, q, 0)
#define _simeth_clean_rxq(a, q) _simeth_clean_q (a, q, 1)
static void _simeth_clean_q (simeth_adapter_t *adapter, simeth_q_t *q, int is_rxq);
static void _simeth_clean_txqs (simeth_adapter_t *adapter);
static void _simeth_clean_rxqs (simeth_adapter_t *adapter);

static void _simeth_config_tx_engine (simeth_adapter_t *adapter, int q_idx);
static void _simeth_config_rx_engine (simeth_adapter_t *adapter, int q_idx);
static void _simeth_config_engines (simeth_adapter_t *adapter);

static void _simeth_stop_sw (simeth_adapter_t *adapter);
static void simeth_down (simeth_adapter_t *adapter);

#define _simeth_setup_txq(a, q, nd) _simeth_setup_q (a, q, nd, 0)
#define _simeth_setup_rxq(a, q, nd) _simeth_setup_q (a, q, nd, 1)
static int _simeth_setup_q (simeth_adapter_t *adapter, simeth_q_t *q, uint32_t n_desc, int is_rxq);
static int _simeth_setup_rxqs (simeth_adapter_t *adapter);
static int _simeth_setup_txqs (simeth_adapter_t *adapter);

static void _setup_ethtool_ops (struct net_device *netdev);

static inline void _simeth_clean_adapter (simeth_adapter_t *adapter);
static int _simeth_setup_adapter (simeth_adapter_t *adapter);

static void _simeth_release_qs (simeth_adapter_t *adapter);
static int _simeth_alloc_qs (simeth_adapter_t *adapter);

static void __used _simeth_irq_enable (simeth_adapter_t *adapter);
static void _simeth_irq_disable (simeth_adapter_t *adapter);

static void _simeth_init_hw (simeth_adapter_t *adapter);
static void _simeth_reset_hw (simeth_adapter_t *adapter);
static void _simeth_init_mdio_ops (simeth_adapter_t *adapter);
static int _simeth_get_valid_mac_addr (simeth_adapter_t *adapter);

static void _simeth_adjust_descq_count (void);

#if SIMETH_EN_DMA_MAPS
#define _simeth_dma_map_skb(dev, va, sz, dir) \
	dma_map_single ((dev), (va), (sz), (dir))
#define _simeth_dma_unmap_skb(dev, dma, sz, dir) \
	dma_unmap_single ((dev), (dma), (sz), (dir))
#else
#define _simeth_dma_map_skb(dev, va, sz, dir) virt_to_phys ((va))
#define _simeth_dma_unmap_skb(dev, dma, sz, dir)
#endif

static inline void _simeth_clean_adapter (simeth_adapter_t *adapter)
{
	_simeth_release_qs (adapter);
}

static void simeth_remove (struct pci_dev *pcidev)
{
	struct net_device *netdev = pci_get_drvdata (pcidev);
	simeth_adapter_t *adapter = netdev_priv (netdev);

	simeth_info (drv, "%s\n", __func__);

    unregister_netdev (netdev);
	netif_napi_del (&adapter->napi);
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

static int _simeth_setup_q (simeth_adapter_t *adapter, simeth_q_t *q, uint32_t n_desc, int is_rxq)
{
	int ret = 0;
	uint32_t size = 0;
	struct pci_dev *pcidev = adapter->pcidev;
	void *mem;

	/*Clean this q first*/
	memset (q, 0, sizeof (*q));

	/*Allocate aligned buf holder ring*/
	size = n_desc * (is_rxq ? sizeof (simeth_rx_buf_t) : \
			sizeof (simeth_tx_buf_t));
	mem = vzalloc (size);
	if (unlikely (!mem)) {
		simeth_err (drv, "%cxq->bring vzalloc failed", \
				is_rxq?'r':'t');
		return -ENOMEM;
	}
	q->bring_sz = size;
	q->bring = mem;

	/*Allocate aligned desc ring*/
	size = n_desc * sizeof (simeth_desc_t);
	size = ALIGN (size, PAGE_SIZE);
	mem = dma_alloc_coherent (&pcidev->dev, \
			size, &q->dring_dma_addr, GFP_KERNEL);
	if (unlikely (!mem)) {
		simeth_err (drv, "%cxq->dring dma_alloc_coherent failed", \
				is_rxq?'r':'t');
		vfree (q->bring);
		return -ENOMEM;
	}
	q->dring_sz = size;
	q->dring = mem;

	/*If we need a 32K/64K boundary check, do it later -XXX*/

	q->n_desc = n_desc;

	return ret;
}

static int _simeth_setup_rxqs (simeth_adapter_t *adapter)
{
	int i, ret = 0;
	simeth_rxq_t *rxq = adapter->rxq;

	for (i = 0; i < adapter->n_rxqs; i++) {
		ret = _simeth_setup_rxq (adapter, rxq + i, g_n_rxds);
		if (unlikely (ret)) {
			for (; i; i--) {
				_simeth_clean_rxq (adapter, rxq + i);
			}
			break;
		}
	}

	return ret;
}

static int _simeth_setup_txqs (simeth_adapter_t *adapter)
{
	int i, ret = 0;
	simeth_txq_t *txq = adapter->txq;

	for (i = 0; i < adapter->n_txqs; i++) {
		ret = _simeth_setup_txq (adapter, txq + i, g_n_txds);
		if (unlikely (ret)) {
			for (; i; i--) {
				_simeth_clean_txq (adapter, txq + i);
			}
			break;
		}
	}

	return ret;
}

static void _simeth_config_tx_engine (simeth_adapter_t *adapter, int q_idx)
{
	simeth_txq_t *txq = adapter->txq;
	dma_addr_t txd_base = txq->dring_dma_addr;

	simeth_sym_void (txd_base);
	/*TODO - configure engine*/
}

static void _simeth_config_rx_engine (simeth_adapter_t *adapter, int q_idx)
{
	simeth_rxq_t *rxq = adapter->rxq;
	dma_addr_t rxd_base = rxq->dring_dma_addr;

	simeth_sym_void (rxd_base);
	/*TODO - configure engine*/
}

static void _simeth_config_engines (simeth_adapter_t *adapter)
{
	int i;

	for (i = 0; i < adapter->n_txqs; i++) {
		_simeth_config_tx_engine (adapter, i);
	}
	for (i = 0; i < adapter->n_rxqs; i++) {
		_simeth_config_rx_engine (adapter, i);
	}
}

static int simeth_ndo_open (struct net_device	*netdev)
{
	int ret = 0;
	simeth_adapter_t *adapter = netdev_priv (netdev);

	simeth_info (probe, "%s\n", __func__);

	/*pm_runtime_get_sync (&pdev->dev);// What am I doing here? -TODO */

	/*netif_carrier_off(netdev);*/

	ret = _simeth_setup_txqs (adapter);
	if (ret) {
		simeth_err (drv, "_simeth_setup_txqs failed: %d\n", ret);
		return ret;
	}

	ret = _simeth_setup_rxqs (adapter);
	if (ret) {
		simeth_err (drv, "_simeth_setup_rxqs failed: %d\n", ret);
		goto do_rel_txqs;
	}

	/*full-power up the phy -TODO*/

	_simeth_config_engines (adapter);

	napi_enable (&adapter->napi);

    return 0;

do_rel_txqs:
	_simeth_clean_txqs (adapter);
    return ret;
}

static void __used _simeth_rel_tx_buf (simeth_adapter_t *adapter, simeth_tx_buf_t *buf)
{
	if (buf->dma_addr) {
		_simeth_dma_unmap_skb (&adapter->pcidev->dev, \
				buf->dma_addr, buf->n_bytes, DMA_TO_DEVICE);
		buf->dma_addr = 0;
	}

	if (buf->skb) {
		dev_kfree_skb_any (buf->skb);
		buf->skb = 0;
	}
}

static void __used _simeth_rel_rx_buf (simeth_adapter_t *adapter, simeth_rx_buf_t *buf)
{
	if (buf->dma_addr) {
		_simeth_dma_unmap_skb (&adapter->pcidev->dev, \
				buf->dma_addr, buf->n_bytes, DMA_TO_DEVICE);
		buf->dma_addr = 0;
	}

	if (buf->skb) {
		dev_kfree_skb_any (buf->skb);
		buf->skb = 0;
	}
}

static void _simeth_clean_q (simeth_adapter_t *adapter, simeth_q_t *q, int is_rxq)
{
	void *buf = q->bring;
	int i;

	if (is_rxq) {
		for (i = 0; i < q->n_desc; i++) {
			_simeth_rel_rx_buf (adapter, buf + i);
		}
	} else {
		for (i = 0; i < q->n_desc; i++) {
			_simeth_rel_rx_buf (adapter, buf + i);
		}
	}
}

static void _simeth_clean_txqs (simeth_adapter_t *adapter)
{
	int i;
	simeth_txq_t *txq = adapter->txq;

	for (i = 0; i < adapter->n_txqs; i++) {
		_simeth_clean_txq (adapter, txq + i);
	}
}

static void _simeth_clean_rxqs (simeth_adapter_t *adapter)
{
	int i;
	simeth_rxq_t *rxq = adapter->rxq;

	for (i = 0; i < adapter->n_rxqs; i++) {
		_simeth_clean_rxq (adapter, rxq + i);
	}
}

static void _simeth_stop_sw (simeth_adapter_t *adapter)
{
	/*If tasklets/workqueues are set, cancel'em all -TODO*/
}

static void simeth_down (simeth_adapter_t *adapter)
{
	struct net_device *netdev = adapter->netdev;

	/*netif_carrier_off (netdev);*/

	/*Disable rx engine - write stop to rxctl -TODO*/

	netif_tx_disable (netdev);

	/*Disable tx engine -write stop to tcxtl -TODO*/
	msleep (10);

	napi_disable (&adapter->napi);

	_simeth_irq_disable (adapter);

	_simeth_stop_sw (adapter);

	_simeth_reset_hw (adapter);

	_simeth_clean_txqs (adapter);
	_simeth_clean_rxqs (adapter);
}

static int simeth_ndo_stop (struct net_device *netdev)
{
	int ret = 0;
	simeth_adapter_t *adapter = netdev_priv (netdev);

	simeth_info (drv, "%s\n", __func__);

	simeth_down (adapter);

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
	static uint8_t _dummy_simeth_mac[] = {0x80, 0xce, 0x62, 0x10, 0x95, 0x2c};
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
	/*Only 1 txq/rxq supported*/
	if (unlikely (adapter->n_txqs != 1)) {
		simeth_crit (drv, "n_txqs(%d) != 1\n", adapter->n_txqs);
		return -EINVAL;
	}
	if (unlikely (adapter->n_rxqs != 1)) {
		simeth_crit (drv, "n_rxqs(%d) != 1\n", adapter->n_rxqs);
		return -EINVAL;
	}

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

static void _simeth_init_hw (simeth_adapter_t *adapter)
{
	simeth_info (probe, "%s\n", __func__);
}

static void _simeth_reset_hw (simeth_adapter_t *adapter)
{
	simeth_info (drv, "%s\n", __func__);
}

static void _simeth_init_mdio_ops (simeth_adapter_t *adapter)
{
	simeth_info (drv, "%s\n", __func__);
}

static int _simeth_get_valid_mac_addr (simeth_adapter_t *adapter)
{
	uint8_t mac_addr[ETH_ALEN];

	mac_addr[0] = (unsigned char)simeth_mac_byte (0);
	mac_addr[1] = (unsigned char)simeth_mac_byte (1);
	mac_addr[2] = (unsigned char)simeth_mac_byte (2);
	mac_addr[3] = (unsigned char)simeth_mac_byte (3);
	mac_addr[4] = (unsigned char)simeth_mac_byte (4);
	mac_addr[5] = (unsigned char)simeth_mac_byte (5);

	if (is_valid_ether_addr (mac_addr)) {
		simeth_info (hw, "simeth_hw_mac_addr: %pM\n", mac_addr);
		memcpy (adapter->mac_addr, mac_addr, ETH_ALEN);
		return 0;
	} else {
		simeth_err (hw, "INVALID simeth_hw_mac_addr: %pM\n", mac_addr);
		return -EINVAL;
	}
}

static void _simeth_adjust_descq_count (void)
{
	if (unlikely ((g_n_txds < 32) || (g_n_txds > 512))) {
		pr_warn ("Param n_txds(%u) out of range(32 to 512). Defaulting to 64\n", g_n_txds);
		g_n_txds = 64;
	} else if (unlikely (g_n_txds % SIMETH_DESC_RING_ALIGNER)) {
		uint32_t _adjust = g_n_txds - (g_n_txds % SIMETH_DESC_RING_ALIGNER);
		pr_warn ("Param n_txds(%u) is not aligned to %lu. Rounding down to: %u\n", \
				g_n_txds, SIMETH_DESC_RING_ALIGNER, _adjust);
		g_n_txds =_adjust;
	}
	if (unlikely ((g_n_rxds < 32) || (g_n_rxds > 512))) {
		pr_warn ("Param n_rxds(%u) out of range(32 to 512). Defaulting to 64\n", g_n_rxds);
		g_n_rxds = 64;
	} else if (unlikely (g_n_rxds % SIMETH_DESC_RING_ALIGNER)) {
		uint32_t _adjust = g_n_rxds - (g_n_rxds % SIMETH_DESC_RING_ALIGNER);
		pr_warn ("Param n_rxds(%u) is not aligned to %lu. Rounding down to: %u\n", \
				g_n_rxds, SIMETH_DESC_RING_ALIGNER, _adjust);
		g_n_rxds = _adjust;
	}
}

static int simeth_probe (struct pci_dev *pcidev, const struct pci_device_id *id)
{
	int ret = 0;
	struct net_device *netdev = NULL;
	simeth_adapter_t *adapter = NULL;
	const unsigned int nic_bar_idx = (unsigned int)(id->driver_data);
	void __iomem *ioaddr;

	if (netif_msg_drv(&debugm)) {
		pr_info ("Probing %s Ethernet driver, Version %s\n", \
				MODULENAME, SIMETH_VER_0);
	}

	_simeth_adjust_descq_count ();

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
		ret = -ENOMEM;
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
	if (ret < 0) {
		simeth_warn (probe, "error pci_set_dma_mask-64: %d", ret);
		ret = pci_set_dma_mask (pcidev, DMA_BIT_MASK (32));
		if (ret < 0) {
			simeth_warn (probe, "error pci_set_dma_mask-32: %d", ret);
			/*goto do_dis_dev;*/
		}
	}

	/* ioremap here */
	ioaddr = ioremap (pci_resource_start(pcidev, 2), \
			pci_resource_len (pcidev, 2));
	if (!ioaddr) {
		simeth_err (probe, "Error ioremap-simethnet\n");
		ret = -ENOMEM;
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

	/* get valid MAC Address or get out of here */
	if (_simeth_get_valid_mac_addr (adapter) == 0) {
		memcpy (adapter->netdev->dev_addr, adapter->mac_addr, ETH_ALEN);
	}
	_setup_ethtool_ops (netdev);

	netif_napi_add (netdev, &adapter->napi, simeth_napi_rxpoll, SIMETH_NAPI_WEIGHT);

	/*netdev->watchdog_timeo = 5*HZ; TODO - add tx-timeout handler*/

	pci_set_drvdata (pcidev, netdev);

	ret = _simeth_setup_adapter (adapter);
    if (ret < 0) {
        simeth_crit (probe, "Failed adapter_setup: %d\n", ret);
		goto do_napi_del;
    }

	_simeth_init_mdio_ops (adapter);

	_simeth_init_hw (adapter);

	/*setup hardware feature flags someday! -TODO*/
	netdev->features = 0;
	netdev->hw_features = 0;
	netdev->vlan_features = 0;

	/*set minimum and maximum mtu values for this netdev*/
	netdev->min_mtu = ETH_ZLEN - ETH_HLEN;
	netdev->max_mtu = MAX_JUMBO_FRAME_SIZE - ETH_HLEN - ETH_FCS_LEN;
	if (netdev->vlan_features) { /*What?? -TODO*/
		netdev->min_mtu -= VLAN_HLEN;
		netdev->max_mtu -= VLAN_HLEN;
	}

	/*wake on lan settings? -TODO*/

	/*other hw feature listing/setup/updates etc -TODO*/

	_simeth_reset_hw (adapter);

	/*strcpy (netdev->name, "eth%d"); // not valid anymore; udev decides netdev-names */

    ret = register_netdev (netdev);
    if (ret < 0) {
        simeth_crit (probe, "Failed register-netdev, ret: %d\n", ret);
		goto do_clean_adapter;
    }

	/*TODO- Disable carrier, we'll enable after ifup happens via open call*/
	/*netif_carrier_off(netdev);*/

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

