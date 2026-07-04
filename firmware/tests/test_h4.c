/**
 * @file test_h4.c — WP-H4 驗收：G5 EMCY、G6 health 儀表、G3 SYNC 鎖存
 *
 * 全部在 C 假硬體（co_bxcan_sim + phu_sim）上：
 *   1) EMCY 解析單元（co_emcy）
 *   2) EMCY→safety E2E：注入 EMCY → SYS_FAULT;error reset(0x0000) → 復歸
 *   3) HealthAgent E2E：busload 估算落在合理區間（500Hz 全速 ≈ 90%,
 *      對照 linux-canopen-master-plan.md §5.2 預算表）
 *   4) SYNC 模式 E2E：init 寫 transmission type=1、每 tick 先發 SYNC、
 *      從站於 SYNC 鎖存/回 TPDO、使能與運動照常
 */
#include "test_framework.h"
#include "loop_engine.h"
#include "app_agents.h"
#include "app_io_agents.h"
#include "dual_arm.h"
#include "co_emcy.h"
#include "control_rate.h"
#include "safety.h"
#include <string.h>

extern uint64_t g_fake_now_us;

void app_main_init(void);
void app_main_tick(void);
void app_joint_move(int joint, float rad);
void sim_bus_inject_rx(co_bus_t bus, const co_frame_t *f);
void sim_bus_set_tap(void (*fn)(co_bus_t, int, const co_frame_t *));

/* ---- EMCY 幀組裝（CiA301：[code u16][reg u8][vendor 5B]）---- */
static co_frame_t emcy_frame(uint8_t node, uint16_t code)
{
    co_frame_t f = {0};
    f.id = (uint16_t)(CO_COBID_EMCY_BASE + node);
    f.dlc = 8;
    f.data[0] = (uint8_t)(code & 0xFF);
    f.data[1] = (uint8_t)(code >> 8);
    f.data[2] = (code != 0) ? 0x01 : 0x00;   /* error register */
    return f;
}

/* ---- SYNC 統計 tap ---- */
static int s_sync_tx[CO_BUS_COUNT];
static int s_tpdo_rx[CO_BUS_COUNT];
static int s_rpdo_before_sync;   /* 違反「SYNC 先於 RPDO」的次數（bus0） */
static int s_seen_sync_bus0;
static void h4_tap(co_bus_t bus, int dir_tx, const co_frame_t *f)
{
    if (dir_tx && f->id == CO_COBID_SYNC) {
        s_sync_tx[bus]++;
        if (bus == CO_BUS_LEFT) s_seen_sync_bus0 = 1;
    }
    if (!dir_tx && f->id >= CO_COBID_TPDO1_BASE + 1 &&
        f->id <= CO_COBID_TPDO1_BASE + 0x7F)
        s_tpdo_rx[bus]++;
    if (dir_tx && bus == CO_BUS_LEFT && !s_seen_sync_bus0 &&
        f->id >= CO_COBID_RPDO1_BASE + 1 && f->id <= CO_COBID_RPDO1_BASE + 0x7F)
        s_rpdo_before_sync++;    /* 第一個 SYNC 之前不得有任何 RPDO */
}

static void tick_on_time(loop_engine_t *e)
{
    g_fake_now_us = eng_next_deadline_us(e);
    eng_tick(e);
}

void test_h4(void)
{
    /* ---- 1) EMCY 解析單元 ---- */
    {
        co_emcy_reset();
        co_frame_t f = emcy_frame(3, 0x2310);          /* E231 過流類 */
        CHECK(co_emcy_process_frame(CO_BUS_LEFT, &f));
        uint16_t code = 0;
        CHECK(co_emcy_take(CO_BUS_LEFT, 3, &code) && code == 0x2310);
        CHECK(!co_emcy_take(CO_BUS_LEFT, 3, &code));   /* 已取走 */
        CHECK(co_emcy_last_code(CO_BUS_LEFT, 3) == 0x2310);
        CHECK(co_emcy_count(CO_BUS_LEFT) == 1);
        co_frame_t sync = {0};
        sync.id = CO_COBID_SYNC;                        /* 0x080 非 EMCY */
        CHECK(!co_emcy_process_frame(CO_BUS_LEFT, &sync));
    }

    /* ---- 2) EMCY → safety E2E（async 模式）---- */
    {
        dual_arm_set_sync(false);
        app_main_init();
        CHECK(dual_arm_present_count() == 14);
        for (int t = 0; t < 100; t++) app_main_tick();
        CHECK(safety_state() == SYS_RUNNING || safety_state() == SYS_ENABLED);

        co_frame_t f = emcy_frame(3, 0x2310);          /* 左臂 J3（idx 2） */
        sim_bus_inject_rx(CO_BUS_LEFT, &f);
        for (int t = 0; t < 3; t++) app_main_tick();
        CHECK(safety_state() == SYS_FAULT);            /* EMCY = safe stop 條款 */

        f = emcy_frame(3, 0x0000);                     /* error reset → 復歸 */
        sim_bus_inject_rx(CO_BUS_LEFT, &f);
        for (int t = 0; t < 50; t++) app_main_tick();
        CHECK(safety_state() == SYS_RUNNING || safety_state() == SYS_ENABLED);
        CHECK(co_emcy_count(CO_BUS_LEFT) == 2);
    }

    /* ---- 3) HealthAgent E2E（busload 估算）---- */
    {
        dual_arm_set_sync(false);
        app_main_init();
        loop_engine_t e;
        eng_cfg_t cfg = { .dt_us = CONTROL_DT_US };
        eng_init(&e, &cfg);
        CHECK(app_agents_register(&e) == 0);
        CHECK(app_io_register(&e) == 0);
        CHECK(eng_configure(&e) == 0);
        g_fake_now_us = 0;
        CHECK(eng_activate(&e) == 0);
        for (int t = 0; t < 150; t++) tick_on_time(&e);

        /* health 於 tick 7 與 107 各推兩筆（左右 bus）;取 tick 107 的滿視窗值 */
        app_health_t h, last[CO_BUS_COUNT];
        int n = 0;
        memset(last, 0, sizeof(last));
        while (app_io_health_pop(&h)) { last[h.bus] = h; n++; }
        CHECK(n >= 4);
        /* 500 Hz 全速 7 軸×2 幀/tick ≈ 91% 負載（§5.2 預算）;容忍 ±15 */
        CHECK(last[CO_BUS_LEFT].h.load_pct > 75 && last[CO_BUS_LEFT].h.load_pct <= 100);
        CHECK(last[CO_BUS_RIGHT].h.load_pct > 75 && last[CO_BUS_RIGHT].h.load_pct <= 100);
        CHECK(last[CO_BUS_LEFT].h.err_events == 0);
        CHECK(last[CO_BUS_LEFT].h.sync_ok == 0);       /* async 模式 */
        eng_deactivate(&e);
    }

    /* ---- 4) SYNC 鎖存模式 E2E（G3）---- */
    {
        dual_arm_set_sync(true);
        memset(s_sync_tx, 0, sizeof(s_sync_tx));
        memset(s_tpdo_rx, 0, sizeof(s_tpdo_rx));
        s_rpdo_before_sync = 0;
        s_seen_sync_bus0 = 0;

        app_main_init();
        CHECK(dual_arm_present_count() == 14);

        sim_bus_set_tap(h4_tap);
        const int TICKS = 300;
        for (int t = 0; t < TICKS; t++) app_main_tick();
        sim_bus_set_tap(0);

        CHECK(s_sync_tx[CO_BUS_LEFT] == TICKS);        /* 每 tick 恰一個 SYNC */
        CHECK(s_sync_tx[CO_BUS_RIGHT] == TICKS);
        CHECK(s_rpdo_before_sync == 0);                /* SYNC 永遠先於 RPDO */
        CHECK(s_tpdo_rx[CO_BUS_LEFT] == TICKS * 7);    /* TPDO 只在 SYNC 邊緣回 */

        /* 使能與運動在 SYNC 模式下照常 */
        int enabled = 0;
        for (int j = 0; j < 14; j++) enabled += g_jstate[j].enabled ? 1 : 0;
        CHECK(enabled == 14);
        int32_t pos_before = g_jstate[0].pos_actual;
        app_joint_move(0, 0.3f);
        for (int t = 0; t < 400; t++) app_main_tick();
        CHECK(g_jstate[0].pos_actual != pos_before);

        dual_arm_set_sync(false);                      /* 還原,不污染其他測試 */
    }
}
