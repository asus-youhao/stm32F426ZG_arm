/* 變因拆解:argv = [pdos|nopdos] [dc|nodc] [cycle_us]
 * nopdos = 不呼叫 ecrt_slave_config_pdos(不重寫 0x1C12/0x1600),用從站現有映射
 * 迴圈順序照 IgH 範例:receive→process→(...)→queue→apptime→sync→send */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "ecrt.h"
#define VID 0x00001097
#define PID 0x00010002
static ec_pdo_entry_info_t rx_e[]={{0x6040,0,16},{0x6060,0,8},{0x607A,0,32},{0x6081,0,32},{0x60FF,0,32},{0x240D,0,32},{0x6071,0,16},{0x6083,0,32},{0x6084,0,32},{0x6087,0,32}};
static ec_pdo_entry_info_t tx_e[]={{0x6041,0,16},{0x6061,0,8},{0x603F,0,16},{0x6064,0,32},{0x606C,0,32},{0x6077,0,16},{0x6074,0,16},{0x60F4,0,32},{0x6079,0,32},{0x60FD,0,32}};
static ec_pdo_info_t rx_p[]={{0x1600,10,rx_e}}, tx_p[]={{0x1A00,10,tx_e}};
static ec_sync_info_t syncs[]={{2,EC_DIR_OUTPUT,1,rx_p,EC_WD_ENABLE},{3,EC_DIR_INPUT,1,tx_p,EC_WD_DISABLE},{0xff,0,0,NULL,EC_WD_DEFAULT}};

int main(int argc,char**argv){
    int use_pdos=(argc>1&&!strcmp(argv[1],"pdos"));
    int use_dc  =(argc>2&&!strcmp(argv[2],"dc"));
    long cyc_us =(argc>3)?atol(argv[3]):4000;
    printf("cfg: pdos=%d dc=%d cycle=%ldus\n",use_pdos,use_dc,cyc_us);
    ec_master_t*m=ecrt_request_master(0); if(!m){printf("no master\n");return 1;}
    ec_domain_t*d=ecrt_master_create_domain(m);
    ec_slave_config_t*sc=ecrt_master_slave_config(m,0,0,VID,PID);
    if(use_pdos && ecrt_slave_config_pdos(sc,EC_END,syncs)){printf("pdos cfg fail\n");return 1;}
    unsigned off_sw,off_cw;
    ec_pdo_entry_reg_t regs[]={{0,0,VID,PID,0x6041,0,&off_sw,NULL},{0,0,VID,PID,0x6040,0,&off_cw,NULL},{}};
    if(ecrt_domain_reg_pdo_entry_list(d,regs)){printf("reg fail\n");return 1;}
    int aa = (argc>4)?strtol(argv[4],NULL,16):0x300;
    if(argc>5 && !strcmp(argv[5],"ct")){
        ecrt_slave_config_sdo32(sc,0x1C32,2,1000000);   /* SM2 cycle time 1ms(ns) */
        ecrt_slave_config_sdo32(sc,0x1C33,2,1000000);
        printf("config SDO: 0x1C32/33:02 = 1ms\n");
    }
    if(argc>5 && !strcmp(argv[5],"ct4")){
        ecrt_slave_config_sdo32(sc,0x1C32,2,4000000);   /* 4ms:放寬同步窗 */
        ecrt_slave_config_sdo32(sc,0x1C33,2,4000000);
        ecrt_slave_config_sdo8(sc,0x60C2,1,4);          /* 插補週期 4ms */
        printf("config SDO: cycle=4ms(0x1C32/33:02+0x60C2)\n");
    }
    if(argc>5 && !strcmp(argv[5],"freerun")){
        ecrt_slave_config_sdo16(sc,0x1C32,1,0);         /* synctype=free-run */
        ecrt_slave_config_sdo16(sc,0x1C33,1,0);
        printf("config SDO: 0x1C32/33:01 = 0(free-run)\n");
    }
    if(use_dc){ ecrt_slave_config_dc(sc,(uint16_t)aa,1000000,250000,0,0);
                printf("DC AssignActivate=0x%X\n",aa); }
    if(ecrt_master_activate(m)){printf("activate fail\n");return 1;}
    uint8_t*pd=ecrt_domain_data(d);
    struct timespec wake; clock_gettime(CLOCK_MONOTONIC,&wake);
    int reached=0;
    for(int i=0;i<15000000/cyc_us && !reached;i++){
        wake.tv_nsec+=cyc_us*1000L;
        while(wake.tv_nsec>=1000000000L){wake.tv_nsec-=1000000000L;wake.tv_sec++;}
        clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&wake,NULL);
        ecrt_master_receive(m); ecrt_domain_process(d);
        ec_master_state_t ms; ecrt_master_state(m,&ms);
        ec_domain_state_t ds; ecrt_domain_state(d,&ds);
        if((i%(1000000/cyc_us))==0)
            printf("  t=%ds al=0x%X wc=%d/%d link=%d sw=0x%04X\n",
                   (int)(i*cyc_us/1000000),ms.al_states,ds.working_counter,3,
                   ms.link_up,*(uint16_t*)(pd+off_sw));
        if(ms.al_states&0x08 && ds.wc_state==EC_WC_COMPLETE){
            printf("== OP+WC 完成! t=%.1fs ==\n",i*cyc_us/1e6); reached=1;
        }
        *(uint16_t*)(pd+off_cw)=0;
        ecrt_domain_queue(d);
        struct timespec now; clock_gettime(CLOCK_REALTIME,&now);
        ecrt_master_application_time(m,(uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec);
        if(use_dc){ecrt_master_sync_reference_clock(m);ecrt_master_sync_slave_clocks(m);}
        ecrt_master_send(m);
    }
    ecrt_release_master(m);
    printf("== 結束 reached_op=%d ==\n",reached);
    return reached?0:1;
}
