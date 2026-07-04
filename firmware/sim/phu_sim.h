/**
 * @file    phu_sim.h
 * @brief   模擬 EYOU PHU 關節（CANopen 從站模型）
 *
 * 依通訊手冊 v1.06 實作關鍵物件與 CiA402 狀態機:
 *   0x1000 device type, 0x6040 CW, 0x6041 SW, 0x6060/61 mode,
 *   0x607A target pos, 0x6064 actual pos, 0x60FF target vel,
 *   0x26A0 node-id, 0x26A1 baudrate(1Mbps), 0x1600/0x1A00 PDO 映射。
 * 並含簡單位置動力學（朝目標收斂）。
 */
#ifndef PHU_SIM_H
#define PHU_SIM_H

#include "canopen.h"

typedef struct {
    uint8_t  node_id;
    uint8_t  nmt_state;       /* CO_NODE_* */
    uint16_t controlword;
    uint16_t statusword;
    int8_t   mode;            /* 0x6060 */
    int32_t  target_pos;      /* 0x607A */
    int32_t  actual_pos;      /* 0x6064 */
    int32_t  target_vel;      /* 0x60FF (counts/s) */
    int32_t  max_step;        /* CSP 每 tick 最大位移(counts) */
    bool     enabled;
    /* WP-H4/G3：transmission type=1（0x1400/0x1800:02）→ SYNC 鎖存模式 */
    bool     sync_mode;
    bool     rpdo_pending;    /* 已收 RPDO,等下個 SYNC 才套用 */
    uint16_t pend_cw;
    int32_t  pend_tp;
} phu_node_t;

void phu_init(phu_node_t *n, uint8_t node_id);

/**
 * @brief 處理一個由主站送來的 frame,產生 0..N 個回應 frame。
 * @param out     回應陣列
 * @param max_out 容量
 * @return 回應數量
 */
int phu_on_frame(phu_node_t *n, const co_frame_t *in,
                 co_frame_t *out, int max_out);

/** @brief 推進一個 tick 的動力學（朝目標收斂）。 */
void phu_step_dynamics(phu_node_t *n);

#endif /* PHU_SIM_H */
