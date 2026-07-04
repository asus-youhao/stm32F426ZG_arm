/**
 * @file    bus_if.h
 * @brief   bus-agnostic 匯流排介面型別（WP-H4/H5）
 *
 * 設計見 docs/design/harness-agent-loop-engine-plan.md §6。
 * 本檔先落地 bus_health_t（H4：HealthAgent 只認這個結構,不認協定）;
 * bus_if_t vtable 於 H5（第二個後端 bus_ecat_igh 出現時）一併定案,
 * 避免單一實作期就凍結錯誤的簽名。
 */
#ifndef ENG_BUS_IF_H
#define ENG_BUS_IF_H

#include <stdint.h>

/** @brief bus 健康快照（CANopen 與 EtherCAT 餵同一結構）。 */
typedef struct bus_health {
    uint32_t tx_drop;    /**< 發送丟棄累計（mailbox/queue 滿） */
    uint32_t rx_lost;    /**< 回授缺席計數（heartbeat 逾時節點數等） */
    uint32_t err_events; /**< CANopen: EMCY 幀數；EtherCAT: AL 異常次數 */
    uint8_t  link_ok;    /**< CAN: 非 bus-off；ECAT: link up */
    uint8_t  sync_ok;    /**< CAN: SYNC 模式運作中；ECAT: DC 鎖定 */
    uint16_t load_pct;   /**< 匯流排負載 %（CANopen 依幀數估算） */
    uint32_t proto[4];   /**< 協定特有：CAN error counter / ECAT WKC、AL states */
} bus_health_t;

#endif /* ENG_BUS_IF_H */
