/**
 * @file    app_main.c
 * @brief   應用進入點：整合 L1–L4 全棧 + 500 Hz 控制迴圈
 *
 * 資料流（每 500 Hz tick = 2 ms）：
 *   回授 g_jstate(counts) → joint_space/task_space 回灌
 *   → da_ctrl_tick_1khz()（L4 協同 → L3 IK → L2 軌跡 → counts）
 *   → dual_arm_set_target() + dual_arm_tick()（L1 CSP 下發）
 *
 * 頻率選 500 Hz 的原因見 control_rate.h（Classic CAN 頻寬限制）。
 * 整合：見 docs/design/firmware-cubemx-integration.md。
 */
#include "dual_arm.h"          /* L1 */
#include "joint_space.h"       /* L2 */
#include "task_space.h"        /* L3 */
#include "dual_arm_ctrl.h"     /* L4 */
#include "safety.h"            /* WP6 */
#include "bus_if.h"            /* WP-H5 */
#include "robot_config.h"
#include "control_rate.h"
#include "stm32f7xx_hal.h"
#include <string.h>

static volatile bool s_ready = false;
static dual_arm_ctrl_t s_dc;
static task_arm_t s_left, s_right;

/* WP-H5：匯流排後端 vtable。預設 CANopen;init 前可 app_select_bus() 切換。
   COMPUTE 側（safety/motion）只碰 g_jstate,經由 s_bus 收發 → bus-agnostic。 */
extern const bus_if_t g_bus_canopen;
static const bus_if_t *s_bus = &g_bus_canopen;

void app_select_bus(const bus_if_t *bus) { if (bus) s_bus = bus; }
const bus_if_t *app_bus(void) { return s_bus; }
int app_present_count(void) { return s_bus->present_count(); }

/** @brief 執行期頻率版 init（WP-H2 `--rate`；對應 WP-C3.2 檔位化）。hz≤0 用預設。 */
void app_main_init_hz(float hz)
{
    if (hz <= 0.0f) hz = CONTROL_HZ;

    /* L1：匯流排 bus-up（CANopen: NMT/PDO/CSP;EtherCAT: PREOP/OP/DC） */
    if (s_bus->init() != 0) return;

    /* L2：joint_space（14 軸設定,dt = 1/hz） */
    js_init(1.0f / hz, robot_js_cfg());

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

void app_main_init(void) { app_main_init_hz(CONTROL_HZ); }

/** @brief 500 Hz 控制 tick（由 TIM6 ISR 呼叫,週期 CONTROL_DT_US=2000us）。 */
void app_main_tick(void)
{
    if (!s_ready) return;

    /* 1) 收回授 + 回灌（counts → rad） */
    s_bus->pump_rx();
    float q_fb[14];
    for (int j = 0; j < 14; j++) {
        js_update_feedback(j, g_jstate[j].pos_actual);
        q_fb[j] = js_counts_to_rad(j, g_jstate[j].pos_actual);
    }
    da_ctrl_sync_feedback(&s_dc, q_fb);

    /* 2) WP6 安全：只在「真的收到新 TPDO」的軸更新看門狗時間戳,
       否則某軸失聯時 last_ms 會被持續刷新而永遠偵測不到（修正前的 bug）。 */
    uint32_t now = HAL_GetTick();
    uint16_t ecode;
    for (int j = 0; j < 14; j++) {
        if (g_jstate[j].fb_fresh)
            safety_report_joint(j, g_jstate[j].statusword, now);
        /* WP-H4/G5：故障事件（EMCY / CiA402 fault）→ safe stop 條款 */
        if (g_jstate[j].present && s_bus->take_fault(j, &ecode))
            safety_report_emcy(j, ecode != 0);
    }
    bool allow = safety_update(now);
    s_bus->set_safe_stop(!allow, safety_safe_controlword());

    /* 3) L4→L3→L2：產生 counts 目標 */
    int32_t cnt[14];
    (void)da_ctrl_tick_1khz(&s_dc, cnt);

    /* 4) L1：下發 CSP 目標 + PDO 交換（安全停止時內部覆寫）。
       未達 RUNNING 門檻（見 safety require_all_enabled_for_run）時目標鎖在
       實際位置：使能交握照常進行,但不產生運動。 */
    bool run = (safety_state() == SYS_RUNNING);
    for (int j = 0; j < 14; j++)
        s_bus->set_target((uint8_t)j, run ? cnt[j] : g_jstate[j].pos_actual);
    s_bus->tick();
}

/* WP6 對外：急停 / 系統狀態 */
void app_set_estop(int active) { safety_set_estop(active != 0); }
const char *app_sys_state(void) { return safety_state_str(); }

/* ---- 對外控制 API（供上位機命令解析呼叫）---- */
void app_set_left_pose(const pose_t *p)  { da_ctrl_set_left_target(&s_dc, p); }
void app_set_right_pose(const pose_t *p) { da_ctrl_set_right_target(&s_dc, p); }
void app_set_mode(da_mode_t m)           { da_ctrl_set_mode(&s_dc, m); }
void app_joint_move(int joint, float rad){ js_move_to(joint, rad); }

/* ---- WP-H1：agent 化需要的內部存取（app_agents.c 用）---- */
dual_arm_ctrl_t *app_ctrl(void) { return &s_dc; }
bool app_is_ready(void) { return s_ready; }

/* ---- 觀測 API（讀「實際被控」的 s_dc 內部臂,非初始化用的 s_left/s_right）---- */
void app_get_left_pose(pose_t *p)  { ts_get_pose(&s_dc.left, p); }
void app_get_right_pose(pose_t *p) { ts_get_pose(&s_dc.right, p); }
const task_arm_t *app_get_left_arm(void)  { return &s_dc.left; }
const task_arm_t *app_get_right_arm(void) { return &s_dc.right; }
int app_safety_hold(void) { return s_dc.safety_hold ? 1 : 0; }
