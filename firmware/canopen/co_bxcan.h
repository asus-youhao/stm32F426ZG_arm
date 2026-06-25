/**
 * @file    co_bxcan.h
 * @brief   STM32 bxCAN 硬體層 — CAN1(左臂)/CAN2(右臂) @1Mbps
 */
#ifndef CO_BXCAN_H
#define CO_BXCAN_H

#include "canopen.h"

/**
 * @brief 初始化指定 channel 的 bxCAN（1 Mbps、接收所有標準 ID）。
 * @note  需先由 CubeMX 完成 GPIO/時脈;此處設定位元時序、濾波器並啟動。
 */
co_status_t co_bxcan_init(co_bus_t bus);

/** @brief 送出一個 frame（非阻塞,寫入空的 TX mailbox）。 */
co_status_t co_bxcan_send(co_bus_t bus, const co_frame_t *f);

/**
 * @brief 從接收佇列取出一個 frame。
 * @return true 若有資料。由 RX 中斷填入的環形佇列讀取。
 */
bool co_bxcan_recv(co_bus_t bus, co_frame_t *out);

/**
 * @brief 由 HAL_CAN_RxFifo0MsgPendingCallback 轉呼叫,將收到的 frame 入佇列。
 * @param bus 對應的 channel
 */
void co_bxcan_on_rx(co_bus_t bus, const co_frame_t *f);

#endif /* CO_BXCAN_H */
