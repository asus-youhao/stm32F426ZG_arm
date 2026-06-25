/**
 * @file    joint_space.h
 * @brief   L2 Joint-space 控制器（管理 14 軸,1 kHz）
 *
 * 職責：
 *   - 維護每軸目標/實際（rad）
 *   - 軌跡插值（點到點）→ 每 tick 產生位置設定點
 *   - rad ↔ encoder counts 換算（CSP 下發）
 *   - 關節限位/限速保護
 * 與 dual_arm（L1）介接：把每軸 counts 目標送給 dual_arm_set_target()。
 */
#ifndef JOINT_SPACE_H
#define JOINT_SPACE_H

#include <stdint.h>
#include <stdbool.h>
#include "trajectory.h"

#define JS_TOTAL_JOINTS 14   /* 雙臂 7+7 */

typedef struct {
    float counts_per_rad;    /* 編碼器換算（依驅動 user-unit 設定） */
    float q_min, q_max;      /* 關節限位 (rad) */
    float vmax, amax;        /* 點到點限制 (rad/s, rad/s^2) */
    float offset_rad;        /* 機械零點偏移 */
} js_joint_cfg_t;

/** @brief 初始化(dt 例 0.001s)。需提供每軸設定。 */
void js_init(float dt, const js_joint_cfg_t cfg[JS_TOTAL_JOINTS]);

/** @brief 設定某軸點到點目標（rad,梯形）。會被限位夾制。 */
void js_move_to(int joint, float q_target_rad);

/** @brief 設定某軸五次多項式目標（指定時程 T 秒）。 */
void js_move_to_quintic(int joint, float q_target_rad, float T);

/** @brief 直接設定某軸即時位置設定點（rad,用於外部 task-space 串接）。 */
void js_set_setpoint(int joint, float q_rad);

/** @brief 更新某軸回授（由 L1 的 counts 回授轉 rad）。 */
void js_update_feedback(int joint, int32_t counts_actual);

/**
 * @brief 1 kHz tick：推進所有軌跡,輸出 counts 目標。
 * @param out_counts 長度 JS_TOTAL_JOINTS,填入每軸 counts 目標（可傳 NULL）。
 */
void js_tick_1khz(int32_t out_counts[JS_TOTAL_JOINTS]);

/** @brief 取得某軸目前命令位置 (rad)。 */
float js_get_cmd(int joint);
/** @brief 所有軸軌跡是否皆完成。 */
bool js_all_idle(void);

/* counts ↔ rad 工具 */
int32_t js_rad_to_counts(int joint, float rad);
float   js_counts_to_rad(int joint, int32_t counts);

#endif /* JOINT_SPACE_H */
