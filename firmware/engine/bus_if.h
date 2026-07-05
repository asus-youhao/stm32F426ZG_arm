/**
 * @file    bus_if.h
 * @brief   bus-agnostic 匯流排介面（WP-H4/H5）
 *
 * 設計見 docs/design/harness-agent-loop-engine-plan.md §6。
 * H4 落地 bus_health_t；H5（第二個後端出現）落地 bus_if_t vtable：
 * app agents 只透過 vtable 存取匯流排,COMPUTE 相位（safety/motion）
 * 完全 bus-agnostic。資料面共用 g_jstate[]（回授快照/目標）。
 * 後端：bus_canopen.c（dual_arm/SocketCAN）、bus_ecat.c（ec_master 門面,
 * 底下 sim/SOEM/IgH 連結期選擇）。
 */
#ifndef ENG_BUS_IF_H
#define ENG_BUS_IF_H

#include <stdbool.h>
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

/** @brief 匯流排後端 vtable（H5）。RT 標註者禁阻塞/禁系統呼叫。 */
typedef struct bus_if {
    const char *name;                    /**< "canopen" / "ethercat" */

    int      (*init)(void);              /**< 非 RT bus-up（交握+組態,可阻塞）;0=成功 */
    int      (*present_count)(void);     /**< init 後在線軸數 */

    void     (*pump_rx)(void);           /**< RT BUS_RX/LATCH：回授 → g_jstate */
    void     (*set_target)(uint8_t j, int32_t counts);   /**< RT */
    void     (*tick)(void);              /**< RT BUS_TX：使能步進 + 下發 */
    void     (*set_safe_stop)(bool on, uint16_t safe_cw); /**< RT */

    uint32_t (*tx_drops)(void);
    /** RT：取走軸 j 的故障事件（CANopen: EMCY;ECAT: CiA402 fault 邊緣）。
     *  code 0x0000 = 復歸。無新事件回 false。 */
    bool     (*take_fault)(int j, uint16_t *code);
    /** RT HOUSEKEEP：bus_idx 儀表快照;window_us = 統計視窗。 */
    void     (*health)(int bus_idx, uint32_t window_us, bus_health_t *out);
    /** RT HOUSEKEEP：SDO/CoE 背景通道推進一小步（sdo_bg 模組）。 */
    void     (*sdo_bg_step)(void);
} bus_if_t;

#endif /* ENG_BUS_IF_H */
