
#ifndef __SIMETH_H
#define __SIMETH_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/u64_stats_sync.h>
#include <linux/netdevice.h>

#include "simeth_nic.h"

/* simeth driver version */
#define SIMETH_VER_0 "0.1"

/* If a real xmit is going to happen, this shall be 1, else 0 (dummy) */
#define XMIT_IS_REAL 1

/* If we need actual dma_mapping of skbs, set to 1, else reset to 0 (dummy) */
#define SIMETH_EN_DMA_MAPS 0

/* NAPI Poll weight */
#define SIMETH_NAPI_WEIGHT 1

/* pci_device_id structure entry macro */
#define simeth_pci_dev_id(vend, dev, drv_data) \
{ PCI_DEVICE(vend, dev), .driver_data = drv_data, }

/* Minimum size of the IVSHMEM bar for simeth to function as expected */
#define SIMETH_BAR_SZ (512 * 1024 * 1024)

/* Maximum size of rx buffer with VLAN tag generally 1518 + 4 */
#define MAX_ETH_VLAN_SZ 1522

/* Maximum jumbo frame size including all types of headers */
#define MAX_JUMBO_FRAME_SIZE 4096

/* Desc count alignment
 * Mind cache lines, alignment for io desc & result desc ring bases
 * this value makes sense for now */
#define SIMETH_DMA_REGION_ALIGNER 128

#define SIMETH_DESC_RING_ALIGNER (SIMETH_DMA_REGION_ALIGNER / sizeof (simeth_desc_t))

/* error logging function macros for simeth */
#define simeth_dbg(format, arg...) \
	netdev_dbg (adapter->netdev, format, ## arg)
#define simeth_err(msglvl, format, arg...) \
	netif_err (adapter, msglvl, adapter->netdev, format, ## arg)
#define simeth_info(msglvl, format, arg...) \
	netif_info (adapter, msglvl, adapter->netdev, format, ## arg)
#define simeth_warn(msglvl, format, arg...) \
	netif_warn (adapter, msglvl, adapter->netdev, format, ## arg)
#define simeth_notice(msglvl, format, arg...) \
	netif_notice (adapter, msglvl, adapter->netdev, format, ## arg)
#define simeth_crit(msglvl, format, arg...) \
	netif_crit (adapter, msglvl, adapter->netdev, format, ## arg)
#define simeth_emerg(msglvl, format, arg...) \
	netif_emerg (adapter, msglvl, adapter->netdev, format, ## arg)
#define simeth_dev_info(format, arg...) \
	dev_info (&adapter->pdev->dev, format, ## arg)
#define simeth_dev_warn(format, arg...) \
	dev_warn (&adapter->pdev->dev, format, ## arg)
#define simeth_dev_err(format, arg...) \
	dev_err (&adapter->pdev->dev, format, ## arg)

typedef struct simeth_stats {
	uint64_t packets;
	uint64_t errors;
	uint64_t dropped;
	uint64_t bytes;
	struct u64_stats_sync syncp;
} simeth_stats_t;

typedef struct simeth_pcpustats {
	simeth_stats_t tx_stats;
	simeth_stats_t rx_stats;
} simeth_pcps_t;

/* simeth tx buf per-sk_buff handler structure */
typedef struct simeth_tx_buf {
	uint64_t            ts; /*timestamp this buf's used*/
	uint32_t            n_bytes; /*num of bytes for the skb (all frags)*/
	struct sk_buff      *skb; /*1st sk-buffer */
	dma_addr_t          dma_addr; /*DMA'ble address for hw*/
} simeth_tx_buf_t;

/* simeth rx buf per-sk_buff handler structure */
typedef struct simeth_rx_buf {
	uint32_t            n_bytes; /*num of bytes for the skb (all frags)*/
	struct sk_buff      *skb; /*1st sk-buffer */
	dma_addr_t          dma_addr; /*DMA'ble address for hw*/
} simeth_rx_buf_t;

/* simeth tx/rx queue handler structure */
typedef struct simeth_q {
	union {
		void            *dring; /*aligned dring buffer pointer*/
		simeth_desc_t   *tx_dring; /*tx dring typecast*/
		simeth_desc_t   *rx_dring; /*rx dring typecast*/
	};
	uint32_t            dring_sz; /*size of desc ring memory in bytes*/

	uint32_t            n_desc; /*number of descs in this q*/

	union {
		void            *bring; /*aligned allocated buffer pointer*/
		simeth_tx_buf_t *tx_bring; /*each node contins skbuff, flags, relevant desc*/
		simeth_rx_buf_t *rx_bring; /*each node contins skbuff, flags, relevant desc*/
	};
	uint32_t            bring_sz; /*size of buffer ring memory in bytes*/

	dma_addr_t          dring_dma_addr;

	void __iomem        *eng_base;

	union { /* desc head of rx/tx desc q */
		uint32_t        txdh;
		uint32_t        rxdh;
	};
	union { /* desc tail of rx/tx desc q */
		uint32_t        txdt;
		uint32_t        rxdt;
	};
} simeth_q_t ____cacheline_internodealigned_in_smp;

typedef simeth_q_t simeth_txq_t;
typedef simeth_q_t simeth_rxq_t;

/* struct to hold various hw parameter values */
typedef struct simeth_hw {
	/*void __iomem        *hw_addr;*/
	/*simeth_phytype_e    phy_type;*/
	/*simeth_mactype_e    mac_type;*/
	/*simeth_fctype_e     fc_type;*/
	uint32_t            phy_revision;
	uint32_t            max_frame_sz;
	uint32_t            min_frame_sz;
	uint8_t             hw_mac_addr[ETH_ALEN];
} simeth_hw_t;

/* Main structure containing simeth driver context */
typedef struct simeth_adapter {
	simeth_pcps_t       cpstats;
	struct napi_struct  napi;
	struct net_device   *netdev;
	struct pci_dev      *pcidev;
	simeth_hw_t         hw;

	uint32_t            n_txqs;
	uint32_t            n_rxqs;
	simeth_q_t          *txq;
	simeth_q_t          *rxq;

	/*simeth_stats_t      drv_tx_stats;*/
	/*simeth_stats_t      drv_rx_stats;*/

	int                 mode;
	int                 msg_enable;
	void __iomem       *ioaddr; /*used for BAR access for nic dma ctrl*/

	uint32_t            rx_buflen;

	uint8_t             mac_addr[ETH_ALEN];
} simeth_adapter_t;

#endif /*__SIMETH_H*/

