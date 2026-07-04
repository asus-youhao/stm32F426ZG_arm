/**
 * @file    eng_port.h
 * @brief   loop engine 的平台移植層（WP-H0）
 *
 * engine 核心（loop_engine.c）不 include 任何 POSIX / HAL 標頭，
 * 唯一的平台相依是本檔宣告的函式，由各平台以連結期提供：
 *   PC（PREEMPT_RT）：clock_gettime(CLOCK_MONOTONIC) 換算 µs
 *   STM32F746       ：DWT->CYCCNT 或 TIM 換算 µs
 *   單元測試        ：假時鐘（可手動推進，見 tests/test_engine.c）
 */
#ifndef ENG_PORT_H
#define ENG_PORT_H

#include <stdint.h>

/** @brief 單調遞增微秒時戳（絕對時間排程與 WCET 量測共用）。 */
uint64_t port_now_us(void);

#endif /* ENG_PORT_H */
