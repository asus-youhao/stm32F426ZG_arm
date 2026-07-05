/**
 * @file test_trace.c — trace ring 驗收（項目 3;WP-H3 儀器,設計文件 §3.6）
 *
 * 1) 單元：未啟用 no-op、push/pop 欄位、滿→丟新+drop、FIFO。
 * 2) engine 整合：準時 tick → late=0/相位耗時正確;miss/overrun tick →
 *    旗標與 late 值;相位耗時 >65535 飽和;預算連續超標 → BUDGET_OVER
 *    log 上下文（哪個 agent、耗時多少）。
 */
#include "test_framework.h"
#include "eng_log.h"
#include "eng_trace.h"
#include "loop_engine.h"
#include <string.h>

extern uint64_t g_fake_now_us;   /* test_engine.c 的假時鐘 */

/* ---- 最小假 agent：compute 內推進假時鐘 = 模擬耗時 ---- */
static uint32_t s_burn_us;
static void burn_comp(void *c) { (void)c; g_fake_now_us += s_burn_us; }

static void tick_on_time(loop_engine_t *e)
{
    g_fake_now_us = eng_next_deadline_us(e);
    eng_tick(e);
}

void test_trace(void)
{
    /* ---- 1) 單元 ---- */
    {
        eng_trace_disable();
        eng_trace_rec_t r = { .t_us = 1, .late_us = 2 };
        CHECK(!eng_trace_enabled());
        CHECK(!eng_trace_push(&r));           /* 未啟用：no-op */
        CHECK(!eng_trace_pop(&r));

        static eng_trace_rec_t mem[8];
        CHECK(eng_trace_init(mem, 6) != 0);   /* 非 2 的冪 */
        CHECK(eng_trace_init(mem, 8) == 0);
        CHECK(eng_trace_enabled() && eng_trace_drops() == 0);

        for (uint32_t i = 0; i < 8; i++) {
            r = (eng_trace_rec_t){ .t_us = i, .late_us = i * 10,
                                   .ph_us = { 1, 2, 3, 4 }, .flags = ETR_MISS };
            CHECK(eng_trace_push(&r));
        }
        CHECK(!eng_trace_push(&r) && !eng_trace_push(&r));
        CHECK(eng_trace_drops() == 2);        /* 滿：丟新 + 計數 */

        CHECK(eng_trace_pop(&r));             /* FIFO：最舊先出,欄位完整 */
        CHECK(r.t_us == 0 && r.late_us == 0 && r.ph_us[ENG_PH_READ] == 1 &&
              r.ph_us[ENG_PH_HOUSE] == 4 && r.flags == ETR_MISS);
        int n = 1;
        while (eng_trace_pop(&r)) n++;
        CHECK(n == 8 && r.t_us == 7);
    }

    /* ---- 2) engine 整合 ---- */
    {
        static eng_trace_rec_t mem[256];
        CHECK(eng_trace_init(mem, 256) == 0);
        eng_log_init();

        loop_engine_t e;
        eng_cfg_t cfg = { .dt_us = 1000 };    /* warn 預設 dt/10=100 µs */
        agent_t a;
        memset(&a, 0, sizeof(a));
        a.name = "burn";
        a.cycle_compute = burn_comp;
        a.budget_us = 100;                    /* budget_over_n 預設 3 */
        eng_init(&e, &cfg);
        CHECK(eng_register(&e, &a) == 0);
        CHECK(eng_configure(&e) == 0);
        g_fake_now_us = 0;
        CHECK(eng_activate(&e) == 0);

        s_burn_us = 200;                      /* 5 個準時 tick,每次算 200 µs */
        for (int t = 0; t < 5; t++) tick_on_time(&e);

        eng_trace_rec_t r;
        for (int t = 0; t < 5; t++) {
            CHECK(eng_trace_pop(&r));
            CHECK(r.late_us == 0 && r.flags == 0);
            CHECK(r.ph_us[ENG_PH_COMPUTE] == 200);
            CHECK(r.ph_us[ENG_PH_READ] == 0 && r.ph_us[ENG_PH_WRITE] == 0 &&
                  r.ph_us[ENG_PH_HOUSE] == 0);
        }
        CHECK(!eng_trace_pop(&r));

        eng_log_rec_t lr;                     /* 預算連續 3 次超標 → 一筆上下文 */
        int budget_logs = 0;
        while (eng_log_pop(&lr))
            if (lr.code == ELC_BUDGET_OVER) {
                budget_logs++;
                CHECK(lr.a == 0 && lr.b == 200);   /* agent idx / 耗時 */
            }
        CHECK(budget_logs == 1);

        s_burn_us = 0;                        /* miss：遲到 150（warn≤late<dt） */
        g_fake_now_us = eng_next_deadline_us(&e) + 150;
        eng_tick(&e);
        CHECK(eng_trace_pop(&r) && r.late_us == 150 && r.flags == ETR_MISS);

        g_fake_now_us = eng_next_deadline_us(&e) + 1500;   /* overrun */
        eng_tick(&e);
        CHECK(eng_trace_pop(&r) && r.late_us == 1500 && r.flags == ETR_OVERRUN);

        s_burn_us = 70000;                    /* 相位耗時 >65535 → 飽和 */
        tick_on_time(&e);
        CHECK(eng_trace_pop(&r) && r.ph_us[ENG_PH_COMPUTE] == 0xFFFF);

        eng_deactivate(&e);
        eng_trace_disable();                  /* 不影響其他測試 */
    }
}
