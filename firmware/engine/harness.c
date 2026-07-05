/**
 * @file    harness.c
 * @brief   非 RT 監督層核心實作（WP-H2）
 */
#include "harness.h"
#include "eng_log.h"

void hn_init(harness_t *h, loop_engine_t *e, const hn_cfg_t *cfg)
{
    h->eng = e;
    h->cfg = *cfg;
    if (h->cfg.stall_checks == 0)    h->cfg.stall_checks = 3;
    if (h->cfg.miss_pct_max == 0)    h->cfg.miss_pct_max = 5;
    if (h->cfg.bus_fail_checks == 0) h->cfg.bus_fail_checks = 3;
    if (h->cfg.agent_over_n == 0)    h->cfg.agent_over_n = 10;
    h->state = HN_BOOT;
    h->last_ticks = 0;
    h->stall = 0;
    h->esc_pending = 0;
    h->safe_stops = 0;
    h->last_miss = 0;
    h->degraded = 0;
    h->link_down = 0;
    h->restart_tried = 0;
    h->bus_fail = 0;
    h->agents_disabled = 0;
}

int hn_configure(harness_t *h)
{
    if (h->state != HN_BOOT) return -1;
    if (eng_configure(h->eng) != 0) return -1;
    h->state = HN_CONFIGURED;
    return 0;
}

int hn_activate(harness_t *h)
{
    if (h->state != HN_CONFIGURED) return -1;
    if (eng_activate(h->eng) != 0) return -1;
    h->last_ticks = 0;
    h->stall = 0;
    h->state = HN_RUN;
    return 0;
}

void hn_notify_escalation(void *h_void, int reason)
{
    (void)reason;                       /* 目前僅 ENG_ESC_OVERRUN 一種 */
    harness_t *h = (harness_t *)h_void;
    h->esc_pending = 1;
    eng_log(EL_ERR, ELC_OVERRUN_ESC,
            (int32_t)eng_stats(h->eng)->overrun_consec, 0);
}

static void enter_safe_stop(harness_t *h)
{
    if (h->state == HN_SAFE_STOP || h->state == HN_SHUTDOWN) return;
    h->state = HN_SAFE_STOP;
    h->safe_stops++;
    if (h->cfg.enter_safe_stop) h->cfg.enter_safe_stop(h->cfg.user);
}

/* miss 率超標的統一出口：可降頻先降頻（一次機會）,否則 SAFE_STOP（§5.2） */
static void miss_overrun_action(harness_t *h)
{
    if (h->cfg.degrade_dt_us && !h->degraded &&
        h->cfg.degrade_dt_us > h->eng->cfg.dt_us) {
        if (hn_change_rate(h, h->cfg.degrade_dt_us) == 0) {
            h->degraded = 1;
            return;
        }
    }
    enter_safe_stop(h);
}

void hn_supervise(harness_t *h)
{
    if (h->state != HN_RUN) return;
    const eng_stats_t *st = eng_stats(h->eng);

    if (h->esc_pending) {               /* 連續 overrun 升級（§5.2 策略表） */
        h->esc_pending = 0;
        miss_overrun_action(h);
        return;
    }

    /* 心跳：tick 必須前進 */
    uint64_t t = st->ticks;
    if (t == h->last_ticks) {
        if (++h->stall >= h->cfg.stall_checks) enter_safe_stop(h);
        return;                          /* 停滯期不算 miss 率（分母不動） */
    }
    uint64_t dticks = t - h->last_ticks;
    h->stall = 0;
    h->last_ticks = t;

    /* miss 率視窗：miss+overrun+skip 佔本視窗 tick 數 */
    uint64_t m = (uint64_t)st->miss + st->overruns + st->skipped;
    uint64_t dmiss = m - h->last_miss;
    h->last_miss = m;
    if (dmiss * 100u > (uint64_t)h->cfg.miss_pct_max * dticks) {
        miss_overrun_action(h);
        return;
    }

    /* bus link（由 hn_feed_health 餵入）：先重啟一次,持續掉→SAFE_STOP */
    if (h->link_down) {
        if (!h->restart_tried && h->cfg.restart_bus) {
            h->restart_tried = 1;
            (void)h->cfg.restart_bus(h->cfg.user);
        }
        if (++h->bus_fail >= h->cfg.bus_fail_checks) {
            enter_safe_stop(h);
            return;
        }
    }

    /* 非關鍵 agent 連續超預算 → 停用（第一層 on_fault 自降級無效之後） */
    for (int i = 0; i < h->cfg.n_noncritical; i++) {
        int idx = h->cfg.noncritical[i];
        const agent_stats_t *as = eng_agent_stats(h->eng, idx);
        if (as && as->budget_over_consec >= h->cfg.agent_over_n &&
            eng_agent_enabled(h->eng, idx)) {
            eng_agent_set_enabled(h->eng, idx, 0);
            h->agents_disabled++;
        }
    }
}

void hn_feed_health(harness_t *h, int bus_idx, const bus_health_t *bh)
{
    (void)bus_idx;
    if (!bh->link_ok) {
        h->link_down = 1;
    } else if (h->link_down) {
        h->link_down = 0;                /* 恢復：清計數,允許再次重啟嘗試 */
        h->bus_fail = 0;
        h->restart_tried = 0;
    }
}

int hn_change_rate(harness_t *h, uint32_t dt_us)
{
    if (h->state != HN_RUN || dt_us == 0) return -1;
    eng_deactivate(h->eng);              /* §5.2：非無縫,短暫 hold */
    h->eng->cfg.dt_us = dt_us;
    if (eng_activate(h->eng) != 0) {     /* 重啟失敗 → 最後防線 */
        enter_safe_stop(h);
        return -1;
    }
    h->last_ticks = 0;                   /* 心跳/視窗基準重錨定 */
    h->last_miss = (uint64_t)eng_stats(h->eng)->miss
                 + eng_stats(h->eng)->overruns + eng_stats(h->eng)->skipped;
    if (h->cfg.on_rate_changed) h->cfg.on_rate_changed(h->cfg.user, dt_us);
    return 0;
}

void hn_request_safe_stop(harness_t *h) { enter_safe_stop(h); }

void hn_shutdown(harness_t *h)
{
    if (h->state == HN_SHUTDOWN) return;
    if (h->eng->state == ENG_ACTIVE) eng_deactivate(h->eng);
    h->state = HN_SHUTDOWN;
}

const char *hn_state_str(const harness_t *h)
{
    switch (h->state) {
        case HN_BOOT:       return "BOOT";
        case HN_CONFIGURED: return "CONFIGURED";
        case HN_RUN:        return "RUN";
        case HN_SAFE_STOP:  return "SAFE_STOP";
        case HN_SHUTDOWN:   return "SHUTDOWN";
    }
    return "?";
}
