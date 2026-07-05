/**
 * @file    harness.h
 * @brief   非 RT 監督層核心（WP-H2）——生命週期編排 + engine 健康監督
 *
 * 設計見 docs/design/harness-agent-loop-engine-plan.md §5。
 * 平台無關：不含執行緒/時鐘，由平台 runner 決定 supervise 的呼叫節奏
 * （PC：非 RT 執行緒 ~10 Hz；F746：背景 super-loop）。
 *
 * 職責分工（§5.2）：毫秒級反應在 RT 域由 agent/safety 完成；
 * harness 只做秒級策略與「RT 執行緒本身死掉」的最後防線——
 * 心跳停滯或連續 overrun 升級時呼叫 enter_safe_stop（可阻塞、
 * 可直接對 bus 動作，因為此時 RT 域可能已經不動了）。
 */
#ifndef ENG_HARNESS_H
#define ENG_HARNESS_H

#include "loop_engine.h"
#include "bus_if.h"

/** @brief 生命週期狀態（§5.1；DEGRADED 的 per-axis 策略屬 WP-H4）。 */
typedef enum {
    HN_BOOT = 0,     /**< 初值；尚未 configure */
    HN_CONFIGURED,   /**< eng_configure 完成（BUS_UP 由平台在此前後自行執行） */
    HN_RUN,          /**< eng_activate 完成，RT 週期運轉中 */
    HN_SAFE_STOP,    /**< 監督觸發：enter_safe_stop 已執行 */
    HN_SHUTDOWN      /**< eng_deactivate 完成 */
} hn_state_t;

typedef struct {
    uint32_t stall_checks;  /**< 連續幾次 supervise 無 tick 進展→SAFE_STOP；0→3 */
    void   (*enter_safe_stop)(void *user); /**< 非 RT、可阻塞；可為 NULL */
    void    *user;

    /* ---- §5.2 策略表（0/NULL = 該條款停用,超標一律 SAFE_STOP）---- */
    uint32_t miss_pct_max;    /**< 視窗 miss+overrun+skip 率門檻 %（0→5） */
    uint32_t degrade_dt_us;   /**< >0：超標第一次先降頻到此週期（§5.2 降頻
                                   語意：deactivate→改 dt→reactivate,非無縫）;
                                   0：直接 SAFE_STOP */
    void   (*on_rate_changed)(void *user, uint32_t dt_us); /**< 降頻後通知
                                   平台（更新 js dt 等）;可為 NULL */
    int    (*restart_bus)(void *user);  /**< link 掉時嘗試重啟;可為 NULL */
    uint32_t bus_fail_checks; /**< 重啟後 link 仍掉的 supervise 次數→SAFE_STOP（0→3） */
    uint32_t agent_over_n;    /**< 非關鍵 agent 連續超預算門檻→停用（0→10） */
    const int *noncritical;   /**< 可停用的 agent 索引表（telemetry/health 等） */
    int      n_noncritical;
} hn_cfg_t;

typedef struct {
    loop_engine_t *eng;
    hn_cfg_t       cfg;
    hn_state_t     state;
    uint64_t       last_ticks;   /**< 上次 supervise 看到的 engine tick 數 */
    uint32_t       stall;        /**< 連續停滯的 supervise 次數 */
    uint32_t       esc_pending;  /**< RT on_escalate 設 1，supervise 消化 */
    uint32_t       safe_stops;   /**< 進入 SAFE_STOP 的累計次數（統計） */
    /* ---- §5.2 策略狀態 ---- */
    uint64_t       last_miss;    /**< 上次視窗的 miss+overrun+skip 累計 */
    uint8_t        degraded;     /**< 已用過降頻退避（再超標→SAFE_STOP） */
    uint8_t        link_down;    /**< health 回報任一 bus link 掉 */
    uint8_t        restart_tried;
    uint32_t       bus_fail;     /**< link 持續掉的 supervise 計數 */
    uint32_t       agents_disabled;
} harness_t;

void hn_init(harness_t *h, loop_engine_t *e, const hn_cfg_t *cfg);

/** @brief BOOT→CONFIGURED（內部呼叫 eng_configure）；回 0 成功。 */
int  hn_configure(harness_t *h);

/** @brief CONFIGURED→RUN（內部呼叫 eng_activate）；回 0 成功。 */
int  hn_activate(harness_t *h);

/**
 * @brief 接到 eng_cfg_t.on_escalate 的橋（RT 內呼叫，只設旗標不阻塞）。
 *        eng_cfg_t.user 指向 harness_t 即可直接掛本函式。
 */
void hn_notify_escalation(void *h_void, int reason);

/**
 * @brief 非 RT 週期監督（建議 ~10 Hz）。RUN 狀態下檢查：
 *        1) engine tick 有無前進（心跳）——連續 stall_checks 次停滯→SAFE_STOP
 *        2) esc_pending（連續 overrun 升級）→SAFE_STOP
 */
void hn_supervise(harness_t *h);

/** @brief 外部（急停命令、訊號）主動要求 SAFE_STOP。 */
void hn_request_safe_stop(harness_t *h);

/** @brief 平台取得 health 快照後餵入（非 RT）。任一 bus link_ok=0 → link down。 */
void hn_feed_health(harness_t *h, int bus_idx, const bus_health_t *bh);

/**
 * @brief 降頻退避（§5.2：RUN → deactivate → 改 dt → reactivate,
 *        中斷 < 100 ms、非無縫變速）。回 0 成功（狀態回 RUN）。
 */
int hn_change_rate(harness_t *h, uint32_t dt_us);

/** @brief RUN/SAFE_STOP→SHUTDOWN（內部 eng_deactivate）。 */
void hn_shutdown(harness_t *h);

const char *hn_state_str(const harness_t *h);

#endif /* ENG_HARNESS_H */
