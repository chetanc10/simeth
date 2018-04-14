
#ifndef __SIMETH_H
#define __SIMETH_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/u64_stats_sync.h>

#include "simeth_nic.h"

/* simeth driver version */
#define SIMETH_VERSION "0.1"

/* If a real xmit is going to happen, this shall be 1, else 0 (dummy) */
#define XMIT_IS_REAL 0

/* NAPI Poll weight */
#define SIMETH_NAPI_WEIGHT 1

/* pci_device_id structure entry macro */
#define simeth_pci_dev_id(vend, dev, drv_data) \
{ PCI_DEVICE(vend, dev), .driver_data = drv_data, }

/* Minimum size of the IVSHMEM bar for simeth to function as expected */
#define SIMETH_BAR_SZ (512 * 1024 * 1024)

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
	uint32_t            ring_sz; /*number of descs in desc ring*/

	uint32_t            dring_align_bytes; /*number of bytes to align the dring*/
	void                *dring_main; /*unaligned allocated buffer pointer*/
	union {
		void            *dring; /*aligned dring buffer pointer*/
		simeth_desc_t   *tx_dring; /*tx dring typecast*/
		simeth_desc_t   *rx_dring; /*rx dring typecast*/
	};
	dma_addr_t          dring_dma_addr;

	uint32_t            qbufs_align_bytes; /*number of bytes to align the buffer ring*/
	void                *qbufs_main; /*unaligned allocated buffer pointer*/
	union {
		void            *bufs; /*aligned allocated buffer pointer*/
		simeth_tx_buf_t *tx_bufs; /*each node contins skbuff, flags, relevant desc*/
		simeth_rx_buf_t *rx_bufs; /*each node contins skbuff, flags, relevant desc*/
	};
} simeth_q_t ____cacheline_internodealigned_in_smp;

typedef simeth_q_t simeth_txq_t;
typedef simeth_q_t simeth_rxq_t;

/* Main structure containing simeth driver context */
typedef struct simeth_priv {
	simeth_pcps_t       cpstats;
	struct napi_struct  napi;
	struct net_device   *netdev;
	struct pci_dev      *pci_dev;

	uint32_t            n_txqs;
	uint32_t            n_rxqs;
	simeth_q_t          *tx_q;
	simeth_q_t          *rx_q;

	/*simeth_stats_t      drv_tx_stats;*/
	/*simeth_stats_t      drv_rx_stats;*/

	int                 mode;
	int                 msg_enable;
	void                *ioaddr; /*used for BAR access for nic dma ctrl*/
} simeth_adapter_t;

#endif /*__SIMETH_H*/

