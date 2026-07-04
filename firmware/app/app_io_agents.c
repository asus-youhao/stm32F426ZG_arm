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
#include "co_emcy.h"
#include "co_nmt.h"
#include <string.h>

/* app_main.c 內部存取（同 app_agents.c 慣例） */
dual_arm_ctrl_t *app_ctrl(void);
void app_set_mode(da_mode_t m);

/* ---- ring 儲存（靜態、容量 2 的冪）---- */
#define CMD_CAP    16
#define TELE_CAP   64
#define HEALTH_CAP 8
static uint8_t s_cmd_mem[CMD_CAP * sizeof(app_cmd_t)];
static uint8_t s_tele_mem[TELE_CAP * sizeof(app_tele_t)];
static uint8_t s_health_mem[HEALTH_CAP * sizeof(app_health_t)];
static spsc_t  s_cmd_q, s_tele_q, s_health_q;
static uint32_t s_tele_drops;
static loop_engine_t *s_eng;
static uint32_t s_last_tx[CO_BUS_COUNT], s_last_rx[CO_BUS_COUNT];

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

/* ---- HealthAgent：per-bus 匯流排儀表（WP-H4/G6）----
 * busload 估算：視窗內成功收發幀數 × ~130 bits/幀（6B PDO 含 stuffing 估計,
 * 見 linux-canopen-master-plan.md §5.2）÷ 視窗時間;1 Mbps → 1 bit/µs。 */
static void health_housekeep(void *ctx)
{
    (void)ctx;
    const uint32_t window_us = 100u * s_eng->cfg.dt_us;   /* divisor × dt */
    for (int b = 0; b < CO_BUS_COUNT; b++) {
        uint32_t tx, rx;
        dual_arm_frame_counts((co_bus_t)b, &tx, &rx);
        uint32_t d = (tx - s_last_tx[b]) + (rx - s_last_rx[b]);
        s_last_tx[b] = tx;
        s_last_rx[b] = rx;

        /* 該 bus present 軸中 heartbeat 逾時（>1.5×週期,heartbeat 1Hz→取 2s）數 */
        uint32_t stale = 0;
        for (int j = 0; j < ARM_COUNT * JOINTS_PER_ARM; j++)
            if (g_jstate[j].present && g_joints[j].bus == (co_bus_t)b &&
                co_nmt_node_age_ms((co_bus_t)b, g_joints[j].node_id) > 2000u)
                stale++;

        app_health_t rec = {
            .bus = (uint8_t)b,
            .h = {
                .tx_drop    = dual_arm_tx_drops(),        /* 目前為全域累計 */
                .rx_lost    = stale,
                .err_events = co_emcy_count((co_bus_t)b),
                .link_ok    = 1,   /* SocketCAN error frame 解析屬後續工作 */
                .sync_ok    = dual_arm_sync_enabled() ? 1 : 0,
                .load_pct   = (uint16_t)((uint64_t)d * 130u * 100u / window_us),
                .proto      = {0},
            },
        };
        (void)spsc_push(&s_health_q, &rec);   /* 滿：丟新,消費端定期抽 */
    }
}

static agent_t s_ag_cmd = {
    .name = "command", .divisor = 10, .phase_offset = 3, .budget_us = 50,
    .housekeep = cmd_housekeep,
};
static agent_t s_ag_tele = {
    .name = "telemetry", .divisor = 5, .phase_offset = 1, .budget_us = 100,
    .housekeep = tele_housekeep,
};
static agent_t s_ag_health = {
    .name = "health", .divisor = 100, .phase_offset = 7, .budget_us = 100,
    .housekeep = health_housekeep,
};

int app_io_register(loop_engine_t *e)
{
    s_eng = e;
    s_tele_drops = 0;
    memset(s_last_tx, 0, sizeof(s_last_tx));
    memset(s_last_rx, 0, sizeof(s_last_rx));
    if (spsc_init(&s_cmd_q,  s_cmd_mem,  sizeof(app_cmd_t),  CMD_CAP))  return -1;
    if (spsc_init(&s_tele_q, s_tele_mem, sizeof(app_tele_t), TELE_CAP)) return -1;
    if (spsc_init(&s_health_q, s_health_mem, sizeof(app_health_t), HEALTH_CAP))
        return -1;
    if (eng_register(e, &s_ag_cmd))    return -1;
    if (eng_register(e, &s_ag_tele))   return -1;
    if (eng_register(e, &s_ag_health)) return -1;
    return 0;
}

bool     app_io_cmd_push(const app_cmd_t *c) { return spsc_push(&s_cmd_q, c); }
bool     app_io_tele_pop(app_tele_t *t)      { return spsc_pop(&s_tele_q, t); }
bool     app_io_health_pop(app_health_t *h)  { return spsc_pop(&s_health_q, h); }
uint32_t app_io_tele_drops(void)             { return s_tele_drops; }
