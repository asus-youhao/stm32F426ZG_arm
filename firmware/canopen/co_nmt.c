/**
 * @file    co_nmt.c
 * @brief   NMT + Heartbeat 實作
 */
#include "co_nmt.h"
#include "co_bxcan.h"
#include "stm32f7xx_hal.h"

#define MAX_NODE 128

typedef struct {
    co_node_state_t state;
    uint32_t last_ms;
} node_info_t;

static node_info_t s_node[CO_BUS_COUNT][MAX_NODE];

co_status_t co_nmt_send(co_bus_t bus, co_nmt_cmd_t cmd, uint8_t node)
{
    co_frame_t f = {0};
    f.id = CO_COBID_NMT;     /* 0x000 */
    f.dlc = 2;
    f.data[0] = (uint8_t)cmd;
    f.data[1] = node;        /* 0 = 廣播 */
    return co_bxcan_send(bus, &f);
}

void co_nmt_process_frame(co_bus_t bus, const co_frame_t *f)
{
    if (!f) return;
    if (f->id >= CO_COBID_HEARTBEAT_BASE + 1 &&
        f->id <= CO_COBID_HEARTBEAT_BASE + 127 && f->dlc >= 1) {
        uint8_t node = (uint8_t)(f->id - CO_COBID_HEARTBEAT_BASE);
        s_node[bus][node].state = (co_node_state_t)f->data[0];
        s_node[bus][node].last_ms = HAL_GetTick();
    }
}

co_node_state_t co_nmt_node_state(co_bus_t bus, uint8_t node)
{
    if (bus >= CO_BUS_COUNT || node >= MAX_NODE) return CO_NODE_UNKNOWN;
    return s_node[bus][node].state;
}

uint32_t co_nmt_node_age_ms(co_bus_t bus, uint8_t node)
{
    if (bus >= CO_BUS_COUNT || node >= MAX_NODE) return 0xFFFFFFFFu;
    return HAL_GetTick() - s_node[bus][node].last_ms;
}
