
#ifndef __SIMETH_REGS_H
#define __SIMETH_REGS_H

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

#endif /*__SIMETH_REGS_H*/

