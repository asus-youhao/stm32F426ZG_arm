/**
 * @file test_agents.c — WP-H1 驗收：app_main_tick() vs agent 化逐幀 diff
 *
 * 方法（等效 candump diff，但用確定性 C 假硬體，逐幀完全可重現）：
 *   sim_bus_set_tap() 記錄兩條 bus 上所有 TX（主站→從站）與 RX（從站回應）幀，
 *   以相同刺激序列（關節命令、急停開關）各跑一輪：
 *     A) 舊版 app_main_tick() 單體管線 ×2 → 驗證同行程重跑的確定性
 *     B) 舊版 vs app_agents（loop engine 驅動）→ 逐幀 byte 級比對
 *   任何一幀（id/dlc/data/bus/方向/順序）不同即 FAIL。
 */
#include "test_framework.h"
#include "loop_engine.h"
#include "app_agents.h"
#include "dual_arm.h"
#include "control_rate.h"
#include <string.h>

/* 假時鐘（port_now_us 定義於 test_engine.c） */
extern uint64_t g_fake_now_us;

/* app_main.c 對外（無公用標頭，與 pc_master_main.c 同樣以 extern 取用） */
void app_main_init(void);
void app_main_tick(void);
void app_set_estop(int active);
void app_joint_move(int joint, float rad);

/* sim 後端的 frame tap（co_bxcan_sim.c） */
void sim_bus_set_tap(void (*fn)(co_bus_t, int, const co_frame_t *));

/* ---- 幀記錄 ---- */
typedef struct {
    uint16_t id;
    uint8_t  bus, dir_tx, dlc;
    uint8_t  data[8];
} rec_t;

#define REC_MAX 80000
#define SLOTS   2
static rec_t s_rec[SLOTS][REC_MAX];
static int   s_nrec[SLOTS];
static int   s_slot;
static int   s_overflow;

static void tap(co_bus_t bus, int dir_tx, const co_frame_t *f)
{
    if (s_nrec[s_slot] >= REC_MAX) { s_overflow = 1; return; }
    rec_t *r = &s_rec[s_slot][s_nrec[s_slot]++];
    memset(r, 0, sizeof(*r));
    r->id = f->id; r->bus = (uint8_t)bus;
    r->dir_tx = (uint8_t)dir_tx; r->dlc = f->dlc;
    memcpy(r->data, f->data, f->dlc <= 8 ? f->dlc : 8);
}

static void capture_begin(int slot)
{
    s_slot = slot; s_nrec[slot] = 0; s_overflow = 0;
    sim_bus_set_tap(tap);
}
static void capture_end(void) { sim_bus_set_tap(0); }

/* ---- 兩輪共用的刺激序列（於每 tick 前施加,涵蓋運動與安全路徑）---- */
#define TICKS 1200
static void stimulus(int t)
{
    if (t == 50)  app_joint_move(0, 0.3f);    /* 左肩 */
    if (t == 60)  app_joint_move(9, -0.4f);   /* 右肘 */
    if (t == 400) app_set_estop(1);           /* 急停 → safe stop 幀 */
    if (t == 520) app_set_estop(0);           /* 復歸 → 重新使能交握 */
    if (t == 700) app_joint_move(6, 0.2f);    /* 左腕 */
}

/* ---- A 模式：舊版單體管線 ---- */
static void run_legacy(int slot)
{
    capture_begin(slot);
    app_main_init();
    CHECK(dual_arm_present_count() == 14);
    for (int t = 0; t < TICKS; t++) { stimulus(t); app_main_tick(); }
    capture_end();
    CHECK(!s_overflow);
}

/* ---- B 模式：loop engine + app_agents ---- */
static void run_agents(int slot)
{
    capture_begin(slot);
    app_main_init();
    CHECK(dual_arm_present_count() == 14);

    loop_engine_t e;
    eng_cfg_t cfg = { .dt_us = CONTROL_DT_US };
    eng_init(&e, &cfg);
    CHECK(app_agents_register(&e) == 0);
    CHECK(eng_configure(&e) == 0);
    CHECK(eng_activate(&e) == 0);
    for (int t = 0; t < TICKS; t++) {
        stimulus(t);
        g_fake_now_us = eng_next_deadline_us(&e);   /* 準時喚醒 */
        eng_tick(&e);
    }
    CHECK(eng_stats(&e)->ticks == TICKS);
    CHECK(eng_stats(&e)->overruns == 0 && eng_stats(&e)->miss == 0);
    CHECK(eng_agent_stats(&e, 2)->runs == TICKS);   /* motion 每 tick 都跑 */
    eng_deactivate(&e);
    capture_end();
    CHECK(!s_overflow);
}

/* ---- 逐幀比對；不同時印出第一個差異幀 ---- */
static void diff_streams(const char *label)
{
    CHECK(s_nrec[0] == s_nrec[1]);
    int n = s_nrec[0] < s_nrec[1] ? s_nrec[0] : s_nrec[1];
    int bad = -1;
    for (int i = 0; i < n; i++)
        if (memcmp(&s_rec[0][i], &s_rec[1][i], sizeof(rec_t)) != 0) { bad = i; break; }
    CHECK(bad == -1);
    if (bad >= 0 || s_nrec[0] != s_nrec[1]) {
        printf("  [%s] frames=%d/%d first-diff=%d\n", label, s_nrec[0], s_nrec[1], bad);
        if (bad >= 0)
            for (int s = 0; s < SLOTS; s++) {
                const rec_t *r = &s_rec[s][bad];
                printf("    slot%d: bus%d %s id=0x%03X dlc=%d "
                       "%02X %02X %02X %02X %02X %02X\n",
                       s, r->bus, r->dir_tx ? "TX" : "RX", r->id, r->dlc,
                       r->data[0], r->data[1], r->data[2],
                       r->data[3], r->data[4], r->data[5]);
            }
    }
}

void test_agents(void)
{
    /* A) 舊版重跑確定性：同刺激兩輪必須逐幀一致（否則 diff 法無效） */
    run_legacy(0);
    run_legacy(1);
    CHECK(s_nrec[0] > 30000);            /* 有實際流量（14 軸 × 1200 tick） */
    diff_streams("legacy-vs-legacy");

    /* B) 舊版 vs agent 化：行為不變的硬驗收 */
    run_legacy(0);
    run_agents(1);
    diff_streams("legacy-vs-agents");
}
