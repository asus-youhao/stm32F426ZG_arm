/**
 * 專案A M5(WP6) · 安全：失聯偵測 → 安全停止（對照 project-dual-arm.html M5）
 *
 * 對手：./run.sh — 起假從站,跑到一半把它 kill 掉(模擬拔線)。
 * 期待：50ms 內看門狗觸發,controlword 切到安全值(0x0006 shutdown),
 *       目標鎖在最後位置。fb_fresh 教訓(L3-16)在這裡是活的。
 */
#include "../../common/can_util.h"

#define NODE       1
#define TIMEOUT_MS 50

int main(void)
{
    int s = can_open("vcan0", 2);
    if (s < 0) return 1;

    uint16_t sw = 0; int32_t pos = 0, target = 0;
    uint32_t last_fb_ms = now_ms();
    int safe_stop = 0, trip_t = -1;
    struct can_frame f;

    for (int t = 0; t < 500; t++) {                 /* 5 秒 @100Hz */
        int fb_fresh = 0;
        while (can_recv(s, &f))
            if ((f.can_id & 0x7FF) == COB_TPDO1(NODE) && f.can_dlc >= 6) {
                memcpy(&sw, &f.data[0], 2);
                memcpy(&pos, &f.data[2], 4);
                fb_fresh = 1;
            }
        if (fb_fresh) last_fb_ms = now_ms();        /* 只有新回授才餵狗! */

        if (!safe_stop && now_ms() - last_fb_ms > TIMEOUT_MS) {
            safe_stop = 1; trip_t = t * 10;
            printf("t=%4dms ** 失聯 %ums → 安全停止 ** target 鎖 %d\n",
                   trip_t, TIMEOUT_MS, pos);
        }

        uint16_t cw = safe_stop ? 0x0006 : 0x000F;  /* 安全停止: shutdown */
        if (!safe_stop) target += 20;               /* 正常時走斜坡 */
        else            target = pos;               /* 鎖在實際位置 */
        uint8_t r[6];
        memcpy(&r[0], &cw, 2); memcpy(&r[2], &target, 4);
        can_send(s, COB_RPDO1(NODE), r, 6);

        if (t % 100 == 0)
            printf("t=%4dms sw=0x%04x pos=%6d %s\n", t * 10, sw, pos,
                   safe_stop ? "[SAFE-STOP]" : "[RUN]");
        usleep(10000);
    }
    printf("%s (對照 firmware/safety/safety.c + test_safety.c)\n",
           safe_stop ? "看門狗有咬 ✅" : "從站沒斷線?預期會觸發 ❌");
    close(s);
    return !safe_stop;
}
