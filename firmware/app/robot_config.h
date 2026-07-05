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

/* ---- 設定檔（項目 7）：key=value 覆寫編譯期預設 ----
 * 鍵格式（<i>=軸號,joint 0..13、dh/q_init 0..6;joint 支援 * 萬用）：
 *   joint.<i|*>.counts_per_rad|q_min|q_max|vmax|amax|offset_rad
 *   ik.lambda|step_gain|max_iters|pos_tol|rot_tol
 *   q_init.<i>
 *   base.left.x|y|z   base.right.x|y|z
 *   dh.left.<i>.a|alpha|d|theta   dh.right.<i>.a|alpha|d|theta
 * 需在 app_main_init 之前套用（js/kin 於 init 時複製設定）。 */

/** @brief 套用單鍵；回 0 成功、-1 未知鍵/越界。 */
int robot_config_apply(const char *key, float value);

/**
 * @brief 載入 key=value 檔（# 註解、空行略過）。
 * @return 套用鍵數；開檔失敗回 -1、任一行壞鍵/格式錯回 -2（不半套用：
 *         先全檔驗證再套用）。
 */
int robot_config_load(const char *path);

/** @brief 還原編譯期預設（測試/重載用）。 */
void robot_config_reset(void);

#endif /* ROBOT_CONFIG_H */
