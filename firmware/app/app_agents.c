/**
 * @file    app_agents.c
 * @brief   WP-H1：app_main_tick() 的 agent 化（行為不變的重構）
 *
 * 對照 app_main_tick() 四步驟 → 相位（docs/design/harness-agent-loop-engine-plan.md §4.2）：
 *   步驟1 收回授+回灌  → bus_rx.cycle_read（pump）+ motion.cycle_read（回灌）
 *   步驟2 安全看門狗    → safety.cycle_compute
 *   步驟3 L4→L2 產生目標 → motion.cycle_compute
 *   步驟4 CSP 下發+PDO  → motion.cycle_write（set_target）+ bus_tx.cycle_write（dual_arm_tick）
 *
 * 執行序列（engine 四相位 pass × 註冊順序）與原單體版完全相同：
 *   pump_rx → js回灌+sync_feedback → safety → da_ctrl_tick → set_target → dual_arm_tick
 * H1 驗收 = tests/test_agents.c 以 C 假硬體逐幀 diff 舊/新兩版的 bus 行為。
 */
#include "app_agents.h"
#include "bus_if.h"            /* WP-H5：經 vtable 收發,不再直呼 dual_arm_* */
#include "dual_arm.h"          /* g_jstate / 關節數常數（資料面共用） */
#include "joint_space.h"       /* L2 */
#include "dual_arm_ctrl.h"     /* L4 */
#include "safety.h"            /* WP6 */
#include "eng_log.h"           /* log ring（§5.3） */
#include "stm32f7xx_hal.h"

/* app_main.c 內部存取（同 board/main.c 的 extern 慣例） */
dual_arm_ctrl_t *app_ctrl(void);
bool app_is_ready(void);
const bus_if_t *app_bus(void);

#define NJ (ARM_COUNT * JOINTS_PER_ARM)

/* motion.cycle_compute 產生、motion.cycle_write 消費（同一 agent、同一 tick 內） */
static int32_t s_cnt[NJ];

/* ---- agent: bus_rx ——原步驟 1 前半 ---- */
static int ag_ready(void *ctx)
{
    (void)ctx;
    return app_is_ready() ? 0 : -1;   /* app_main_init() 未成功不得啟用 */
}

static void busrx_read(void *ctx)
{
    (void)ctx;
    app_bus()->pump_rx();
}

/* ---- agent: safety ——原步驟 2（逐字保留：只在 fb_fresh 的軸刷看門狗） ---- */
static void safety_compute(void *ctx)
{
    (void)ctx;
    uint32_t now = HAL_GetTick();
    uint16_t ecode;
    for (int j = 0; j < NJ; j++) {
        if (g_jstate[j].fb_fresh)
            safety_report_joint(j, g_jstate[j].statusword, now);
        /* WP-H4/G5：故障事件 → safe stop 條款（與 app_main_tick 逐字一致） */
        if (g_jstate[j].present && app_bus()->take_fault(j, &ecode)) {
            safety_report_emcy(j, ecode != 0);
            eng_log(EL_WARN, ELC_FAULT_EVT, j, (int32_t)ecode);
        }
    }
    bool allow = safety_update(now);
    app_bus()->set_safe_stop(!allow, safety_safe_controlword());

    /* safe stop 邊緣 → log ring（RT 只記代碼,格式化在非 RT） */
    static bool s_prev_allow = true;
    if (allow != s_prev_allow) {
        s_prev_allow = allow;
        eng_log(allow ? EL_INFO : EL_ERR,
                allow ? ELC_SAFE_STOP_OFF : ELC_SAFE_STOP_ON,
                (int32_t)safety_state(), 0);
    }
}

/* ---- agent: motion ——原步驟 1 後半 + 步驟 3 + 步驟 4 前半 ---- */
static void motion_read(void *ctx)
{
    (void)ctx;
    float q_fb[NJ];
    for (int j = 0; j < NJ; j++) {
        js_update_feedback(j, g_jstate[j].pos_actual);
        q_fb[j] = js_counts_to_rad(j, g_jstate[j].pos_actual);
    }
    da_ctrl_sync_feedback(app_ctrl(), q_fb);
}

static void motion_compute(void *ctx)
{
    (void)ctx;
    (void)da_ctrl_tick_1khz(app_ctrl(), s_cnt);
}

static void motion_write(void *ctx)
{
    (void)ctx;
    bool run = (safety_state() == SYS_RUNNING);
    for (int j = 0; j < NJ; j++)
        app_bus()->set_target((uint8_t)j, run ? s_cnt[j] : g_jstate[j].pos_actual);
}

/* ---- agent: bus_tx ——原步驟 4 後半（註冊最後,write pass 收尾送 PDO） ---- */
static void bustx_write(void *ctx)
{
    (void)ctx;
    app_bus()->tick();
}

/* budget 為觀測用初值（500Hz 週期 2000µs 的相對量級）;H1 不掛 on_fault */
static agent_t s_ag_busrx = {
    .name = "bus_rx", .divisor = 1, .budget_us = 300,
    .on_activate = ag_ready, .cycle_read = busrx_read,
};
static agent_t s_ag_safety = {
    .name = "safety", .divisor = 1, .budget_us = 100,
    .cycle_compute = safety_compute,
};
static agent_t s_ag_motion = {
    .name = "motion", .divisor = 1, .budget_us = 800,
    .cycle_read = motion_read, .cycle_compute = motion_compute,
    .cycle_write = motion_write,
};
static agent_t s_ag_bustx = {
    .name = "bus_tx", .divisor = 1, .budget_us = 300,
    .cycle_write = bustx_write,
};

int app_agents_register(loop_engine_t *e)
{
    /* 註冊順序 = 各相位內執行順序：safety 在 motion 前、bus_tx 最後 */
    if (eng_register(e, &s_ag_busrx))  return -1;
    if (eng_register(e, &s_ag_safety)) return -1;
    if (eng_register(e, &s_ag_motion)) return -1;
    if (eng_register(e, &s_ag_bustx))  return -1;
    return 0;
}
