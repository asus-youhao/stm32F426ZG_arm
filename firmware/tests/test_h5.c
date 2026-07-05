/**
 * @file test_h5.c — WP-H5 驗收：bus_if_t 雙後端 + 相位微調
 *
 * 1) eng_phase_trim_us：deadline 平移與 ±5% 限幅。
 * 2) bus 切換 E2E：**同一套 app agents（H1 的四個）零修改**跑在
 *    bus_ecat（ec_master_sim 後端）@1 kHz——init/使能/點到點運動/
 *    掉軸→safety 看門狗 50ms 內 safe stop→回線復歸。
 *    （CANopen 路徑的等價驗證 = test_agents 逐幀 diff,持續把關）
 */
#include "test_framework.h"
#include "loop_engine.h"
#include "bus_if.h"
#include "app_agents.h"
#include "app_io_agents.h"
#include "dual_arm.h"
#include "ec_master_sim.h"
#include "joint_space.h"
#include "safety.h"
#include <math.h>

extern uint64_t g_fake_now_us;      /* 假時鐘（test_engine.c） */

void app_main_init_hz(float hz);
void app_select_bus(const bus_if_t *bus);
const bus_if_t *app_bus(void);
int  app_present_count(void);
void app_joint_move(int joint, float rad);
extern const bus_if_t g_bus_canopen, g_bus_ecat;

static void tick_on_time(loop_engine_t *e)
{
    g_fake_now_us = eng_next_deadline_us(e);
    eng_tick(e);
}

void test_h5(void)
{
    /* ---- 1) eng_phase_trim_us ---- */
    {
        loop_engine_t e;
        eng_cfg_t cfg = { .dt_us = 1000 };
        eng_init(&e, &cfg);
        CHECK(eng_configure(&e) == 0);
        g_fake_now_us = 0;
        CHECK(eng_activate(&e) == 0);           /* deadline = 1000 */
        eng_phase_trim_us(&e, 20);
        CHECK(eng_next_deadline_us(&e) == 1020);
        eng_phase_trim_us(&e, -20);
        CHECK(eng_next_deadline_us(&e) == 1000);
        eng_phase_trim_us(&e, 500);             /* 限幅 +5% = 50 */
        CHECK(eng_next_deadline_us(&e) == 1050);
        eng_phase_trim_us(&e, -500);            /* 限幅 -50 */
        CHECK(eng_next_deadline_us(&e) == 1000);
        eng_deactivate(&e);
    }

    /* ---- 2) 同一套 agents 跑 EtherCAT 後端 @1 kHz ---- */
    {
        app_select_bus(&g_bus_ecat);
        app_main_init_hz(1000.0f);
        CHECK(app_present_count() == 14);
        CHECK(app_bus()->name[0] == 'e');

        loop_engine_t e;
        eng_cfg_t cfg = { .dt_us = 1000 };      /* 1 kHz（CANopen 上限外!） */
        eng_init(&e, &cfg);
        CHECK(app_agents_register(&e) == 0);    /* H1 的四個 agent,零修改 */
        CHECK(app_io_register(&e) == 0);
        CHECK(eng_configure(&e) == 0);
        g_fake_now_us = 0;
        CHECK(eng_activate(&e) == 0);

        /* 使能：全軸 operation enabled */
        for (int t = 0; t < 100; t++) tick_on_time(&e);
        int en = 0;
        for (int j = 0; j < 14; j++) en += g_jstate[j].enabled ? 1 : 0;
        CHECK(en == 14);
        CHECK(safety_state() == SYS_RUNNING || safety_state() == SYS_ENABLED);

        /* 點到點：J3 → 0.4 rad,經 L2 軌跡 + CSP 收斂 */
        int32_t tgt_counts = js_rad_to_counts(3, 0.4f);
        app_joint_move(3, 0.4f);
        for (int t = 0; t < 3000; t++) tick_on_time(&e);
        CHECK(g_jstate[3].pos_actual == tgt_counts);

        /* 掉軸：axis 6 offline → fb_fresh 消失 → 看門狗(50ms) → safe stop */
        phu_ecat_set_offline(6, true);
        for (int t = 0; t < 120; t++) tick_on_time(&e);   /* >50ms @1kHz */
        CHECK(safety_state() == SYS_FAULT || safety_state() == SYS_ESTOP);

        /* 回線 + 復歸急停 → 系統可再使能 */
        phu_ecat_set_offline(6, false);
        safety_set_estop(false);
        for (int t = 0; t < 400; t++) tick_on_time(&e);
        en = 0;
        for (int j = 0; j < 14; j++) en += g_jstate[j].enabled ? 1 : 0;
        CHECK(en == 14);

        /* health（bus 0 = EtherCAT 儀表）：WKC 全對、DC 鎖定 */
        bus_health_t h;
        app_bus()->health(0, 100000, &h);
        CHECK(h.sync_ok == 1 && h.link_ok == 1);
        CHECK(h.proto[0] == h.proto[1]);        /* 實得 WKC == 期望 */

        eng_deactivate(&e);
        app_select_bus(&g_bus_canopen);         /* 還原,勿影響其他測試 */
    }
}
