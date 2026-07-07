/**
 * @file  osal.c（F746 bare-metal port, WP-SE4 前置）
 * @brief SOEM OSAL——DWT cycle counter 計時（µs 級,SOEM 逾時全靠它）,無 RTOS。
 *
 * 時基：DWT->CYCCNT @216 MHz,32-bit 每 ~19.9 s 回繞,以 64-bit 軟體延伸;
 * 前提是 osal 函式被呼叫的間隔 < 19 s（loop engine 1 kHz 下必然成立）。
 * 執行緒：不支援（回失敗）;互斥鎖：單執行緒,no-op。
 */
#include "osal.h"
#include "stm32f7xx_hal.h"
#include <stdlib.h>

/* ---- 64-bit cycle 計數（軟體延伸 DWT->CYCCNT） ---- */
static uint64_t s_cyc_hi;
static uint32_t s_cyc_last;

static uint64_t cycles64(void)
{
    uint32_t now = DWT->CYCCNT;
    if (now < s_cyc_last)
        s_cyc_hi += 0x100000000ULL;      /* 回繞 */
    s_cyc_last = now;
    return s_cyc_hi | now;
}

void osal_dwt_init(void)                  /* oshw_mac_init 會呼叫 */
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_cyc_hi = 0;
    s_cyc_last = 0;
}

static uint64_t cycles_to_us(uint64_t c) { return c / (SystemCoreClock / 1000000U); }

void osal_get_monotonic_time(ec_timet *ts)
{
    uint64_t us = cycles_to_us(cycles64());
    ts->tv_sec = (time_t)(us / 1000000ULL);
    ts->tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
}

ec_timet osal_current_time(void)
{
    ec_timet ts;
    osal_get_monotonic_time(&ts);
    return ts;
}

void osal_time_diff(ec_timet *start, ec_timet *end, ec_timet *diff)
{
    if (end->tv_nsec < start->tv_nsec) {
        diff->tv_sec = end->tv_sec - start->tv_sec - 1;
        diff->tv_nsec = end->tv_nsec + 1000000000L - start->tv_nsec;
    } else {
        diff->tv_sec = end->tv_sec - start->tv_sec;
        diff->tv_nsec = end->tv_nsec - start->tv_nsec;
    }
}

/* ---- 逾時計時器 ---- */
struct osal_timer_impl { uint64_t stop_cyc; };  /* osal_timert 內部欄位見 osal.h */

void osal_timer_start(osal_timert *self, uint32 timeout_usec)
{
    uint64_t us = cycles_to_us(cycles64()) + timeout_usec;
    self->stop_time.tv_sec = (time_t)(us / 1000000ULL);
    self->stop_time.tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
}

boolean osal_timer_is_expired(osal_timert *self)
{
    ec_timet now;
    osal_get_monotonic_time(&now);
    return (now.tv_sec > self->stop_time.tv_sec) ||
           (now.tv_sec == self->stop_time.tv_sec && now.tv_nsec >= self->stop_time.tv_nsec);
}

int osal_usleep(uint32 usec)
{
    uint64_t end = cycles64() + (uint64_t)usec * (SystemCoreClock / 1000000U);
    while (cycles64() < end) { __NOP(); }
    return 0;
}

int osal_monotonic_sleep(ec_timet *ts)
{
    ec_timet now;
    do { osal_get_monotonic_time(&now); }
    while (now.tv_sec < ts->tv_sec ||
           (now.tv_sec == ts->tv_sec && now.tv_nsec < ts->tv_nsec));
    return 0;
}

/* ---- 記憶體 ---- */
void *osal_malloc(size_t size) { return malloc(size); }
void osal_free(void *ptr) { free(ptr); }

/* ---- 執行緒（bare-metal 不支援;SOEM 核心不需要,samples 才用） ---- */
int osal_thread_create(void *thandle, int stacksize, void *func, void *param)
{
    (void)thandle; (void)stacksize; (void)func; (void)param;
    return 0;
}
int osal_thread_create_rt(void *thandle, int stacksize, void *func, void *param)
{
    (void)thandle; (void)stacksize; (void)func; (void)param;
    return 0;
}

/* ---- 互斥鎖（單執行緒 no-op;回非 NULL 佔位） ---- */
static int s_mtx_dummy;
void *osal_mutex_create(void) { return &s_mtx_dummy; }
void osal_mutex_destroy(void *mutex) { (void)mutex; }
void osal_mutex_lock(void *mutex) { (void)mutex; }
void osal_mutex_unlock(void *mutex) { (void)mutex; }
