/**
 * @file    app_main.c
 * @brief   應用進入點：整合 L1–L4 全棧 + 1 kHz 控制迴圈
 *
 * 資料流（每 1 kHz tick）：
 *   回授 g_jstate(counts) → joint_space/task_space 回灌
 *   → da_ctrl_tick_1khz()（L4 協同 → L3 IK → L2 軌跡 → counts）
 *   → dual_arm_set_target() + dual_arm_tick_1khz()（L1 CSP 下發）
 *
 * 整合：見 docs/design/firmware-cubemx-integration.md。
 */
#include "dual_arm.h"          /* L1 */
#include "joint_space.h"       /* L2 */
#include "task_space.h"        /* L3 */
#include "dual_arm_ctrl.h"     /* L4 */
#include "safety.h"            /* WP6 */
#include "robot_config.h"
#include "stm32f7xx_hal.h"
#include <string.h>

static volatile bool s_ready = false;
static dual_arm_ctrl_t s_dc;
static task_arm_t s_left, s_right;

void app_main_init(void)
{
    /* L1：bxCAN + CANopen + CiA402（雙 channel） */
    if (dual_arm_init() != CO_OK) return;

    /* L2：joint_space（14 軸設定） */
    js_init(0.001f, robot_js_cfg());

    /* L3：兩臂 task_space（DH + IK + joint_space 起始索引） */
    ts_init(&s_left,  robot_left_kin(),  robot_ik_cfg(), 0, robot_q_init());
    ts_init(&s_right, robot_right_kin(), robot_ik_cfg(), 7, robot_q_init());

    /* L4：雙臂協同（預設獨立模式,最小末端距離 5 cm 保護） */
    da_ctrl_init(&s_dc, &s_left, &s_right, DA_MODE_INDEPENDENT, 0.05f);

    /* WP6：安全（通訊逾時 50ms） */
    safety_cfg_t scfg = { .comms_timeout_ms = 50, .require_all_enabled_for_run = false };
    safety_init(&scfg);

    s_ready = true;
}

/** @brief 1 kHz 控制 tick（由 TIM6 ISR 呼叫）。 */
void app_main_tick(void)
{
    if (!s_ready) return;

    /* 1) 收回授 + 回灌（counts → rad） */
    dual_arm_pump_rx();
    float q_fb[14];
    for (int j = 0; j < 14; j++) {
        js_update_feedback(j, g_jstate[j].pos_actual);
        q_fb[j] = js_counts_to_rad(j, g_jstate[j].pos_actual);
    }
    da_ctrl_sync_feedback(&s_dc, q_fb);

    /* 2) WP6 安全：彙整狀態,決定是否允許運動 */
    uint32_t now = HAL_GetTick();
    for (int j = 0; j < 14; j++)
        safety_report_joint(j, g_jstate[j].statusword, now);
    bool allow = safety_update(now);
    dual_arm_set_safe_stop(!allow, safety_safe_controlword());

    /* 3) L4→L3→L2：產生 counts 目標 */
    int32_t cnt[14];
    (void)da_ctrl_tick_1khz(&s_dc, cnt);

    /* 4) L1：下發 CSP 目標 + PDO 交換（安全停止時內部覆寫） */
    for (int j = 0; j < 14; j++) dual_arm_set_target(j, cnt[j]);
    dual_arm_tick_1khz();
}

/* WP6 對外：急停 / 系統狀態 */
void app_set_estop(int active) { safety_set_estop(active != 0); }
const char *app_sys_state(void) { return safety_state_str(); }

/* ---- 對外控制 API（供上位機命令解析呼叫）---- */
void app_set_left_pose(const pose_t *p)  { da_ctrl_set_left_target(&s_dc, p); }
void app_set_right_pose(const pose_t *p) { da_ctrl_set_right_target(&s_dc, p); }
void app_set_mode(da_mode_t m)           { da_ctrl_set_mode(&s_dc, m); }
void app_joint_move(int joint, float rad){ js_move_to(joint, rad); }

/* ---- 觀測 API（讀「實際被控」的 s_dc 內部臂,非初始化用的 s_left/s_right）---- */
void app_get_left_pose(pose_t *p)  { ts_get_pose(&s_dc.left, p); }
void app_get_right_pose(pose_t *p) { ts_get_pose(&s_dc.right, p); }
const task_arm_t *app_get_left_arm(void)  { return &s_dc.left; }
const task_arm_t *app_get_right_arm(void) { return &s_dc.right; }
int app_safety_hold(void) { return s_dc.safety_hold ? 1 : 0; }
