/**
 * @file    eng_trace.c
 * @brief   trace ring 實作（spsc_ring 上的每 tick 固定紀錄）
 */
#include "eng_trace.h"
#include "spsc_ring.h"

static spsc_t   s_q;
static uint32_t s_drops;
static bool     s_on;

int eng_trace_init(void *mem, uint32_t cap)
{
    if (spsc_init(&s_q, mem, sizeof(eng_trace_rec_t), cap) != 0) return -1;
    s_drops = 0;
    s_on = true;
    return 0;
}

void eng_trace_disable(void) { s_on = false; }
bool eng_trace_enabled(void) { return s_on; }

bool eng_trace_push(const eng_trace_rec_t *r)
{
    if (!s_on) return false;
    if (spsc_push(&s_q, r)) return true;
    s_drops++;                       /* 滿：丟新留舊,連續段不破洞 */
    return false;
}

bool eng_trace_pop(eng_trace_rec_t *out)
{
    return s_on && spsc_pop(&s_q, out);
}

uint32_t eng_trace_drops(void) { return s_drops; }
