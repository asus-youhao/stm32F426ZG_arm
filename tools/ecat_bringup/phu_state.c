/* 診斷：逐級 PREOP->SAFEOP->OP,每級讀 ALstatus;先讀 SM sync 模式。 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "soem/soem.h"
static ecx_contextt ctx; static char IOmap[256];
static void nap(long us){struct timespec t={us/1000000,(us%1000000)*1000};nanosleep(&t,NULL);}
static void exch(void){ecx_send_processdata(&ctx);ecx_receive_processdata(&ctx,EC_TIMEOUTRET);}
static void rd(uint16_t i,uint8_t s,const char*nm){int v=0,sz=4;int w=ecx_SDOread(&ctx,1,i,s,FALSE,&sz,&v,EC_TIMEOUTRXM);printf("  %s 0x%04X:%02X = %d (wkc=%d)\n",nm,i,s,v,w);}
static void showstate(const char*tag){ecx_readstate(&ctx);printf("[%s] state=0x%02X AL=0x%04X %s\n",tag,ctx.slavelist[1].state,ctx.slavelist[1].ALstatuscode,ec_ALstatuscode2string(ctx.slavelist[1].ALstatuscode));}

int main(int argc,char**argv){
    if(argc<2){printf("usage:%s ifname [dc]\n",argv[0]);return 1;}
    int use_dc = (argc>2 && !strcmp(argv[2],"dc"));
    if(!ecx_init(&ctx,argv[1])){printf("init fail\n");return 1;}
    if(ecx_config_init(&ctx)<=0){printf("no slaves\n");return 1;}
    if(argc>3 && !strcmp(argv[3],"lsa")){
        ctx.grouplist[0].logstartaddr = 0x10000;   /* 避開 logical addr 0(TwinCAT 慣例) */
        printf("logstartaddr=0x10000\n");
    }
    printf("slaves=%d state after init=0x%02X\n",ctx.slavecount,ctx.slavelist[1].state);
    printf("== SM sync 模式(0x1C32/0x1C33)==\n");
    rd(0x1C32,1,"SM2 synctype"); rd(0x1C32,2,"SM2 cycletime");
    rd(0x1C33,1,"SM3 synctype"); rd(0x1C33,2,"SM3 cycletime");
    ecx_config_map_group(&ctx,IOmap,0);
    printf("O=%d I=%d hasDC=%d\n",ctx.slavelist[1].Obytes,ctx.slavelist[1].Ibytes,ctx.slavelist[1].hasdc);
    if(use_dc){ ecx_configdc(&ctx); printf("configdc done\n"); }
    showstate("after map");

    /* 明確請求 SAFEOP */
    exch();
    ctx.slavelist[0].state=EC_STATE_SAFE_OP; ecx_writestate(&ctx,0);
    for(int i=0;i<50;i++){exch();nap(2000);}
    ecx_statecheck(&ctx,0,EC_STATE_SAFE_OP,EC_TIMEOUTSTATE*4);
    showstate("req SAFEOP");
    if((ctx.slavelist[1].state&0x0F)!=EC_STATE_SAFE_OP){ printf("!! 停在此,無法 SAFEOP\n"); ecx_close(&ctx); return 1; }

    if(use_dc){ ecx_dcsync0(&ctx,1,TRUE,1000000,250000); printf("SYNC0 1ms on\n"); exch(); nap(2000); }

    /* 請求 OP,持續送 process data */
    ctx.slavelist[0].state=EC_STATE_OPERATIONAL; ecx_writestate(&ctx,0);
    for(int i=0;i<200;i++){exch();ecx_statecheck(&ctx,0,EC_STATE_OPERATIONAL,20000);if((ctx.slavelist[1].state&0x0F)==EC_STATE_OPERATIONAL)break;nap(1000);}
    showstate("req OP");
    if(use_dc) ecx_dcsync0(&ctx,1,FALSE,0,0);
    ctx.slavelist[0].state=EC_STATE_INIT; ecx_writestate(&ctx,0);
    ecx_close(&ctx);
    return 0;
}
