/**
 * @file    ec_master_sim.c
 * @brief   fake EtherCAT 後端（WP-L3.5）——ec_master.h 門面接 phu_sim
 *
 * 目的：免硬體 CI。從站 CiA402 行為**單一來源**：重用 phu_sim 的模型
 * （CANopen 假從站同一份）,以 CAN 幀編碼在行程內往返——
 *   process data 交換 → RPDO/TPDO 幀 + SYNC 鎖存（= DC-Synchron 類比）
 *   CoE SDO          → CANopen SDO 幀（CoE 本就是 CANopen over EtherCAT）
 *   ESM              → NMT 狀態類比（PREOP/OP）
 *
 * 一拍延遲模型（linux-rt-ethercat-master-plan.md §5.2）：
 *   exchange() = ①送出上次 set_output 的 RxPDO（從站暫存,等 SYNC）
 *                ②SYNC 廣播 → 從站鎖存輸出、跑動力學、回 TxPDO → 存入輸入區
 *   之後 get_input 讀到的是「本次 SYNC 鎖存」的回授。
 * WKC 模型：每顆健在從站貢獻 3（LRW 讀 1 + 寫 2）;offline 軸不貢獻、
 *   輸入凍結（等同拔線,WKC 缺 3 → 呼叫端判失軸）。
 */
#include "ec_master.h"
#include "ec_master_sim.h"
#include "canopen.h"
#include "eng_port.h"
#include <string.h>

typedef enum { ST_IDLE, ST_PREOP, ST_OP } sim_state_t;

static phu_node_t s_node[EC_AXES_MAX];
static bool       s_offline[EC_AXES_MAX];
static ec_out_t   s_out[EC_AXES_MAX];
static ec_in_t    s_in[EC_AXES_MAX];
static uint8_t    s_fresh[EC_AXES_MAX];
static int        s_n;
static sim_state_t s_state = ST_IDLE;
static int        s_last_wkc;
static uint32_t   s_wkc_err_events;
/* DC 漂移模型：從站柵格週期 = dt×(1+ppm/1e6),主站喚醒 vs 柵格的誤差 */
static double     s_dc_grid;      /* 下一個柵格時刻（µs,0=未鎖定） */
static double     s_dc_dt;        /* 柵格週期（µs;0=漂移模型停用） */
static int32_t    s_dc_err_us;

/* ---- 幀工具（行程內直達 phu_on_frame,無佇列）---- */
static int node_frame(int axis, const co_frame_t *in, co_frame_t *out)
{
    return phu_on_frame(&s_node[axis], in, out, 1);
}

static void nmt_to(int axis, uint8_t cmd)
{
    co_frame_t f = { .id = CO_COBID_NMT, .dlc = 2,
                     .data = { cmd, s_node[axis].node_id } };
    co_frame_t resp;
    (void)node_frame(axis, &f, &resp);
}

/* ---- 門面實作 ---- */

int ec_master_init(int expected_axes)
{
    if (expected_axes < 1 || expected_axes > EC_AXES_MAX) return -1;
    s_n = expected_axes;                       /* 掃鏈：sim 一定全數到齊 */
    memset(s_offline, 0, sizeof(s_offline));
    memset(s_out, 0, sizeof(s_out));
    memset(s_in, 0, sizeof(s_in));
    memset(s_fresh, 0, sizeof(s_fresh));
    s_last_wkc = 0;
    s_wkc_err_events = 0;
    s_dc_grid = 0.0;
    s_dc_dt = 0.0;
    s_dc_err_us = 0;
    for (int a = 0; a < s_n; a++) {
        phu_init(&s_node[a], (uint8_t)(a + 1));  /* 位址=鏈序（IN→OUT） */
        nmt_to(a, CO_NMT_RESET_COMM);
        nmt_to(a, CO_NMT_PRE_OP);
    }
    s_state = ST_PREOP;
    return s_n;
}

int ec_master_op(void)
{
    if (s_state != ST_PREOP) return -1;
    for (int a = 0; a < s_n; a++) {
        /* DC/SYNC0 啟用：類比 = transmission type 1（SYNC 鎖存,G3 同一機制） */
        if (ec_coe_write(a, 0x1400, 0x02, 1)) return -1;
        nmt_to(a, CO_NMT_START);               /* SAFEOP→OP 類比 */
    }
    s_state = ST_OP;
    return 0;
}

int ec_master_expected_wkc(void) { return s_n * 3; }

int ec_master_exchange(void)
{
    if (s_state != ST_OP) return -1;
    co_frame_t f, resp;
    int wkc = 0;

    /* ① RxPDO：送出上次 set_output（sync 模式從站只暫存） */
    for (int a = 0; a < s_n; a++) {
        if (s_offline[a]) continue;
        f.id = (uint16_t)(CO_COBID_RPDO1_BASE + s_node[a].node_id);
        f.dlc = 6;
        f.data[0] = (uint8_t)(s_out[a].controlword & 0xFF);
        f.data[1] = (uint8_t)(s_out[a].controlword >> 8);
        f.data[2] = (uint8_t)(s_out[a].target_pos & 0xFF);
        f.data[3] = (uint8_t)((s_out[a].target_pos >> 8) & 0xFF);
        f.data[4] = (uint8_t)((s_out[a].target_pos >> 16) & 0xFF);
        f.data[5] = (uint8_t)((s_out[a].target_pos >> 24) & 0xFF);
        (void)node_frame(a, &f, &resp);
        wkc += 2;                               /* LRW 寫成功 */
    }

    /* ② SYNC 廣播：鎖存 + 動力學 + TxPDO → 輸入區 */
    f.id = CO_COBID_SYNC;
    f.dlc = 0;
    memset(s_fresh, 0, sizeof(s_fresh));
    for (int a = 0; a < s_n; a++) {
        if (s_offline[a]) continue;             /* 掉軸：輸入凍結 */
        if (node_frame(a, &f, &resp) == 1 && resp.dlc >= 6) {
            s_fresh[a] = 1;
            s_in[a].statusword = (uint16_t)(resp.data[0] | (resp.data[1] << 8));
            s_in[a].pos_actual = (int32_t)((uint32_t)resp.data[2]
                               | ((uint32_t)resp.data[3] << 8)
                               | ((uint32_t)resp.data[4] << 16)
                               | ((uint32_t)resp.data[5] << 24));
            wkc += 1;                           /* LRW 讀成功 */
        }
    }

    s_last_wkc = wkc;
    if (wkc != ec_master_expected_wkc()) s_wkc_err_events++;

    if (s_dc_dt > 0.0) {                        /* DC 漂移模型 */
        double t = (double)port_now_us();
        if (s_dc_grid == 0.0) s_dc_grid = t;    /* 首次 exchange 對齊柵格 */
        s_dc_err_us = (int32_t)(t - s_dc_grid);
        s_dc_grid += s_dc_dt;
    }
    return wkc;
}

void ec_axis_set_output(int axis, const ec_out_t *o)
{
    if (axis >= 0 && axis < s_n) s_out[axis] = *o;
}

void ec_axis_get_input(int axis, ec_in_t *i)
{
    if (axis >= 0 && axis < s_n) *i = s_in[axis];
}

int ec_axis_fresh(int axis)
{
    return (axis >= 0 && axis < s_n) ? s_fresh[axis] : 0;
}

int32_t ec_master_dc_error_us(void) { return s_dc_err_us; }

void phu_ecat_set_dc_drift(uint32_t dt_us, int32_t drift_ppm)
{
    s_dc_dt = (dt_us == 0) ? 0.0
            : (double)dt_us * (1.0 + (double)drift_ppm * 1e-6);
    s_dc_grid = 0.0;
    s_dc_err_us = 0;
}

int ec_coe_read(int axis, uint16_t idx, uint8_t sub, uint32_t *val)
{
    if (axis < 0 || axis >= s_n) return -1;
    co_frame_t f = { .id = (uint16_t)(CO_COBID_SDO_RX_BASE + s_node[axis].node_id),
                     .dlc = 8, .data = { 0x40, (uint8_t)(idx & 0xFF),
                                         (uint8_t)(idx >> 8), sub } };
    co_frame_t resp;
    if (node_frame(axis, &f, &resp) != 1 || (resp.data[0] & 0x80)) return -1;
    if (val)
        *val = (uint32_t)resp.data[4] | ((uint32_t)resp.data[5] << 8)
             | ((uint32_t)resp.data[6] << 16) | ((uint32_t)resp.data[7] << 24);
    return 0;
}

int ec_coe_write(int axis, uint16_t idx, uint8_t sub, uint32_t val)
{
    if (axis < 0 || axis >= s_n) return -1;
    co_frame_t f = { .id = (uint16_t)(CO_COBID_SDO_RX_BASE + s_node[axis].node_id),
                     .dlc = 8,
                     .data = { 0x23, (uint8_t)(idx & 0xFF), (uint8_t)(idx >> 8), sub,
                               (uint8_t)(val & 0xFF), (uint8_t)((val >> 8) & 0xFF),
                               (uint8_t)((val >> 16) & 0xFF),
                               (uint8_t)((val >> 24) & 0xFF) } };
    co_frame_t resp;
    if (node_frame(axis, &f, &resp) != 1 || resp.data[0] != 0x60) return -1;
    return 0;
}

void ec_master_health(bus_health_t *h)
{
    uint32_t offline = 0, al = 0;
    for (int a = 0; a < s_n; a++) {
        if (s_offline[a]) offline++;
        else if (s_node[a].nmt_state == CO_NODE_OPERATIONAL) al |= 0x08; /* OP */
    }
    h->tx_drop    = 0;
    h->rx_lost    = offline;
    h->err_events = s_wkc_err_events;
    h->link_ok    = 1;
    h->sync_ok    = (s_state == ST_OP) ? 1 : 0;
    /* 頻寬：process image (12B out + 15B in)×n / 100 Mbps → 1 kHz 下 ~0%,
       這裡回報幀時間占週期比的粗估（整數 %,sim 恆小） */
    h->load_pct   = 0;
    h->proto[0]   = (uint32_t)s_last_wkc;
    h->proto[1]   = (uint32_t)ec_master_expected_wkc();
    h->proto[2]   = al;
    h->proto[3]   = 0;
}

void ec_master_close(void) { s_state = ST_IDLE; s_n = 0; }

/* ---- 測試鉤子 ---- */
void phu_ecat_set_offline(int axis, bool offline)
{
    if (axis >= 0 && axis < s_n) s_offline[axis] = offline;
}

phu_node_t *phu_ecat_node(int axis)
{
    return (axis >= 0 && axis < s_n) ? &s_node[axis] : 0;
}
