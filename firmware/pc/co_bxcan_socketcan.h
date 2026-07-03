/**
 * @file    co_bxcan_socketcan.h
 * @brief   SocketCAN 後端的額外設定 API（PC 主站專用）
 *
 * co_bxcan.h 的四個函式由 co_bxcan_socketcan.c 實作;本檔僅提供
 * 「init 前指定 CAN 介面名稱」的入口（預設 vcan0=左臂、vcan1=右臂）。
 */
#ifndef CO_BXCAN_SOCKETCAN_H
#define CO_BXCAN_SOCKETCAN_H

#include "canopen.h"

/**
 * @brief 指定某條 bus 綁定的 SocketCAN 介面（須在 co_bxcan_init 前呼叫）。
 * @param ifname 介面名稱（"vcan0"/"can0"…）;NULL 或 "" 表示停用該臂,
 *               init 會回 CO_ERR_STATE,dual_arm_init 依既有邏輯優雅降級。
 */
void co_socketcan_set_ifname(co_bus_t bus, const char *ifname);

/** @brief 讀回目前綁定的介面名稱（除錯/記錄用）。 */
const char *co_socketcan_ifname(co_bus_t bus);

#endif /* CO_BXCAN_SOCKETCAN_H */
