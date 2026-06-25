/**
 * @file    app_main.c
 * @brief   應用進入點：初始化雙臂 CANopen + 1 kHz 控制迴圈骨架
 *
 * 整合方式（在 CubeMX 產生的 main.c）：
 *   - MX_CAN1_Init(); MX_CAN2_Init(); 之後呼叫 app_main_init();
 *   - 以 1 kHz 來源（TIM 中斷或 SysTick 計數）呼叫 app_main_tick();
 *   - HAL_CAN_RxFifo0MsgPendingCallback 內轉呼叫 co_bxcan_on_rx()（見 co_bxcan.c）。
 */
#include "dual_arm.h"
#include "stm32f7xx_hal.h"

static volatile bool s_ready = false;

void app_main_init(void)
{
    if (dual_arm_init() == CO_OK) {
        s_ready = true;
    }
    /* else: 進入錯誤處理 / 重試（TODO：點燈、回報上位機） */
}

/**
 * @brief 1 kHz 控制 tick。
 * 之後 joint-space / task-space 控制器在此填入 dual_arm_set_target(...)。
 */
void app_main_tick(void)
{
    if (!s_ready) return;

    /* TODO L2 joint-space：軌跡插值 → 每軸目標
       TODO L3 task-space：FK/IK/Jacobian → 每軸目標
       目前先呼叫低階 CANopen 週期交換。 */

    dual_arm_tick_1khz();
}

/* 範例：若用 TIM6 產生 1 kHz
 * void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
 *     if (htim->Instance == TIM6) app_main_tick();
 * }
 */
