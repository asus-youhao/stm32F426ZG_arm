/* 讀 ESC 看門狗暫存器 + SAFEOP 彈跳計時（SOEM FPRD 原始暫存器讀取） */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "soem/soem.h"
static ecx_contextt ctx; static char IOmap[256];
static uint16_t frd16(uint16_t reg){ uint16_t v=0; ecx_FPRD(&ctx.port,ctx.slavelist[1].configadr,reg,2,&v,EC_TIMEOUTRET); return v; }
int main(int argc,char**argv){
    if(!ecx_init(&ctx,argv[1])){printf("init fail\n");return 1;}
    if(ecx_config_init(&ctx)<=0){printf("no slaves\n");return 1;}
    printf("WD divider 0x0400 = %u (預設2498≈100us/unit)\n",frd16(0x0400));
    printf("WD SM      0x0420 = %u → SM看門狗 ≈ %.1f ms\n",frd16(0x0420),
           frd16(0x0400)? (frd16(0x0420)*( (frd16(0x0400)+2)*0.04 )/1000.0):0);
    printf("WD PDI     0x0410 = %u\n",frd16(0x0410));
    ecx_config_map_group(&ctx,IOmap,0);
    /* map 內部會請 SAFEOP;立刻高速輪詢 AL 狀態抓彈跳時刻 */
    struct timespec t0,t; clock_gettime(CLOCK_MONOTONIC,&t0);
    uint16_t last=0xFFFF;
    for(int i=0;i<4000;i++){
        uint16_t st=frd16(0x0130), al=frd16(0x0134);
        if(st!=last){ clock_gettime(CLOCK_MONOTONIC,&t);
            double ms=(t.tv_sec-t0.tv_sec)*1e3+(t.tv_nsec-t0.tv_nsec)/1e6;
            printf("  t=%8.2fms AL state=0x%04X code=0x%04X\n",ms,st,al); last=st; }
    }
    ecx_close(&ctx); return 0;
}
