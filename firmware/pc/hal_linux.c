/**
 * @file    hal_linux.c
 * @brief   PC 主站的真實時鐘 HAL（取代 sim/hal_shim.c 的假時鐘）
 *
 * sim 版 HAL_GetTick 每呼叫 +1ms 是為了讓同步注入的模擬迴圈必定推進;
 * PC 主站對接的是「真的會延遲」的外部行程（python can_slave）,
 * SDO 逾時、安全看門狗都必須用牆鐘時間。
 */
#include "stm32f7xx_hal.h"
#include <time.h>

uint32_t HAL_GetTick(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

void HAL_Delay(uint32_t ms)
{
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
}

/* loop engine 的 port 層（eng_port.h）：與 RT 迴圈的 clock_nanosleep
 * 共用 CLOCK_MONOTONIC 時基,deadline 才能直接換算 timespec。 */
uint64_t port_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
