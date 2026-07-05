/**
 * @file    loop_engine.c
 * @brief   RT 週期執行器核心實作（WP-H0）
 *
 * 平台無關：僅依賴 eng_port.h 的 port_now_us()。禁 malloc / 鎖 / 系統呼叫。
 * overrun 採 SKIP 政策（不補跑）的理由見設計文件 §3.1：
 * 補發過期設定點對從站是錯誤資訊，CANopen 90% 負載下連環補發會灌爆匯流排。
 */
#include "loop_engine.h"
#include "eng_log.h"
#include "eng_port.h"
#include "eng_trace.h"
#include <string.h>

/* histogram 桶界（µs）：≤10/25/50/100/250/500/1000/其餘 */
static const uint32_t HIST_EDGE[ENG_HIST_BUCKETS - 1] =
    { 10, 25, 50, 100, 250, 500, 1000 };

static void hist_add(uint32_t *hist, uint32_t us)
{
    for (int i = 0; i < ENG_HIST_BUCKETS - 1; i++)
        if (us <= HIST_EDGE[i]) { hist[i]++; return; }
    hist[ENG_HIST_BUCKETS - 1]++;
}

void eng_init(loop_engine_t *e, const eng_cfg_t *cfg)
{
    memset(e, 0, sizeof(*e));
    e->cfg = *cfg;
    if (e->cfg.late_warn_us == 0)       e->cfg.late_warn_us = e->cfg.dt_us / 10;
    if (e->cfg.overrun_escalate_n == 0) e->cfg.overrun_escalate_n = 3;
    if (e->cfg.budget_over_n == 0)      e->cfg.budget_over_n = 3;
    e->state = ENG_IDLE;
}

int eng_register(loop_engine_t *e, agent_t *a)
{
    if (e->state == ENG_ACTIVE || e->n_agents >= ENG_AGENT_MAX) return -1;
    if (a->divisor == 0) a->divisor = 1;
    e->enabled[e->n_agents] = 1;
    e->agents[e->n_agents++] = a;
    return 0;
}

int eng_configure(loop_engine_t *e)
{
    if (e->state == ENG_ACTIVE) return -1;
    for (int i = 0; i < e->n_agents; i++) {
        agent_t *a = e->agents[i];
        if (a->on_configure && a->on_configure(a->ctx, &e->cfg) != 0)
            return -1;                      /* 狀態留在 IDLE，harness 決策 */
    }
    e->state = ENG_CONFIGURED;
    return 0;
}

int eng_activate(loop_engine_t *e)
{
    if (e->state != ENG_CONFIGURED) return -1;
    for (int i = 0; i < e->n_agents; i++) {
        agent_t *a = e->agents[i];
        if (a->on_activate && a->on_activate(a->ctx) != 0) {
            for (int k = i - 1; k >= 0; k--)      /* 反序回滾已啟用者 */
                if (e->agents[k]->on_deactivate)
                    e->agents[k]->on_deactivate(e->agents[k]->ctx);
            return -1;
        }
    }
    memset(&e->stats, 0, sizeof(e->stats));
    memset(e->astats, 0, sizeof(e->astats));
    e->tick = 0;
    e->next_us = port_now_us() + e->cfg.dt_us;
    e->state = ENG_ACTIVE;
    return 0;
}

void eng_deactivate(loop_engine_t *e)
{
    if (e->state != ENG_ACTIVE) return;
    for (int i = e->n_agents - 1; i >= 0; i--)
        if (e->agents[i]->on_deactivate)
            e->agents[i]->on_deactivate(e->agents[i]->ctx);
    e->state = ENG_CONFIGURED;
}

uint64_t eng_next_deadline_us(const loop_engine_t *e) { return e->next_us; }

void eng_agent_set_enabled(loop_engine_t *e, int idx, int enabled)
{
    if (idx >= 0 && idx < e->n_agents) e->enabled[idx] = (uint8_t)(enabled != 0);
}

int eng_agent_enabled(const loop_engine_t *e, int idx)
{
    return (idx >= 0 && idx < e->n_agents) ? e->enabled[idx] : 0;
}

void eng_phase_trim_us(loop_engine_t *e, int32_t trim)
{
    /* DC 跟隨模式（SOEM,WP-L2.2）的鎖相微調：限幅 ±5% 週期,
       避免大 trim 打亂 overrun/SKIP 判定（設計文件 §3.1）。 */
    int32_t lim = (int32_t)(e->cfg.dt_us / 20u);
    if (trim >  lim) trim =  lim;
    if (trim < -lim) trim = -lim;
    e->next_us = (uint64_t)((int64_t)e->next_us + trim);
}

/** @brief agent 本 tick 是否輪到（divisor/phase_offset 錯峰）。 */
static int agent_due(const loop_engine_t *e, const agent_t *a)
{
    return (e->tick % a->divisor) == (a->phase_offset % a->divisor);
}

/** @brief 跑一個相位 pass：量測每 agent 耗時、compute 相位做預算記帳。
 *         回傳本相位總耗時（trace ring 用）。 */
static uint32_t run_phase(loop_engine_t *e, int phase)
{
    uint32_t sum = 0;
    for (int i = 0; i < e->n_agents; i++) {
        agent_t *a = e->agents[i];
        if (!e->enabled[i]) continue;           /* harness 停用的非關鍵 agent */
        if (!agent_due(e, a)) continue;

        void (*fn)(void *) =
            (phase == ENG_PH_READ)    ? a->cycle_read :
            (phase == ENG_PH_COMPUTE) ? a->cycle_compute :
            (phase == ENG_PH_WRITE)   ? a->cycle_write : a->housekeep;
        if (!fn) continue;

        uint64_t t0 = port_now_us();
        fn(a->ctx);
        uint32_t dur = (uint32_t)(port_now_us() - t0);
        sum += dur;

        agent_stats_t *st = &e->astats[i];
        if (dur > st->max_us[phase]) st->max_us[phase] = dur;

        if (phase != ENG_PH_COMPUTE) continue;
        st->runs++;
        hist_add(st->hist, dur);
        if (a->budget_us == 0) continue;
        if (dur > a->budget_us) {
            st->budget_over_total++;
            st->budget_over_consec++;
            /* 連續達門檻的那一次通知（每段 streak 只通知一次）;
               同時記 log 上下文——事後回答「那 300 µs 去哪了」（§3.6） */
            if (st->budget_over_consec == e->cfg.budget_over_n) {
                eng_log(EL_WARN, ELC_BUDGET_OVER, i, (int32_t)dur);
                if (a->on_fault) a->on_fault(a->ctx, AG_FAULT_BUDGET);
            }
        } else {
            st->budget_over_consec = 0;
        }
    }
    return sum;
}

void eng_tick(loop_engine_t *e)
{
    if (e->state != ENG_ACTIVE) return;

    /* ---- 遲到分級（設計文件 §3.1）---- */
    uint64_t now  = port_now_us();
    uint32_t dt   = e->cfg.dt_us;
    uint32_t late = 0;
    uint8_t  trf  = 0;                       /* trace flags（ETR_*） */
    if (now >= e->next_us) {
        late = (uint32_t)(now - e->next_us);
        hist_add(e->stats.late_hist, late);
        if (late > e->stats.late_max_us) e->stats.late_max_us = late;

        if (late >= dt) {
            /* overrun：SKIP 政策——跳過錯過的 tick，deadline 重錨定 now+dt */
            trf |= ETR_OVERRUN;
            e->stats.overruns++;
            e->stats.skipped += late / dt;
            e->stats.overrun_consec++;
            e->next_us = now + dt;
            if (e->stats.overrun_consec == e->cfg.overrun_escalate_n &&
                e->cfg.on_escalate)
                e->cfg.on_escalate(e->cfg.user, ENG_ESC_OVERRUN);
        } else {
            if (late >= e->cfg.late_warn_us) { e->stats.miss++; trf |= ETR_MISS; }
            e->stats.overrun_consec = 0;
            e->next_us += dt;
        }
    } else {
        /* 提早喚醒（平台睡不足；視為準時） */
        e->stats.overrun_consec = 0;
        e->next_us += dt;
    }

    /* ---- 四相位 pass（同相位內依註冊順序）---- */
    uint32_t ph[ENG_PH_N];
    ph[ENG_PH_READ]    = run_phase(e, ENG_PH_READ);
    ph[ENG_PH_COMPUTE] = run_phase(e, ENG_PH_COMPUTE);
    ph[ENG_PH_WRITE]   = run_phase(e, ENG_PH_WRITE);
    ph[ENG_PH_HOUSE]   = run_phase(e, ENG_PH_HOUSE);

    /* ---- trace ring（§3.6 每 tick 一筆;未啟用時 push 為 no-op）---- */
    if (eng_trace_enabled()) {
        eng_trace_rec_t r = { .t_us = now, .late_us = late, .flags = trf };
        for (int p = 0; p < ENG_PH_N; p++)
            r.ph_us[p] = (ph[p] > 0xFFFFu) ? 0xFFFF : (uint16_t)ph[p];
        eng_trace_push(&r);
    }

    e->tick++;
    e->stats.ticks++;
}

const eng_stats_t *eng_stats(const loop_engine_t *e) { return &e->stats; }

const agent_stats_t *eng_agent_stats(const loop_engine_t *e, int idx)
{
    return (idx >= 0 && idx < e->n_agents) ? &e->astats[idx] : (void *)0;
}
