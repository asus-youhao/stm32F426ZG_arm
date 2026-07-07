/**
 * @file  hal_stub.h
 * @brief L1 範例用的迷你 STM32 HAL 替身 — 讓教學程式碼「免板子」在 PC 上跑。
 *
 * 用法：範例 main.c 寫成
 *   #ifdef HOST_SIM
 *   #include "../../common/hal_stub.h"
 *   #else
 *   #include "stm32f7xx_hal.h"   // 真板子走 CubeMX 專案
 *   #endif
 * 同一份程式碼，host 模擬印出行為、target 直接燒錄。
 * 本手法與 repo 的 firmware/sim/hal_shim.c 同一哲學（見 L2-12）。
 */
#ifndef HAL_STUB_H
#define HAL_STUB_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* ---- GPIO ---- */
typedef struct { const char *name; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;

static GPIO_TypeDef hal_stub_gpiob __attribute__((unused)) = { "GPIOB" };
#define GPIOB (&hal_stub_gpiob)
#define GPIO_PIN_0            ((uint16_t)0x0001)
#define GPIO_MODE_OUTPUT_PP   0x0001u
#define GPIO_NOPULL           0x0000u
#define GPIO_SPEED_FREQ_LOW   0x0000u
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)

/* ---- 時間 ---- */
static inline uint32_t HAL_GetTick(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
static inline void HAL_Delay(uint32_t ms) { usleep(ms * 1000u); }
static inline void HAL_Init(void)          { printf("[HAL] init (host sim)\n"); }
static inline void SystemClock_Config(void){ printf("[RCC] 216 MHz (pretend)\n"); }

/* ---- GPIO 行為：印出來就是「看得到的 LED」 ---- */
static inline void HAL_GPIO_Init(GPIO_TypeDef *g, GPIO_InitTypeDef *init)
{
    printf("[GPIO] %s pin 0x%04x mode=%u init\n", g->name, (unsigned)init->Pin,
           (unsigned)init->Mode);
}
static inline void HAL_GPIO_TogglePin(GPIO_TypeDef *g, uint16_t pin)
{
    printf("[%8u ms] %s pin 0x%04x TOGGLE\n", HAL_GetTick(), g->name, pin);
}

/* ---- UART：轉 stdout ---- */
typedef struct { int instance; } UART_HandleTypeDef;
#define HAL_OK 0
static inline int HAL_UART_Transmit(UART_HandleTypeDef *h, const uint8_t *buf,
                                    uint16_t len, uint32_t timeout)
{
    (void)h; (void)timeout;
    fwrite(buf, 1, len, stdout);
    fflush(stdout);
    return HAL_OK;
}

#endif /* HAL_STUB_H */
