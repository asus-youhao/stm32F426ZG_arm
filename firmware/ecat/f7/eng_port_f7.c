/**
 * @file  eng_port_f7.c（WP-SE5）
 * @brief loop engine 的 F746 時間源：port_now_us() 以 DWT CYCCNT 導出。
 *
 * CYCCNT 是 32-bit @216 MHz（~19.9 s 迴繞）→ 以「上次讀值差」累加成 64-bit。
 * 前提：呼叫間隔 < 迴繞週期（engine 每 tick 都呼叫,遠小於）;
 *       單一執行緒（板端 engine 在主迴圈跑,無並發呼叫者）。
 * DWT 解鎖（LAR=0xC5ACCE55,M7 必要）由 ecat/f7/osal.c 的初始化負責。
 */
#include "eng_port.h"
#include "stm32f7xx_hal.h"

uint64_t port_now_us(void)
{
    static uint64_t s_total_cyc;
    static uint32_t s_last;
    uint32_t now = DWT->CYCCNT;
    s_total_cyc += (uint32_t)(now - s_last);     /* 無號減法天然處理迴繞 */
    s_last = now;
    return s_total_cyc / (SystemCoreClock / 1000000U);
}
