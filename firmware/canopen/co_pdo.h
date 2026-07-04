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

/** @brief 回授序號（每收到一個 TPDO 遞增）。比較前後值即可判斷是否有新回授。 */
uint32_t co_pdo_feedback_seq(co_bus_t bus, uint8_t node);

/** @brief 清空全部回授快取/序號（bus 重啟或重新 init 時呼叫,避免殘留舊回授）。 */
void co_pdo_reset(void);

/** @brief 發 SYNC（COB 0x080, dlc 0）。同步模式下每週期於 BUS_TX 開頭呼叫（G3）。 */
co_status_t co_pdo_send_sync(co_bus_t bus);

#endif /* CO_PDO_H */
