/**
 * @file    stm32f7xx_hal.h (SIM shim)
 * @brief   主機模擬用的最小 HAL 替身,讓 protocol 層可在 PC 上編譯。
 *          僅提供 HAL_GetTick / HAL_Delay;不含任何 STM32 周邊。
 */
#ifndef SIM_STM32F7XX_HAL_H
#define SIM_STM32F7XX_HAL_H

#include <stdint.h>

uint32_t HAL_GetTick(void);
void     HAL_Delay(uint32_t ms);

#endif /* SIM_STM32F7XX_HAL_H */
