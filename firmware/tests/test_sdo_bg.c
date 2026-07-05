/**
 * @file test_sdo_bg.c — SDO/CoE 背景通道驗收（H4 遞延項,設計文件 §5.3）
 *
 * 1) 單元：佇列滿、逾時、abort/讀寫回應解析（直接注入 frame）。
 * 2) SIL E2E（CANopen,C 假硬體）：RUN 中經背景通道讀/寫 OD——
 *    - 回應正確（0x6061=8、寫 0x6083 成功）
 *    - **不擾動 PDO 時序**：背景讀期間每 tick 的 RPDO 幀數不變,
 *      額外流量僅 0x600/0x580 幀
 * 3) E2E（EtherCAT 後端）：同一非 RT API 讀 CoE。
 */
#include "test_framework.h"
#include "sdo_bg.h"
#include "bus_if.h"
#include "loop_engine.h"
#include "app_agents.h"
#include "app_io_agents.h"
#include "dual_arm.h"
#include "control_rate.h"
#include <string.h>

extern uint64_t g_fake_now_us;

void app_main_init(void);
void app_main_init_hz(float hz);
void app_select_bus(const bus_if_t *bus);
int  app_present_count(void);
extern const bus_if_t g_bus_canopen, g_bus_ecat;

/* sim frame tap（co_bxcan_sim.c）:統計 RPDO/SDO 幀 */
void sim_bus_set_tap(void (*fn)(co_bus_t, int, const co_frame_t *));
static uint32_t s_n_rpdo, s_n_sdo;
static void tap(co_bus_t bus, int dir_tx, const co_frame_t *f)
{
    (void)bus; (void)dir_tx;
    if (f->id >= 0x200 && f->id <= 0x27F) s_n_rpdo++;
    if ((f->id >= 0x580 && f->id <= 0x67F)) s_n_sdo++;
}

static void tick_on_time(loop_engine_t *e)
{
    g_fake_now_us = eng_next_deadline_us(e);
    eng_tick(e);
}

void test_sdo_bg(void)
{
    /* ---- 1a) 佇列滿 ---- */
    {
        sdo_bg_init(10);
        sdo_bg_req_t r = { .tag = 1, .bus = 0, .node = 1, .index = 0x6061 };
        int pushed = 0;
        while (sdo_bg_request(&r)) pushed++;
        CHECK(pushed >= 7 && pushed <= 8);       /* REQ_CAP-1..CAP（SPSC 實作） */
    }

    /* ---- 1b) 逾時：對不存在的 node（無回應） ---- */
    {
        sdo_bg_init(5);
        app_select_bus(&g_bus_canopen);
        app_main_init();                          /* 建 bus,才能 send */
        sdo_bg_req_t r = { .tag = 42, .bus = 0, .node = 99, .index = 0x1000 };
        CHECK(sdo_bg_request(&r));
        sdo_bg_rsp_t rsp;
        CHECK(!sdo_bg_poll(&rsp));
        for (int i = 0; i < 8; i++) sdo_bg_step_canopen();  /* 1 送 + 5 等 */
        CHECK(sdo_bg_poll(&rsp));
        CHECK(rsp.tag == 42 && rsp.status == SDO_BG_TIMEOUT);
    }

    /* ---- 1c) abort 解析（直接注入 0x80 回應幀） ---- */
    {
        sdo_bg_init(50);
        sdo_bg_req_t r = { .tag = 7, .bus = 0, .node = 3,
                           .index = 0x1234, .sub = 1 };
        CHECK(sdo_bg_request(&r));
        sdo_bg_step_canopen();                    /* 送出,進 WAIT */
        co_frame_t f = { .id = 0x583, .dlc = 8,
                         .data = { 0x80, 0x34, 0x12, 0x01,
                                   0x02, 0x00, 0x04, 0x06 } };
        CHECK(sdo_bg_on_frame(CO_BUS_LEFT, &f));
        sdo_bg_rsp_t rsp;
        CHECK(sdo_bg_poll(&rsp));
        CHECK(rsp.tag == 7 && rsp.status == SDO_BG_ABORT);
        CHECK(rsp.value == 0x06040002u);          /* abort code */
        /* index 不符的殘留回應不得被撿走 */
        CHECK(!sdo_bg_on_frame(CO_BUS_LEFT, &f)); /* 已 IDLE */
    }

    /* ---- 2) SIL E2E：RUN 中背景讀寫,PDO 時序不受擾動 ---- */
    {
        app_select_bus(&g_bus_canopen);
        app_main_init();
        CHECK(app_present_count() == 14);

        loop_engine_t e;
        eng_cfg_t cfg = { .dt_us = CONTROL_DT_US };
        eng_init(&e, &cfg);
        CHECK(app_agents_register(&e) == 0);
        CHECK(app_io_register(&e) == 0);          /* 內含 sdo_bg_init */
        eng_configure(&e);
        g_fake_now_us = 0;
        CHECK(eng_activate(&e) == 0);
        for (int t = 0; t < 50; t++) tick_on_time(&e);   /* 使能 */

        /* 基準：100 tick 的 RPDO 幀數（無背景流量） */
        s_n_rpdo = s_n_sdo = 0;
        sim_bus_set_tap(tap);
        for (int t = 0; t < 100; t++) tick_on_time(&e);
        uint32_t rpdo_base = s_n_rpdo;
        CHECK(s_n_sdo == 0);

        /* RUN 中讀 node3 的 0x6061（模式=CSP=8） */
        sdo_bg_req_t r = { .tag = 100, .bus = CO_BUS_LEFT, .node = 3,
                           .index = 0x6061, .sub = 0 };
        CHECK(sdo_bg_request(&r));
        s_n_rpdo = s_n_sdo = 0;
        sdo_bg_rsp_t rsp;
        bool got = false;
        for (int t = 0; t < 100; t++) {
            tick_on_time(&e);
            if (!got && sdo_bg_poll(&rsp)) got = true;
        }
        CHECK(got);
        CHECK(rsp.tag == 100 && rsp.status == SDO_BG_OK && rsp.value == 8);
        CHECK(s_n_rpdo == rpdo_base);             /* PDO 流量一幀不差 */
        CHECK(s_n_sdo == 2);                      /* 只多請求+回應各一幀 */

        /* RUN 中寫（0x6083 加速度,phu 接受任意寫） */
        r = (sdo_bg_req_t){ .tag = 101, .bus = CO_BUS_LEFT, .node = 5,
                            .index = 0x6083, .sub = 0,
                            .is_write = 1, .size = 4, .value = 12345 };
        CHECK(sdo_bg_request(&r));
        got = false;
        for (int t = 0; t < 100 && !got; t++) {
            tick_on_time(&e);
            if (sdo_bg_poll(&rsp)) got = true;
        }
        CHECK(got && rsp.tag == 101 && rsp.status == SDO_BG_OK);

        sim_bus_set_tap(0);
        eng_deactivate(&e);
    }

    /* ---- 3) EtherCAT 後端：同一 API ---- */
    {
        app_select_bus(&g_bus_ecat);
        app_main_init_hz(1000.0f);
        CHECK(app_present_count() == 14);

        loop_engine_t e;
        eng_cfg_t cfg = { .dt_us = 1000 };
        eng_init(&e, &cfg);
        CHECK(app_agents_register(&e) == 0);
        CHECK(app_io_register(&e) == 0);
        eng_configure(&e);
        g_fake_now_us = 0;
        CHECK(eng_activate(&e) == 0);

        sdo_bg_req_t r = { .tag = 200, .node = 4, .index = 0x6061, .sub = 0 };
        CHECK(sdo_bg_request(&r));
        sdo_bg_rsp_t rsp;
        bool got = false;
        for (int t = 0; t < 20 && !got; t++) {
            tick_on_time(&e);
            if (sdo_bg_poll(&rsp)) got = true;
        }
        CHECK(got && rsp.tag == 200 && rsp.status == SDO_BG_OK && rsp.value == 8);

        eng_deactivate(&e);
        app_select_bus(&g_bus_canopen);           /* 還原 */
    }
}
