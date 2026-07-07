/**
 * L1-3 · GPIO Blink — 第一支程式（對照 learn/l1-basic.html 第 3 節）
 *
 * host 模擬：make run（LED 狀態印在終端機）
 * 真板：把本檔丟進 CubeMX F746 專案（拿掉 HOST_SIM），PB0 = Nucleo 綠 LED
 */
#ifdef HOST_SIM
#include "../../common/hal_stub.h"
#else
#include "stm32f7xx_hal.h"
#endif

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    __HAL_RCC_GPIOB_CLK_ENABLE();          /* 周邊先開時脈！(經典坑) */
    GPIO_InitTypeDef g = {
        .Pin   = GPIO_PIN_0,
        .Mode  = GPIO_MODE_OUTPUT_PP,
        .Pull  = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(GPIOB, &g);

#ifdef HOST_SIM
    for (int i = 0; i < 6; i++) {          /* 模擬跑 6 次就收工 */
#else
    while (1) {
#endif
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
        HAL_Delay(500);
    }
    return 0;
}
