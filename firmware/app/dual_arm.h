/**
 * @file    dual_arm.h
 * @brief   雙臂設定與控制（左臂=CAN1, 右臂=CAN2, 各 7 軸 CANopen）
 */
#ifndef DUAL_ARM_H
#define DUAL_ARM_H

#include "canopen.h"
#include "cia402.h"

#define JOINTS_PER_ARM  7
#define ARM_COUNT       2

/* 關節型號（對照 CLAUDE.md 關節表） */
typedef enum { PHU14, PHU17, PHU20 } phu_model_t;

typedef struct {
    co_bus_t   bus;       /* 所在 CAN channel（左/右） */
    uint8_t    node_id;   /* CANopen 節點 ID 1..7 */
    phu_model_t model;
    const char *name;     /* 如 "L_J1_Shoulder" */
} joint_cfg_t;

typedef struct {
    int32_t  target_pos;     /* CSP 目標位置（counts） */
    int32_t  pos_actual;     /* 回授實際位置 */
    uint16_t statusword;     /* 回授狀態字 */
    uint16_t controlword;    /* 目前送出的控制字 */
    bool     enabled;        /* 是否已進入 OPERATION_ENABLED */
    bool     present;        /* init 時有回應（bus 活著且 SDO 設定成功） */
    uint32_t fb_seq;         /* 上次處理過的回授序號 */
    bool     fb_fresh;       /* 本 tick 是否收到「新」TPDO 回授（看門狗用） */
} joint_state_t;

/* 全部 14 軸（左 0..6, 右 7..13） */
extern const joint_cfg_t g_joints[ARM_COUNT * JOINTS_PER_ARM];
extern joint_state_t      g_jstate[ARM_COUNT * JOINTS_PER_ARM];

/**
 * @brief 初始化兩條 bus + 全部關節（NMT、模式 CSP、PDO 映射、使能）。
 *
 * 優雅降級：單條 bus 失敗或個別節點無回應不會中止——缺席軸標記
 * present=false 後跳過（單臂/單軸 HIL 也能跑）。全部缺席才回錯誤。
 */
co_status_t dual_arm_init(void);

/** @brief init 後實際在線的軸數（present==true）。 */
int dual_arm_present_count(void);

/** @brief 控制週期呼叫：送 RPDO(目標) + 收 TPDO(回授) + 維持使能。 */
void dual_arm_tick(void);

/** @brief 累計 PDO 下發丟幀數（co_bxcan_send 回 CO_ERR_TX,通常 mailbox 滿 → 頻寬不足）。 */
uint32_t dual_arm_tx_drops(void);

/** @brief 把所有收到的 CAN frame 分派給 NMT/PDO 處理（於 tick 內或背景呼叫）。 */
void dual_arm_pump_rx(void);

/** @brief 設定某關節 CSP 目標位置（由上層 joint/task-space 控制器呼叫）。 */
void dual_arm_set_target(uint8_t joint_index, int32_t target_pos);

/**
 * @brief 安全停止覆寫（WP6）。啟用時強制控制字、目標維持實際位置。
 * @param on  是否安全停止
 * @param safe_cw  安全控制字（quick stop / disable voltage）
 */
void dual_arm_set_safe_stop(bool on, uint16_t safe_cw);

#endif /* DUAL_ARM_H */
