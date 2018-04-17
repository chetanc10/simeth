
#ifndef __SIMETH_H
#define __SIMETH_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/u64_stats_sync.h>
#include <linux/netdevice.h>

#include "simeth_nic.h"

/* simeth driver version */
#define SIMETH_VERSION "0.1"

/* If a real xmit is going to happen, this shall be 1, else 0 (dummy) */
#define XMIT_IS_REAL 1

/* NAPI Poll weight */
#define SIMETH_NAPI_WEIGHT 1

/* pci_device_id structure entry macro */
#define simeth_pci_dev_id(vend, dev, drv_data) \
{ PCI_DEVICE(vend, dev), .driver_data = drv_data, }

/* Minimum size of the IVSHMEM bar for simeth to function as expected */
#define SIMETH_BAR_SZ (512 * 1024 * 1024)

/* Maximum size of rx buffer with VLAN tag generally 1518 + 4 */
#define MAX_ETH_VLAN_SZ 1522

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

/* Resource realeasing and nullify pointer */
#define simeth_release(rel_fn, ptr) \
	if ((ptr)) { rel_fn ((ptr)); (ptr) = NULL; }

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

/* simeth tx buf per-sk_buf handler structure */
typedef struct simeth_tx_buf {
	uint64_t            ts; /*timestamp this buf's used*/
	uint32_t            n_bytes; /*num of bytes for the skb (all frags)*/
	struct sk_buf       *skb; /*1st sk-buffer */
	dma_addr_t          dma_addr; /*DMA'ble address for hw*/
} simeth_tx_buf_t;

/* simeth rx buf per-sk_buf handler structure */
typedef struct simeth_rx_buf {
	uint32_t            n_bytes; /*num of bytes for the skb (all frags)*/
	struct sk_buf       *skb; /*1st sk-buffer */
	dma_addr_t          dma_addr; /*DMA'ble address for hw*/
} simeth_rx_buf_t;

/* simeth tx/rx queue handler structure */
typedef struct simeth_q {
	union {
		void            *dring; /*aligned dring buffer pointer*/
		simeth_desc_t   *tx_dring; /*tx dring typecast*/
		simeth_desc_t   *rx_dring; /*rx dring typecast*/
	};

	uint32_t            ring_sz; /*number of descs in desc ring*/

	void                *dring_main; /*unaligned allocated buffer pointer*/
	dma_addr_t          dring_dma_addr;
	uint32_t            dring_align_bytes; /*number of bytes to align the dring*/

	uint32_t            qbufs_align_bytes; /*number of bytes to align the buffer ring*/
	void                *qbufs_main; /*unaligned allocated buffer pointer*/
	union {
		void            *bufs; /*aligned allocated buffer pointer*/
		simeth_tx_buf_t *tx_bufs; /*each node contins skbuff, flags, relevant desc*/
		simeth_rx_buf_t *rx_bufs; /*each node contins skbuff, flags, relevant desc*/
	};

	union { /* desc head of rx/tx desc q */
		uint32_t            txdh;
		uint32_t            rxdh;
	};
	union { /* desc tail of rx/tx desc q */
		uint32_t            txdt;
		uint32_t            rxdt;
	};
} simeth_q_t ____cacheline_internodealigned_in_smp;

typedef simeth_q_t simeth_txq_t;
typedef simeth_q_t simeth_rxq_t;

/* Main structure containing simeth driver context */
typedef struct simeth_priv {
	simeth_pcps_t       cpstats;
	struct napi_struct  napi;
	struct net_device   *netdev;
	struct pci_dev      *pcidev;

	uint32_t            n_txqs;
	uint32_t            n_rxqs;
	simeth_q_t          *txq;
	simeth_q_t          *rxq;

	/*simeth_stats_t      drv_tx_stats;*/
	/*simeth_stats_t      drv_rx_stats;*/

	int                 mode;
	int                 msg_enable;
	void                *ioaddr; /*used for BAR access for nic dma ctrl*/

	uint32_t            rx_buflen;
} simeth_adapter_t;

#endif /*__SIMETH_H*/

