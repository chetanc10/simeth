
#ifndef __SIMETH_H
#define __SIMETH_H

#include <linux/list.h>

/* simeth driver version */
#define SIMETH_VERSION "0.1"

/* simeth tx buf per-sk_buf handler structure */
typedef struct simeth_tx_buf {

} simeth_tx_buf_t;

/* simeth tx/rx queue handler structure */
typedef struct simeth_txq {
	uint32_t            ring_sz; /*number of descs in desc ring*/
	union {
		void              *dring; /*desc ring buffer pointer*/
		simeth_dring_t    *tx_dring; /*tx dring typecast*/
		simeth_dring_t    *rx_dring; /*rx dring typecast*/
	};
	union {
		simeth_tx_buf_t   *tx_bufs; /*each node contins skbuff, flags, relevant desc*/
		simeth_rx_buf_t   *rx_bufs; /*each node contins skbuff, flags, relevant desc*/
	};
} simeth_dring_t;

#endif /*__SIMETH_H*/

