/**
 * @file    ik.h
 * @brief   數值逆運動學（Damped Least Squares, 適合 7-DoF 冗餘臂與奇異點）
 *
 * dq = J^T (J J^T + λ²I)^-1 · e
 */
#ifndef IK_H
#define IK_H

#include "kinematics.h"
#include <stdbool.h>

typedef struct {
    float lambda;        /* 阻尼係數（奇異點穩定,如 0.05） */
    float step_gain;     /* 每步增益 0..1（如 0.5） */
    int   max_iters;     /* 最大迭代（離線解算用） */
    float pos_tol;       /* 位置收斂 (m) */
    float rot_tol;       /* 旋轉收斂 (rad) */
} ik_cfg_t;

/**
 * @brief 單步 DLS 更新：由目前 q 朝 desired 位姿前進一步,寫回 q。
 * @return 目前 6 維誤差範數（位置+旋轉,粗略）。
 * 適合 1 kHz 即時迴圈（每 tick 一步,追隨平滑目標）。
 */
float ik_step(const arm_kin_t *k, const ik_cfg_t *cfg,
              const pose_t *desired, float q[ARM_DOF]);

/**
 * @brief 迭代求解（離線/規劃用）：反覆 ik_step 直到收斂或達 max_iters。
 * @return true 收斂。
 */
bool ik_solve(const arm_kin_t *k, const ik_cfg_t *cfg,
              const pose_t *desired, float q[ARM_DOF]);

#endif /* IK_H */
