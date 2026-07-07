/**
 * @file test_dc_pll.c — DC 鎖相驗收（項目 8;WP-L2.2 的 SIL 預演）
 *
 * 1) PI 單元：方向、限幅、殘差進位。
 * 2) E2E（完整 app 棧 + ec_master_sim 漂移模型 @1 kHz）：
 *    - 無鎖相基線：+200 ppm 漂移 → 相位誤差線性發散（~0.2 µs/tick）
 *    - 開鎖相：同漂移 → 收斂後 |err| ≤ 3 µs（整數 trim + 進位可跟上
 *      次 µs 級漂移）;反向 -500 ppm 亦收斂
 *    真機（WP-L2.2）驗收門檻 σ<10 µs,本測試在理想時鐘下驗證控制器
 *    本身的收斂性與極性正確。
 */
#include "test_framework.h"
#include "ec_dc_pll.h"
#include "ec_master.h"
#include "ec_master_sim.h"
#include "bus_if.h"
#include "loop_engine.h"
#include "app_agents.h"
#include "app_io_agents.h"

extern uint64_t g_fake_now_us;

void app_main_init_hz(float hz);
void app_select_bus(const bus_if_t *bus);
int  app_present_count(void);
extern const bus_if_t g_bus_canopen, g_bus_ecat;
void bus_ecat_dc_pll_enable(loop_engine_t *e);
void bus_ecat_dc_pll_disable(void);

static void tick_on_time(loop_engine_t *e)
{
    g_fake_now_us = eng_next_deadline_us(e);
    eng_tick(e);
}

static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

/* 起一套 ecat 全棧 engine（1 kHz）;回傳前已使能 */
static void start_stack(loop_engine_t *e)
{
    app_select_bus(&g_bus_ecat);
    app_main_init_hz(1000.0f);
    CHECK(app_present_count() == 14);
    eng_cfg_t cfg = { .dt_us = 1000 };
    eng_init(e, &cfg);
    CHECK(app_agents_register(e) == 0);
    CHECK(app_io_register(e) == 0);
    eng_configure(e);
    g_fake_now_us = 0;
    CHECK(eng_activate(e) == 0);
    for (int t = 0; t < 50; t++) tick_on_time(e);
}

void test_dc_pll(void)
{
    /* ---- 1) PI 單元 ---- */
    {
        ec_dc_pll_t p;
        ec_dc_pll_init(&p, 0, 0, 0);
        CHECK(p.kp > 0.0f && p.ki > 0.0f && p.max_trim_us == 50);
        CHECK(ec_dc_pll_step(&p, 100) < 0);      /* 晚到 → 提前喚醒 */
        ec_dc_pll_init(&p, 0, 0, 10);
        CHECK(ec_dc_pll_step(&p, 10000) == -10); /* 限幅 */
        /* 殘差進位：固定小誤差,累計輸出不可為 0（整數直接捨會凍結） */
        ec_dc_pll_init(&p, 0.1f, 0.001f, 50);
        int32_t sum = 0;
        for (int i = 0; i < 50; i++) sum += ec_dc_pll_step(&p, 2);
        CHECK(sum < 0);
    }

    /* ---- 2a) 基線：+200 ppm、無鎖相 → 誤差發散 ---- */
    {
        loop_engine_t e;
        start_stack(&e);
        bus_ecat_dc_pll_disable();
        phu_ecat_set_dc_drift(1000, +200);       /* 柵格 1000.2 µs */
        for (int t = 0; t < 2000; t++) tick_on_time(&e);
        int32_t err = ec_master_dc_error_us();
        CHECK(iabs32(err) > 100);                /* ~0.2µs/tick × 2000 = 400 */
        eng_deactivate(&e);
    }

    /* ---- 2b) 開鎖相：+200 ppm → 收斂;殘差有界 ---- */
    {
        loop_engine_t e;
        start_stack(&e);
        phu_ecat_set_dc_drift(1000, +200);
        bus_ecat_dc_pll_enable(&e);
        for (int t = 0; t < 300; t++) tick_on_time(&e);   /* 收斂期 */
        int32_t worst = 0;
        for (int t = 0; t < 2000; t++) {
            tick_on_time(&e);
            int32_t a = iabs32(ec_master_dc_error_us());
            if (a > worst) worst = a;
        }
        CHECK(worst <= 3);                        /* 鎖定後 |err| ≤ 3 µs */
        bus_ecat_dc_pll_disable();
        eng_deactivate(&e);
    }

    /* ---- 2c) 反向 -500 ppm 亦收斂 ---- */
    {
        loop_engine_t e;
        start_stack(&e);
        phu_ecat_set_dc_drift(1000, -500);
        bus_ecat_dc_pll_enable(&e);
        for (int t = 0; t < 300; t++) tick_on_time(&e);
        int32_t worst = 0;
        for (int t = 0; t < 2000; t++) {
            tick_on_time(&e);
            int32_t a = iabs32(ec_master_dc_error_us());
            if (a > worst) worst = a;
        }
        CHECK(worst <= 3);
        bus_ecat_dc_pll_disable();
        eng_deactivate(&e);
        app_select_bus(&g_bus_canopen);           /* 還原 */
    }
}
