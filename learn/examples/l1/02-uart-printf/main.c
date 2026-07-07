/**
 * L1-4 · UART printf 除錯（對照 learn/l1-basic.html 第 4 節）
 *
 * 真板上 printf 要經過 _write() retarget 到 USART3（ST-Link VCP）；
 * host 模擬時 hal_stub 的 HAL_UART_Transmit 直接印到 stdout。
 */
#include <stdio.h>
#ifdef HOST_SIM
#include "../../common/hal_stub.h"
#else
#include "stm32f7xx_hal.h"
#endif

UART_HandleTypeDef huart3;                 /* 真板由 CubeMX 產生並初始化 */

/* printf 的出口：newlib 的低階 write 轉 UART（真板/模擬同一份） */
int _write(int fd, char *buf, int len)
{
    (void)fd;
    HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)len, 100);
    return len;
}

int main(void)
{
    HAL_Init();
    printf("dual-arm fw boot\r\n");

    for (int n = 0; n < 5; n++) {
        printf("[%8u ms] count = %d\r\n", (unsigned)HAL_GetTick(), n);
        HAL_Delay(200);
    }
    printf("done — 真板上請開 115200-8N1 序列埠看這些字\r\n");
    return 0;
}
