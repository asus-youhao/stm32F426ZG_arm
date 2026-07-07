/**
 * L1-5 · SysTick / HAL_GetTick 與逾時判斷（對照 learn/l1-basic.html 第 5 節）
 *
 * 用 1ms 系統節拍做「多久沒收到資料」的逾時偵測 —
 * 這個 pattern 就是 firmware/safety 通訊看門狗（L3-16）的最小雛形。
 */
#include <stdio.h>
#ifdef HOST_SIM
#include "../../common/hal_stub.h"
#else
#include "stm32f7xx_hal.h"
#endif

#define TIMEOUT_MS 300u

int main(void)
{
    HAL_Init();

    uint32_t last_rx_ms = HAL_GetTick();   /* 假裝開機時收過一次 */
    int fed = 0;

    for (int i = 0; i < 12; i++) {
        HAL_Delay(100);
        uint32_t now = HAL_GetTick();

        /* 模擬：前 4 輪有「新資料」餵狗，之後資料斷線 */
        if (i < 4) {
            last_rx_ms = now;              /* 只有真的收到才更新！(L3-16 教訓) */
            fed++;
        }

        uint32_t age = now - last_rx_ms;   /* unsigned 相減,溢位也正確 */
        printf("[%8u ms] age=%4u ms %s\n", (unsigned)now, (unsigned)age,
               age > TIMEOUT_MS ? "** TIMEOUT — 安全停止! **" : "ok");
    }
    printf("餵狗 %d 次後斷線,看門狗在 %u ms 後咬人\n", fed, TIMEOUT_MS);
    return 0;
}
