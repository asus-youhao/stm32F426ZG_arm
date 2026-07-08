/**
 * @file    ec_master_igh.c
 * @brief   EtherCAT 主站門面的 IgH（ecrt）實作——方案 B（WP-I3）
 *
 * 由 tools/ecat_bringup/igh_spin.c（2026-07-08 於 gx701 實測讓 PHU17 轉動）
 * 重構而成,配方常數全取自 ec_config.h。**與 sim 後端二選一連結**,不動
 * bus_ecat.c / 呼叫端。只在有 IgH（ecrt.h + libethercat）的機器編譯：
 *   gcc ... -I$IGH/include -L$IGH/lib/.libs -lethercat
 *
 * 血淚配方（見 ec_config.h 註解）：不重映射 PDO（用出廠預設佈局的位元組
 * 偏移直接讀寫）、DC assign_activate=0x300 且每 cycle sync 時鐘、mode 走
 * process data（不能 SDO 寫）、控制權須 0x2100=1。
 */
#include "ec_master.h"
#include "ec_config.h"
#include <string.h>
#include <time.h>
#include "ecrt.h"

static ec_master_t     *s_master;
static ec_domain_t     *s_domain;
static ec_slave_config_t *s_sc[EC_AXES_MAX];
static uint8_t         *s_pd;
static unsigned         s_off_out[EC_AXES_MAX];   /* 各軸 RxPDO 於 domain 的起始 offset */
static unsigned         s_off_in[EC_AXES_MAX];    /* 各軸 TxPDO 起始 offset */
static int              s_naxes;
static int              s_last_wkc;

/* 出廠預設映射的 entry 表（順序/大小＝slaveinfo -map；不重寫,只宣告給 IgH 對位） */
static ec_pdo_entry_info_t s_rx_e[] = {
    {0x6040,0,16},{0x6060,0,8},{0x607A,0,32},{0x6081,0,32},{0x60FF,0,32},
    {0x240D,0,32},{0x6071,0,16},{0x6083,0,32},{0x6084,0,32},{0x6087,0,32}};
static ec_pdo_entry_info_t s_tx_e[] = {
    {0x6041,0,16},{0x6061,0,8},{0x603F,0,16},{0x6064,0,32},{0x606C,0,32},
    {0x6077,0,16},{0x6074,0,16},{0x60F4,0,32},{0x6079,0,32},{0x60FD,0,32}};
static ec_pdo_info_t s_rx_p[] = {{EC_RXPDO_INDEX,10,s_rx_e}};
static ec_pdo_info_t s_tx_p[] = {{EC_TXPDO_INDEX,10,s_tx_e}};
/* 只宣告 SM2/SM3（不含 mailbox SM）；watchdog 交給 drive 預設 */
static ec_sync_info_t s_syncs[] = {
    {2,EC_DIR_OUTPUT,1,s_rx_p,EC_WD_ENABLE},
    {3,EC_DIR_INPUT ,1,s_tx_p,EC_WD_DISABLE},
    {0xff,0,0,NULL,EC_WD_DEFAULT}};

int ec_master_init(int expected_axes)
{
    s_naxes = expected_axes > EC_AXES_MAX ? EC_AXES_MAX : expected_axes;
    s_master = ecrt_request_master(0);
    if (!s_master) return -1;
    s_domain = ecrt_master_create_domain(s_master);
    if (!s_domain) return -1;

    for (int a = 0; a < s_naxes; a++) {
        s_sc[a] = ecrt_master_slave_config(s_master, 0, a, EC_VENDOR_ID, EC_PRODUCT_CODE);
        if (!s_sc[a]) return -1;
        /* 關鍵：不呼叫 ecrt_slave_config_pdos（不重映射）——此 drive 會拒。
           只宣告 sync 讓 IgH 知道預設佈局以便 domain 對位。 */
        if (EC_REMAP_PDO && ecrt_slave_config_pdos(s_sc[a], EC_END, s_syncs)) return -1;
        /* DC：CSP 動作的必要條件（見 ec_config.h §3） */
        ecrt_slave_config_dc(s_sc[a], EC_DC_ASSIGN_ACTIVATE,
                             EC_DC_SYNC0_NS, EC_DC_SYNC0_SHIFT_NS, 0, 0);
    }
    /* 註冊各軸 CW / target(out) 與 SW / pos(in) 的 domain 位置 */
    for (int a = 0; a < s_naxes; a++) {
        ec_pdo_entry_reg_t regs[] = {
            {0,(uint16_t)a,EC_VENDOR_ID,EC_PRODUCT_CODE,0x6040,0,&s_off_out[a],NULL},
            {0,(uint16_t)a,EC_VENDOR_ID,EC_PRODUCT_CODE,0x6041,0,&s_off_in[a],NULL},
            {}};
        if (ecrt_domain_reg_pdo_entry_list(s_domain, regs)) return -1;
    }
    return s_naxes;
}

int ec_master_op(void)
{
    if (ecrt_master_activate(s_master)) return -1;
    s_pd = ecrt_domain_data(s_domain);
    if (!s_pd) return -1;

    struct timespec wake;
    clock_gettime(CLOCK_MONOTONIC, &wake);
    for (int i = 0; i < 3000; i++) {              /* 最多等 ~30 s 到 OP */
        wake.tv_nsec += EC_DC_SYNC0_NS;
        while (wake.tv_nsec >= 1000000000L) { wake.tv_nsec -= 1000000000L; wake.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, NULL);
        (void)ec_master_exchange();
        ec_master_state_t ms; ecrt_master_state(s_master, &ms);
        ec_domain_state_t ds; ecrt_domain_state(s_domain, &ds);
        if ((ms.al_states & 0x08) && ds.wc_state == EC_WC_COMPLETE) return 0;
    }
    return -1;
}

int ec_master_exchange(void)
{
    struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
    ecrt_master_receive(s_master);
    ecrt_domain_process(s_domain);
    ec_domain_state_t ds; ecrt_domain_state(s_domain, &ds);
    s_last_wkc = ds.working_counter;

    ecrt_domain_queue(s_domain);
    ecrt_master_application_time(s_master,
        (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec);
    ecrt_master_sync_reference_clock(s_master);   /* DC：主站是跟隨者,對齊 SYNC0 柵格 */
    ecrt_master_sync_slave_clocks(s_master);
    ecrt_master_send(s_master);
    return s_last_wkc;
}

int ec_master_expected_wkc(void) { return 3 * s_naxes; }

void ec_axis_set_output(int axis, const ec_out_t *o)
{
    if (axis < 0 || axis >= s_naxes || !s_pd) return;
    uint8_t *rx = s_pd + s_off_out[axis];         /* off_out 對到 0x6040(CW),其後接預設佈局 */
    EC_WRITE_U16(rx + EC_RX_OFF_CW,     o->controlword);
    EC_WRITE_S8 (rx + EC_RX_OFF_MODE,   EC_MODE_CSP);   /* mode 走 PDO（不能 SDO 寫） */
    EC_WRITE_S32(rx + EC_RX_OFF_TARGET, o->target_pos);
}

void ec_axis_get_input(int axis, ec_in_t *i)
{
    if (axis < 0 || axis >= s_naxes || !s_pd) { i->statusword = 0; i->pos_actual = 0; return; }
    uint8_t *tx = s_pd + s_off_in[axis];
    i->statusword = EC_READ_U16(tx + EC_TX_OFF_SW);
    i->pos_actual = EC_READ_S32(tx + EC_TX_OFF_POS);
}

int ec_axis_fresh(int axis) { (void)axis; return s_last_wkc >= ec_master_expected_wkc(); }

int ec_coe_read(int axis, uint16_t idx, uint8_t sub, uint32_t *val)
{
    size_t rsz; uint32_t ab; int sz = 4;
    return ecrt_master_sdo_upload(s_master, (uint16_t)axis, idx, sub,
                                  (uint8_t *)val, sz, &rsz, &ab) ? -1 : 0;
}
int ec_coe_write(int axis, uint16_t idx, uint8_t sub, uint32_t val)
{
    uint32_t ab;
    return ecrt_master_sdo_download(s_master, (uint16_t)axis, idx, sub,
                                    (uint8_t *)&val, 4, &ab) ? -1 : 0;
}

void ec_master_health(bus_health_t *h)
{
    memset(h, 0, sizeof(*h));
    h->proto[0] = (uint16_t)s_last_wkc;
    h->proto[1] = (uint16_t)ec_master_expected_wkc();
    h->sync_ok  = (s_last_wkc >= ec_master_expected_wkc());
}

void ec_master_close(void)
{
    if (s_master) ecrt_release_master(s_master);
    s_master = NULL; s_pd = NULL; s_naxes = 0;
}
