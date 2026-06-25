/**
 * @file    task_space.h
 * @brief   L3 Task-space 控制器（單臂笛卡爾,1 kHz）
 *
 * 每 tick：以目前關節角追隨笛卡爾目標位姿（IK 單步 DLS）→ 產生關節目標,
 * 交給 joint_space（js_set_setpoint）。可選 Cartesian 阻抗（位置誤差 → 速度）。
 */
#ifndef TASK_SPACE_H
#define TASK_SPACE_H

#include "kinematics.h"
#include "ik.h"

typedef struct {
    arm_kin_t kin;          /* 該臂 DH */
    ik_cfg_t  ik;           /* IK 設定 */
    int       joint_base;   /* 在 joint_space 中的起始索引（左=0,右=7） */
    float     q[ARM_DOF];   /* 內部追蹤的關節角（rad） */
    pose_t    target;       /* 目標位姿 */
    bool      has_target;
} task_arm_t;

/** @brief 初始化單臂 task-space（提供 DH、IK 設定、joint_space 起始索引、初始關節角）。 */
void ts_init(task_arm_t *ta, const arm_kin_t *kin, const ik_cfg_t *ik,
             int joint_base, const float q_init[ARM_DOF]);

/** @brief 設定笛卡爾目標位姿。 */
void ts_set_target(task_arm_t *ta, const pose_t *target);

/** @brief 由目前關節角更新內部追蹤（從 joint_space 回授回灌）。 */
void ts_sync_q(task_arm_t *ta, const float q_fb[ARM_DOF]);

/**
 * @brief 1 kHz tick：IK 單步 → 寫入 joint_space 設定點。
 * @return 目前位姿誤差範數。
 */
float ts_tick_1khz(task_arm_t *ta);

/** @brief 取得目前末端位姿（FK）。 */
void ts_get_pose(const task_arm_t *ta, pose_t *out);

#endif /* TASK_SPACE_H */
