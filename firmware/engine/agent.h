/**
 * @file    agent.h
 * @brief   Agent 統一介面（WP-H0）——掛進 loop engine 的自治週期單元
 *
 * 設計見 docs/design/harness-agent-loop-engine-plan.md §4：
 *   - 生命週期 callback（on_configure/on_activate/on_deactivate）由 harness
 *     在非 RT 情境呼叫，可阻塞（SDO 交握、NMT 等）。
 *   - 相位 callback（cycle_read/cycle_compute/cycle_write/housekeep）由
 *     loop engine 在 RT 週期內呼叫：禁系統呼叫、禁阻塞、禁 malloc。
 *   - on_fault 為 RT 內故障通知：agent 自行降級，不得阻塞。
 * 不需要的 callback 填 NULL。註冊順序即各相位內的執行順序
 * （safety 永遠排在 motion 前、bus TX 永遠排最後）。
 */
#ifndef ENG_AGENT_H
#define ENG_AGENT_H

#include <stdint.h>

/** @brief engine 執行期設定（執行期參數，取代編譯期 CONTROL_HZ 常數）。 */
typedef struct eng_cfg {
    uint32_t dt_us;              /**< base tick 週期（2000=500Hz、1000=1kHz） */
    uint32_t late_warn_us;       /**< 輕度遲到門檻；0 → dt_us/10 */
    uint32_t overrun_escalate_n; /**< 連續 overrun 幾次通知 harness；0 → 3 */
    uint32_t budget_over_n;      /**< 連續超預算幾次呼叫 on_fault；0 → 3 */
    void    *user;               /**< 回傳給 on_escalate 的 harness context */
    void   (*on_escalate)(void *user, int reason); /**< RT 內呼叫，不得阻塞 */
} eng_cfg_t;

/** @brief on_escalate 的 reason。 */
enum { ENG_ESC_OVERRUN = 1 };

/** @brief on_fault 的故障類別。 */
typedef enum {
    AG_FAULT_BUDGET,    /**< 連續超 WCET 預算（engine 偵測） */
    AG_FAULT_BUS,       /**< 匯流排層故障（bus agent 轉發） */
    AG_FAULT_AXIS_LOST, /**< 軸失聯（axis agent 轉發） */
    AG_FAULT_INTERNAL   /**< agent 內部錯誤 */
} agent_fault_t;

typedef struct agent {
    const char *name;
    uint32_t    divisor;      /**< 每幾個 base tick 跑一次；0 視為 1 */
    uint32_t    phase_offset; /**< 錯峰偏移（有效值 0..divisor-1） */
    uint32_t    budget_us;    /**< cycle_compute WCET 預算；0=不檢查 */
    void       *ctx;

    /* ---- harness 生命週期（非 RT，可阻塞）；回 0 = 成功 ---- */
    int  (*on_configure)(void *ctx, const eng_cfg_t *cfg);
    int  (*on_activate)(void *ctx);
    void (*on_deactivate)(void *ctx);

    /* ---- loop engine 相位（RT，禁阻塞）---- */
    void (*cycle_read)(void *ctx);    /**< LATCH 之後：取回授快照 */
    void (*cycle_compute)(void *ctx); /**< 純計算（bus-agnostic） */
    void (*cycle_write)(void *ctx);   /**< BUS_TX 之前：提交輸出 */
    void (*housekeep)(void *ctx);     /**< 遙測/統計/背景通道步進 */

    /* ---- 故障通知（RT，agent 自行降級）---- */
    void (*on_fault)(void *ctx, agent_fault_t f);
} agent_t;

#endif /* ENG_AGENT_H */
