/**
 * @file test_ecat.c — WP-L3.1/L3.5 驗收：ec_master 門面 + fake 後端
 *
 * 模擬 WP-L1 的 bring-up 序列（免硬體）：
 *   掃鏈 → PREOP CoE 組態（0x6060=8）→ OP+DC → CiA402 使能 → CSP 跟隨
 * 關鍵驗證：
 *   - 同一份 cia402.c（enable_step/decode）不改一行跑在 EtherCAT 門面上
 *   - 一拍延遲：get_input 只在 exchange 後更新
 *   - WKC：掉軸 → 期望值缺 3、該軸輸入凍結、health 反映
 */
#include "test_framework.h"
#include "ec_master.h"
#include "ec_master_sim.h"
#include "cia402.h"

#define N 14

static ec_in_t  s_in[N];
static ec_out_t s_out[N];

/* 一個控制週期：讀回授 → enable_step/維持 → 寫輸出 → exchange */
static int cycle(int hold_target[N])
{
    for (int a = 0; a < N; a++) ec_axis_get_input(a, &s_in[a]);
    for (int a = 0; a < N; a++) {
        bool en = (cia402_decode(s_in[a].statusword) == DS_OPERATION_ENABLED);
        s_out[a].controlword = en ? CW_ENABLE_OP
                                  : cia402_enable_step(s_in[a].statusword);
        s_out[a].target_pos = hold_target ? hold_target[a] : s_in[a].pos_actual;
        ec_axis_set_output(a, &s_out[a]);
    }
    return ec_master_exchange();
}

void test_ecat(void)
{
    /* ---- 掃鏈 + PREOP 組態 ---- */
    CHECK(ec_master_init(N) == N);
    uint32_t v = 0;
    CHECK(ec_coe_read(0, 0x1000, 0, &v) == 0 && v == 0x00020192u); /* CiA402 */
    for (int a = 0; a < N; a++)
        CHECK(ec_coe_write(a, 0x6060, 0, 8) == 0);                 /* CSP */
    CHECK(ec_coe_read(3, 0x6061, 0, &v) == 0 && v == 8);           /* 讀回 */
    CHECK(ec_master_exchange() == -1);                             /* 未 OP 不可交換 */

    /* ---- OP + DC ---- */
    CHECK(ec_master_op() == 0);
    CHECK(ec_master_expected_wkc() == N * 3);

    /* ---- 使能：同一份 cia402.c 邏輯,數個週期內全軸 Operation enabled ---- */
    int all_en = 0;
    for (int t = 0; t < 20 && !all_en; t++) {
        CHECK(cycle(NULL) == N * 3);
        all_en = 1;
        for (int a = 0; a < N; a++)
            if (cia402_decode(s_in[a].statusword) != DS_OPERATION_ENABLED)
                all_en = 0;
    }
    CHECK(all_en);

    /* ---- 一拍延遲語意：set_output 本身不動輸入,exchange 才更新 ---- */
    ec_in_t before, after;
    ec_axis_get_input(0, &before);
    int tgt[N];
    for (int a = 0; a < N; a++) tgt[a] = s_in[a].pos_actual + 50000;
    for (int a = 0; a < N; a++) {
        s_out[a].target_pos = tgt[a];
        ec_axis_set_output(a, &s_out[a]);
    }
    ec_axis_get_input(0, &after);
    CHECK(before.pos_actual == after.pos_actual);   /* 尚未 exchange */

    /* ---- CSP 跟隨：每週期步進 ≤ max_step(5000),最終收斂到目標 ---- */
    int32_t prev = after.pos_actual;
    for (int t = 0; t < 30; t++) {
        CHECK(cycle(tgt) == N * 3);
        int32_t step = s_in[0].pos_actual - prev;   /* 注意:s_in 是上週期鎖存 */
        CHECK(step >= 0 && step <= 5000);
        prev = s_in[0].pos_actual;
    }
    for (int a = 0; a < N; a++) ec_axis_get_input(a, &s_in[a]);
    for (int a = 0; a < N; a++) CHECK(s_in[a].pos_actual == tgt[a]);

    /* ---- 掉軸：WKC 缺 3、輸入凍結、health 反映 ---- */
    phu_ecat_set_offline(5, true);
    int32_t frozen = s_in[5].pos_actual;
    for (int a = 0; a < N; a++) tgt[a] += 20000;
    for (int t = 0; t < 10; t++)
        CHECK(cycle(tgt) == (N - 1) * 3);           /* 期望 42 → 實得 39 */
    CHECK(s_in[5].pos_actual == frozen);            /* 該軸凍結 */
    CHECK(s_in[0].pos_actual != frozen ||
          s_in[0].pos_actual == tgt[0]);            /* 其他軸照走 */

    bus_health_t h;
    ec_master_health(&h);
    CHECK(h.rx_lost == 1);
    CHECK(h.err_events >= 10);
    CHECK(h.sync_ok == 1 && h.link_ok == 1);
    CHECK(h.proto[0] == (uint32_t)((N - 1) * 3));
    CHECK(h.proto[1] == (uint32_t)(N * 3));

    /* 軸回線 → WKC 復原 */
    phu_ecat_set_offline(5, false);
    CHECK(cycle(tgt) == N * 3);

    ec_master_close();
    CHECK(ec_master_exchange() == -1);
}
