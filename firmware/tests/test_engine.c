/**
 * @file test_engine.c — WP-H0 loop engine 核心 + SPSC ring 單元測試
 *
 * 驗收對照 docs/design/harness-agent-loop-engine-plan.md §9 H0：
 *   SKIP 政策（不補跑、重錨定、升級通知）、divisor/phase_offset、
 *   預算量測與 AG_FAULT_BUDGET、生命週期（含 activate 失敗回滾）、
 *   相位 pass 順序、SPSC push/pop/回繞。
 * 假時鐘：本檔提供 port_now_us()，測試手動推進。
 */
#include "test_framework.h"
#include "loop_engine.h"
#include "spsc_ring.h"
#include "eng_port.h"
#include <string.h>

/* ---- 假時鐘 ---- */
static uint64_t s_now;
uint64_t port_now_us(void) { return s_now; }

/* ---- 升級通知計數 ---- */
static int s_esc;
static void on_esc(void *user, int reason)
{
    (void)user;
    if (reason == ENG_ESC_OVERRUN) s_esc++;
}

/* ---- 測試 agent：記錄呼叫次數 / 全域相位序列 / 可模擬耗時與失敗 ---- */
#define SEQ_MAX 64
static int s_seq[SEQ_MAX], s_seq_n;

typedef struct {
    int id;
    int cfg_n, act_n, deact_n, read_n, comp_n, write_n, house_n, fault_n;
    agent_fault_t last_fault;
    int cfg_ret, act_ret;
    uint32_t comp_advance_us;   /* compute 內推進假時鐘 = 模擬計算耗時 */
    int log_seq;                /* 是否記錄到全域相位序列 */
} tagent_t;

static void seq_log(tagent_t *t, int phase)
{
    if (t->log_seq && s_seq_n < SEQ_MAX) s_seq[s_seq_n++] = t->id * 10 + phase;
}
static int  t_cfg(void *c, const eng_cfg_t *e) { (void)e; tagent_t *t = c; t->cfg_n++; return t->cfg_ret; }
static int  t_act(void *c)   { tagent_t *t = c; t->act_n++; return t->act_ret; }
static void t_deact(void *c) { tagent_t *t = c; t->deact_n++; }
static void t_read(void *c)  { tagent_t *t = c; t->read_n++;  seq_log(t, ENG_PH_READ); }
static void t_comp(void *c)  { tagent_t *t = c; t->comp_n++;  seq_log(t, ENG_PH_COMPUTE);
                               s_now += t->comp_advance_us; }
static void t_write(void *c) { tagent_t *t = c; t->write_n++; seq_log(t, ENG_PH_WRITE); }
static void t_house(void *c) { tagent_t *t = c; t->house_n++; seq_log(t, ENG_PH_HOUSE); }
static void t_fault(void *c, agent_fault_t f) { tagent_t *t = c; t->fault_n++; t->last_fault = f; }

static void tagent_bind(agent_t *a, tagent_t *t, const char *name)
{
    memset(a, 0, sizeof(*a));
    a->name = name; a->ctx = t;
    a->on_configure = t_cfg;   a->on_activate = t_act;  a->on_deactivate = t_deact;
    a->cycle_read = t_read;    a->cycle_compute = t_comp;
    a->cycle_write = t_write;  a->housekeep = t_house;  a->on_fault = t_fault;
}

/* 準時推進：睡到 deadline 再 tick */
static void tick_on_time(loop_engine_t *e)
{
    s_now = eng_next_deadline_us(e);
    eng_tick(e);
}

void test_engine(void)
{
    const eng_cfg_t CFG = { .dt_us = 1000, .on_escalate = on_esc };

    /* ---- 生命週期：configure/activate 順序、deactivate 反序後仍可再啟用 ---- */
    {
        loop_engine_t e;
        agent_t a1, a2; tagent_t t1 = { .id = 1 }, t2 = { .id = 2 };
        tagent_bind(&a1, &t1, "a1"); tagent_bind(&a2, &t2, "a2");
        eng_init(&e, &CFG);
        CHECK(e.cfg.late_warn_us == 100);          /* 預設 dt/10 */
        CHECK(eng_activate(&e) == -1);             /* IDLE 不可 activate */
        CHECK(eng_register(&e, &a1) == 0);
        CHECK(eng_register(&e, &a2) == 0);
        CHECK(eng_configure(&e) == 0 && e.state == ENG_CONFIGURED);
        CHECK(t1.cfg_n == 1 && t2.cfg_n == 1);
        s_now = 0;
        CHECK(eng_activate(&e) == 0 && e.state == ENG_ACTIVE);
        CHECK(eng_register(&e, &a1) == -1);        /* ACTIVE 不可註冊 */
        CHECK(eng_next_deadline_us(&e) == 1000);   /* now+dt */
        eng_deactivate(&e);
        CHECK(e.state == ENG_CONFIGURED && t1.deact_n == 1 && t2.deact_n == 1);
    }

    /* ---- activate 第 2 個失敗 → 第 1 個被反序回滾 ---- */
    {
        loop_engine_t e;
        agent_t a1, a2; tagent_t t1 = { .id = 1 }, t2 = { .id = 2, .act_ret = -1 };
        tagent_bind(&a1, &t1, "a1"); tagent_bind(&a2, &t2, "a2");
        eng_init(&e, &CFG);
        eng_register(&e, &a1); eng_register(&e, &a2);
        CHECK(eng_configure(&e) == 0);
        CHECK(eng_activate(&e) == -1);
        CHECK(t1.act_n == 1 && t1.deact_n == 1);   /* 已啟用者被回滾 */
        CHECK(t2.deact_n == 0);                    /* 失敗者不回滾 */
        CHECK(e.state == ENG_CONFIGURED);
    }

    /* ---- 相位 pass 順序：所有 read → 所有 compute → 所有 write → house ---- */
    {
        loop_engine_t e;
        agent_t a1, a2;
        tagent_t t1 = { .id = 1, .log_seq = 1 }, t2 = { .id = 2, .log_seq = 1 };
        tagent_bind(&a1, &t1, "a1"); tagent_bind(&a2, &t2, "a2");
        eng_init(&e, &CFG);
        eng_register(&e, &a1); eng_register(&e, &a2);
        eng_configure(&e); s_now = 0; eng_activate(&e);
        s_seq_n = 0;
        tick_on_time(&e);
        const int want[8] = { 10, 20, 11, 21, 12, 22, 13, 23 };
        CHECK(s_seq_n == 8);
        for (int i = 0; i < 8; i++) CHECK(s_seq[i] == want[i]);
    }

    /* ---- divisor / phase_offset 錯峰 ---- */
    {
        loop_engine_t e;
        agent_t af, a4, a2o0; tagent_t tf = { .id = 1 }, t4 = { .id = 2 }, t2 = { .id = 3 };
        tagent_bind(&af, &tf, "fast");                    /* divisor 1 */
        tagent_bind(&a4, &t4, "div4off1"); a4.divisor = 4; a4.phase_offset = 1;
        tagent_bind(&a2o0, &t2, "div2");   a2o0.divisor = 2;
        eng_init(&e, &CFG);
        eng_register(&e, &af); eng_register(&e, &a4); eng_register(&e, &a2o0);
        eng_configure(&e); s_now = 0; eng_activate(&e);
        for (int i = 0; i < 8; i++) tick_on_time(&e);     /* tick 0..7 */
        CHECK(tf.comp_n == 8);
        CHECK(t4.comp_n == 2);                            /* tick 1,5 */
        CHECK(t2.comp_n == 4);                            /* tick 0,2,4,6 */
        CHECK(eng_stats(&e)->ticks == 8);
        CHECK(eng_stats(&e)->miss == 0 && eng_stats(&e)->overruns == 0);
    }

    /* ---- 遲到分級：輕度 miss / overrun SKIP 不補跑 / 重錨定 ---- */
    {
        loop_engine_t e;
        agent_t a; tagent_t t = { .id = 1 };
        tagent_bind(&a, &t, "a");
        eng_init(&e, &CFG);
        eng_register(&e, &a);
        eng_configure(&e); s_now = 0; eng_activate(&e);   /* deadline=1000 */

        /* 輕度遲到：late=200（warn=100 ≤ 200 < dt=1000）→ miss，deadline 照排 */
        s_now = 1200; eng_tick(&e);
        CHECK(eng_stats(&e)->miss == 1 && eng_stats(&e)->overruns == 0);
        CHECK(eng_next_deadline_us(&e) == 2000);          /* 不重錨定 */
        CHECK(eng_stats(&e)->late_max_us == 200);

        /* overrun：late=3500 ≥ dt → 跳過 3 tick、重錨定 now+dt、agent 只多跑一次 */
        int before = t.comp_n;
        s_now = 5500; eng_tick(&e);
        CHECK(eng_stats(&e)->overruns == 1);
        CHECK(eng_stats(&e)->skipped == 3);
        CHECK(eng_next_deadline_us(&e) == 6500);          /* now+dt 重錨定 */
        CHECK(t.comp_n == before + 1);                    /* SKIP：無 burst 補跑 */
    }

    /* ---- 連續 overrun 升級：第 3 次通知一次；復原後重新計 ---- */
    {
        loop_engine_t e;
        agent_t a; tagent_t t = { .id = 1 };
        tagent_bind(&a, &t, "a");
        eng_init(&e, &CFG);                               /* escalate_n 預設 3 */
        eng_register(&e, &a);
        eng_configure(&e); s_now = 0; eng_activate(&e);
        s_esc = 0;
        for (int i = 0; i < 4; i++) {                     /* 連續 4 次 overrun */
            s_now = eng_next_deadline_us(&e) + 2000;
            eng_tick(&e);
        }
        CHECK(s_esc == 1);                                /* 只在 ==3 時通知一次 */
        tick_on_time(&e);                                 /* 準時 → consec 歸零 */
        CHECK(eng_stats(&e)->overrun_consec == 0);
        for (int i = 0; i < 3; i++) {
            s_now = eng_next_deadline_us(&e) + 2000;
            eng_tick(&e);
        }
        CHECK(s_esc == 2);                                /* 新 streak 再通知 */
    }

    /* ---- WCET 預算：連續 3 次超標 → on_fault(BUDGET) 一次；達標歸零 ---- */
    {
        loop_engine_t e;
        agent_t a; tagent_t t = { .id = 1, .comp_advance_us = 100 };
        tagent_bind(&a, &t, "hog"); a.budget_us = 50;
        eng_init(&e, &CFG);                               /* budget_over_n 預設 3 */
        eng_register(&e, &a);
        eng_configure(&e); s_now = 0; eng_activate(&e);

        for (int i = 0; i < 4; i++) tick_on_time(&e);     /* 4 次都超標 */
        CHECK(t.fault_n == 1 && t.last_fault == AG_FAULT_BUDGET);
        const agent_stats_t *st = eng_agent_stats(&e, 0);
        CHECK(st->budget_over_total == 4);
        CHECK(st->max_us[ENG_PH_COMPUTE] == 100);
        CHECK(st->hist[3] == 4);                          /* 100µs → ≤100 桶 */
        CHECK(st->runs == 4);

        t.comp_advance_us = 0;                            /* 恢復達標 → 歸零 */
        tick_on_time(&e);
        CHECK(eng_agent_stats(&e, 0)->budget_over_consec == 0);
        t.comp_advance_us = 100;                          /* 新 streak 再通知 */
        for (int i = 0; i < 3; i++) tick_on_time(&e);
        CHECK(t.fault_n == 2);
    }
}

void test_spsc(void)
{
    static uint32_t mem[8];
    spsc_t q;
    uint32_t v;

    /* 容量必須為 2 的冪 */
    CHECK(spsc_init(&q, mem, sizeof(uint32_t), 3) == -1);
    CHECK(spsc_init(&q, mem, sizeof(uint32_t), 0) == -1);
    CHECK(spsc_init(&q, mem, sizeof(uint32_t), 8) == 0);

    /* 空 pop / 滿 push 邊界 */
    CHECK(!spsc_pop(&q, &v));
    for (uint32_t i = 0; i < 8; i++) { v = 100 + i; CHECK(spsc_push(&q, &v)); }
    CHECK(spsc_count(&q) == 8);
    v = 999; CHECK(!spsc_push(&q, &v));                   /* 滿：丟新留舊 */

    /* FIFO 順序 */
    for (uint32_t i = 0; i < 8; i++) {
        CHECK(spsc_pop(&q, &v));
        CHECK(v == 100 + i);
    }
    CHECK(!spsc_pop(&q, &v) && spsc_count(&q) == 0);

    /* 回繞：跨越索引邊界 1000 次仍保序 */
    for (uint32_t i = 0; i < 1000; i++) {
        CHECK(spsc_push(&q, &i));
        CHECK(spsc_pop(&q, &v) && v == i);
    }

    /* 交錯：3 進 1 出 6 進 = 8 滿 */
    for (uint32_t i = 0; i < 3; i++) spsc_push(&q, &i);
    spsc_pop(&q, &v);
    for (uint32_t i = 0; i < 6; i++) { CHECK(spsc_push(&q, &i)); }
    v = 0; CHECK(!spsc_push(&q, &v));
    CHECK(spsc_count(&q) == 8);
}
