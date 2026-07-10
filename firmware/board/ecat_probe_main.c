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
#include "tick_f7.h"
#include "ec_dc_pll.h"
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

    /* 先直接叫 oshw_mac_init 取錯誤碼：-1=HAL_ETH_Init(查 REF_CLK/DMA reset)
       -2=PHY 無 link(查網線,≒E403) -3=ETH start */
    int mrc = oshw_mac_init((const uint8_t *)priMAC);
    while (mrc != 0) {
        logf("[FAIL] oshw_mac_init=%d（-1=HAL/REF_CLK -2=無 link -3=start）\r\n", mrc);
        HAL_Delay(2000);
        mrc = oshw_mac_init((const uint8_t *)priMAC);   /* 插上網線即自動恢復 */
    }
    if (!ecx_init(&ctx, "eth0")) {               /* → oshw_mac_init（含 PHY link 等待） */
        logf("[FAIL] ecx_init\r\n");
        while (1) { HAL_Delay(1000); logf("."); }
    }
    logf("[ ok ] ETH MAC + PHY link up\r\n");
    /* GPIO 診斷：PG11/PG13 與 PB13 應為 AF11(MODER=10,AFR=0xB) */
    logf("G.MODER=0x%08lX G.AFRH=0x%08lX B.MODER=0x%08lX B.AFRH=0x%08lX MACDBGR=0x%08lX\r\n",
         (unsigned long)GPIOG->MODER, (unsigned long)GPIOG->AFR[1],
         (unsigned long)GPIOB->MODER, (unsigned long)GPIOB->AFR[1],
         (unsigned long)ETH->MACDBGR);

    int n = ecx_config_init(&ctx);
    logf("掃鏈：%d 從站\r\n", n);
    if (n <= 0) {
        logf("[HIL-0] 無從站——用 Wireshark 應看得到本板送出的 0x88A4 幀\r\n");
        while (1) {                              /* 每秒重掃,接上假從站即自動續跑 */
            HAL_Delay(1000);
            /* TX 診斷：直接送 60B 廣播測試幀,印 oshw_mac_send 回傳與 DMA 狀態 */
            static uint8_t tf[60];
            memset(tf, 0, sizeof(tf));
            memset(tf, 0xFF, 6);
            tf[6] = 0x02; tf[11] = 0x01;
            tf[12] = 0x88; tf[13] = 0xA4;
            int src = oshw_mac_send(tf, sizeof(tf));
            /* RX 輪詢 800ms：任何幀（Windows 週期性 ARP/LLDP 廣播）都算 */
            static uint8_t rxb[1536];
            int rx_n = 0, rx_last = 0;
            uint32_t t1 = HAL_GetTick();
            while (HAL_GetTick() - t1 < 800) {
                int r = oshw_mac_recv(rxb, sizeof(rxb));
                if (r > 0) { rx_n++; rx_last = r; }
            }
            uint32_t bsr = 0, ssr = 0;
            oshw_phy_read(1, &bsr); oshw_phy_read(31, &ssr);
            logf("重掃… tx=%d TX好=%lu RX幀=%d(最後%dB) RXCRC=%lu 漏=%lu BSR=%04lX SSR=%04lX\r\n",
                 src,
                 (unsigned long)ETH->MMCTGFCR,
                 rx_n, rx_last,
                 (unsigned long)ETH->MMCRFCECR,
                 (unsigned long)ETH->DMAMFBOCR & 0xFFFF,   /* DMA 丟幀計數 */
                 (unsigned long)bsr, (unsigned long)ssr);
            n = ecx_config_init(&ctx);
            if (n > 0) break;
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

    /* DC：假從站 SII 報無 DC → hasdc=0,PLL 走防護路徑;真 ESC/PHU 會啟用 */
    boolean hasdc = ecx_configdc(&ctx);
    ec_dc_pll_t pll;
    ec_dc_pll_init(&pll, 0, 0, 0);
    logf("DC：%s\r\n", hasdc ? "有(鎖相啟用)" : "無(假從站,PLL 待真 ESC)");

    out_t *out = (out_t *)ctx.slavelist[1].outputs;
    in_t *in = (in_t *)ctx.slavelist[1].inputs;
    int expected = ctx.grouplist[0].outputsWKC * 2 + ctx.grouplist[0].inputsWKC;

    /* CiA402 使能三步（協定面,npcap RTT 下用寬鬆節拍） */
    const uint16_t seq[3] = {0x0006, 0x0007, 0x000F};
    for (int p = 0; p < 3; p++) {
        out->cw = seq[p];
        for (int i = 0; i < 8; i++) {
            ecx_send_processdata(&ctx);
            ecx_receive_processdata(&ctx, 5000);
            HAL_Delay(10);
        }
        logf("cw=0x%02X → sw=0x%04X\r\n", seq[p], in->sw);
    }

    /* ===== Phase A（SE4 驗收）：TIM6 硬體 1 kHz × 10 s,tick 抖動統計 =====
       npcap 假從站 RTT ms 級 → WKC 大量 miss 屬預期,本階段只驗週期源品質 */
    tick_f7_init(1000);
    uint32_t hist[65] = {0};                     /* late 直方圖:1µs 桶 + 溢位 */
    uint32_t late_max = 0, xchg_max = 0, wkc_ok = 0, nA = 0;
    out->tgt = 5000;
    for (nA = 0; nA < 10000; nA++) {
        tick_f7_wait();
        uint32_t late = tick_f7_late_us();
        hist[late > 64 ? 64 : late]++;
        if (late > late_max) late_max = late;
        uint32_t t0 = DWT->CYCCNT;
        ecx_send_processdata(&ctx);
        if (ecx_receive_processdata(&ctx, 500) == expected) wkc_ok++;
        uint32_t us = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000U);
        if (us > xchg_max) xchg_max = us;
        if (hasdc) {                             /* DC 鎖相（真 ESC 才會進來） */
            int32_t err_us = (int32_t)(((ctx.DCtime % 1000000LL) + 1500000LL) % 1000000LL - 500000LL) / 1000;
            tick_f7_trim_us(ec_dc_pll_step(&pll, err_us));
        }
    }
    /* 直方圖 → p50/p99 */
    uint32_t acc = 0, p50 = 64, p99 = 64;
    for (int i = 0; i < 65; i++) {
        acc += hist[i];
        if (p50 == 64 && acc * 2 >= nA) p50 = i;
        if (p99 == 64 && acc * 100 >= nA * 99) p99 = i;
    }
    logf("SE4 tick@1kHz×10s：late p50=%luµs p99=%luµs max=%luµs overrun=%lu 交換max=%luµs → %s\r\n",
         (unsigned long)p50, (unsigned long)p99, (unsigned long)late_max,
         (unsigned long)tick_f7_overruns(), (unsigned long)xchg_max,
         (p99 < 20 && tick_f7_overruns() == 0) ? "PASS" : "FAIL");
    logf("  （WKC ok=%lu/10000,npcap RTT 下 miss 屬預期;真 ESC 見 Phase B）\r\n",
         (unsigned long)wkc_ok);

    /* ===== Phase B：TIM6 100 Hz × 10 s,npcap RTT 塞得進 → WKC/CSP 驗證 ===== */
    tick_f7_init(100);
    uint32_t okB = 0;
    for (uint32_t i = 0; i < 1000; i++) {
        tick_f7_wait();
        ecx_send_processdata(&ctx);
        if (ecx_receive_processdata(&ctx, 9000) == expected) okB++;
    }
    logf("Phase B 100Hz×10s：WKC ok=%lu/1000 CSP pos=%ld（目標 5000）→ %s\r\n",
         (unsigned long)okB, (long)in->pos,
         (okB > 900 && in->pos > 4800) ? "PASS" : "FAIL");

    tick_f7_init(1000);                          /* 保持 OP,1kHz 持續交換 */
    while (1) {
        tick_f7_wait();
        ecx_send_processdata(&ctx);
        ecx_receive_processdata(&ctx, 500);
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
