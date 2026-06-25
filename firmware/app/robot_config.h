/**
 * @file    robot_config.h
 * @brief   機器人參數設定（關節限位/換算、雙臂 DH、IK 設定）
 *
 * ⚠️ 內含「佔位參數」：DH、限位、counts_per_rad 等需由 WP0.4 實機量測填入。
 */
#ifndef ROBOT_CONFIG_H
#define ROBOT_CONFIG_H

#include "joint_space.h"
#include "kinematics.h"
#include "ik.h"

/** @brief 取得 14 軸 joint_space 設定。 */
const js_joint_cfg_t *robot_js_cfg(void);

/** @brief 取得左/右臂 DH 參數。 */
const arm_kin_t *robot_left_kin(void);
const arm_kin_t *robot_right_kin(void);

/** @brief 取得 IK 預設設定。 */
const ik_cfg_t *robot_ik_cfg(void);

/** @brief 取得初始關節角（rad,長度 7,左右共用初始姿態）。 */
const float *robot_q_init(void);

#endif /* ROBOT_CONFIG_H */
