/**
 * L2-10 · PDO 即時交換 + CiA402 使能（對照 learn/l2-mid.html 第 10 節）
 *
 * 對手：python3 ../../common/fake_slave.py --node 1
 * 行為：100Hz 迴圈 — 收 TPDO1 statusword → enable_step 推下一個 controlword
 *       → RPDO1 下發 (cw + target)。使能完成後 target 走小斜坡，看回授跟隨。
 * 對照 repo：co_pdo.h 的 send_csp/get_feedback + cia402.h 的 enable_step。
 */
#include "../../common/can_util.h"

static uint16_t enable_step(uint16_t sw)       /* 同 firmware cia402_enable_step */
{
    if (sw & 0x0008) return 0x0080;            /* FAULT -> reset */
    if ((sw & 0x006F) == 0x0027) return 0x000F;/* OPERATION_ENABLED 保持 */
    if ((sw & 0x006F) == 0x0023) return 0x000F;/* SWITCHED_ON -> enable */
    if ((sw & 0x006F) == 0x0021) return 0x0007;/* READY -> switch on */
    return 0x0006;                             /* 其他 -> shutdown */
}

int main(void)
{
    int s = can_open("vcan0", 5);
    if (s < 0) return 1;

    uint16_t sw = 0; int32_t pos = 0, target = 0;
    struct can_frame f;

    for (int tick = 0; tick < 300; tick++) {   /* 3 秒 @100Hz */
        while (can_recv(s, &f))                /* 收乾 TPDO1 */
            if ((f.can_id & 0x7FF) == COB_TPDO1(1) && f.can_dlc >= 6) {
                memcpy(&sw, &f.data[0], 2);
                memcpy(&pos, &f.data[2], 4);
            }

        uint16_t cw = enable_step(sw);
        int enabled = (sw & 0x006F) == 0x0027;
        if (enabled) target += 100;            /* 使能後才走斜坡 */

        uint8_t rpdo[6];
        memcpy(&rpdo[0], &cw, 2);
        memcpy(&rpdo[2], &target, 4);
        can_send(s, COB_RPDO1(1), rpdo, 6);

        if (tick % 25 == 0)
            printf("t=%4dms sw=0x%04x cw=0x%04x target=%6d pos=%6d\n",
                   tick * 10, sw, cw, target, pos);
        usleep(10000);
    }
    printf("結束：pos 應緊跟 target（一階慣性落後）\n");
    close(s);
    return 0;
}
