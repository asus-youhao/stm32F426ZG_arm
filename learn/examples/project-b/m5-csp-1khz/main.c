/**
 * 專案B M5 · 單軸 CSP @1kHz 正弦跟隨 — SOEM 骨架（對照 project-ecat-master.html M5）
 *
 * = L3-18 的 RT 迴圈 + L3-13 的使能交握 + CoE PDO。
 * ⚠️ 單位：EYOU EtherCAT 介面實測為馬達端 19-bit × 減速比 101
 *          = 52953088 counts/輸出圈（與專案 A 輸出端 524288 不同,先驗證!）
 * 建置同 l3/05-ecat-skeleton（-DHAVE_SOEM + libsoem）;無 SOEM 時編 dry-run。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <stdint.h>

#define COUNTS_PER_REV 52953088.0     /* 524288 × 101 (EYOU EtherCAT 實測) */
#define PERIOD_NS      1000000L

/* PDO 佈局要照 ESI 對齊 — 這裡假設 RxPDO: cw(u16)+target(i32), TxPDO: sw(u16)+pos(i32) */
typedef struct __attribute__((packed)) { uint16_t cw; int32_t target; } rxpdo_t;
typedef struct __attribute__((packed)) { uint16_t sw; int32_t pos; }    txpdo_t;

static uint16_t __attribute__((unused)) enable_step(uint16_t sw)
{
    if (sw & 0x0008)             return 0x0080;
    if ((sw & 0x006F) == 0x0027) return 0x000F;
    if ((sw & 0x006F) == 0x0023) return 0x000F;
    if ((sw & 0x006F) == 0x0021) return 0x0007;
    return 0x0006;
}

#ifdef HAVE_SOEM
#include "ethercat.h"
static char IOmap[4096];

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: sudo %s <iface>\n", argv[0]); return 1; }
    struct sched_param sp = { .sched_priority = 90 };
    sched_setscheduler(0, SCHED_FIFO, &sp);
    mlockall(MCL_CURRENT | MCL_FUTURE);

    if (!ec_init(argv[1]) || ec_config_init(FALSE) <= 0) return 1;
    ec_config_map(&IOmap);
    ec_configdc();
    ec_statecheck(0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);
    ec_slave[0].state = EC_STATE_OPERATIONAL;
    ec_send_processdata(); ec_receive_processdata(EC_TIMEOUTRET);
    ec_writestate(0);
    ec_statecheck(0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE);

    rxpdo_t *out = (rxpdo_t *)ec_slave[1].outputs;
    txpdo_t *in  = (txpdo_t *)ec_slave[1].inputs;
    int32_t origin = 0; int homed = 0;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    for (long t = 0; t < 10000; t++) {              /* 10 秒 @1kHz */
        next.tv_nsec += PERIOD_NS;
        if (next.tv_nsec >= 1000000000L) { next.tv_sec++; next.tv_nsec -= 1000000000L; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        ec_send_processdata();
        ec_receive_processdata(EC_TIMEOUTRET);

        out->cw = enable_step(in->sw);
        int en = (in->sw & 0x006F) == 0x0027;
        if (en && !homed) { origin = in->pos; homed = 1; }    /* 使能點當原點 */
        double amp = 0.05 * COUNTS_PER_REV / (2 * M_PI);      /* ±0.05 rad */
        out->target = homed ? origin + (int32_t)(amp * sin(2 * M_PI * 0.5 * t / 1000.0))
                            : in->pos;                        /* 未使能鎖實際位置 */
        if (t % 1000 == 0)
            printf("t=%lds sw=0x%04x ferr=%d counts\n",
                   t / 1000, in->sw, out->target - in->pos);
    }
    ec_close();
    return 0;
}
#else
int main(void)
{
    puts("(dry-run: 加 -DHAVE_SOEM 與 libsoem 連結後上真機)");
    printf("單位自檢: 0.05 rad = %d counts (52953088/圈) vs %d counts (524288/圈)\n",
           (int)(0.05 * COUNTS_PER_REV / (2 * M_PI)),
           (int)(0.05 * 524288.0 / (2 * M_PI)));
    puts("流程: RT設定 -> 掃描/映射/DC -> OP -> 1kHz: 收sw ->enable_step-> 發正弦target");
    puts("驗收: following error 無週期性尖峰(有=RT/DC沒調好), 記錄 CSV 給 m6");
    return 0;
}
#endif
