
#ifndef __SIMETH_H
#define __SIMETH_H

#include <linux/types.h>
#include <linux/list.h>

/* simeth driver version */
#define SIMETH_VERSION "0.1"

/* simeth tx buf per-sk_buf handler structure */
typedef struct simeth_tx_buf {
	uint64_t            ts;/*timestamp this buf's used*/
	uint32_t            n_bytes;
	struct sk_buf       *skb;
} simeth_tx_buf_t;

/* simeth tx/rx queue handler structure */
typedef struct simeth_q {
	uint32_t            ring_sz; /*number of descs in desc ring*/
	union {
		void            *dring; /*desc ring buffer pointer*/
		simeth_dring_t  *tx_dring; /*tx dring typecast*/
		simeth_dring_t  *rx_dring; /*rx dring typecast*/
	};
	union {
		void            *bufs;
		simeth_tx_buf_t *tx_bufs; /*each node contins skbuff, flags, relevant desc*/
		simeth_rx_buf_t *rx_bufs; /*each node contins skbuff, flags, relevant desc*/
	};
} simeth_q_t;

typedef simeth_q_t simeth_txq_t;
typedef simeth_q_t simeth_rxq_t;

#endif /*__SIMETH_H*/

