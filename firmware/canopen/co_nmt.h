/**
 * @file    co_nmt.h
 * @brief   NMT 主站控制 + Heartbeat 監看
 */
#ifndef CO_NMT_H
#define CO_NMT_H

#include "canopen.h"

/**
 * @brief 送 NMT 命令。
 * @param node 0 = 廣播給所有節點;否則指定節點 ID。
 */
co_status_t co_nmt_send(co_bus_t bus, co_nmt_cmd_t cmd, uint8_t node);

/** @brief 解析一個收到的 frame,若為 heartbeat 則更新節點狀態表。 */
void co_nmt_process_frame(co_bus_t bus, const co_frame_t *f);

/** @brief 取得最近一次紀錄的節點狀態。 */
co_node_state_t co_nmt_node_state(co_bus_t bus, uint8_t node);

/** @brief 自上次 heartbeat 以來經過的毫秒（用於逾時判斷）。 */
uint32_t co_nmt_node_age_ms(co_bus_t bus, uint8_t node);

/** @brief 清空節點狀態表（bus 重啟或重新 init 時呼叫,避免殘留舊狀態）。 */
void co_nmt_reset_cache(void);

#endif /* CO_NMT_H */
