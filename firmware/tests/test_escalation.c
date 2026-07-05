/**
 * @file test_escalation.c — §5.2 升級策略表逐格驗收（項目 2）
 *
 * 覆蓋策略表每一格的第二層動作：
 *   g1) 連續 overrun 升級：有降頻設定 → 降頻一次（RUN 續跑）;再犯 → SAFE_STOP
 *   g2) miss 率視窗超標 → 同上路徑
 *   g3) 無降頻設定 → 直接 SAFE_STOP（原行為不變）
 *   g4) bus link 掉 → restart_bus 嘗試一次;持續掉 N 次 → SAFE_STOP;
 *       恢復 → 計數清零、允許下次再重啟
 *   g5) 非關鍵 agent 連續超預算 → 停用（runs 停止增加）;關鍵 agent 不受影響
 *   g6) 降頻語意：deactivate→改 dt→reactivate（agent 生命週期各 +1）,
 *       on_rate_changed 通知,心跳/視窗基準重錨定
 */
#include "test_framework.h"
#include "harness.h"
#include <string.h>

extern uint64_t g_fake_now_us;

/* ---- 記錄用 callbacks ---- */
static int s_stop_n, s_restart_n, s_rate_n;
static uint32_t s_rate_last;
static void cb_stop(void *u)    { (void)u; s_stop_n++; }
static int  cb_restart(void *u) { (void)u; s_restart_n++; return 0; }
static void cb_rate(void *u, uint32_t dt) { (void)u; s_rate_n++; s_rate_last = dt; }

/* ---- 可控 agent：compute 可拖時間（超預算注入）+ 生命週期計數 ---- */
typedef struct { int act, deact, runs; uint32_t drag_us; } tag_t;
static int  t_act(void *c)   { ((tag_t *)c)->act++; return 0; }
static void t_deact(void *c) { ((tag_t *)c)->deact++; }
static void t_comp(void *c)  { tag_t *t = c; t->runs++; g_fake_now_us += t->drag_us; }

static void bind(agent_t *a, tag_t *t, const char *name, uint32_t budget)
{
    memset(a, 0, sizeof(*a));
    memset(t, 0, sizeof(*t));
    a->name = name; a->divisor = 1; a->budget_us = budget; a->ctx = t;
    a->on_activate = t_act; a->on_deactivate = t_deact; a->cycle_compute = t_comp;
}

static void tick_on_time(loop_engine_t *e)
{
    g_fake_now_us = eng_next_deadline_us(e);
    eng_tick(e);
}

static void reset_cbs(void) { s_stop_n = s_restart_n = s_rate_n = 0; s_rate_last = 0; }

/* 共用建立流程 */
static void setup(loop_engine_t *e, harness_t *h, agent_t *a, tag_t *t,
                  const hn_cfg_t *hc)
{
    static const eng_cfg_t EC = { .dt_us = 1000 };
    eng_cfg_t ec = EC;
    ec.user = h;
    ec.on_escalate = hn_notify_escalation;
    eng_init(e, &ec);
    bind(a, t, "a", 0);
    eng_register(e, a);
    hn_init(h, e, hc);
    hn_configure(h);
    g_fake_now_us = 0;
    hn_activate(h);
}

void test_escalation(void)
{
    /* ---- g1+g6) overrun 升級 → 降頻一次;再犯 → SAFE_STOP ---- */
    {
        loop_engine_t e; harness_t h; agent_t a; tag_t t;
        hn_cfg_t hc = { .stall_checks = 99, .enter_safe_stop = cb_stop,
                        .degrade_dt_us = 2000, .on_rate_changed = cb_rate };
        reset_cbs();
        setup(&e, &h, &a, &t, &hc);
        CHECK(t.act == 1);

        for (int i = 0; i < 3; i++) {                 /* 連續 3 overrun */
            g_fake_now_us = eng_next_deadline_us(&e) + 2000;
            eng_tick(&e);
        }
        CHECK(h.esc_pending == 1);
        hn_supervise(&h);
        CHECK(h.state == HN_RUN && h.degraded == 1);  /* 降頻退避,沒停機 */
        CHECK(e.cfg.dt_us == 2000);                   /* dt 已改 */
        CHECK(s_rate_n == 1 && s_rate_last == 2000);
        CHECK(t.deact == 1 && t.act == 2);            /* g6：走完整生命週期 */
        CHECK(s_stop_n == 0);

        tick_on_time(&e);                             /* 新 rate 下正常跑 */
        hn_supervise(&h);
        CHECK(h.state == HN_RUN);

        for (int i = 0; i < 3; i++) {                 /* 再犯 → SAFE_STOP */
            g_fake_now_us = eng_next_deadline_us(&e) + 4000;
            eng_tick(&e);
        }
        hn_supervise(&h);
        CHECK(h.state == HN_SAFE_STOP && s_stop_n == 1);
        hn_shutdown(&h);
    }

    /* ---- g2) miss 率視窗超標（非連續,不觸發 on_escalate）→ 降頻 ---- */
    {
        loop_engine_t e; harness_t h; agent_t a; tag_t t;
        hn_cfg_t hc = { .stall_checks = 99, .enter_safe_stop = cb_stop,
                        .miss_pct_max = 5, .degrade_dt_us = 2000,
                        .on_rate_changed = cb_rate };
        reset_cbs();
        setup(&e, &h, &a, &t, &hc);
        hn_supervise(&h);                             /* 錨定視窗基準 */

        for (int i = 0; i < 20; i++) {                /* 交錯:miss 但不連續 */
            g_fake_now_us = eng_next_deadline_us(&e) + 500; /* 輕度遲到=miss */
            eng_tick(&e);
            tick_on_time(&e);                         /* 準時,消 consec */
        }
        CHECK(eng_stats(&e)->overrun_consec == 0);    /* 沒觸發 escalate 路徑 */
        hn_supervise(&h);                             /* miss 率 50% > 5% */
        CHECK(h.state == HN_RUN && h.degraded == 1 && s_rate_n == 1);
        hn_shutdown(&h);
    }

    /* ---- g3) 無降頻設定 → 直接 SAFE_STOP（原行為迴歸） ---- */
    {
        loop_engine_t e; harness_t h; agent_t a; tag_t t;
        hn_cfg_t hc = { .stall_checks = 99, .enter_safe_stop = cb_stop };
        reset_cbs();
        setup(&e, &h, &a, &t, &hc);
        for (int i = 0; i < 3; i++) {
            g_fake_now_us = eng_next_deadline_us(&e) + 2000;
            eng_tick(&e);
        }
        hn_supervise(&h);
        CHECK(h.state == HN_SAFE_STOP && s_stop_n == 1);
        hn_shutdown(&h);
    }

    /* ---- g4) bus link 掉 → 重啟一次;持續 → SAFE_STOP;恢復 → 清零 ---- */
    {
        loop_engine_t e; harness_t h; agent_t a; tag_t t;
        hn_cfg_t hc = { .stall_checks = 99, .enter_safe_stop = cb_stop,
                        .restart_bus = cb_restart, .bus_fail_checks = 3 };
        reset_cbs();
        setup(&e, &h, &a, &t, &hc);
        bus_health_t down = { .link_ok = 0 }, up = { .link_ok = 1 };

        hn_feed_health(&h, 0, &down);
        tick_on_time(&e); hn_supervise(&h);           /* 第 1 次:嘗試重啟 */
        CHECK(s_restart_n == 1 && h.state == HN_RUN);
        hn_feed_health(&h, 0, &up);                   /* 恢復 → 清零 */
        tick_on_time(&e); hn_supervise(&h);
        CHECK(h.bus_fail == 0 && h.restart_tried == 0);

        hn_feed_health(&h, 0, &down);                 /* 再掉,持續 3 次 */
        for (int i = 0; i < 3; i++) { tick_on_time(&e); hn_supervise(&h); }
        CHECK(s_restart_n == 2);                      /* 恢復後允許再重啟 */
        CHECK(h.state == HN_SAFE_STOP && s_stop_n == 1);
        hn_shutdown(&h);
    }

    /* ---- g5) 非關鍵 agent 連續超預算 → 停用;關鍵 agent 不動 ---- */
    {
        loop_engine_t e; harness_t h;
        agent_t crit, hog; tag_t tc, th;
        static const int NONCRIT[] = { 1 };           /* hog 的註冊索引 */
        hn_cfg_t hc = { .stall_checks = 99, .enter_safe_stop = cb_stop,
                        .miss_pct_max = 90,           /* 拖時會遲到,放寬避免搶戲 */
                        .agent_over_n = 5,
                        .noncritical = NONCRIT, .n_noncritical = 1 };
        reset_cbs();
        static const eng_cfg_t EC = { .dt_us = 1000 };
        eng_cfg_t ec = EC;
        eng_init(&e, &ec);
        bind(&crit, &tc, "crit", 0);                  /* 無預算限制 */
        bind(&hog, &th, "hog", 50);                   /* 預算 50µs */
        th.drag_us = 200;                             /* 每次超 4 倍 */
        eng_register(&e, &crit);
        eng_register(&e, &hog);
        hn_init(&h, &e, &hc);
        hn_configure(&h);
        g_fake_now_us = 0;
        hn_activate(&h);

        for (int i = 0; i < 6; i++) tick_on_time(&e); /* 連續超預算 ≥5 */
        hn_supervise(&h);
        CHECK(h.agents_disabled == 1);
        CHECK(!eng_agent_enabled(&e, 1) && eng_agent_enabled(&e, 0));

        int runs_frozen = th.runs, runs_crit = tc.runs;
        for (int i = 0; i < 5; i++) tick_on_time(&e);
        CHECK(th.runs == runs_frozen);                /* 停用後不再跑 */
        CHECK(tc.runs == runs_crit + 5);              /* 關鍵 agent 照跑 */
        CHECK(h.state == HN_RUN);                     /* 不影響整體運轉 */
        hn_shutdown(&h);
    }
}
