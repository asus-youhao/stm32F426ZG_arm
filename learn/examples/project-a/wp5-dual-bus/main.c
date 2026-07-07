/**
 * 專案A M4(WP5) · 雙 bus 14 軸 @500Hz + 丟幀/逾週期統計（對照 project-dual-arm.html M4）
 *
 * 對手：./run.sh 會在 vcan0/vcan1 各開 7 個假從站。
 * 每個 2ms tick：兩條 bus 各下發 7 個 RPDO、收乾 TPDO；統計回授新鮮度與
 * 逾週期次數 — 對應 firmware 的 dual_arm_tick() + dual_arm_tx_drops()。
 */
#define _GNU_SOURCE
#include <time.h>
#include "../../common/can_util.h"

#define AXES_PER_BUS 7
#define TICK_NS      2000000L
#define N_TICKS      1500          /* 3 秒 */

typedef struct { uint16_t sw; int32_t pos; uint32_t rx_count; } jstate_t;

int main(void)
{
    int bus[2] = { can_open("vcan0", 0), can_open("vcan1", 0) };
    if (bus[0] < 0 || bus[1] < 0) return 1;

    jstate_t js[2][AXES_PER_BUS + 1];
    memset(js, 0, sizeof js);
    int32_t target = 0;
    long overrun = 0;
    struct timespec next, now;
    clock_gettime(CLOCK_MONOTONIC, &next);

    for (int t = 0; t < N_TICKS; t++) {
        next.tv_nsec += TICK_NS;
        if (next.tv_nsec >= 1000000000L) { next.tv_sec++; next.tv_nsec -= 1000000000L; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        target += 10;                                   /* 全軸同一條斜坡 */
        for (int b = 0; b < 2; b++) {
            struct can_frame f;
            while (can_recv(bus[b], &f)) {              /* 收乾 */
                uint32_t id = f.can_id & 0x7FF;
                if (id > COB_TPDO1(0) && id <= COB_TPDO1(AXES_PER_BUS)) {
                    jstate_t *j = &js[b][id - COB_TPDO1(0)];
                    memcpy(&j->sw, &f.data[0], 2);
                    memcpy(&j->pos, &f.data[2], 4);
                    j->rx_count++;
                }
            }
            for (int n = 1; n <= AXES_PER_BUS; n++) {   /* 下發 7 軸 */
                uint16_t cw = 0x000F;                   /* 假設已使能(見 wp2) */
                uint8_t r[6];
                memcpy(&r[0], &cw, 2); memcpy(&r[2], &target, 4);
                can_send(bus[b], COB_RPDO1(n), r, 6);
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        long late = (now.tv_sec - next.tv_sec) * 1000000000L
                  + (now.tv_nsec - next.tv_nsec);
        if (late > TICK_NS / 2) overrun++;              /* 半個週期算逾期 */
    }

    printf("%d ticks @500Hz, 逾週期 %ld 次\n", N_TICKS, overrun);
    for (int b = 0; b < 2; b++)
        for (int n = 1; n <= AXES_PER_BUS; n++)
            printf("bus%d node%d: rx=%4u pos=%7d %s\n", b, n,
                   js[b][n].rx_count, js[b][n].pos,
                   js[b][n].rx_count > 0 ? "✓" : "✗ 沒回授");
    close(bus[0]); close(bus[1]);
    return 0;
}
