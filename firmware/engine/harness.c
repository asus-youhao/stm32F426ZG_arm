/**
 * @file    harness.c
 * @brief   非 RT 監督層核心實作（WP-H2）
 */
#include "harness.h"

void hn_init(harness_t *h, loop_engine_t *e, const hn_cfg_t *cfg)
{
    h->eng = e;
    h->cfg = *cfg;
    if (h->cfg.stall_checks == 0) h->cfg.stall_checks = 3;
    h->state = HN_BOOT;
    h->last_ticks = 0;
    h->stall = 0;
    h->esc_pending = 0;
    h->safe_stops = 0;
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
    ((harness_t *)h_void)->esc_pending = 1;
}

static void enter_safe_stop(harness_t *h)
{
    if (h->state == HN_SAFE_STOP || h->state == HN_SHUTDOWN) return;
    h->state = HN_SAFE_STOP;
    h->safe_stops++;
    if (h->cfg.enter_safe_stop) h->cfg.enter_safe_stop(h->cfg.user);
}

void hn_supervise(harness_t *h)
{
    if (h->state != HN_RUN) return;

    if (h->esc_pending) {               /* 連續 overrun 升級（§5.2 策略表） */
        h->esc_pending = 0;
        enter_safe_stop(h);
        return;
    }

    uint64_t t = eng_stats(h->eng)->ticks;   /* 心跳：tick 必須前進 */
    if (t == h->last_ticks) {
        if (++h->stall >= h->cfg.stall_checks) enter_safe_stop(h);
    } else {
        h->stall = 0;
        h->last_ticks = t;
    }
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
