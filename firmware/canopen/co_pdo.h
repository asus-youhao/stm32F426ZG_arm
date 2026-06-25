/**
 * @file    co_pdo.h
 * @brief   PDO 收發（cyclic，用於 1kHz CSP 控制）
 *
 * 預設映射（需與關節 OD 的 PDO 映射一致,見 co_pdo.c 註解）：
 *   RPDO1 (0x200+node)：Controlword(0x6040,u16) + Target Position(0x607A,i32)
 *   TPDO1 (0x180+node)：Statusword(0x6041,u16)  + Position Actual(0x6064,i32)
 */
#ifndef CO_PDO_H
#define CO_PDO_H

#include "canopen.h"

/** @brief 送出 RPDO1：控制字 + 目標位置（CSP）。 */
co_status_t co_pdo_send_csp(co_bus_t bus, uint8_t node,
                            uint16_t controlword, int32_t target_pos);

/** @brief 解析收到的 frame,若為某節點 TPDO1 則更新其狀態字/實際位置快取。 */
void co_pdo_process_frame(co_bus_t bus, const co_frame_t *f);

/** @brief 讀取快取的 TPDO1 回授。 */
bool co_pdo_get_feedback(co_bus_t bus, uint8_t node,
                         uint16_t *statusword, int32_t *pos_actual);

#endif /* CO_PDO_H */
