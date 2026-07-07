/* 唯讀 SDO 探測（SOEM 2.x context API）：讀 PHU17 關鍵物件,不驅動、不改參數。 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "soem/soem.h"

static ecx_contextt ctx;
static char IOmap[256];

static int64_t rd(uint16_t idx, uint8_t sub, int bytes) {
    int64_t v = 0; int sz = bytes;
    int wkc = ecx_SDOread(&ctx, 1, idx, sub, FALSE, &sz, &v, EC_TIMEOUTRXM);
    if (wkc <= 0) { printf("  0x%04X:%02X  <fail wkc=%d>\n", idx, sub, wkc); return 0; }
    printf("  0x%04X:%02X = %" PRId64 " (0x%" PRIX64 ", %dB)\n", idx, sub, v, v, sz);
    return v;
}
static void rds(uint16_t idx, uint8_t sub) {
    char buf[64]; memset(buf,0,sizeof(buf)); int sz = sizeof(buf)-1;
    int wkc = ecx_SDOread(&ctx, 1, idx, sub, FALSE, &sz, buf, EC_TIMEOUTRXM);
    if (wkc <= 0) { printf("  0x%04X:%02X  <str fail>\n", idx, sub); return; }
    printf("  0x%04X:%02X = \"%s\"\n", idx, sub, buf);
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: %s ifname\n", argv[0]); return 1; }
    if (!ecx_init(&ctx, argv[1])) { printf("ecx_init fail\n"); return 1; }
    if (ecx_config_init(&ctx) <= 0) { printf("no slaves\n"); ecx_close(&ctx); return 1; }
    ecx_config_map_group(&ctx, IOmap, 0);
    printf("slaves=%d state=%d\n", ctx.slavecount, ctx.slavelist[1].state);
    printf("== identity ==\n");
    rds(0x1008,0); rds(0x1009,0); rds(0x100A,0);
    rd(0x1018,1,4); rd(0x1018,2,4); rd(0x1018,3,4); rd(0x1018,4,4);
    printf("== CiA402 狀態/模式 ==\n");
    rd(0x6041,0,2); rd(0x6061,0,1); rd(0x6060,0,1); rd(0x6502,0,4);
    printf("== 電氣/回授 ==\n");
    rd(0x6079,0,4); rd(0x6064,0,4); rd(0x606C,0,4); rd(0x603F,0,2); rd(0x60F4,0,4);
    printf("== 單位換算(關鍵) ==\n");
    rd(0x2025,0,4); rd(0x6091,1,4); rd(0x6091,2,4); rd(0x6092,1,4); rd(0x6092,2,4);
    rd(0x26A2,0,4); rd(0x26A3,0,4); rd(0x6076,0,4); rd(0x6080,0,4);
    printf("== STO/煞車相關 ==\n");
    rd(0x253B,0,2); rd(0x253C,0,2);
    printf("== 週期參數 ==\n");
    rd(0x60C2,1,1); rd(0x60C2,2,1);
    ecx_close(&ctx);
    return 0;
}
