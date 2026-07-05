/**
 * @file    eng_trace.h
 * @brief   trace ring（WP-H3 儀器；設計文件 §3.6）——每 tick 一筆固定紀錄
 *
 * RT 端（engine 於 eng_tick 尾端）寫入：喚醒時戳、遲到量、四相位耗時、
 * 旗標。一筆 24 B、無格式化——日常儀表。非 RT 端排水成 CSV，離線用
 * tools/trace_report.py 算 p50/p99/max 與 histogram。
 *
 * 記憶體由呼叫端提供（PC 給大環、F746 給小環）；未 init 時 engine 的
 * 寫入是 no-op（不計 drop）。滿了丟新留舊並計 drop——排水端跟不上時
 * 紀錄從尾端截斷，已存的連續段不破洞。
 */
#ifndef ENG_TRACE_H
#define ENG_TRACE_H

#include "loop_engine.h"   /* ENG_PH_N */
#include <stdbool.h>
#include <stdint.h>

/* flags 位元 */
enum { ETR_MISS = 1u << 0, ETR_OVERRUN = 1u << 1 };

typedef struct {
    uint64_t t_us;              /**< 喚醒時刻（port_now_us） */
    uint32_t late_us;           /**< 相對 deadline 的遲到量（早醒=0） */
    uint16_t ph_us[ENG_PH_N];   /**< 各相位總耗時；>65535 飽和 */
    uint8_t  flags;             /**< ETR_* */
    uint8_t  _pad;
} eng_trace_rec_t;              /* 24 B */

/** @brief 啟用；mem 至少 cap×sizeof(eng_trace_rec_t)，cap 為 2 的冪。 */
int  eng_trace_init(void *mem, uint32_t cap);
void eng_trace_disable(void);
bool eng_trace_enabled(void);

/** @brief RT 端寫入（engine 內部呼叫）；未啟用回 false 且不計 drop。 */
bool eng_trace_push(const eng_trace_rec_t *r);

/** @brief 非 RT 端取出；空回 false。 */
bool eng_trace_pop(eng_trace_rec_t *out);

uint32_t eng_trace_drops(void);

#endif /* ENG_TRACE_H */
