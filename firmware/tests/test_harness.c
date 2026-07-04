/**
 * @file test_harness.c — WP-H2 驗收：harness 生命週期/監督 + cmd/telemetry ring
 *
 * 1) 生命週期與監督（獨立小 engine + 假時鐘）：
 *    configure/activate 狀態轉移、心跳停滯→SAFE_STOP、
 *    overrun 升級→SAFE_STOP、hn_request_safe_stop、shutdown。
 * 2) IO agents E2E（完整 app 棧 + C 假硬體）：
 *    cmd ring 下 JOINT_MOVE → 目標 counts 移動;telemetry ring 有快照且
 *    tick/sys_state 合理;estop 命令經 ring 生效。
 */
#include "test_framework.h"
#include "harness.h"
#include "app_agents.h"
#include "app_io_agents.h"
#include "dual_arm.h"
#include "control_rate.h"
#include "safety.h"
#include <string.h>

extern uint64_t g_fake_now_us;   /* port_now_us 假時鐘（test_engine.c） */

void app_main_init(void);

/* ---- 小工具 ---- */
static int  s_stop_called;
static void t_safe_stop(void *u) { (void)u; s_stop_called++; }

static void noop(void *c) { (void)c; }
static agent_t s_noop_agent = { .name = "noop", .divisor = 1, .cycle_compute = noop };

static void tick_on_time(loop_engine_t *e)
{
    g_fake_now_us = eng_next_deadline_us(e);
    eng_tick(e);
}

void test_harness(void)
{
    static const eng_cfg_t BASE = { .dt_us = 1000 };

    /* ---- 1a) 生命週期轉移與守門 ---- */
    {
        loop_engine_t e;
        harness_t h;
        eng_cfg_t cfg = BASE;
        eng_init(&e, &cfg);
        eng_register(&e, &s_noop_agent);
        hn_cfg_t hc = { .stall_checks = 3, .enter_safe_stop = t_safe_stop };
        hn_init(&h, &e, &hc);

        CHECK(h.state == HN_BOOT);
        CHECK(hn_activate(&h) == -1);            /* 未 configure 不可 activate */
        CHECK(hn_configure(&h) == 0 && h.state == HN_CONFIGURED);
        CHECK(hn_configure(&h) == -1);           /* 重複 configure 拒絕 */
        g_fake_now_us = 0;
        CHECK(hn_activate(&h) == 0 && h.state == HN_RUN);
        hn_shutdown(&h);
        CHECK(h.state == HN_SHUTDOWN && e.state == ENG_CONFIGURED);
    }

    /* ---- 1b) 心跳停滯 → SAFE_STOP ---- */
    {
        loop_engine_t e;
        harness_t h;
        eng_cfg_t cfg = BASE;
        eng_init(&e, &cfg);
        eng_register(&e, &s_noop_agent);
        hn_cfg_t hc = { .stall_checks = 3, .enter_safe_stop = t_safe_stop };
        hn_init(&h, &e, &hc);
        hn_configure(&h);
        g_fake_now_us = 0;
        hn_activate(&h);
        s_stop_called = 0;

        for (int i = 0; i < 5; i++) { tick_on_time(&e); hn_supervise(&h); }
        CHECK(h.state == HN_RUN && s_stop_called == 0);  /* tick 有進展 → 正常 */

        hn_supervise(&h); hn_supervise(&h);              /* 停止 tick：停滯 2 次 */
        CHECK(h.state == HN_RUN);
        hn_supervise(&h);                                /* 第 3 次 → SAFE_STOP */
        CHECK(h.state == HN_SAFE_STOP && s_stop_called == 1);
        hn_supervise(&h);                                /* 已停,不重複觸發 */
        CHECK(s_stop_called == 1);
        hn_shutdown(&h);
    }

    /* ---- 1c) overrun 升級（engine on_escalate → harness）→ SAFE_STOP ---- */
    {
        loop_engine_t e;
        harness_t h;
        eng_cfg_t cfg = BASE;               /* escalate_n 預設 3 */
        cfg.user = &h;
        cfg.on_escalate = hn_notify_escalation;
        eng_init(&e, &cfg);
        eng_register(&e, &s_noop_agent);
        hn_cfg_t hc = { .stall_checks = 30, .enter_safe_stop = t_safe_stop };
        hn_init(&h, &e, &hc);
        hn_configure(&h);
        g_fake_now_us = 0;
        hn_activate(&h);
        s_stop_called = 0;

        for (int i = 0; i < 3; i++) {        /* 連續 3 次 overrun → esc_pending */
            g_fake_now_us = eng_next_deadline_us(&e) + 2000;
            eng_tick(&e);
        }
        CHECK(h.esc_pending == 1);
        hn_supervise(&h);
        CHECK(h.state == HN_SAFE_STOP && s_stop_called == 1);
        hn_shutdown(&h);
    }

    /* ---- 2) IO agents E2E（完整 app 棧 + 假硬體）---- */
    {
        app_main_init();
        CHECK(dual_arm_present_count() == 14);

        loop_engine_t e;
        eng_cfg_t cfg = { .dt_us = CONTROL_DT_US };
        eng_init(&e, &cfg);
        CHECK(app_agents_register(&e) == 0);
        CHECK(app_io_register(&e) == 0);
        harness_t h;
        hn_cfg_t hc = { .stall_checks = 3, .enter_safe_stop = t_safe_stop };
        hn_init(&h, &e, &hc);
        CHECK(hn_configure(&h) == 0);
        g_fake_now_us = 0;
        CHECK(hn_activate(&h) == 0);

        /* 使能完成 */
        for (int t = 0; t < 100; t++) tick_on_time(&e);

        /* cmd ring：J0 點到點 → 目標 counts 應離開 0。
           遙測邊跑邊抽（模擬 pc_master 主執行緒 50Hz 消費;
           不抽的話 ring 滿會「丟新留舊」,最後快照停在早期 tick）。 */
        int32_t tgt_before = g_jstate[0].target_pos;
        app_cmd_t c = { .op = APP_CMD_JOINT_MOVE, .idx = 0, .val = 0.5f };
        CHECK(app_io_cmd_push(&c));
        app_tele_t tl = {0}, tmp;
        int n = 0;
        for (int t = 0; t < 400; t++) {
            tick_on_time(&e);
            if (t % 10 == 0)
                while (app_io_tele_pop(&tmp)) { tl = tmp; n++; }
        }
        while (app_io_tele_pop(&tmp)) { tl = tmp; n++; }
        CHECK(g_jstate[0].target_pos != tgt_before);

        /* telemetry：有快照、tick 合理、末端位姿非零 */
        CHECK(n > 0);
        CHECK(tl.tick > 400 && tl.tick <= eng_stats(&e)->ticks);
        CHECK(tl.lpos[0] != 0.0f || tl.lpos[1] != 0.0f || tl.lpos[2] != 0.0f);

        /* estop 經 cmd ring 生效 → safety 進 ESTOP */
        c = (app_cmd_t){ .op = APP_CMD_ESTOP, .val = 1.0f };
        CHECK(app_io_cmd_push(&c));
        for (int t = 0; t < 40; t++) tick_on_time(&e);
        CHECK(safety_state() == SYS_ESTOP);

        hn_shutdown(&h);
        CHECK(h.state == HN_SHUTDOWN);
    }
}
