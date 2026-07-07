/**
 * @file  sil_csp_test.c
 * @brief SIL-C 驗收：真 SOEM 主站 ↔ ecat_slave.py 假 PHU 從站（WP-SE5 前置）
 *
 * 流程 = 板端 ec_master_soem.c 未來要做的事（去掉 DC）：
 *   init → config_init（掃鏈/SII）→ config_map（CoE 讀 PDO 映射 + FMMU）
 *   → SAFEOP → OP → CoE SDO（0x6060=8 CSP、驗 0x6061、RO abort）
 *   → 1 kHz LRW：CiA402 使能三步（0x06→0x07→0x0F, sw 0x21→0x23→0x27）
 *   → CSP 目標 5000，500 週期跟隨驗證 + WKC 全程檢查
 *
 * 用法：sil_csp_test <iface> [axes]      （veth 上跑：sil_csp_test ecm0 2）
 * 回傳 0 = PASS。
 *
 * 建置（WSL）：
 *   gcc -O2 -o sil_csp_test sil_csp_test.c -I<SOEM>/include -L<build> -lsoem -lpthread -lrt
 */
#include "soem/soem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ecx_contextt ctx;
static uint8 iomap[4096];
static int checks = 0, fails = 0;

#define CHK(cond, ...) do { \
    checks++; \
    if (cond) { printf("    ok: " __VA_ARGS__); printf("\n"); } \
    else { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } \
} while (0)

/* 精簡 PDO（= 韌體 ec_out_t / ec_in_t） */
typedef struct __attribute__((packed)) { uint16 cw; int32 tgt; } out_t;
typedef struct __attribute__((packed)) { uint16 sw; int32 pos; } in_t;

static int roundtrip(void)
{
    ecx_send_processdata(&ctx);
    return ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
}

/* 送 n 個週期（1 kHz），回傳最後 WKC */
static int cycles(int n)
{
    int wkc = 0;
    for (int i = 0; i < n; i++) {
        wkc = roundtrip();
        osal_usleep(1000);
    }
    return wkc;
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s <iface> [axes]\n", argv[0]); return 2; }
    int axes = (argc > 2) ? atoi(argv[2]) : 1;

    printf("== SIL-C：SOEM 真主站 ↔ ecat_slave.py ==\n");
    if (!ecx_init(&ctx, argv[1])) { fprintf(stderr, "ecx_init(%s) 失敗（要 root）\n", argv[1]); return 2; }

    CHK(ecx_config_init(&ctx) > 0 && ctx.slavecount == axes,
        "掃鏈 %d 軸（期望 %d）", ctx.slavecount, axes);
    for (int s = 1; s <= ctx.slavecount; s++)
        CHK(ctx.slavelist[s].eep_man == 0x1097 && ctx.slavelist[s].eep_id == 0x00010002,
            "軸 %d 身分 vendor=0x%04X product=0x%08X", s,
            (unsigned)ctx.slavelist[s].eep_man, (unsigned)ctx.slavelist[s].eep_id);

    ecx_config_map_group(&ctx, iomap, 0);
    ec_groupt *grp = ctx.grouplist;
    CHK((int)grp->Obytes == 6 * axes && (int)grp->Ibytes == 6 * axes,
        "CoE PDO 映射 %dO+%dI bytes（精簡 6B/軸）", grp->Obytes, grp->Ibytes);

    ecx_statecheck(&ctx, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
    CHK(ctx.slavelist[0].state == EC_STATE_SAFE_OP, "全軸 SAFEOP");

    /* CoE SDO：裝置型號 / CSP 模式 / RO abort（對照 can_slave selftest） */
    uint32 v = 0; int sz = sizeof(v);
    ecx_SDOread(&ctx, 1, 0x1000, 0, FALSE, &sz, &v, EC_TIMEOUTRXM);
    CHK(v == 0x00020192, "SDO rd 0x1000 = 0x%08X", v);
    for (int s = 1; s <= ctx.slavecount; s++) {
        int8 mode = 8;
        CHK(ecx_SDOwrite(&ctx, s, 0x6060, 0, FALSE, 1, &mode, EC_TIMEOUTRXM) > 0,
            "軸 %d SDO wr 0x6060=8 (CSP)", s);
    }
    int8 m = 0; sz = 1;
    ecx_SDOread(&ctx, 1, 0x6061, 0, FALSE, &sz, &m, EC_TIMEOUTRXM);
    CHK(m == 8, "SDO rd 0x6061 = %d", m);
    /* 寫 RO 物件 → 從站回 SDO abort（SOEM 收進 error list，wkc 仍為 1） */
    ec_errort err;
    while (ecx_poperror(&ctx, &err)) {}                /* 清既有錯誤 */
    v = 123;
    ecx_SDOwrite(&ctx, 1, 0x6064, 0, FALSE, 4, &v, EC_TIMEOUTRXM);
    boolean aborted = ecx_poperror(&ctx, &err) && err.Etype == EC_ERR_TYPE_SDO_ERROR;
    CHK(aborted && err.AbortCode == 0x06010002,
        "寫 RO 0x6064 → SDO abort 0x%08X", aborted ? (unsigned)err.AbortCode : 0);

    /* SAFEOP → OP */
    roundtrip();
    ctx.slavelist[0].state = EC_STATE_OPERATIONAL;
    ecx_writestate(&ctx, 0);
    for (int i = 0; i < 10 && ctx.slavelist[0].state != EC_STATE_OPERATIONAL; i++) {
        roundtrip();
        ecx_statecheck(&ctx, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE / 10);
    }
    CHK(ctx.slavelist[0].state == EC_STATE_OPERATIONAL, "全軸 OP");

    out_t *out[EC_MAXSLAVE]; in_t *in[EC_MAXSLAVE];
    for (int s = 1; s <= ctx.slavecount; s++) {
        out[s] = (out_t *)ctx.slavelist[s].outputs;
        in[s]  = (in_t *)ctx.slavelist[s].inputs;
        out[s]->cw = 0; out[s]->tgt = 0;
    }
    int expected_wkc = grp->outputsWKC * 2 + grp->inputsWKC;

    /* CiA402 使能三步 */
    for (int s = 1; s <= ctx.slavecount; s++) out[s]->cw = 0x0006;
    int wkc = cycles(5);
    CHK(wkc == expected_wkc, "LRW WKC=%d（期望 %d）", wkc, expected_wkc);
    CHK((in[1]->sw & 0x6F) == 0x21, "cw=0x06 → sw=0x%04X (ready)", in[1]->sw);
    for (int s = 1; s <= ctx.slavecount; s++) out[s]->cw = 0x0007;
    cycles(5);
    CHK((in[1]->sw & 0x6F) == 0x23, "cw=0x07 → sw=0x%04X (switched-on)", in[1]->sw);
    for (int s = 1; s <= ctx.slavecount; s++) out[s]->cw = 0x000F;
    cycles(5);
    for (int s = 1; s <= ctx.slavecount; s++)
        CHK((in[s]->sw & 0x6F) == 0x27, "軸 %d cw=0x0F → sw=0x%04X (op-enabled)", s, in[s]->sw);

    /* CSP 點動：目標 5000，500 週期 @1kHz */
    for (int s = 1; s <= ctx.slavecount; s++) out[s]->tgt = 5000;
    int bad_wkc = 0;
    for (int i = 0; i < 500; i++) {
        if (roundtrip() != expected_wkc) bad_wkc++;
        osal_usleep(1000);
    }
    CHK(bad_wkc == 0, "500 週期 WKC 全對（漏 %d）", bad_wkc);
    for (int s = 1; s <= ctx.slavecount; s++)
        CHK(abs(in[s]->pos - 5000) < 200, "軸 %d CSP 跟隨 pos=%d（目標 5000）", s, in[s]->pos);

    ctx.slavelist[0].state = EC_STATE_INIT;
    ecx_writestate(&ctx, 0);
    ecx_close(&ctx);

    printf("\n總計 %d 檢查, %d 失敗 → %s\n", checks, fails, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
