
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
#include <linux/pci.h>
#include <linux/netdevice.h>
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

typedef struct simeth_drv {
	struct net_device   *netdev;
	struct pci_dev      *pci_dev;

	uint32_t            n_txqs;
	uint32_t            n_rxqs;
	simeth_q_t          *tx_q;
	simeth_q_t          *rx_q;
} simeth_drv_t;

static int __init simeth_init_module (void)
{
	int ret = 0;

	pr_info ("::%s\n", __func__);

	return ret;
}

module_init (simeth_init_module);

static void __exit simeth_exit_module (void)
{
	pr_info ("::%s\n", __func__);
}

module_exit (simeth_exit_module);

