/**
 * @file    app_io_agents.c
 * @brief   WP-H2：CommandAgent / TelemetryAgent 實作
 *
 * 兩個 agent 都只掛 HOUSEKEEP 相位（不碰 bus、不影響控制序列，
 * 所以 H1 的逐幀 diff 驗收對它們同樣成立）。
 */
#include "app_io_agents.h"
#include "spsc_ring.h"
#include "dual_arm.h"
#include "joint_space.h"
#include "dual_arm_ctrl.h"
#include "task_space.h"
#include "safety.h"

/* app_main.c 內部存取（同 app_agents.c 慣例） */
dual_arm_ctrl_t *app_ctrl(void);
void app_set_mode(da_mode_t m);

/* ---- ring 儲存（靜態、容量 2 的冪）---- */
#define CMD_CAP  16
#define TELE_CAP 64
static uint8_t s_cmd_mem[CMD_CAP * sizeof(app_cmd_t)];
static uint8_t s_tele_mem[TELE_CAP * sizeof(app_tele_t)];
static spsc_t  s_cmd_q, s_tele_q;
static uint32_t s_tele_drops;
static loop_engine_t *s_eng;

/* ---- CommandAgent：HOUSEKEEP 消化 cmd ring（每次最多 4 筆）---- */
static void cmd_housekeep(void *ctx)
{
    (void)ctx;
    app_cmd_t c;
    for (int n = 0; n < 4 && spsc_pop(&s_cmd_q, &c); n++) {
        switch (c.op) {
            case APP_CMD_JOINT_MOVE:
                if (c.idx < ARM_COUNT * JOINTS_PER_ARM) js_move_to(c.idx, c.val);
                break;
            case APP_CMD_ESTOP:
                safety_set_estop(c.val != 0.0f);
                break;
            case APP_CMD_MODE:
                app_set_mode((da_mode_t)c.idx);
                break;
            default: break;
        }
    }
}

/* ---- TelemetryAgent：HOUSEKEEP 產生快照 ---- */
static void tele_housekeep(void *ctx)
{
    (void)ctx;
    app_tele_t t;
    const eng_stats_t *st = eng_stats(s_eng);
    pose_t L, R;

    ts_get_pose(&app_ctrl()->left, &L);
    ts_get_pose(&app_ctrl()->right, &R);

    t.tick        = st->ticks;
    t.sys_state   = (uint8_t)safety_state();
    t.sw0         = g_jstate[0].statusword;
    t.pos0        = g_jstate[0].pos_actual;
    t.tgt0        = g_jstate[0].target_pos;
    t.tx_drops    = dual_arm_tx_drops();
    t.late_max_us = st->late_max_us;
    t.miss        = (uint64_t)st->miss + st->overruns;
    for (int i = 0; i < 3; i++) { t.lpos[i] = L.p[i]; t.rpos[i] = R.p[i]; }

    if (!spsc_push(&s_tele_q, &t)) s_tele_drops++;   /* 滿：丟新留舊 */
}

static agent_t s_ag_cmd = {
    .name = "command", .divisor = 10, .phase_offset = 3, .budget_us = 50,
    .housekeep = cmd_housekeep,
};
static agent_t s_ag_tele = {
    .name = "telemetry", .divisor = 5, .phase_offset = 1, .budget_us = 100,
    .housekeep = tele_housekeep,
};

int app_io_register(loop_engine_t *e)
{
    s_eng = e;
    s_tele_drops = 0;
    if (spsc_init(&s_cmd_q,  s_cmd_mem,  sizeof(app_cmd_t),  CMD_CAP))  return -1;
    if (spsc_init(&s_tele_q, s_tele_mem, sizeof(app_tele_t), TELE_CAP)) return -1;
    if (eng_register(e, &s_ag_cmd))  return -1;
    if (eng_register(e, &s_ag_tele)) return -1;
    return 0;
}

bool     app_io_cmd_push(const app_cmd_t *c) { return spsc_push(&s_cmd_q, c); }
bool     app_io_tele_pop(app_tele_t *t)      { return spsc_pop(&s_tele_q, t); }
uint32_t app_io_tele_drops(void)             { return s_tele_drops; }
