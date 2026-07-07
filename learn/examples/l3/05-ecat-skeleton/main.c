/**
 * L3-17 · SOEM 最小主站骨架：掃描 → OP → 讀 PDO（對照 learn/l3-adv.html 第 17 節）
 *
 * ⚠️ 需要 SOEM 函式庫 + 一張實體網卡 + root。這是唯一不能純 PC 模擬的範例。
 * 建置（SOEM 在 EtherCAT 分支的 third_party/，或 github.com/OpenEtherCATsociety/SOEM）:
 *   gcc -Wall -O2 -DHAVE_SOEM main.c -I<soem>/install/include/soem \
 *       -L<soem>/install/lib -lsoem -o demo
 *   sudo ./demo enp2s0
 * 沒有 SOEM 時 `make` 仍可編譯（dry-run 模式,印出流程說明）。
 */
#include <stdio.h>

#ifdef HAVE_SOEM
#include "ethercat.h"

static char IOmap[4096];

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: sudo %s <iface>\n", argv[0]); return 1; }

    if (!ec_init(argv[1])) { fprintf(stderr, "ec_init %s failed (root?)\n", argv[1]); return 1; }
    if (ec_config_init(FALSE) <= 0) { fprintf(stderr, "no slaves found\n"); return 1; }
    printf("found %d slaves:\n", ec_slavecount);
    for (int i = 1; i <= ec_slavecount; i++)
        printf("  %2d: %-30s vendor=0x%08x product=0x%08x\n", i,
               ec_slave[i].name, ec_slave[i].eep_man, ec_slave[i].eep_id);

    ec_config_map(&IOmap);
    ec_configdc();                                     /* 分散時鐘 */
    ec_statecheck(0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);
    printf("all slaves SAFEOP, requesting OP...\n");

    ec_slave[0].state = EC_STATE_OPERATIONAL;
    ec_send_processdata(); ec_receive_processdata(EC_TIMEOUTRET);
    ec_writestate(0);
    ec_statecheck(0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE);

    if (ec_slave[0].state == EC_STATE_OPERATIONAL) {
        printf("OP! 交換 100 輪過程資料...\n");
        for (int n = 0; n < 100; n++) {
            ec_send_processdata();
            int wkc = ec_receive_processdata(EC_TIMEOUTRET);
            if (n % 25 == 0) printf("  cycle %3d wkc=%d\n", n, wkc);
            osal_usleep(1000);                         /* 1ms — 正式版用 L3-18 RT 迴圈 */
        }
    } else {
        /* 卡 SAFEOP 的除錯起點: AL status code(專案 B 實戰紀錄) */
        ec_readstate();
        for (int i = 1; i <= ec_slavecount; i++)
            printf("  slave %d state=0x%02x ALstatus=0x%04x : %s\n", i,
                   ec_slave[i].state, ec_slave[i].ALstatuscode,
                   ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
    }
    ec_close();
    return 0;
}

#else /* dry-run：沒裝 SOEM 也能編,把流程背起來 */

int main(void)
{
    puts("(dry-run: 未連結 SOEM,以下是真實流程)");
    puts(" 1. ec_init(iface)         raw socket 綁網卡(要 root)");
    puts(" 2. ec_config_init()        掃描從站,讀 EEPROM/ESI");
    puts(" 3. ec_config_map(&IOmap)   PDO 映射進行程記憶體");
    puts(" 4. ec_configdc()           啟用分散時鐘 DC");
    puts(" 5. statecheck SAFEOP -> 寫 OP -> statecheck OP");
    puts(" 6. 迴圈 ec_send/receive_processdata (1ms, 見 06-rt-1khz)");
    puts(" 卡 SAFEOP? 讀 ALstatuscode — 見 project-ecat-master.html 實戰紀錄");
    return 0;
}
#endif
