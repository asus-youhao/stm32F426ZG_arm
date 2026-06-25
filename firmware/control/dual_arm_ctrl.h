/**
 * @file    dual_arm_ctrl.h
 * @brief   L4 雙臂協同控制（整合 L1–L4,1 kHz 共用時基）
 *
 * 串接：task_space(左/右) → joint_space(14軸) → dual_arm(L1 CSP 下發)。
 * 協同模式：
 *   - INDEPENDENT：兩臂各自追隨各自笛卡爾目標。
 *   - COORDINATED：兩臂共用時基同步更新（同上,但保證同週期）。
 *   - BIMANUAL：右臂目標 = 左臂位姿 × 相對變換（雙手夾持同物件）。
 */
#ifndef DUAL_ARM_CTRL_H
#define DUAL_ARM_CTRL_H

#include <stdint.h>
#include "task_space.h"

typedef enum {
    DA_MODE_INDEPENDENT = 0,
    DA_MODE_COORDINATED,
    DA_MODE_BIMANUAL
} da_mode_t;

typedef struct {
    task_arm_t left;
    task_arm_t right;
    da_mode_t  mode;

    /* BIMANUAL：右相對左的相對位姿（p_rel, R_rel） */
    pose_t     rel_right_in_left;

    /* 安全：兩末端最小距離（m），小於則保持不前進 */
    float      min_ee_distance;
    bool       safety_hold;     /* 觸發保護中 */
} dual_arm_ctrl_t;

/** @brief 初始化（提供已 ts_init 過的兩臂、模式、安全距離）。 */
void da_ctrl_init(dual_arm_ctrl_t *dc,
                  const task_arm_t *left, const task_arm_t *right,
                  da_mode_t mode, float min_ee_distance);

void da_ctrl_set_mode(dual_arm_ctrl_t *dc, da_mode_t mode);

/** @brief 設定單臂笛卡爾目標（INDEPENDENT/COORDINATED）。 */
void da_ctrl_set_left_target(dual_arm_ctrl_t *dc, const pose_t *p);
void da_ctrl_set_right_target(dual_arm_ctrl_t *dc, const pose_t *p);

/** @brief 設定 BIMANUAL 相對位姿（右相對左）。 */
void da_ctrl_set_relative(dual_arm_ctrl_t *dc, const pose_t *rel_right_in_left);

/** @brief 由 14 軸回授回灌兩臂關節角（q_fb 長度 14：左0..6, 右7..13）。 */
void da_ctrl_sync_feedback(dual_arm_ctrl_t *dc, const float q_fb14[14]);

/**
 * @brief 1 kHz 全棧 tick：
 *   1) BIMANUAL 時由左臂位姿算右臂目標
 *   2) 自碰撞/工作空間檢查（必要時保持）
 *   3) 兩臂 task_space 各走一步 IK → joint_space 設定點
 *   4) joint_space → counts → dual_arm 下發
 * @param out_counts 長度 14（可 NULL）。
 * @return 兩臂位姿誤差和（粗略）。
 */
float da_ctrl_tick_1khz(dual_arm_ctrl_t *dc, int32_t out_counts[14]);

#endif /* DUAL_ARM_CTRL_H */
