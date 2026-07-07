/**
 * L3-18 · Linux 1kHz 即時迴圈 + 抖動統計（對照 learn/l3-adv.html 第 18 節）
 *
 * 一般權限可跑（數字差）；sudo ./demo 會嘗試 SCHED_FIFO+mlockall（數字好）。
 * 在 PREEMPT_RT kernel 上兩者差距會非常明顯 — 這就是專案 B 的地基。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>

#define PERIOD_NS 1000000L      /* 1 ms */
#define N_CYCLES  2000          /* 2 秒 */

int main(void)
{
    struct sched_param sp = { .sched_priority = 90 };
    if (sched_setscheduler(0, SCHED_FIFO, &sp) == 0 &&
        mlockall(MCL_CURRENT | MCL_FUTURE) == 0)
        printf("RT 模式: SCHED_FIFO 90 + mlockall ✓\n");
    else
        printf("一般模式(要好數字請 sudo,最好配 PREEMPT_RT kernel)\n");

    long lat[N_CYCLES];
    struct timespec next, now;
    clock_gettime(CLOCK_MONOTONIC, &next);

    for (int i = 0; i < N_CYCLES; i++) {
        next.tv_nsec += PERIOD_NS;
        if (next.tv_nsec >= 1000000000L) { next.tv_sec++; next.tv_nsec -= 1000000000L; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        clock_gettime(CLOCK_MONOTONIC, &now);
        lat[i] = (now.tv_sec - next.tv_sec) * 1000000000L
               + (now.tv_nsec - next.tv_nsec);
        /* 這裡放 ec_send/receive_processdata() + control_tick() */
    }

    long mx = 0, sum = 0;
    int over100us = 0;
    for (int i = 0; i < N_CYCLES; i++) {
        if (lat[i] > mx) mx = lat[i];
        sum += lat[i];
        if (lat[i] > 100000) over100us++;
    }
    printf("%d cycles @1kHz: avg=%ld µs max=%ld µs, >100µs 的有 %d 次\n",
           N_CYCLES, sum / N_CYCLES / 1000, mx / 1000, over100us);
    printf("驗收標準參考: RT 機 max 應壓在百 µs 內(先用 cyclictest 量 kernel 底)\n");
    return 0;
}
