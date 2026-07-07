/**
 * 專案B M6 · 抖動量測 + CSV 報告（對照 project-ecat-master.html M6）
 *
 * 量 1kHz 迴圈每個 cycle 的喚醒延遲,輸出 jitter.csv + 直方圖摘要 —
 * 拿去跟專案 A(500Hz MCU 硬體 timer)對照,就是里程碑要的比較報告。
 * 一般權限可跑;sudo + PREEMPT_RT 才是正式數據。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>

#define PERIOD_NS 1000000L
#define N_CYCLES  10000            /* 10 秒 */

int main(void)
{
    struct sched_param sp = { .sched_priority = 90 };
    int rt = sched_setscheduler(0, SCHED_FIFO, &sp) == 0 &&
             mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
    printf("mode: %s\n", rt ? "SCHED_FIFO+mlockall" : "normal (數據僅供參考)");

    static long lat[N_CYCLES];
    struct timespec next, now;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (int i = 0; i < N_CYCLES; i++) {
        next.tv_nsec += PERIOD_NS;
        if (next.tv_nsec >= 1000000000L) { next.tv_sec++; next.tv_nsec -= 1000000000L; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        clock_gettime(CLOCK_MONOTONIC, &now);
        lat[i] = (now.tv_sec - next.tv_sec) * 1000000000L
               + (now.tv_nsec - next.tv_nsec);
    }

    FILE *csv = fopen("jitter.csv", "w");
    fprintf(csv, "cycle,latency_us\n");
    long mx = 0, sum = 0;
    int bins[6] = { 0 };                       /* <10 <50 <100 <500 <1000 >= */
    for (int i = 0; i < N_CYCLES; i++) {
        long us = lat[i] / 1000;
        fprintf(csv, "%d,%ld\n", i, us);
        if (lat[i] > mx) mx = lat[i];
        sum += lat[i];
        bins[us < 10 ? 0 : us < 50 ? 1 : us < 100 ? 2 : us < 500 ? 3 : us < 1000 ? 4 : 5]++;
    }
    fclose(csv);

    printf("%d cycles @1kHz -> jitter.csv\n", N_CYCLES);
    printf("avg=%ld µs  max=%ld µs\n", sum / N_CYCLES / 1000, mx / 1000);
    const char *lbl[6] = { "<10µs", "<50µs", "<100µs", "<500µs", "<1ms", ">=1ms" };
    for (int b = 0; b < 6; b++)
        printf("%7s: %5d (%4.1f%%)\n", lbl[b], bins[b], 100.0 * bins[b] / N_CYCLES);
    printf("報告寫法: RT vs 非RT 各跑一次,附 cyclictest 底噪,結論才有說服力\n");
    return 0;
}
