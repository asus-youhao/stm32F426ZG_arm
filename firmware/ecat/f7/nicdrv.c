/**
 * @file  nicdrv.c（F746 bare-metal port, WP-SE3）
 * @brief SOEM 低階收發——索引化緩衝管理照抄 oshw/rtk,傳輸換 oshw_mac_*（ETH MAC）。
 *
 * 與 rtk 版差異：
 *   - 互斥鎖 → osal_mutex_*（bare-metal no-op,單執行緒 loop engine）
 *   - 冗餘（redundant/secondary）不支援：Nucleo 單網孔,secondary 一律失敗
 *   - close 無 socket 可關,只停 MAC
 * 原始邏輯版權 rt-labs SOEM v2（GPLv3/商用雙授權）。
 */
#include <string.h>

#include "osal.h"
#include "oshw.h"

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/** Redundancy modes */
enum
{
   ECT_RED_NONE,
   ECT_RED_DOUBLE
};

const uint16 priMAC[3] = EC_PRIMARY_MAC_ARRAY;
const uint16 secMAC[3] = EC_SECONDARY_MAC_ARRAY;

#define RX_PRIM priMAC[1]
#define RX_SEC  secMAC[1]

static void ecx_clear_rxbufstat(int *rxbufstat)
{
   int i;
   for (i = 0; i < EC_MAXBUF; i++)
   {
      rxbufstat[i] = EC_BUF_EMPTY;
   }
}

/** 連接 NIC。ifname 忽略（板上唯一 MAC）;secondary 不支援。回 >0 成功。 */
int ecx_setupnic(ecx_portt *port, const char *ifname, int secondary)
{
   int i;
   (void)ifname;

   if (secondary)
      return 0;                          /* 單網孔無冗餘 */

   if (oshw_mac_init((const uint8_t *)priMAC) != 0)
      return 0;

   port->getindex_mutex = osal_mutex_create();
   port->tx_mutex = osal_mutex_create();
   port->rx_mutex = osal_mutex_create();
   port->sockhandle = -1;
   port->lastidx = 0;
   port->redstate = ECT_RED_NONE;
   port->redport = NULL;
   port->stack.sock = &(port->sockhandle);
   port->stack.txbuf = &(port->txbuf);
   port->stack.txbuflength = &(port->txbuflength);
   port->stack.tempbuf = &(port->tempinbuf);
   port->stack.rxbuf = &(port->rxbuf);
   port->stack.rxbufstat = &(port->rxbufstat);
   port->stack.rxsa = &(port->rxsa);
   ecx_clear_rxbufstat(&(port->rxbufstat[0]));

   /* 預填 ethernet header,之後不再重寫 */
   for (i = 0; i < EC_MAXBUF; i++)
   {
      ec_setupheader(&(port->txbuf[i]));
      port->rxbufstat[i] = EC_BUF_EMPTY;
   }
   ec_setupheader(&(port->txbuf2));

   return 1;
}

int ecx_closenic(ecx_portt *port)
{
   (void)port;
   return 0;
}

/** 填 ethernet header：目的地廣播,EtherType 0x88A4。 */
void ec_setupheader(void *p)
{
   ec_etherheadert *bp;
   bp = p;
   bp->da0 = oshw_htons(0xffff);
   bp->da1 = oshw_htons(0xffff);
   bp->da2 = oshw_htons(0xffff);
   bp->sa0 = oshw_htons(priMAC[0]);
   bp->sa1 = oshw_htons(priMAC[1]);
   bp->sa2 = oshw_htons(priMAC[2]);
   bp->etype = oshw_htons(ETH_P_ECAT);
}

/** 取新 frame index 並配置對應 rx 緩衝。 */
uint8 ecx_getindex(ecx_portt *port)
{
   uint8 idx;
   uint8 cnt;

   osal_mutex_lock(port->getindex_mutex);

   idx = port->lastidx + 1;
   if (idx >= EC_MAXBUF)
   {
      idx = 0;
   }
   cnt = 0;
   while ((port->rxbufstat[idx] != EC_BUF_EMPTY) && (cnt < EC_MAXBUF))
   {
      idx++;
      cnt++;
      if (idx >= EC_MAXBUF)
      {
         idx = 0;
      }
   }
   port->rxbufstat[idx] = EC_BUF_ALLOC;
   port->lastidx = idx;

   osal_mutex_unlock(port->getindex_mutex);

   return idx;
}

void ecx_setbufstat(ecx_portt *port, uint8 idx, int bufstat)
{
   port->rxbufstat[idx] = bufstat;
}

/** 送出 txbuf[idx]（非阻塞）。 */
int ecx_outframe(ecx_portt *port, uint8 idx, int stacknumber)
{
   int lp, rval;
   ec_stackT *stack;

   (void)stacknumber;
   stack = &(port->stack);
   lp = (*stack->txbuflength)[idx];
   (*stack->rxbufstat)[idx] = EC_BUF_TX;
   rval = oshw_mac_send((*stack->txbuf)[idx], lp);

   return rval;
}

int ecx_outframe_red(ecx_portt *port, uint8 idx)
{
   ec_etherheadert *ehp;

   ehp = (ec_etherheadert *)&(port->txbuf[idx]);
   ehp->sa1 = oshw_htons(priMAC[1]);
   return ecx_outframe(port, idx, 0);
}

/** 非阻塞收一框到 tempbuf。回 >0 = 有框。 */
static int ecx_recvpkt(ecx_portt *port, int stacknumber)
{
   int lp, bytesrx;
   ec_stackT *stack;

   (void)stacknumber;
   stack = &(port->stack);
   lp = sizeof(port->tempinbuf);
   bytesrx = oshw_mac_recv((*stack->tempbuf), lp);
   port->tempinbufs = bytesrx;

   return (bytesrx > 0);
}

/** 非阻塞收框並依 index 歸位（亂序重排,同 rtk 版邏輯）。 */
int ecx_inframe(ecx_portt *port, uint8 idx, int stacknumber)
{
   uint16 l;
   int rval;
   uint8 idxf;
   ec_etherheadert *ehp;
   ec_comt *ecp;
   ec_stackT *stack;
   ec_bufT *rxbuf;

   stack = &(port->stack);
   rval = EC_NOFRAME;
   rxbuf = &(*stack->rxbuf)[idx];
   /* 該 index 已在緩衝？ */
   if ((idx < EC_MAXBUF) && ((*stack->rxbufstat)[idx] == EC_BUF_RCVD))
   {
      l = (*rxbuf)[0] + ((uint16)((*rxbuf)[1] & 0x0f) << 8);
      rval = ((*rxbuf)[l] + ((uint16)(*rxbuf)[l + 1] << 8));
      (*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;
   }
   else if (ecx_recvpkt(port, stacknumber))
   {
      rval = EC_OTHERFRAME;
      ehp = (ec_etherheadert *)(stack->tempbuf);
      if (ehp->etype == oshw_htons(ETH_P_ECAT))
      {
         stack->rxcnt++;
         ecp = (ec_comt *)(&(*stack->tempbuf)[ETH_HEADERSIZE]);
         l = etohs(ecp->elength) & 0x0fff;
         idxf = ecp->index;
         if (idxf == idx)
         {
            /* 剝 ethernet header 放進索引緩衝 */
            memcpy(rxbuf, &(*stack->tempbuf)[ETH_HEADERSIZE], (*stack->txbuflength)[idx] - ETH_HEADERSIZE);
            rval = ((*rxbuf)[l] + ((uint16)((*rxbuf)[l + 1]) << 8));
            (*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;
            (*stack->rxsa)[idx] = oshw_ntohs(ehp->sa1);
         }
         else if (idxf < EC_MAXBUF && (*stack->rxbufstat)[idxf] == EC_BUF_TX)
         {
            /* 別的 index 的回框：先存,等它的 waitinframe 來拿 */
            rxbuf = &(*stack->rxbuf)[idxf];
            memcpy(rxbuf, &(*stack->tempbuf)[ETH_HEADERSIZE], (*stack->txbuflength)[idxf] - ETH_HEADERSIZE);
            (*stack->rxbufstat)[idxf] = EC_BUF_RCVD;
            (*stack->rxsa)[idxf] = oshw_ntohs(ehp->sa1);
         }
      }
   }

   return rval;
}

/** 阻塞（至 timer 到期）等待指定 index 的回框。無冗餘,直接輪詢主 stack。 */
static int ecx_waitinframe_red(ecx_portt *port, uint8 idx, osal_timert timer)
{
   int wkc = EC_NOFRAME;

   do
   {
      if (wkc <= EC_NOFRAME)
      {
         wkc = ecx_inframe(port, idx, 0);
      }
   } while ((wkc <= EC_NOFRAME) && (osal_timer_is_expired(&timer) == FALSE));

   return wkc;
}

int ecx_waitinframe(ecx_portt *port, uint8 idx, int timeout)
{
   int wkc;
   osal_timert timer;

   osal_timer_start(&timer, timeout);
   wkc = ecx_waitinframe_red(port, idx, timer);

   return wkc;
}

/** 送出+等回應,WKC<=0 且時間許可則重送（非過程資料用）。 */
int ecx_srconfirm(ecx_portt *port, uint8 idx, int timeout)
{
   int wkc = EC_NOFRAME;
   osal_timert timer;

   osal_timer_start(&timer, timeout);
   do
   {
      osal_timert read_timer;

      ecx_outframe_red(port, idx);
      osal_timer_start(&read_timer, MIN(timeout, EC_TIMEOUTRET));
      wkc = ecx_waitinframe_red(port, idx, read_timer);
   } while ((wkc <= EC_NOFRAME) && (osal_timer_is_expired(&timer) == FALSE));

   return wkc;
}
