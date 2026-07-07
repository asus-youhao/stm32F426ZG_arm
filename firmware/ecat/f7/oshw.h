/**
 * @file  oshw.h（F746 bare-metal port）
 * @brief 硬體抽象：三個 MAC 函式（init/send/recv）+ byte order + adapter 佔位
 */
#ifndef _oshw_
#define _oshw_

#ifdef __cplusplus
extern "C" {
#endif

#include "soem/soem.h"
#include "nicdrv.h"

/** ETH MAC 初始化：MPU non-cacheable 區、RMII 腳位、LAN8742A PHY、
 *  自動協商、HAL_ETH_Start。回 0 成功。 */
int oshw_mac_init(const uint8_t *mac_address);
int oshw_mac_send(const void *payload, size_t tot_len);
/** 非阻塞收：無框回 0,有框回長度。 */
int oshw_mac_recv(void *buffer, size_t buffer_length);

/** PHY link 狀態（HIL-0 診斷用）：1=up */
int oshw_mac_link_up(void);

uint16 oshw_htons(uint16 host);
uint16 oshw_ntohs(uint16 network);

ec_adaptert *oshw_find_adapters(void);
void oshw_free_adapters(ec_adaptert *adapter);

#ifdef __cplusplus
}
#endif

#endif
