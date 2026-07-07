/**
 * L2-9 · SDO expedited client（對照 learn/l2-mid.html 第 9 節）
 *
 * 對手：python3 ../../common/fake_slave.py --node 1
 * 流程：讀 0x1000 Device Type → 寫 0x6060=8(CSP) → 讀回驗證。
 * 對照 repo：firmware/canopen/co_sdo.h 的 co_sdo_read / co_sdo_write。
 */
#include "../../common/can_util.h"

/* 最小 expedited SDO：回傳 0=OK、-1=逾時、-2=abort */
static int sdo_xfer(int s, uint8_t node, int write, uint16_t idx, uint8_t sub,
                    uint32_t in, uint32_t *out, uint32_t timeout)
{
    uint8_t req[8] = { write ? 0x23 : 0x40,        /* 0x23=寫4byte, 0x40=讀 */
                       idx & 0xFF, idx >> 8, sub };
    if (write) memcpy(&req[4], &in, 4);
    can_send(s, COB_SDO_RX(node), req, 8);

    uint32_t t0 = now_ms();
    struct can_frame f;
    while (now_ms() - t0 < timeout) {
        if (!can_recv(s, &f)) continue;
        if ((f.can_id & 0x7FF) != COB_SDO_TX(node)) continue;
        if ((f.data[1] | f.data[2] << 8) != idx || f.data[3] != sub) continue;
        if (f.data[0] == 0x80) return -2;          /* abort */
        if (out) memcpy(out, &f.data[4], 4);
        return 0;
    }
    return -1;
}

int main(void)
{
    int s = can_open("vcan0", 20);
    if (s < 0) return 1;
    uint32_t v;

    if (sdo_xfer(s, 1, 0, 0x1000, 0, 0, &v, 200) == 0)
        printf("0x1000 Device Type = 0x%08x (0x192=402 profile)\n", v);

    sdo_xfer(s, 1, 1, 0x6060, 0, 8 /*CSP*/, NULL, 200);
    printf("wrote 0x6060 = 8 (CSP)\n");

    if (sdo_xfer(s, 1, 0, 0x6060, 0, 0, &v, 200) == 0)
        printf("read-back 0x6060 = %u %s\n", v, v == 8 ? "✓" : "✗");

    if (sdo_xfer(s, 1, 0, 0x9999, 0, 0, &v, 200) == -2)
        printf("0x9999 不存在 → SDO abort (正確行為)\n");

    close(s);
    return 0;
}
