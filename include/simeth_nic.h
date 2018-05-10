
#ifndef __SIMETH_REGS_H
#define __SIMETH_REGS_H

#ifdef __KERNEL__
/*kernel based headers*/
#include <linux/types.h>

#else
/*userspace based headers*/
#include <stdint.h>

#endif /*__KERNEL__*/

/* (S)IM(E)TH 32-bit (R)egister Set */

/*simeth device statistics RO only for driver!!!*/
#define SER_TX_STATS               0x0000 /*Full struct of tx-stats as below, TXB: bytes, TXP: packets*/
#define SER_TX_STATS_PKT_ALL       0x0000
#define SER_TX_STATS_PKT_SENT      0x0008
#define SER_TX_STATS_PKT_ERR       0x0010
#define SER_TX_STATS_BYTES         0x0018
#define SER_RX_STATS               0x0020 /*Full struct of rx-stats as below, RXB: bytes, RXP: packets*/
#define SER_RX_STATS_PKT_ALL       0x0020
#define SER_RX_STATS_PKT_SENT      0x0028
#define SER_RX_STATS_PKT_ERR       0x0030
#define SER_RX_STATS_BYTES         0x0038

/*descriptor queue management*/
#define SER_TX_DRING_BASE          0x0100/*tx desc register set base-offset*/
#define SER_TX_DRING_PA            0x0100
#define SER_TX_DRING_PA_L          0x0100
#define SER_TX_DRING_PA_H          0x0104
#define SER_TX_DRING_SZ            0x0108
#define SER_TX_DRING_CTRL          0x0110
#define SER_TX_DRING_ST            0x0114

#define SER_RX_DRING_BASE          0x0200/*rx desc register set base-offset*/
#define SER_RX_DRING_PA            0x0200
#define SER_RX_DRING_PA_L          0x0200
#define SER_RX_DRING_PA_H          0x0204
#define SER_RX_DRING_SZ            0x0208
#define SER_RX_DRING_CTRL          0x0210
#define SER_RX_DRING_ST            0x0214

/*descq ctrl/status flags*/
#define SER_DRING_EN               0x0001
#define SER_DRING_RST              0x0002
#define SER_DRING_EN               0x0001

/*desc options*/
#define SER_DF_SOP                 (1 << 12)
#define SER_DF_EOP                 (1 << 13)
#define SER_DF_FRAG_CNT(n)         (((n) & 0xf) << 16)
/* Descriptor structure */
typedef struct simeth_desc {
	uint32_t            buf_pa_hi;
	uint32_t            buf_pa_lo;
	uint32_t            opts1; /*len: 0-11, sop: 12, eop: 13, rsvd: 14-15, frags: 16-19, rsvd: 21-31*/
	uint32_t            opts2; /*rsvd*/
} simeth_desc_t;

#endif /*__SIMETH_REGS_H*/

