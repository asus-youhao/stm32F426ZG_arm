/**
 * @file    eng_log.c
 * @brief   log ring 實作（spsc_ring 上的固定紀錄）
 */
#include "eng_log.h"
#include "eng_port.h"
#include "spsc_ring.h"

#define LOG_CAP 64
static uint8_t  s_mem[LOG_CAP * sizeof(eng_log_rec_t)];
static spsc_t   s_q;
static uint32_t s_drops;

void eng_log_init(void)
{
    (void)spsc_init(&s_q, s_mem, sizeof(eng_log_rec_t), LOG_CAP);
    s_drops = 0;
}

bool eng_log(uint8_t level, uint16_t code, int32_t a, int32_t b)
{
    eng_log_rec_t r = { .t_us = port_now_us(), .code = code,
                        .level = level, .a = a, .b = b };
    if (spsc_push(&s_q, &r)) return true;
    s_drops++;                       /* 滿：丟新留舊,日誌不搶控制預算 */
    return false;
}

bool eng_log_pop(eng_log_rec_t *out) { return spsc_pop(&s_q, out); }

uint32_t eng_log_drops(void) { return s_drops; }

const char *eng_log_code_str(uint16_t code)
{
    switch (code) {
        case ELC_SAFE_STOP_ON:  return "SAFE_STOP_ON";
        case ELC_SAFE_STOP_OFF: return "SAFE_STOP_OFF";
        case ELC_FAULT_EVT:     return "FAULT_EVT";
        case ELC_OVERRUN_ESC:   return "OVERRUN_ESC";
        case ELC_AXIS_STALE:    return "AXIS_STALE";
        default:                return "?";
    }
}
