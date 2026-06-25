/**
 * @file    co_sdo.h
 * @brief   SDO client（expedited read/write，最多 4 bytes）
 *
 * 用於組態關節物件字典：節點 ID、波特率、運行模式、限位等。
 * 阻塞式（含逾時），適合初始化階段使用;即時控制請改用 PDO。
 */
#ifndef CO_SDO_H
#define CO_SDO_H

#include "canopen.h"

/**
 * @brief SDO expedited 下載（寫入,1..4 bytes）。
 * @param bus      CAN channel
 * @param node     節點 ID（1..127）
 * @param index    物件索引
 * @param sub      子索引
 * @param value    要寫入的值（小端）
 * @param size     位元組數 1..4
 * @param timeout_ms 等待回應逾時
 */
co_status_t co_sdo_write(co_bus_t bus, uint8_t node,
                         uint16_t index, uint8_t sub,
                         uint32_t value, uint8_t size,
                         uint32_t timeout_ms);

/**
 * @brief SDO expedited 上傳（讀取,1..4 bytes）。
 * @param out_value 讀回的值（小端，依實際 size 填入）
 */
co_status_t co_sdo_read(co_bus_t bus, uint8_t node,
                        uint16_t index, uint8_t sub,
                        uint32_t *out_value,
                        uint32_t timeout_ms);

#endif /* CO_SDO_H */
