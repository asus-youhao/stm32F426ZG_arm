/* PHU17 單軸 CSP 點動（WP-L1.4/1.5, SOEM 2.x）
 * 安全設計：SM-sync(不啟 DC)、±MAX_CNT 極小幅正弦、使能前 0x607A<-0x6064 防跳、
 *           每 cycle 監看 fault/errcode、逾時保護、結束歸位+解除使能。
 * PDO 偏移取自 slaveinfo -map（出廠映射）：
 *   OUT: CW@0(u16) mode@2(i8) tgtpos@3(i32) tgttq@0x13(i16)
 *   IN : SW@0(u16) modedisp@2(i8) errcode@3(u16) posact@5(i32) follerr@0x11(i32) dcv@0x15(u32)
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include "soem/soem.h"

static ecx_contextt ctx;
static char IOmap[256];

#define CYCLE_US   1000     /* 1 kHz 迴圈,配 SYNC0=1ms */
#define MAX_CNT    2900     /* ±約 2°輸出(最壞情況);524288 cnt/rev */
#define JOG_PERIOD 4.0      /* 正弦週期 s */
#define JOG_SECS   8.0      /* 點動總時長 s */

static uint8_t *OUT, *IN;
static void  w16(int off, uint16_t v){ memcpy(OUT+off,&v,2); }
static void  w8 (int off, int8_t v){ OUT[off]=(uint8_t)v; }
static void  w32(int off, int32_t v){ memcpy(OUT+off,&v,4); }
static uint16_t r16(int off){ uint16_t v; memcpy(&v,IN+off,2); return v; }
static uint16_t rerr(void){ uint16_t v; memcpy(&v,IN+3,2); return v; }
static int32_t  rpos(void){ int32_t v; memcpy(&v,IN+5,4); return v; }
static int32_t  rfoll(void){ int32_t v; memcpy(&v,IN+0x11,4); return v; }
static uint32_t rdcv(void){ uint32_t v; memcpy(&v,IN+0x15,4); return v; }

static void nsleep_us(long us){ struct timespec t={us/1000000,(us%1000000)*1000}; nanosleep(&t,NULL); }
static void exch(void){ ecx_send_processdata(&ctx); ecx_receive_processdata(&ctx,EC_TIMEOUTRET); }
static const char* st_name(uint16_t sw){
    uint16_t s=sw&0x6F;
    if((sw&0x4F)==0x40) return "SwitchOnDisabled";
    if(s==0x21) return "ReadyToSwitchOn";
    if(s==0x23) return "SwitchedOn";
    if(s==0x27) return "OperationEnabled";
    if(s==0x07) return "QuickStop";
    if((sw&0x4F)==0x0F||(sw&0x4F)==0x08) return "Fault";
    return "?";
}

int main(int argc,char**argv){
    if(argc<2){printf("usage:%s ifname\n",argv[0]);return 1;}
    if(!ecx_init(&ctx,argv[1])){printf("ecx_init fail\n");return 1;}
    if(ecx_config_init(&ctx)<=0){printf("no slaves\n");ecx_close(&ctx);return 1;}
    ecx_config_map_group(&ctx,IOmap,0);
    ecx_configdc(&ctx);                 /* 啟用 DC（伺服 CSP 多半必須） */
    printf("slaves=%d Obytes=%d Ibytes=%d hasDC=%d\n",ctx.slavecount,
           ctx.slavelist[1].Obytes,ctx.slavelist[1].Ibytes,ctx.slavelist[1].hasdc);
    if(ctx.slavelist[1].Obytes!=33||ctx.slavelist[1].Ibytes!=29){
        printf("!! PDO 大小非 33/29,偏移不符,中止\n");ecx_close(&ctx);return 1;}
    OUT=ctx.slavelist[1].outputs; IN=ctx.slavelist[1].inputs;

    /* SAFEOP -> OP（送 process data 餵 watchdog） */
    ecx_statecheck(&ctx,0,EC_STATE_SAFE_OP,EC_TIMEOUTSTATE*4);
    ecx_readstate(&ctx);
    printf("SAFEOP? state=0x%02X AL=0x%04X %s\n",ctx.slavelist[1].state,
           ctx.slavelist[1].ALstatuscode,ec_ALstatuscode2string(ctx.slavelist[1].ALstatuscode));
    w8(2,8); w16(0,0);                 /* mode=CSP, CW=0 */
    exch();
    ecx_dcsync0(&ctx,1,TRUE,1000000,250000); /* SYNC0 1ms, shift 250us */
    exch(); nsleep_us(CYCLE_US);
    ctx.slavelist[0].state=EC_STATE_OPERATIONAL;
    ecx_writestate(&ctx,0);
    int chk=200;
    do{ exch(); ecx_statecheck(&ctx,0,EC_STATE_OPERATIONAL,50000);}while(chk--&&ctx.slavelist[0].state!=EC_STATE_OPERATIONAL);
    if(ctx.slavelist[0].state!=EC_STATE_OPERATIONAL){
        ecx_readstate(&ctx);
        printf("!! 到不了 OP state=0x%02X AL=0x%04X %s\n",ctx.slavelist[1].state,
               ctx.slavelist[1].ALstatuscode,ec_ALstatuscode2string(ctx.slavelist[1].ALstatuscode));
        for(int i=0;i<30;i++){exch();nsleep_us(CYCLE_US);}
        ecx_dcsync0(&ctx,1,FALSE,0,0);
        ecx_close(&ctx);return 1;}
    printf("== OP 達成 ==\n");

    /* 防跳：target = 現在位置 */
    exch();
    int32_t start=rpos();
    w32(3,start); w8(2,8);
    printf("start pos=%d sw=0x%04X(%s) err=0x%04X dcv=%umV\n",
           start,r16(0),st_name(r16(0)),rerr(),rdcv());

    /* CiA402 使能 FSM：0x06 -> 0x07 -> 0x0F,監看 fault */
    const uint16_t seq[3]={0x06,0x07,0x0F};
    for(int s=0;s<3;s++){
        int to=500;                    /* 每步最多 1s */
        while(to--){
            w32(3,rpos()); w16(0,seq[s]); w8(2,8); exch();
            uint16_t sw=r16(0);
            if((sw&0x08)||((sw&0x4F)==0x0F&&(sw&0x08))){/*fault bit3*/}
            if(sw&0x0008){ printf("!! 使能中 FAULT sw=0x%04X err=0x%04X\n",sw,rerr()); goto disable; }
            if(s==0&&(sw&0x6F)==0x21) break;
            if(s==1&&(sw&0x6F)==0x23) break;
            if(s==2&&(sw&0x6F)==0x27) break;
            nsleep_us(CYCLE_US);
        }
        printf("  step CW=0x%02X -> sw=0x%04X(%s)\n",seq[s],r16(0),st_name(r16(0)));
    }
    if((r16(0)&0x6F)!=0x27){
        printf("!! 未達 OperationEnabled(sw=0x%04X)——極可能是 STO 未接的硬體閘。歸位解除。\n",r16(0));
        goto disable;
    }
    printf("== OperationEnabled!(0x253B=0 已旁路 STO) 等 1s 鬆閘 ==\n");
    for(int i=0;i<500;i++){ w32(3,rpos()); w16(0,0x0F); exch(); nsleep_us(CYCLE_US);} /* 1s hold */

    /* CSP 點動：正弦 ±MAX_CNT,sin(0)=0 無跳 */
    printf("== 點動 ±%d cnt,%us ==\n",MAX_CNT,(unsigned)JOG_SECS);
    double t=0; int n=(int)(JOG_SECS*1e6/CYCLE_US);
    for(int i=0;i<n;i++){
        int32_t tgt=start+(int32_t)(MAX_CNT*sin(2*M_PI*t/JOG_PERIOD));
        w32(3,tgt); w16(0,0x0F); w8(2,8); exch();
        uint16_t sw=r16(0);
        if(sw&0x0008){printf("!! 點動中 FAULT sw=0x%04X err=0x%04X\n",sw,rerr());break;}
        if(i%100==0) printf("  t=%.1fs tgt=%d pos=%d follerr=%d dcv=%umV sw=0x%04X\n",
                            t,tgt,rpos(),rfoll(),rdcv(),sw);
        t+=CYCLE_US/1e6; nsleep_us(CYCLE_US);
    }
    /* 歸位 */
    printf("== 歸位 ==\n");
    for(int i=0;i<250;i++){ w32(3,start); w16(0,0x0F); exch(); nsleep_us(CYCLE_US);}
    printf("final pos=%d (start=%d)\n",rpos(),start);

disable:
    for(int i=0;i<50;i++){ w16(0,0x06); exch(); nsleep_us(CYCLE_US);}  /* 解除使能 */
    ctx.slavelist[0].state=EC_STATE_SAFE_OP; ecx_writestate(&ctx,0);
    ecx_close(&ctx);
    printf("== 結束,已解除使能 ==\n");
    return 0;
}
