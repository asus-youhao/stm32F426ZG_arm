/**
 * L2-8 · NMT 啟動 + Heartbeat 監看（對照 learn/l2-mid.html 第 8 節）
 *
 * 對手：python3 ../../common/fake_slave.py --node 1（可多開幾個 node）
 * 行為：廣播 NMT start-all，然後監看 0x700+n 心跳、計算每節點年齡。
 * 對照 repo：firmware/canopen/co_nmt.h 的 co_nmt_send / co_nmt_node_age_ms。
 */
#include "../../common/can_util.h"

#define MAX_NODE 8

int main(void)
{
    int s = can_open("vcan0", 100);
    if (s < 0) return 1;

    uint8_t nmt_start[2] = { 0x01, 0x00 };      /* cmd=start, node=0(all) */
    can_send(s, COB_NMT, nmt_start, 2);
    printf("NMT start-all sent\n");

    uint32_t last_ms[MAX_NODE] = { 0 };
    uint32_t t0 = now_ms(), t_report = t0;
    struct can_frame f;

    while (now_ms() - t0 < 3000) {
        if (can_recv(s, &f)) {
            uint32_t id = f.can_id & 0x7FF;
            if (id >= COB_HB(1) && id < COB_HB(MAX_NODE))   /* heartbeat */
                last_ms[id - COB_HB(0)] = now_ms();
        }
        if (now_ms() - t_report >= 1000) {                  /* 每秒報告年齡 */
            t_report = now_ms();
            for (int n = 1; n < MAX_NODE; n++) {
                if (!last_ms[n]) continue;
                uint32_t age = now_ms() - last_ms[n];
                printf("node %d: heartbeat age %u ms %s\n", n, age,
                       age > 300 ? "** LOST **" : "ok");
            }
        }
    }
    close(s);
    return 0;
}
