/* v4：PREOP 先讀指派物件 + 先啟 SYNC0 再請求 SAFEOP->OP。 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "soem/soem.h"
static ecx_contextt ctx; static char IOmap[256];
static void nap(long us){struct timespec t={us/1000000,(us%1000000)*1000};nanosleep(&t,NULL);}
static void exch(void){ecx_send_processdata(&ctx);ecx_receive_processdata(&ctx,EC_TIMEOUTRET);}
static void rd(uint16_t i,uint8_t s,const char*nm){unsigned v=0;int sz=4;int w=ecx_SDOread(&ctx,1,i,s,FALSE,&sz,&v,EC_TIMEOUTRXM);printf("  %-14s 0x%04X:%02X = %u/0x%X (wkc=%d,%dB)\n",nm,i,s,v,v,w,sz);}
static void ss(const char*t){ecx_readstate(&ctx);printf("[%s] state=0x%02X AL=0x%04X %s\n",t,ctx.slavelist[1].state,ctx.slavelist[1].ALstatuscode,ec_ALstatuscode2string(ctx.slavelist[1].ALstatuscode));}

int main(int argc,char**argv){
    if(!ecx_init(&ctx,argv[1])){printf("init fail\n");return 1;}
    if(ecx_config_init(&ctx)<=0){printf("no slaves\n");return 1;}
    ecx_config_map_group(&ctx,IOmap,0);
    printf("O=%d I=%d hasDC=%d\n",ctx.slavelist[1].Obytes,ctx.slavelist[1].Ibytes,ctx.slavelist[1].hasdc);
    ss("after map(PREOP)");
    printf("== PDO 指派 / sync 模式 ==\n");
    rd(0x1C12,0,"RxAssign cnt"); rd(0x1C12,1,"RxAssign[1]");
    rd(0x1C13,0,"TxAssign cnt"); rd(0x1C13,1,"TxAssign[1]");
    rd(0x1C32,1,"SM2 synctype"); rd(0x1C33,1,"SM3 synctype");

    /* 先在 PREOP 啟 DC + SYNC0,再請 SAFEOP */
    ecx_configdc(&ctx);
    ecx_dcsync0(&ctx,1,TRUE,1000000,250000);
    printf("SYNC0 1ms 已啟(PREOP)\n");
    for(int i=0;i<100;i++){exch();nap(1000);}      /* 送 100ms process data 讓 DC 穩 */

    ctx.slavelist[0].state=EC_STATE_SAFE_OP; ecx_writestate(&ctx,0);
    for(int i=0;i<200;i++){exch();ecx_statecheck(&ctx,0,EC_STATE_SAFE_OP,20000);if((ctx.slavelist[1].state&0x0F)==EC_STATE_SAFE_OP)break;nap(1000);}
    ss("req SAFEOP");
    if((ctx.slavelist[1].state&0x0F)==EC_STATE_SAFE_OP){
        ctx.slavelist[0].state=EC_STATE_OPERATIONAL; ecx_writestate(&ctx,0);
        for(int i=0;i<300;i++){exch();ecx_statecheck(&ctx,0,EC_STATE_OPERATIONAL,20000);if((ctx.slavelist[1].state&0x0F)==EC_STATE_OPERATIONAL)break;nap(1000);}
        ss("req OP");
    }
    ecx_dcsync0(&ctx,1,FALSE,0,0);
    ctx.slavelist[0].state=EC_STATE_INIT; ecx_writestate(&ctx,0);
    ecx_close(&ctx);
    return 0;
}
