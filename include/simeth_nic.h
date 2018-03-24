
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

/*Descriptor Q management registers*/
#define SER_DQ_TYPE                0x0000 /*RO - type of desc q*/
#define SER_DQ_SZ                  0x0001 /*RW - number of descs per q*/
#define SER_DQ_HEAD                0x0010 /*RW - desc head to be used to fill in*/
#define SER_DQ_TAIL                0x0020 /*RW - desc tail to be used to check last processed descriptor*/
#define SER_DQ_AVL                 0x0030 /*RO - Basically diff between Q HEAD and TAIL, to check number of available descs */

/*Descriptor Q status and statistics registers*/
#define SER_DQ_TX_PKT_CNT          0x0000 /*RO - tx packet count on selected Q*/
#define SER_DQ_TX_ERR_CNT          0x0000 /*RO - tx error count on selected Q*/
#define SER_DQ_TX_BYTE_CNT         0x0000 /*RO - tx byte count on selected Q*/

#define SER_DQ_RX_PKT_CNT          0x0000 /*RO - rx packet count on selected Q*/
#define SER_DQ_RX_ERR_CNT          0x0000 /*RO - rx error count on selected Q*/
#define SER_DQ_RX_BYTE_CNT         0x0000 /*RO - rx byte count on selected Q*/

/* Descriptor structure */
typedef struct simeth_desc {
	uint32_t            buf_pa_hi;
	uint32_t            buf_pa_lo;
	uint32_t            opts1; /*len: 0-14, sop: 15, spread: 16-19, rsvd: 20-31*/
	uint32_t            opts2; /*rsvd*/
} simeth_desc_t;

#endif /*__SIMETH_REGS_H*/

