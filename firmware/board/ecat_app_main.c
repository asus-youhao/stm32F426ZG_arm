/**
 * @file  ecat_app_main.c（WP-SE5/SE6）
 * @brief 板端 EtherCAT「harness 正式路徑」韌體——獨立 target `make ecat-app`
 *
 * 與 ecat_probe_main.c（裸 SOEM 探測）的差別：本檔走完整正式棧
 *   bus_ecat(bus_if vtable) → ec_master_soem(門面) → f7 port(nicdrv/osal)
 *   + loop engine 四相位 agent（bus_rx/safety/motion/bus_tx,WP-H1）
 *   + L1–L4 全棧（app_main_init_hz）
 * ＝ pc_master --bus ethercat 的板端等價物（無 harness 執行緒,監督簡化為
 *    tick_f7 + 報告;RT 路徑與 PC 完全同一份 C）。
 *
 * 流程：BUS_UP(掃鏈→OP,阻塞) → engine 啟動 @ECAT_APP_HZ → t=2s 下 0.01 rad
 * 關節移動 → t=12s 出 SE6 驗收報告（engine/agent 統計、WKC、CSP 跟隨）→
 * 之後 keepalive 續跑,每 5 s 一行短狀態。
 *
 * 對接：PC 跑 ecat_slave.py **--factory-pdo**（ec_master_soem 不重映射,
 * 依 ec_config.h 出廠 33B/29B 佈局取偏移;精簡 6B 佈局會錯位）。
 * 建置注意：EC_TIMEOUTRET 需 ≪ 週期（見 Makefile ecat-app 的 -D）。
 */
#include "stm32f7xx_hal.h"
#include "bus_if.h"
#include "dual_arm.h"          /* g_jstate */
#include "loop_engine.h"
#include "app_agents.h"
#include "joint_space.h"       /* js_rad_to_counts（驗收期望值換算） */
#include "tick_f7.h"
#include "oshw.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>

#ifndef ECAT_APP_HZ
#define ECAT_APP_HZ 250        /* AF_PACKET 假從站 RTT ~1ms → 250Hz 全收;真機再上 1k */
#endif

/* app_main.c 對外 API（無集中標頭,同 pc_master_main.c 作法宣告） */
void app_select_bus(const bus_if_t *bus);
void app_main_init_hz(float hz);
bool app_is_ready(void);
int  app_present_count(void);
void app_joint_move(int joint, float rad);
const char *app_sys_state(void);
void ec_soem_set_ifname(const char *n);      /* ec_master_soem.c */
extern const bus_if_t g_bus_ecat;

static UART_HandleTypeDef huart3;            /* ST-Link VCP */
static loop_engine_t s_eng;

/* app_main.c 的預設 bus 指標指向 CANopen;本 target 不編 CANopen 棧 →
   佔位空 vtable（main 進場即 app_select_bus(&g_bus_ecat),不會被呼叫） */
const bus_if_t g_bus_canopen = { .name = "canopen-stub" };

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

/* ---- 時脈 216 MHz（同 ecat_probe_main.c/board/main.c） ---- */
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

static void report(uint32_t rate_hz)
{
    const eng_stats_t *st = eng_stats(&s_eng);
    bus_health_t h;
    g_bus_ecat.health(0, 0, &h);

    logf("SE6 harness@%luHz：tick=%lu skip=%lu miss=%lu overrun=%lu late_max=%luµs\r\n",
         (unsigned long)rate_hz, (unsigned long)st->ticks, (unsigned long)st->skipped,
         (unsigned long)st->miss, (unsigned long)st->overruns,
         (unsigned long)st->late_max_us);
    static const char *agn[] = { "bus_rx", "safety", "motion", "bus_tx" };
    for (int i = 0; i < 4; i++) {
        const agent_stats_t *a = eng_agent_stats(&s_eng, i);
        logf("  %s：max R/C/W/H=%lu/%lu/%lu/%luµs 超算=%lu\r\n", agn[i],
             (unsigned long)a->max_us[0], (unsigned long)a->max_us[1],
             (unsigned long)a->max_us[2], (unsigned long)a->max_us[3],
             (unsigned long)a->budget_over_total);
    }
    logf("  WKC=%u/%u sync=%d 安全=%s\r\n", h.proto[0], h.proto[1],
         (int)h.sync_ok, app_sys_state());
    for (int j = 0; j < 2; j++)
        logf("  軸%d：sw=0x%04X pos=%ld tgt=%ld en=%d fresh=%d\r\n", j,
             g_jstate[j].statusword, (long)g_jstate[j].pos_actual,
             (long)g_jstate[j].target_pos, (int)g_jstate[j].enabled,
             (int)g_jstate[j].fb_fresh);
}

int main(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();
    HAL_Init();
    SystemClock_Config();
    vcp_init();

    logf("\r\n=== ecat_app（WP-SE5/SE6 harness 正式路徑）===\r\n");

    int mrc;
    while ((mrc = oshw_mac_init((const uint8_t *)priMAC)) != 0) {
        logf("[等待] oshw_mac_init=%d（-1=HAL/REF_CLK -2=無 link -3=start）\r\n", mrc);
        HAL_Delay(2000);
    }
    logf("[ ok ] ETH MAC + PHY link up\r\n");

    ec_soem_set_ifname("eth0");                  /* f7 nicdrv 不看名字,非 NULL 即可 */
    app_select_bus(&g_bus_ecat);

    /* BUS_UP：掃鏈→PREOP→(不重映射)→SAFEOP→OP + L2–L4 初始化（阻塞,engine 前） */
    while (1) {
        app_main_init_hz((float)ECAT_APP_HZ);
        if (app_is_ready()) break;
        logf("[等待] BUS_UP 失敗（掃鏈/OP）——確認假從站已跑 --factory-pdo,2s 後重試\r\n");
        HAL_Delay(2000);
    }
    logf("[ ok ] BUS_UP：%d 軸 present,OP 完成\r\n", app_present_count());

    eng_cfg_t ecfg = { .dt_us = 1000000UL / ECAT_APP_HZ };
    eng_init(&s_eng, &ecfg);
    if (app_agents_register(&s_eng) || eng_configure(&s_eng) || eng_activate(&s_eng)) {
        logf("[FAIL] engine/agents 啟動\r\n");
        while (1) HAL_Delay(1000);
    }
    tick_f7_init(ECAT_APP_HZ);
    logf("engine 啟動 @%dHz（bus_rx/safety/motion/bus_tx）\r\n", ECAT_APP_HZ);

    /* 腳本：2s 使能穩定 → 關節 0 移 0.01 rad → 12s 出報告。
       期望位移由 L2 軸表換算（預設 524288 counts/圈 → 0.01 rad ≈ 834 counts;
       真機 PHU 含 101 減速比為 52953088/圈,屬 robot_config 軸表組態,非此處） */
    const uint32_t T_MOVE = 2u * ECAT_APP_HZ, T_REPORT = 12u * ECAT_APP_HZ;
    int32_t pos_before = 0;
    for (uint32_t n = 0; n <= T_REPORT; n++) {
        tick_f7_wait();
        eng_tick(&s_eng);
        if (n == T_MOVE) {
            pos_before = g_jstate[0].pos_actual;
            app_joint_move(0, 0.01f);
            logf("t=2s：joint0 move 0.01rad（起點 pos=%ld）\r\n", (long)pos_before);
        }
    }
    report(ECAT_APP_HZ);

    long moved = (long)(g_jstate[0].pos_actual - pos_before);
    long expect = (long)js_rad_to_counts(0, 0.01f);
    long follow = (long)(g_jstate[0].target_pos - g_jstate[0].pos_actual);
    long err = moved - expect;
    int pass = (app_present_count() == 2) && g_jstate[0].enabled && g_jstate[1].enabled
               && err > -50 && err < 50 && follow > -500 && follow < 500;
    logf("CSP：Δpos=%ld counts（期望 %ld）跟隨差=%ld tick 溢=%lu → %s\r\n",
         moved, expect, follow, (unsigned long)tick_f7_overruns(), pass ? "PASS" : "FAIL");

    /* keepalive：續跑,每 5s 一行短狀態（~60B@115200≈5ms,250Hz 下偶發 1 次 miss 屬預期） */
    for (uint32_t n = 1;; n++) {
        tick_f7_wait();
        eng_tick(&s_eng);
        if (n % (5u * ECAT_APP_HZ) == 0) {
            bus_health_t h;
            g_bus_ecat.health(0, 0, &h);
            logf("… tick=%lu wkc=%u/%u pos0=%ld\r\n",
                 (unsigned long)eng_stats(&s_eng)->ticks, h.proto[0], h.proto[1],
                 (long)g_jstate[0].pos_actual);
        }
    }
}

/* ---- 最小 IRQ/Msp（獨立 target,不掛 board/stm32f7xx_it.c） ---- */
void SysTick_Handler(void) { HAL_IncTick(); }
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_USART3_CLK_ENABLE();
        g.Pin = GPIO_PIN_8 | GPIO_PIN_9;         /* PD8 TX / PD9 RX（VCP） */
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF7_USART3;
        HAL_GPIO_Init(GPIOD, &g);
    }
}
