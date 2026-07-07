/**
 * @file  ecat_probe_main.c
 * @brief HIL-0/HIL-1 探測韌體（WP-SE3/SE6 驗收）——獨立 target `make ecat-probe`
 *
 * 開機後經 VCP(115200) 輸出：
 *   1. PHY link 狀態（無線 → 等待重試,≒真機 E403）
 *   2. 掃鏈（ecx_config_init）：從站數、身分（vendor/product）
 *   3. 有從站 → CoE 讀 0x1000、AL 狀態爬 PREOP→SAFEOP→OP + 精簡 PDO CSP 點動
 *
 * 對接對象：
 *   HIL-0 = 直連 PC NIC + Wireshark（看得到 0x88A4 幀即過）
 *   HIL-1 = PC 跑 firmware/sim_py/ecat_slave.py --iface <NIC>（假 PHU）
 *   HIL-2 = 真 PHU（先做 SE-P0：0x2100 控制權切 EtherCAT）
 */
#include "stm32f7xx_hal.h"
#include "soem/soem.h"
#include "oshw.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef huart3;    /* ST-Link VCP */
static ecx_contextt ctx;
static uint8 iomap[512];             /* 14 軸精簡 PDO 6B/軸 → 168B,留裕度 */

typedef struct __attribute__((packed)) { uint16_t cw; int32_t tgt; } out_t;
typedef struct __attribute__((packed)) { uint16_t sw; int32_t pos; } in_t;

static void logf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)n, 100);
}

/* ---- 時脈 216 MHz（同 board/main.c） ---- */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_BYPASS;              /* Nucleo：ST-Link MCO 8 MHz */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 432;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 9;
    HAL_RCC_OscConfig(&osc);
    HAL_PWREx_EnableOverDrive();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7);
}

static void vcp_init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart3);
}

int main(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();                          /* 開 D-cache 驗證 MPU 區有效性 */
    HAL_Init();
    SystemClock_Config();
    vcp_init();

    logf("\r\n=== ecat_probe（WP-SE3 HIL-0/HIL-1）===\r\n");

    if (!ecx_init(&ctx, "eth0")) {               /* → oshw_mac_init（含 PHY link 等待） */
        logf("[FAIL] ETH 初始化/PHY link（查網線,≒E403）\r\n");
        while (1) { HAL_Delay(1000); logf("."); }
    }
    logf("[ ok ] ETH MAC + PHY link up\r\n");

    int n = ecx_config_init(&ctx);
    logf("掃鏈：%d 從站\r\n", n);
    if (n <= 0) {
        logf("[HIL-0] 無從站——用 Wireshark 應看得到本板送出的 0x88A4 幀\r\n");
        while (1) {                              /* 每秒重掃,接上假從站即自動續跑 */
            HAL_Delay(1000);
            n = ecx_config_init(&ctx);
            if (n > 0) break;
            logf("重掃…\r\n");
        }
        logf("掃到 %d 從站\r\n", n);
    }
    for (int s = 1; s <= ctx.slavecount; s++)
        logf("  軸 %d：vendor=0x%08X product=0x%08X\r\n", s,
             (unsigned)ctx.slavelist[s].eep_man, (unsigned)ctx.slavelist[s].eep_id);

    ecx_config_map_group(&ctx, iomap, 0);
    logf("PDO 映射：%uO+%uI bytes\r\n",
         (unsigned)ctx.grouplist[0].Obytes, (unsigned)ctx.grouplist[0].Ibytes);

    ecx_statecheck(&ctx, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
    logf("AL：SAFEOP=%s\r\n", ctx.slavelist[0].state == EC_STATE_SAFE_OP ? "OK" : "FAIL");

    uint32_t v = 0; int sz = sizeof(v);
    ecx_SDOread(&ctx, 1, 0x1000, 0, FALSE, &sz, &v, EC_TIMEOUTRXM);
    logf("CoE：0x1000=0x%08X（期望 0x00020192）\r\n", (unsigned)v);
    int8_t mode = 8;
    ecx_SDOwrite(&ctx, 1, 0x6060, 0, FALSE, 1, &mode, EC_TIMEOUTRXM);

    ecx_send_processdata(&ctx);
    ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
    ctx.slavelist[0].state = EC_STATE_OPERATIONAL;
    ecx_writestate(&ctx, 0);
    for (int i = 0; i < 10 && ctx.slavelist[0].state != EC_STATE_OPERATIONAL; i++) {
        ecx_send_processdata(&ctx);
        ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
        ecx_statecheck(&ctx, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE / 10);
    }
    logf("AL：OP=%s\r\n", ctx.slavelist[0].state == EC_STATE_OPERATIONAL ? "OK" : "FAIL");

    /* 1 kHz CSP 點動（使能三步 + 目標步進）——SysTick 輪詢節拍 */
    out_t *out = (out_t *)ctx.slavelist[1].outputs;
    in_t *in = (in_t *)ctx.slavelist[1].inputs;
    int expected = ctx.grouplist[0].outputsWKC * 2 + ctx.grouplist[0].inputsWKC;
    const uint16_t seq[3] = {0x0006, 0x0007, 0x000F};
    for (int p = 0; p < 3; p++) {
        out->cw = seq[p];
        for (int i = 0; i < 5; i++) {
            ecx_send_processdata(&ctx);
            ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
            HAL_Delay(1);
        }
        logf("cw=0x%02X → sw=0x%04X\r\n", seq[p], in->sw);
    }
    uint32_t bad = 0, t_next = HAL_GetTick();
    out->tgt = 5000;
    for (int i = 0; i < 2000; i++) {             /* 2 秒 @1kHz */
        while ((int32_t)(HAL_GetTick() - t_next) < 0) {}
        t_next += 1;
        ecx_send_processdata(&ctx);
        if (ecx_receive_processdata(&ctx, EC_TIMEOUTRET) != expected) bad++;
    }
    logf("CSP：pos=%ld（目標 5000）WKC 漏=%lu → %s\r\n",
         (long)in->pos, (unsigned long)bad,
         (in->pos > 4800 && bad == 0) ? "PASS" : "FAIL");

    while (1) {                                  /* 保持 OP,持續交換 */
        ecx_send_processdata(&ctx);
        ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
        HAL_Delay(1);
    }
}

/* HAL 弱符號補齊（同 bring-up main.c 樣式） */
void SysTick_Handler(void) { HAL_IncTick(); }
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_USART3_CLK_ENABLE();
        g.Pin = GPIO_PIN_8 | GPIO_PIN_9;         /* PD8 TX / PD9 RX */
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF7_USART3;
        HAL_GPIO_Init(GPIOD, &g);
    }
}
