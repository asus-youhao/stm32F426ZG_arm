/**
 * @file    co_emcy.h
 * @brief   EMCY（0x080+node）解析與快取（WP-H4，方案 C 差距 G5）
 *
 * EYOU 故障碼（通訊手冊表 5-2）：E1xx 電流/溫度、E2xx 編碼器、
 * E301 STO、E4xx 通訊。code 0x0000 = error reset / no error（復歸）。
 * 上層（app 安全步驟）以 co_emcy_take() 取走事件並餵 safety。
 */
#ifndef CO_EMCY_H
#define CO_EMCY_H

#include "canopen.h"

/** @brief 嘗試以 EMCY 消化 frame；是 EMCY（0x081..0x0FF）回 true。 */
bool co_emcy_process_frame(co_bus_t bus, const co_frame_t *f);

/**
 * @brief 取走某節點自上次 take 以來的最新 EMCY 事件。
 * @param code 輸出 error code（0x0000 = 復歸）
 * @return 有新事件回 true（並清除 pending）
 */
bool co_emcy_take(co_bus_t bus, uint8_t node, uint16_t *code);

/** @brief 某節點最後一筆 EMCY 的 error code（無 → 0）。 */
uint16_t co_emcy_last_code(co_bus_t bus, uint8_t node);

/** @brief 整條 bus 的 EMCY 累計數（error reset 幀也計）。 */
uint32_t co_emcy_count(co_bus_t bus);

/** @brief 清空全部快取（dual_arm_init / bus 重啟時呼叫）。 */
void co_emcy_reset(void);

#endif /* CO_EMCY_H */
