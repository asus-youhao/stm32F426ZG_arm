/**
 * @file  tick_f7.c（WP-SE4）
 * @brief TIM6 1 kHz 週期源。bare-register（不掛 HAL TIM,IRQ 內零呼叫負擔）。
 *
 * 時脈（影響硬體行為,文件須標註）：APB1=54 MHz → TIM6 內核時脈 108 MHz;
 * PSC=107 → 1 MHz 計數;ARR=週期-1。trim 直接改下一週期 ARR,update 後自動復原。
 * NVIC：TIM6_DAC_IRQn 優先權 1（SysTick 預設 15,不會反壓）。
 */
#include "tick_f7.h"
#include "stm32f7xx_hal.h"

static volatile uint32_t s_tick;         /* IRQ 累計 */
static volatile uint32_t s_edge_cyc;     /* tick 邊緣的 DWT 時戳 */
static volatile int32_t  s_trim_us;      /* 待套用的單次 trim */
static uint32_t s_period_us;
static uint32_t s_seen;                  /* 主迴圈已消化的 tick */
static uint32_t s_late_us;
static uint32_t s_overruns;

void TIM6_DAC_IRQHandler(void)
{
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR = ~TIM_SR_UIF;
        s_edge_cyc = DWT->CYCCNT;
        s_tick++;
        /* trim：本次 update 已載入的 ARR 是「上次設定」;此處設定的 ARR
           會在下一個 update 生效（ARPE=0 直接寫也只影響尚未到的 compare;
           為單次語意,先套 trim 再於下下次復原） */
        if (s_trim_us) {
            TIM6->ARR = (uint32_t)((int32_t)s_period_us - 1 + s_trim_us);
            s_trim_us = 0;
        } else if (TIM6->ARR != s_period_us - 1) {
            TIM6->ARR = s_period_us - 1;             /* 復原 nominal */
        }
    }
}

void tick_f7_init(uint32_t hz)
{
    s_period_us = 1000000U / hz;
    s_tick = s_seen = s_overruns = 0;
    s_trim_us = 0;

    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
    (void)RCC->APB1ENR;
    TIM6->CR1 = 0;
    /* TIM6 內核時脈 = 2×APB1 = 108 MHz（SystemClock 216/APB1 div4 前提） */
    TIM6->PSC = (HAL_RCC_GetPCLK1Freq() * 2U / 1000000U) - 1U;   /* → 1 MHz */
    TIM6->ARR = s_period_us - 1U;
    TIM6->EGR = TIM_EGR_UG;                          /* 載入 PSC/ARR */
    TIM6->SR = 0;
    TIM6->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
}

uint32_t tick_f7_wait(void)
{
    uint32_t t = s_tick;
    while (t == s_seen) {                            /* 自旋到下一邊緣 */
        t = s_tick;
    }
    if (t - s_seen > 1)
        s_overruns += t - s_seen - 1;                /* 錯過的 tick */
    s_seen = t;
    s_late_us = (DWT->CYCCNT - s_edge_cyc) / (SystemCoreClock / 1000000U);
    return t;
}

uint32_t tick_f7_late_us(void) { return s_late_us; }
uint32_t tick_f7_overruns(void) { return s_overruns; }

void tick_f7_trim_us(int32_t us)
{
    if (us > 50) us = 50;                            /* ±5% 週期硬限 */
    if (us < -50) us = -50;
    s_trim_us = us;
}
