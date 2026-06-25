/**
 * @file    integration_example.c
 * @brief   CubeMX 整合「接線」範例（參考用,請複製片段到 CubeMX 產生的 main.c）
 *
 * 本檔示範如何把 firmware/canopen + firmware/app 接到 STM32CubeMX 專案:
 *   - HAL_CAN RX 中斷回呼 → 分流到 co_bxcan_on_rx()
 *   - TIM6 1 kHz → 呼叫 app_main_tick()
 *   - main() 啟動順序
 *
 * 不建議直接編譯本檔（會與 CubeMX 產生的 main.c 重複定義）;請當作範本。
 */
#include "stm32f7xx_hal.h"
#include "canopen.h"
#include "co_bxcan.h"

extern CAN_HandleTypeDef hcan1;   /* CubeMX：左臂 */
extern CAN_HandleTypeDef hcan2;   /* CubeMX：右臂 */
extern TIM_HandleTypeDef htim6;   /* CubeMX：1 kHz tick 來源 */

void app_main_init(void);
void app_main_tick(void);

/* ---- 1) CAN 接收中斷回呼：FIFO0 有新訊息 → 入佇列 ---- */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h)
{
    CAN_RxHeaderTypeDef rh;
    co_frame_t f;
    if (HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &rh, f.data) != HAL_OK) return;
    f.id  = (uint16_t)rh.StdId;
    f.dlc = (uint8_t)rh.DLC;
    co_bxcan_on_rx((h->Instance == CAN1) ? CO_BUS_LEFT : CO_BUS_RIGHT, &f);
}

/* ---- 2) TIM6 週期回呼：1 kHz 控制 tick ---- */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        app_main_tick();
    }
}

/* ---- 3) main() 啟動順序（節錄,放進 CubeMX main 的 USER CODE 區）----
 *
 * int main(void){
 *     HAL_Init();
 *     SystemClock_Config();
 *     MX_GPIO_Init();
 *     MX_CAN1_Init();          // 由 CubeMX 產生（可只設 GPIO，時序由 co_bxcan_init 覆寫）
 *     MX_CAN2_Init();
 *     MX_TIM6_Init();          // 設定為 1 kHz 更新事件
 *
 *     app_main_init();         // 內部呼叫 dual_arm_init()（含 co_bxcan_init）
 *     HAL_TIM_Base_Start_IT(&htim6);   // 啟動 1 kHz tick
 *
 *     while (1) {
 *         // 背景工作（log、上位機通訊…）；即時控制在 TIM6 ISR
 *     }
 * }
 *
 * 注意：
 *  - co_bxcan_init() 內已呼叫 HAL_CAN_Init/Start/ActivateNotification 並覆寫位元時序,
 *    若 CubeMX 的 MX_CANx_Init 也呼叫 HAL_CAN_Init,請擇一（建議讓 co_bxcan_init 主導）。
 *  - TIM6：APBx timer clock / (PSC+1) / (ARR+1) = 1000 Hz。
 *    例：timer clk 90 MHz → PSC=89, ARR=999 → 1 kHz。
 *  - NVIC 需開啟 CAN1_RX0_IRQ、CAN2_RX0_IRQ、TIM6_DAC_IRQ。
 */
