/**
 * @file    bringup.h
 * @brief   WP2 單軸 bring-up 測試（SDO 讀寫驗證 + 單軸轉動）
 *
 * 用途：在整合進雙臂前,先針對「一條 bus 上的一個節點」驗證:
 *   1) bxCAN 收發是否正常
 *   2) SDO 讀寫是否成功（讀身分/狀態字/位置;寫模式/速度）
 *   3) CiA402 使能流程是否正確
 *   4) 馬達是否確實轉動
 */
#ifndef BRINGUP_H
#define BRINGUP_H

#include "canopen.h"

/* 測試結果（逐步回報） */
typedef struct {
    co_status_t can_init;
    co_status_t read_device_type;   /* 0x1000 */
    uint32_t    device_type;
    co_status_t read_statusword;    /* 0x6041 */
    uint16_t    statusword;
    co_status_t read_baudrate;      /* 0x26A1 */
    uint32_t    baudrate_bps;
    co_status_t read_node_id;       /* 0x26A0 */
    uint32_t    node_id_read;
    co_status_t read_pos_before;    /* 0x6064 */
    int32_t     pos_before;
    co_status_t enable_result;
    co_status_t spin_result;
    int32_t     pos_after;          /* 轉動後位置（應與 before 不同） */
    bool        moved;              /* 是否觀察到位移 */
} bringup_report_t;

/**
 * @brief 對單一節點執行完整 bring-up（阻塞,僅供測試）。
 * @param bus      CO_BUS_LEFT / CO_BUS_RIGHT
 * @param node     節點 ID（1..7）
 * @param spin_rpm 轉速（Profile Velocity,counts/s 之換算前的概略值,見實作註解）
 * @param spin_ms  轉動持續時間（ms）
 * @param rep      輸出報告
 * @return CO_OK 表示全部步驟通過且觀察到位移
 *
 * @warning 會讓馬達實際轉動！請先確保關節可安全自由轉動、低速、周圍淨空。
 */
co_status_t bringup_single_axis(co_bus_t bus, uint8_t node,
                                int32_t spin_velocity, uint32_t spin_ms,
                                bringup_report_t *rep);

/* 弱連結記錄函式：預設空實作,可在專案中以 printf/UART/SWO 覆寫。 */
void bringup_log(const char *fmt, ...);

#endif /* BRINGUP_H */
