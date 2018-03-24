
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
#include "simnic_regs.h"

MODULE_AUTHOR ("ChetaN KS <chetan.kumar@gmail.com>");
MODULE_DESCRIPTION ("SIMulated ETHernet driver using IVSHMEM on VM \
		demonstrating simulated NIC engine on host userspace");
MODULE_LICENSE ("GPL");
MODULE_VERSION (SIMETH_VERSION);

#define MODULENAME "simeth"

typedef enum simeth_dev_region {
	SIMETH_DR_0 = 0,
	SIMETH_DR_1 = 1,
	SIMETH_DR_2 = 2,
	SIMETH_DR_3 = 3,
	SIMETH_DR_4 = 4,
	SIMETH_DR_5 = 5,
} simeth_dev_region_t;

static struct pci_device_id simeth_dev_ids[] = {
	{0x1af4, 0x1110, PCI_ANY_ID, PCI_ANY_ID, 0, 0, SIMETH_DR_2},
};

static void simeth_remove (struct pci_dev *pci_dev)
{
	struct net_device *ndev = pci_get_drvdata (pci_dev);
	simeth_drv_t *simeth_drv = netdev_priv (ndev);

	printk ("Removing simeth\n");

	netif_napi_del (&simeth_drv->napi);
    unregister_netdev (ndev);
    free_netdev (ndev);

	iounmap (simeth_drv->ioaddr);
	pci_release_regions (pci_dev);
	pci_clear_master (pci_dev);
	pci_disable_device (pci_dev);
}

static uint8_t _dummy_simeth_mac[] = {0x34, 0x23, 0x87, 0xdf, 0x49, 0xd2};
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
	simeth_drv_t *simeth_drv;
	void __iomem *ioaddr;

	printk ("Probing simeth\n");

    ndev = alloc_etherdev (sizeof (struct cnet_pcpustats));
    if (!ndev) {
        printk ("Failed alloc-ether-cnet-dev\n");
        return -ENOMEM;
    }

	SET_NETDEV_DEV (ndev, &pci_dev->dev);
	ndev->netdev_ops = &cnet_dev_ops;
	simeth_drv = netdev_priv (ndev);
	simeth_drv->ndev = ndev;

	/* any more net __inits? -TODO */

	/* PCI setup */
	ret = pci_enable_device (pci_dev);
	if (ret < 0) {
		printk ("ERROR simeth-enable-device\n");
		goto do_free_netdev;
	} else {
		printk ("simeth-dev-irq: %d\n", pci_dev->irq);
	}

	ret = pci_request_regions (pci_dev, MODULENAME);
	if (ret < 0) {
		printk ("ERROR simeth-request-regions\n");
		goto do_napi_del;
	}

	/* pci-dma-mask-set comes here -TODO */

	for (i = 0; i < 6; i++) {
		base_addr[i] = pci_resource_start (pci_dev, i);
		base_len[i] = pci_resource_len (pci_dev, i);
		if (base_addr[i]) {
			printk ("bar%d @ %8llx of %8llx(%llu) size\n", \
				i, base_addr[i], base_len[i], base_len[i]);
		} else {
			//break;
		}
	}

	/* ioremap here */
	ioaddr = ioremap(pci_resource_start(pci_dev, 2), \
			pci_resource_len (pci_dev, 2));
	if (!ioaddr) {
		printk ("Error ioremap-simethnet\n");
		ret = -1;
		goto do_rel_regions;
	}

	simeth_drv->ioaddr = ioaddr;

	pci_set_master (pci_dev);

	/* MAC Address */
	for (i = 0; i < ETH_ALEN; i++) {
		ndev->dev_addr[i] = (unsigned char)simeth_mac_byte (i);
	}

	netif_napi_add (ndev, &simeth_drv->napi, cnet_poll, CNET_NAPI_WEIGHT);

	ndev->features |= NETIF_F_RXCSUM |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;

    ret = register_netdev (ndev);
    if (ret < 0) {
        printk ("Failed register-netdev\n");
		goto do_napi_del;
    }

	pci_set_drvdata (pci_dev, ndev);

	return 0;

do_napi_del:
	netif_napi_del (&simeth_drv->napi);
	iounmap (simeth_drv->ioaddr);
do_rel_regions:
	pci_release_regions (pci_dev);
/*do_pci_dev_dis:*/
	pci_disable_device (pci_dev);
do_free_netdev:
	free_netdev (ndev);

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

	/* TODO - Ain't I mad enough to try create workq yet?*/
	return ret;
}

static void __exit simeth_exit_module (void)
{
	pr_info ("::%s\n", __func__);
}

module_init (simeth_init_module);
module_exit (simeth_exit_module);

