/**
 * @file test_log.c — log ring 驗收（項目 5,輕量級）
 *
 * 1) 單元：FIFO 順序、滿→丟新+drop 計數、時戳/欄位。
 * 2) E2E：estop on/off 經 cmd ring → safety 邊緣 → SAFE_STOP_ON/OFF
 *    紀錄出現在 log ring（RT 路徑零 printf 的替代觀測管道）。
 */
#include "test_framework.h"
#include "eng_log.h"
#include "bus_if.h"
#include "loop_engine.h"
#include "app_agents.h"
#include "app_io_agents.h"
#include "control_rate.h"

extern uint64_t g_fake_now_us;

void app_main_init(void);
void app_select_bus(const bus_if_t *bus);
int  app_present_count(void);
extern const bus_if_t g_bus_canopen;

static void tick_on_time(loop_engine_t *e)
{
    g_fake_now_us = eng_next_deadline_us(e);
    eng_tick(e);
}

void test_log(void)
{
    /* ---- 1) 單元 ---- */
    {
        eng_log_init();
        eng_log_rec_t r;
        CHECK(!eng_log_pop(&r));

        g_fake_now_us = 12345;
        CHECK(eng_log(EL_WARN, ELC_FAULT_EVT, 3, 0x2310));
        CHECK(eng_log_pop(&r));
        CHECK(r.t_us == 12345 && r.level == EL_WARN &&
              r.code == ELC_FAULT_EVT && r.a == 3 && r.b == 0x2310);

        int pushed = 0;                       /* 填滿 → 丟新 + drop 計數 */
        while (eng_log(EL_INFO, ELC_AXIS_STALE, pushed, 0)) pushed++;
        CHECK(pushed >= 63);
        CHECK(!eng_log(EL_INFO, ELC_AXIS_STALE, 999, 0));
        CHECK(eng_log_drops() == 2);
        CHECK(eng_log_pop(&r) && r.a == 0);   /* FIFO：最舊先出 */
        eng_log_init();
        CHECK(eng_log_drops() == 0);
    }

    /* ---- 2) E2E：estop 邊緣 → SAFE_STOP_ON/OFF 紀錄 ---- */
    {
        app_select_bus(&g_bus_canopen);
        app_main_init();
        CHECK(app_present_count() == 14);

        loop_engine_t e;
        eng_cfg_t cfg = { .dt_us = CONTROL_DT_US };
        eng_init(&e, &cfg);
        CHECK(app_agents_register(&e) == 0);
        CHECK(app_io_register(&e) == 0);      /* 內含 eng_log_init */
        eng_configure(&e);
        g_fake_now_us = 0;
        CHECK(eng_activate(&e) == 0);
        for (int t = 0; t < 60; t++) tick_on_time(&e);   /* 使能穩定 */

        eng_log_rec_t r;
        while (eng_log_pop(&r)) { }           /* 清掉啟動期紀錄 */

        app_cmd_t c = { .op = APP_CMD_ESTOP, .val = 1.0f };
        CHECK(app_io_cmd_push(&c));
        for (int t = 0; t < 40; t++) tick_on_time(&e);
        int on = 0, off = 0;
        while (eng_log_pop(&r))
            if (r.code == ELC_SAFE_STOP_ON)  on++;
            else if (r.code == ELC_SAFE_STOP_OFF) off++;
        CHECK(on == 1 && off == 0);           /* 邊緣觸發,只記一筆 */

        c.val = 0.0f;                         /* 復歸 */
        CHECK(app_io_cmd_push(&c));
        for (int t = 0; t < 200; t++) tick_on_time(&e);
        on = off = 0;
        while (eng_log_pop(&r))
            if (r.code == ELC_SAFE_STOP_ON)  on++;
            else if (r.code == ELC_SAFE_STOP_OFF) off++;
        CHECK(off == 1 && on == 0);

        eng_deactivate(&e);
    }
}
