/**
 * @file  tick_f7.h（WP-SE4）
 * @brief F746 硬體 1 kHz 週期源——TIM6 update IRQ + DWT 時戳 + deadline trim
 *
 * 用法（RT 迴圈）：
 *   tick_f7_init(1000);
 *   for (;;) {
 *       tick_f7_wait();                    // 阻塞到下一個 tick 邊緣
 *       uint32_t late = tick_f7_late_us(); // 邊緣→喚醒延遲（抖動量測）
 *       ...exchange...
 *       tick_f7_trim_us(pll_trim);         // DC 鎖相：下一週期 ±µs（單次生效）
 *   }
 */
#ifndef TICK_F7_H
#define TICK_F7_H

#include <stdint.h>

void     tick_f7_init(uint32_t hz);      /* TIM6 IRQ 週期源（hz ≤ 1 MHz） */
uint32_t tick_f7_wait(void);             /* 等下一 tick,回累計 tick 數 */
uint32_t tick_f7_late_us(void);          /* 本 tick 邊緣到 wait 返回的 µs */
uint32_t tick_f7_overruns(void);         /* wait 前已錯過的 tick 累計 */
void     tick_f7_trim_us(int32_t us);    /* 下一週期長度 ±us（PLL,單次） */

#endif
