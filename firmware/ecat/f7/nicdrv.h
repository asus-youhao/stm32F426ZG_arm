/**
 * @file  nicdrv.h（F746 bare-metal port）
 * @brief SOEM 低階收發介面——改編自 oshw/rtk（去 RTOS：互斥鎖為佔位 void*）
 *
 * 原始碼版權：rt-labs SOEM v2（GPLv3/商用雙授權）,結構須與 src/ 相容,勿改欄位。
 */
#ifndef _nicdrvh_
#define _nicdrvh_

#include "osal.h"

/** pointer structure to Tx and Rx stacks */
typedef struct
{
   int *sock;
   ec_bufT (*txbuf)[EC_MAXBUF];
   int (*txbuflength)[EC_MAXBUF];
   ec_bufT *tempbuf;
   ec_bufT (*rxbuf)[EC_MAXBUF];
   int (*rxbufstat)[EC_MAXBUF];
   int (*rxsa)[EC_MAXBUF];
   uint64 rxcnt;
} ec_stackT;

/** pointer structure to buffers for redundant port */
typedef struct
{
   ec_stackT stack;
   int sockhandle;
   ec_bufT rxbuf[EC_MAXBUF];
   int rxbufstat[EC_MAXBUF];
   int rxsa[EC_MAXBUF];
   ec_bufT tempinbuf;
} ecx_redportt;

/** pointer structure to buffers, vars and mutexes for port instantiation */
typedef struct
{
   ec_stackT stack;
   int sockhandle;
   ec_bufT rxbuf[EC_MAXBUF];
   int rxbufstat[EC_MAXBUF];
   int rxsa[EC_MAXBUF];
   ec_bufT tempinbuf;
   int tempinbufs;
   ec_bufT txbuf[EC_MAXBUF];
   int txbuflength[EC_MAXBUF];
   ec_bufT txbuf2;
   int txbuflength2;
   uint8 lastidx;
   int redstate;
   ecx_redportt *redport;
   osal_mutext getindex_mutex;
   osal_mutext tx_mutex;
   osal_mutext rx_mutex;
} ecx_portt;

extern const uint16 priMAC[3];
extern const uint16 secMAC[3];

void ec_setupheader(void *p);
int ecx_setupnic(ecx_portt *port, const char *ifname, int secondary);
int ecx_closenic(ecx_portt *port);
void ecx_setbufstat(ecx_portt *port, uint8 idx, int bufstat);
uint8 ecx_getindex(ecx_portt *port);
int ecx_outframe(ecx_portt *port, uint8 idx, int stacknumber);
int ecx_outframe_red(ecx_portt *port, uint8 idx);
int ecx_waitinframe(ecx_portt *port, uint8 idx, int timeout);
int ecx_srconfirm(ecx_portt *port, uint8 idx, int timeout);

#endif
