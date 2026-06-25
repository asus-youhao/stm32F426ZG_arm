/**
 * @file    hal_shim.c
 * @brief   主機模擬時鐘：HAL_GetTick 每呼叫遞增,確保 SDO 等待迴圈會逾時。
 */
#include "stm32f7xx_hal.h"

static uint32_t s_tick = 0;

uint32_t HAL_GetTick(void)
{
    /* 每次查詢前進 1 ms,保證等待迴圈有進展（模擬回應為同步注入,通常第一輪即取得）。 */
    return ++s_tick;
}

void HAL_Delay(uint32_t ms)
{
    s_tick += ms;
}
