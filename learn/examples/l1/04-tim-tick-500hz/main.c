/**
 * L1-6 · 固定 2ms 控制節拍 + 抖動量測（對照 learn/l1-basic.html 第 6 節）
 *
 * 真板：TIM6 Prescaler=108-1 / Period=2000-1 → 每 2000µs 進一次
 *       HAL_TIM_PeriodElapsedCallback（見教學頁程式碼）。
 * host 模擬：用 clock_nanosleep 絕對時間法模擬同一個 2ms tick，
 *       順便量抖動 — 這正是專案 B (L3-18) RT 迴圈的骨架。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#define TICK_NS  2000000L      /* 2ms = CONTROL_DT_US (firmware/app/control_rate.h) */
#define N_TICKS  500           /* 跑 1 秒 */

static void control_tick(int n)          /* 真板上這裡就是 app_main_tick() */
{
    (void)n;
}

int main(void)
{
    struct timespec next, real;
    long jit, max_jit = 0, sum_jit = 0;

    clock_gettime(CLOCK_MONOTONIC, &next);
    for (int i = 0; i < N_TICKS; i++) {
        next.tv_nsec += TICK_NS;                       /* 絕對時間:不累積漂移 */
        if (next.tv_nsec >= 1000000000L) { next.tv_sec++; next.tv_nsec -= 1000000000L; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        clock_gettime(CLOCK_MONOTONIC, &real);
        jit = (real.tv_sec - next.tv_sec) * 1000000000L + (real.tv_nsec - next.tv_nsec);
        if (jit > max_jit) max_jit = jit;
        sum_jit += jit;
        control_tick(i);
    }
    printf("%d ticks @500Hz: 平均喚醒延遲 %ld µs, 最大 %ld µs\n",
           N_TICKS, sum_jit / N_TICKS / 1000, max_jit / 1000);
    printf("(一般 kernel 最大值可能到 ms 級 — 這就是專案 B 要 PREEMPT_RT 的理由)\n");
    return 0;
}
