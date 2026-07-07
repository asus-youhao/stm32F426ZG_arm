/**
 * L2-7 · CAN 訊框收發（對照 learn/l2-mid.html 第 7 節）
 *
 * 在 vcan0 上送一個 frame、再收印 3 秒內看到的所有 frame。
 * 先跑 ../../common/vcan_setup.sh；另開終端 candump vcan0 對照。
 */
#include "../../common/can_util.h"

int main(void)
{
    int s = can_open("vcan0", 200);
    if (s < 0) return 1;

    uint8_t hello[3] = { 0xAA, 0xBB, 0xCC };
    can_send(s, 0x123, hello, sizeof hello);
    printf("sent id=0x123 dlc=3\n");

    uint32_t t0 = now_ms();
    struct can_frame f;
    while (now_ms() - t0 < 3000) {
        if (!can_recv(s, &f)) continue;
        printf("recv id=0x%03x dlc=%u data:", f.can_id & 0x7FF, f.can_dlc);
        for (int i = 0; i < f.can_dlc; i++) printf(" %02x", f.data[i]);
        printf("\n");
    }
    close(s);
    return 0;
}
