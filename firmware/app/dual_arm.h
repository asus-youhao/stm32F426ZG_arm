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
} joint_state_t;

/* 全部 14 軸（左 0..6, 右 7..13） */
extern const joint_cfg_t g_joints[ARM_COUNT * JOINTS_PER_ARM];
extern joint_state_t      g_jstate[ARM_COUNT * JOINTS_PER_ARM];

/** @brief 初始化兩條 bus + 全部關節（NMT、模式 CSP、PDO 映射、使能）。 */
co_status_t dual_arm_init(void);

/** @brief 1 kHz 週期呼叫：送 RPDO(目標) + 收 TPDO(回授) + 維持使能。 */
void dual_arm_tick_1khz(void);

/** @brief 把所有收到的 CAN frame 分派給 NMT/PDO 處理（於 tick 內或背景呼叫）。 */
void dual_arm_pump_rx(void);

/** @brief 設定某關節 CSP 目標位置（由上層 joint/task-space 控制器呼叫）。 */
void dual_arm_set_target(uint8_t joint_index, int32_t target_pos);

#endif /* DUAL_ARM_H */
