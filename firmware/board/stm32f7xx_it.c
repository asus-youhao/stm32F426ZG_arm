/**
 * @file    stm32f7xx_it.c
 * @brief   中斷處理：SysTick（HAL tick）+ CAN1/CAN2 RX0。
 *          其餘核心例外（HardFault…）沿用 startup 的弱預設。
 */
#include "stm32f7xx_hal.h"

extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void CAN1_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&hcan1);
}

void CAN2_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&hcan2);
}
