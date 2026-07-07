/**
 * 專案A M1(WP2) · 單軸 bring-up 完整流程（對照 project-dual-arm.html M1）
 *
 * 對手：python3 ../../common/fake_slave.py --node 1
 * 流程 = 教學頁 checklist 原樣：
 *   heartbeat 確認 → SDO 讀 0x1000 → NMT start → 0x6060=CSP →
 *   PDO 使能交握 → CSP 小步階 ±1000 counts → 驗證跟隨
 * 真機版把 can_util 換回 firmware/canopen/co_bxcan 即可（同一刀介面）。
 */
#include "../../common/can_util.h"

#define NODE 1

static uint16_t enable_step(uint16_t sw)
{
    if (sw & 0x0008)            return 0x0080;
    if ((sw & 0x006F) == 0x0027) return 0x000F;
    if ((sw & 0x006F) == 0x0023) return 0x000F;
    if ((sw & 0x006F) == 0x0021) return 0x0007;
    return 0x0006;
}

static int sdo(int s, int wr, uint16_t idx, uint8_t sub, uint32_t in, uint32_t *out)
{
    uint8_t q[8] = { wr ? 0x23 : 0x40, idx & 0xFF, idx >> 8, sub };
    if (wr) memcpy(&q[4], &in, 4);
    can_send(s, COB_SDO_RX(NODE), q, 8);
    uint32_t t0 = now_ms(); struct can_frame f;
    while (now_ms() - t0 < 200)
        if (can_recv(s, &f) && (f.can_id & 0x7FF) == COB_SDO_TX(NODE)) {
            if (f.data[0] == 0x80) return -2;
            if (out) memcpy(out, &f.data[4], 4);
            return 0;
        }
    return -1;
}

int main(void)
{
    int s = can_open("vcan0", 5);
    if (s < 0) return 1;
    struct can_frame f;
    uint32_t v;

    /* 1) 等一個 heartbeat：節點活著才繼續 */
    printf("[1] 等 node %d heartbeat...\n", NODE);
    uint32_t t0 = now_ms(); int hb = 0;
    while (now_ms() - t0 < 1500 && !hb)
        if (can_recv(s, &f) && (f.can_id & 0x7FF) == COB_HB(NODE)) hb = 1;
    if (!hb) { fprintf(stderr, "沒心跳 — 假從站有跑嗎?\n"); return 1; }
    printf("    heartbeat ✓\n");

    /* 2) SDO 讀 Device Type、設 CSP 模式 */
    if (sdo(s, 0, 0x1000, 0, 0, &v)) return 1;
    printf("[2] 0x1000=0x%08x ✓\n", v);
    uint8_t nmt[2] = { 0x01, NODE };
    can_send(s, COB_NMT, nmt, 2);                       /* NMT start */
    sdo(s, 1, 0x6060, 0, 8, NULL);
    printf("[3] NMT start + 0x6060=8(CSP) ✓\n");

    /* 3) 100Hz 迴圈：使能交握 → 步階 → 驗證跟隨 */
    uint16_t sw = 0; int32_t pos = 0, target = 0;
    int enabled_at = -1;
    for (int t = 0; t < 400; t++) {                     /* 4 秒 */
        while (can_recv(s, &f))
            if ((f.can_id & 0x7FF) == COB_TPDO1(NODE) && f.can_dlc >= 6) {
                memcpy(&sw, &f.data[0], 2);
                memcpy(&pos, &f.data[2], 4);
            }
        int en = (sw & 0x006F) == 0x0027;
        if (en && enabled_at < 0) {
            enabled_at = t;
            printf("[4] OPERATION_ENABLED @%dms ✓ — 下步階 +1000\n", t * 10);
            target = 1000;
        }
        if (en && t == enabled_at + 150) { printf("[5] 步階 -1000\n"); target = -1000; }

        uint16_t cw = enable_step(sw);
        uint8_t r[6];
        memcpy(&r[0], &cw, 2); memcpy(&r[2], &target, 4);
        can_send(s, COB_RPDO1(NODE), r, 6);
        usleep(10000);
    }
    int err = target - pos;
    printf("[6] 結束: target=%d pos=%d err=%d %s\n", target, pos, err,
           (err > -50 && err < 50) ? "→ 跟隨 OK ✅" : "→ 沒跟上 ❌");
    close(s);
    return !(err > -50 && err < 50);
}
