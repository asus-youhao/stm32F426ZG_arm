/**
 * @file    main.c
 * @brief   Nucleo-F746ZG bring-up 韌體入口（WP2 單軸 SDO 驗證 + 選擇性轉動）
 *
 * 流程：HAL_Init → 216 MHz 時脈 → USART3(VCP) log → 設定 CAN handle →
 *       bringup_single_axis(CO_BUS_LEFT, node=1) → 印出報告 → 閒置。
 *
 * 硬體接線（務必與本檔一致）：
 *   - CAN1_RX = PD0, CAN1_TX = PD1（AF9）→ 接 CAN transceiver → PHU 關節
 *   - 兩端各 120Ω 終端電阻；關節 24–48V 供電、共地
 *   - log：USART3 = Nucleo ST-Link VCP（PD8=TX/PD9=RX, 115200-8-N-1）
 *
 * 安全：第一次上電請讓關節可安全自由轉動、低速、周圍淨空、備妥急停。
 */
#include "stm32f7xx_hal.h"
#include "canopen.h"
#include "co_bxcan.h"
#include "test/bringup.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- co_bxcan.c 以 extern 取用這兩個 handle ---- */
CAN_HandleTypeDef hcan1;   /* 左臂 bus（bring-up 用） */
CAN_HandleTypeDef hcan2;   /* 右臂 bus（本版未啟用，僅供連結） */

static UART_HandleTypeDef huart3;   /* ST-Link VCP，log 用 */

/* ---- bring-up 測試參數（可依需要調整）---- */
#define BRINGUP_NODE        1        /* 關節節點 ID（出廠多為 1）*/
#define BRINGUP_SPIN_VEL    2000     /* 目標速度（小值，先驗證；設 0 = 不轉只讀）*/
#define BRINGUP_SPIN_MS     2000     /* 轉動持續時間 */

static void SystemClock_Config(void);
static void UART3_Init(void);
static void Error_Handler(void);

/* ===================== bring-up log → USART3 ===================== */
/* 覆寫 bringup.c 的弱實作：vsnprintf 後經 VCP 送出。 */
void bringup_log(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf)) n = (int)sizeof(buf);
    HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)n, 100);
}

/* ===================== CAN RX 中斷 → 入佇列 ===================== */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h)
{
    CAN_RxHeaderTypeDef rh;
    co_frame_t f;
    if (HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &rh, f.data) != HAL_OK) return;
    f.id  = (uint16_t)rh.StdId;
    f.dlc = (uint8_t)rh.DLC;
    co_bxcan_on_rx((h->Instance == CAN1) ? CO_BUS_LEFT : CO_BUS_RIGHT, &f);
}

/* ===================== CAN MspInit：腳位/時脈/NVIC ===================== */
void HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
    GPIO_InitTypeDef g = {0};

    if (hcan->Instance == CAN1) {
        __HAL_RCC_CAN1_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        /* PD0 = CAN1_RX, PD1 = CAN1_TX (AF9) */
        g.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_NOPULL;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF9_CAN1;
        HAL_GPIO_Init(GPIOD, &g);

        HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    } else if (hcan->Instance == CAN2) {
        /* CAN2 為 CAN1 之 slave，需同時開 CAN1 時脈 */
        __HAL_RCC_CAN1_CLK_ENABLE();
        __HAL_RCC_CAN2_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        /* PB12 = CAN2_RX, PB13 = CAN2_TX (AF9) */
        g.Pin       = GPIO_PIN_12 | GPIO_PIN_13;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_NOPULL;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF9_CAN2;
        HAL_GPIO_Init(GPIOB, &g);

        HAL_NVIC_SetPriority(CAN2_RX0_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(CAN2_RX0_IRQn);
    }
}

/* ===================== UART MspInit ===================== */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef g = {0};
    if (huart->Instance == USART3) {
        __HAL_RCC_USART3_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        /* PD8 = USART3_TX, PD9 = USART3_RX (AF7) — Nucleo VCP */
        g.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Pull      = GPIO_PULLUP;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF7_USART3;
        HAL_GPIO_Init(GPIOD, &g);
    }
}

/* ===================== main ===================== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    UART3_Init();

    /* Nucleo LD1 (PB0) 心跳燈 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef led = {0};
    led.Pin   = GPIO_PIN_0;
    led.Mode  = GPIO_MODE_OUTPUT_PP;
    led.Pull  = GPIO_NOPULL;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &led);

    bringup_log("\r\n=== Nucleo-F746ZG CANopen bring-up ===\r\n");
    bringup_log("SYSCLK=%lu Hz  PCLK1(APB1)=%lu Hz\r\n",
                (unsigned long)HAL_RCC_GetSysClockFreq(),
                (unsigned long)HAL_RCC_GetPCLK1Freq());

    /* co_bxcan_init() 會呼叫 HAL_CAN_Init()，故先指定 Instance。*/
    hcan1.Instance = CAN1;
    hcan2.Instance = CAN2;

    bringup_report_t rep;
    co_status_t st = bringup_single_axis(CO_BUS_LEFT, BRINGUP_NODE,
                                         BRINGUP_SPIN_VEL, BRINGUP_SPIN_MS, &rep);

    bringup_log("=== bring-up result = %d (0=OK) ===\r\n", st);
    bringup_log("  deviceType=0x%08lX baud=%lu node=%lu\r\n",
                (unsigned long)rep.device_type,
                (unsigned long)rep.baudrate_bps,
                (unsigned long)rep.node_id_read);
    bringup_log("  pos_before=%ld pos_after=%ld moved=%d\r\n",
                (long)rep.pos_before, (long)rep.pos_after, rep.moved);

    for (;;) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);  /* Nucleo LD1 (PB0) 心跳 */
        HAL_Delay(500);
    }
}

/* ===================== 216 MHz 時脈（HSE 8 MHz）===================== */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* HSE 8 MHz → PLL: /M=4 → 2 MHz, *N=216 → 432 MHz, /P=2 → 216 MHz SYSCLK
       Q=9 → 48 MHz（USB/SDMMC 用）。*/
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_BYPASS;   /* Nucleo：ST-Link MCO 提供時脈 */
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 4;
    osc.PLL.PLLN       = 216;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    /* 啟用 over-drive 才能到 216 MHz */
    if (HAL_PWREx_EnableOverDrive() != HAL_OK) Error_Handler();

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* HCLK = 216 MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV4;     /* APB1 = 54 MHz（CAN1/2、USART3）*/
    clk.APB2CLKDivider = RCC_HCLK_DIV2;     /* APB2 = 108 MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK) Error_Handler();
}

static void UART3_Init(void)
{
    huart3.Instance        = USART3;
    huart3.Init.BaudRate   = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits   = UART_STOPBITS_1;
    huart3.Init.Parity     = UART_PARITY_NONE;
    huart3.Init.Mode       = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart3) != HAL_OK) Error_Handler();
}

static void Error_Handler(void)
{
    __disable_irq();
    for (;;) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
