/* PHU17 單軸 CSP 點動 — IgH ecrt 最小週期應用（P0 驗證 + WP-I1）
 * SM-sync（0x1C32:01=1,不配 DC）;1ms 迴圈;使能前 0x607A<-0x6064 防跳;
 * ±2900 counts(≈輸出端 2°) 正弦 8s;fault 即停;結束歸位+解除使能。 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include "ecrt.h"

#define VID 0x00001097
#define PID 0x00010002
#define CYCLE_NS  10000000L
#define MAX_CNT   2900
#define JOG_PERIOD 4.0
#define JOG_SECS   8.0

static ec_master_t *master; static ec_domain_t *domain;
static uint8_t *pd;
static unsigned off_cw, off_mode, off_tgt, off_sw, off_err, off_pos, off_foll, off_dcv;
static volatile sig_atomic_t s_stop = 0;
static void on_int(int s){(void)s;s_stop=1;}

static ec_pdo_entry_info_t rx_e[] = {
    {0x6040,0,16},{0x6060,0,8},{0x607A,0,32},{0x6081,0,32},{0x60FF,0,32},
    {0x240D,0,32},{0x6071,0,16},{0x6083,0,32},{0x6084,0,32},{0x6087,0,32}};
static ec_pdo_entry_info_t tx_e[] = {
    {0x6041,0,16},{0x6061,0,8},{0x603F,0,16},{0x6064,0,32},{0x606C,0,32},
    {0x6077,0,16},{0x6074,0,16},{0x60F4,0,32},{0x6079,0,32},{0x60FD,0,32}};
static ec_pdo_info_t rx_p[] = {{0x1600,10,rx_e}};
static ec_pdo_info_t tx_p[] = {{0x1A00,10,tx_e}};
static ec_sync_info_t syncs[] = {
    {2,EC_DIR_OUTPUT,1,rx_p,EC_WD_ENABLE},
    {3,EC_DIR_INPUT ,1,tx_p,EC_WD_DISABLE},
    {0xff,0,0,NULL,EC_WD_DEFAULT}};

static const char* st_name(uint16_t sw){
    if((sw&0x4F)==0x40) return "SwitchOnDisabled";
    uint16_t s=sw&0x6F;
    if(s==0x21) return "ReadyToSwitchOn";
    if(s==0x23) return "SwitchedOn";
    if(s==0x27) return "OperationEnabled";
    if(s==0x07) return "QuickStop";
    if(sw&0x08) return "Fault";
    return "?";
}

int main(int argc,char**argv){
    int use_dc = (argc>1 && !strcmp(argv[1],"dc"));
    signal(SIGINT,on_int);
    master = ecrt_request_master(0);
    if(!master){printf("request_master fail\n");return 1;}
    domain = ecrt_master_create_domain(master);
    ec_slave_config_t *sc = ecrt_master_slave_config(master,0,0,VID,PID);
    if(!sc){printf("slave cfg fail\n");return 1;}
    /* 不呼叫 ecrt_slave_config_pdos:此 drive 不接受重映射,用預設(等同 --no-remap-pdo) */
    ec_pdo_entry_reg_t regs[] = {
        {0,0,VID,PID,0x6040,0,&off_cw,NULL},
        {0,0,VID,PID,0x6060,0,&off_mode,NULL},
        {0,0,VID,PID,0x607A,0,&off_tgt,NULL},
        {0,0,VID,PID,0x6041,0,&off_sw,NULL},
        {0,0,VID,PID,0x603F,0,&off_err,NULL},
        {0,0,VID,PID,0x6064,0,&off_pos,NULL},
        {0,0,VID,PID,0x60F4,0,&off_foll,NULL},
        {0,0,VID,PID,0x6079,0,&off_dcv,NULL},
        {}};
    if(ecrt_domain_reg_pdo_entry_list(domain,regs)){printf("reg fail\n");return 1;}
    ecrt_slave_config_sdo8 (sc,0x60C2,1,10);         /* 插補週期 10ms=cycle */
    if(use_dc){
        ecrt_slave_config_dc(sc,0x0300,4000000,1000000,0,0);
        printf("DC-Synchron 已配置(4ms)\n");
    }
    if(ecrt_master_activate(master)){printf("activate fail\n");return 1;}
    pd = ecrt_domain_data(domain);
    printf("activated; off cw=%u tgt=%u sw=%u pos=%u\n",off_cw,off_tgt,off_sw,off_pos);

    struct timespec wake; clock_gettime(CLOCK_MONOTONIC,&wake);
    enum {WAIT_OP, ENABLE, HOLD, JOG, HOME, DISABLE, DONE} ph = WAIT_OP;
    int tick=0, ph_tick=0, en_step=0, printed=0;
    int32_t start=0; double t=0;
    uint16_t cw=0x00;

    while(!s_stop && ph!=DONE && tick < 40000){          /* 上限 45 s */
        wake.tv_nsec += CYCLE_NS;
        while(wake.tv_nsec>=1000000000L){wake.tv_nsec-=1000000000L;wake.tv_sec++;}
        clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&wake,NULL);

        struct timespec now; clock_gettime(CLOCK_REALTIME,&now);
        ecrt_master_application_time(master,
            (uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec);
        if(use_dc){ ecrt_master_sync_reference_clock(master);
                    ecrt_master_sync_slave_clocks(master); }
        ecrt_master_receive(master); ecrt_domain_process(domain);
        uint16_t sw  = EC_READ_U16(pd+off_sw);
        uint16_t ec_ = EC_READ_U16(pd+off_err);
        int32_t  pos = EC_READ_S32(pd+off_pos);
        int32_t  tgt = pos;                              /* 預設跟隨(防跳) */

        ec_domain_state_t ds; ecrt_domain_state(domain,&ds);
        ec_master_state_t ms; ecrt_master_state(master,&ms);

        if(sw&0x0008 && ph<HOME){                        /* fault 即停 */
            printf("!! FAULT sw=0x%04X err=0x%04X @tick=%d ph=%d\n",sw,ec_,tick,ph);
            ph=DISABLE; ph_tick=0;
        }
        switch(ph){
        case WAIT_OP:                                    /* 等 domain WC 正常+AL OP */
            cw=0x00;
            if(ds.wc_state==EC_WC_COMPLETE && ms.al_states&0x08){
                start=pos;
                printf("== OP! wc=%d al=0x%X start=%d sw=0x%04X(%s) dcv=%umV ==\n",
                       ds.working_counter,ms.al_states,start,sw,st_name(sw),EC_READ_U32(pd+off_dcv));
                ph=ENABLE; ph_tick=0; en_step=0;
            }
            break;
        case ENABLE: {
            const uint16_t seq[3]={0x06,0x07,0x0F};
            const uint16_t want[3]={0x21,0x23,0x27};
            cw=seq[en_step];
            if((sw&0x6F)==want[en_step]){
                printf("  CW=0x%02X -> %s\n",cw,st_name(sw));
                if(++en_step==3){ printf("== OperationEnabled! 等1s鬆閘 ==\n"); ph=HOLD; ph_tick=0; }
                else ph_tick=0;
            }
            if(++ph_tick>750){                          /* 每步 3s 逾時 */
                printf("!! 使能逾時 step=%d sw=0x%04X(%s) err=0x%04X——疑 STO 未接\n",
                       en_step,sw,st_name(sw),ec_);
                ph=DISABLE; ph_tick=0;
            }
            break; }
        case HOLD:
            cw=0x0F;
            if(++ph_tick>=250){ ph=JOG; ph_tick=0; t=0;
                printf("== 點動 ±%d cnt %.0fs ==\n",(int)MAX_CNT,JOG_SECS); }
            break;
        case JOG:
            cw=0x0F;
            tgt = start + (int32_t)(MAX_CNT*sin(2*M_PI*t/JOG_PERIOD));
            t += CYCLE_NS/1e9;
            if((ph_tick++%250)==0)
                printf("  t=%.1fs tgt=%d pos=%d foll=%d dcv=%umV sw=0x%04X\n",
                       t,tgt,pos,EC_READ_S32(pd+off_foll),EC_READ_U32(pd+off_dcv),sw);
            if(t>=JOG_SECS){ ph=HOME; ph_tick=0; printf("== 歸位 ==\n"); }
            break;
        case HOME:
            cw=0x0F; tgt=start;
            if(++ph_tick>=125){ printf("final pos=%d (start=%d)\n",pos,start); ph=DISABLE; ph_tick=0; }
            break;
        case DISABLE:
            cw=0x06; tgt=pos;
            if(++ph_tick>=50) ph=DONE;
            break;
        default: break;
        }
        EC_WRITE_U16(pd+off_cw,cw);
        EC_WRITE_S8 (pd+off_mode,8);
        EC_WRITE_S32(pd+off_tgt,tgt);
        ecrt_domain_queue(domain); ecrt_master_send(master);
        tick++;
        if(tick%2500==0 && ph==WAIT_OP && !printed){
            printf("  waiting OP... t=%ds al=0x%X wc_state=%d\n",tick*4/1000,ms.al_states,ds.wc_state);
            if(tick>=22500){printed=1;printf("!! 90s 未達 OP,放棄\n");break;}
        }
    }
    ecrt_release_master(master);
    printf("== 結束(tick=%d) ==\n",tick);
    return 0;
}
