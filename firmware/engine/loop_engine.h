/**
 * @file    loop_engine.h
 * @brief   RT 週期執行器核心（WP-H0）——時間源、相位排程、預算量測、overrun 政策
 *
 * 設計見 docs/design/harness-agent-loop-engine-plan.md §3。
 * 平台無關（僅依賴 eng_port.h 的 port_now_us）；零 malloc、零鎖、靜態表。
 *
 * 使用方式（平台 runner 負責睡與喚醒，engine 負責其餘）：
 *   eng_init → eng_register×N → eng_configure → eng_activate
 *   loop { 平台睡到 eng_next_deadline_us(); eng_tick(); }
 *   eng_deactivate
 *
 * 每個 eng_tick 依序跑四個相位 pass（同相位內依註冊順序）：
 *   cycle_read → cycle_compute → cycle_write → housekeep
 * BUS_RX/LATCH 即 bus agent 的 cycle_read（註冊在最前），
 * BUS_TX 即 bus agent 的 cycle_write（註冊在最後）。
 */
#ifndef LOOP_ENGINE_H
#define LOOP_ENGINE_H

#include "agent.h"
#include <stdint.h>

#define ENG_AGENT_MAX    32
#define ENG_HIST_BUCKETS 8   /* ≤10/25/50/100/250/500/1000/>1000 µs */

/** @brief 相位索引（agent_stats_t.max_us 下標）。 */
enum { ENG_PH_READ, ENG_PH_COMPUTE, ENG_PH_WRITE, ENG_PH_HOUSE, ENG_PH_N };

/** @brief 每 agent 統計（engine 於 RT 內更新，harness 於非 RT 讀）。 */
typedef struct {
    uint32_t runs;                      /**< cycle_compute 執行次數 */
    uint32_t max_us[ENG_PH_N];          /**< 各相位最大耗時 */
    uint32_t hist[ENG_HIST_BUCKETS];    /**< compute 耗時 histogram */
    uint32_t budget_over_total;         /**< 超預算總次數 */
    uint32_t budget_over_consec;        /**< 連續超預算（歸零於達標週期） */
} agent_stats_t;

/** @brief engine 整體統計。 */
typedef struct {
    uint64_t ticks;         /**< 實際執行的 tick 數 */
    uint64_t skipped;       /**< overrun SKIP 政策跳過（不補跑）的 tick 數 */
    uint32_t miss;          /**< 輕度遲到（warn ≤ late < dt）次數 */
    uint32_t overruns;      /**< 超週期（late ≥ dt）事件數 */
    uint32_t overrun_consec;/**< 目前連續 overrun 數 */
    uint32_t late_max_us;   /**< 遲到最大值（可由 harness 週期歸零） */
    uint32_t late_hist[ENG_HIST_BUCKETS]; /**< 遲到 histogram */
} eng_stats_t;

typedef enum { ENG_IDLE, ENG_CONFIGURED, ENG_ACTIVE } eng_state_t;

typedef struct loop_engine {
    eng_cfg_t     cfg;
    eng_state_t   state;
    agent_t      *agents[ENG_AGENT_MAX];
    agent_stats_t astats[ENG_AGENT_MAX];
    int           n_agents;
    uint64_t      next_us;  /**< 下一 tick 絕對 deadline（µs） */
    uint64_t      tick;     /**< 週期計數（divisor/phase_offset 判斷用） */
    eng_stats_t   stats;
} loop_engine_t;

/** @brief 初始化；cfg 中為 0 的欄位套用預設（見 eng_cfg_t 註解）。 */
void eng_init(loop_engine_t *e, const eng_cfg_t *cfg);

/** @brief 註冊 agent（僅 IDLE/CONFIGURED 可註冊）；回 0 成功、-1 表滿/狀態錯。 */
int eng_register(loop_engine_t *e, agent_t *a);

/** @brief 依註冊順序呼叫 on_configure；任一失敗回 -1（狀態留在 IDLE）。 */
int eng_configure(loop_engine_t *e);

/**
 * @brief 依註冊順序呼叫 on_activate；第 k 個失敗時反序 deactivate 前 k-1 個
 *        並回 -1。成功後 deadline 錨定為 now+dt、狀態 ACTIVE。
 */
int eng_activate(loop_engine_t *e);

/** @brief 反註冊順序呼叫 on_deactivate；狀態回 CONFIGURED。 */
void eng_deactivate(loop_engine_t *e);

/** @brief 下一 tick 絕對 deadline（µs），供平台 runner 睡到該時刻。 */
uint64_t eng_next_deadline_us(const loop_engine_t *e);

/**
 * @brief 相位微調（WP-H5;SOEM DC 跟隨模式的 PI 鎖相用）。
 *        把下一個 deadline 平移 trim µs,內部限幅 ±5% 週期。
 *        CANopen（主站即 SYNC 源）與 IgH（主站發號）不需呼叫。
 */
void eng_phase_trim_us(loop_engine_t *e, int32_t trim);

/**
 * @brief 執行一個週期（平台睡醒後呼叫）。內部：
 *        遲到分級（正常 / miss / overrun-SKIP 重錨定 + 升級通知）
 *        → 四相位 pass → 每 agent 耗時/預算記帳。
 */
void eng_tick(loop_engine_t *e);

const eng_stats_t   *eng_stats(const loop_engine_t *e);
const agent_stats_t *eng_agent_stats(const loop_engine_t *e, int idx);

#endif /* LOOP_ENGINE_H */
