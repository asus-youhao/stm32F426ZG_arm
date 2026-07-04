/**
 * @file    co_emcy.c
 * @brief   EMCY 解析與快取實作（WP-H4 / G5）
 *
 * EMCY 幀格式（CiA 301）：[error code u16][error register u8][vendor 5B]。
 * 僅快取,不做策略——「EMCY = safe stop」的條款由 safety 層執行。
 */
#include "co_emcy.h"

#define MAX_NODE 128

typedef struct {
    uint16_t last_code;
    uint8_t  last_reg;
    bool     pending;    /* 自上次 take 後有新事件 */
} emcy_t;

static emcy_t   s_emcy[CO_BUS_COUNT][MAX_NODE];
static uint32_t s_count[CO_BUS_COUNT];

bool co_emcy_process_frame(co_bus_t bus, const co_frame_t *f)
{
    if (bus >= CO_BUS_COUNT || !f) return false;
    /* 0x080 本身是 SYNC；EMCY 是 0x080+node（node 1..127） */
    if (f->id <= CO_COBID_EMCY_BASE || f->id > CO_COBID_EMCY_BASE + 127)
        return false;
    if (f->dlc < 3) return false;

    uint8_t node = (uint8_t)(f->id - CO_COBID_EMCY_BASE);
    emcy_t *e = &s_emcy[bus][node];
    e->last_code = (uint16_t)(f->data[0] | (f->data[1] << 8));
    e->last_reg  = f->data[2];
    e->pending   = true;
    s_count[bus]++;
    return true;
}

bool co_emcy_take(co_bus_t bus, uint8_t node, uint16_t *code)
{
    if (bus >= CO_BUS_COUNT || node >= MAX_NODE) return false;
    emcy_t *e = &s_emcy[bus][node];
    if (!e->pending) return false;
    e->pending = false;
    if (code) *code = e->last_code;
    return true;
}

uint16_t co_emcy_last_code(co_bus_t bus, uint8_t node)
{
    if (bus >= CO_BUS_COUNT || node >= MAX_NODE) return 0;
    return s_emcy[bus][node].last_code;
}

uint32_t co_emcy_count(co_bus_t bus)
{
    return (bus < CO_BUS_COUNT) ? s_count[bus] : 0;
}

void co_emcy_reset(void)
{
    for (int b = 0; b < CO_BUS_COUNT; b++) {
        s_count[b] = 0;
        for (int n = 0; n < MAX_NODE; n++)
            s_emcy[b][n] = (emcy_t){0};
    }
}
