/**
 * @file    sdo_bg.c
 * @brief   SDO/CoE 背景通道實作
 *
 * CANopen：非阻塞 SDO client 狀態機（IDLE→WAIT）。step 送請求幀,
 * 回應由 pump（dual_arm_pump_rx → sdo_bg_on_frame）分派進來,
 * step 只負責計逾時。每 step 至多送一幀——對 §5.2 的 95% 匯流排
 * 負載預算,額外流量 = 每 timeout 窗最多 2 幀,可忽略。
 * EtherCAT：CoE 版 step 在 bus_ecat.c（避免本模組帶 ec_master 依賴）。
 */
#include "sdo_bg.h"
#include "spsc_ring.h"
#include "co_bxcan.h"
#include <string.h>

#define REQ_CAP 8
#define RSP_CAP 8

static uint8_t s_req_mem[REQ_CAP * sizeof(sdo_bg_req_t)];
static uint8_t s_rsp_mem[RSP_CAP * sizeof(sdo_bg_rsp_t)];
static spsc_t  s_req_q, s_rsp_q;

typedef enum { BG_IDLE, BG_WAIT } bg_state_t;
static bg_state_t   s_state;
static sdo_bg_req_t s_cur;
static uint32_t     s_wait_steps;
static uint32_t     s_timeout_steps = 50;

void sdo_bg_init(uint32_t timeout_steps)
{
    (void)spsc_init(&s_req_q, s_req_mem, sizeof(sdo_bg_req_t), REQ_CAP);
    (void)spsc_init(&s_rsp_q, s_rsp_mem, sizeof(sdo_bg_rsp_t), RSP_CAP);
    s_state = BG_IDLE;
    s_timeout_steps = timeout_steps ? timeout_steps : 50;
}

bool sdo_bg_request(const sdo_bg_req_t *r) { return spsc_push(&s_req_q, r); }
bool sdo_bg_poll(sdo_bg_rsp_t *out)        { return spsc_pop(&s_rsp_q, out); }

static void respond(int8_t status, uint8_t size, uint32_t value)
{
    sdo_bg_rsp_t rsp = { .tag = s_cur.tag, .status = status,
                         .size = size, .value = value };
    (void)spsc_push(&s_rsp_q, &rsp);      /* 滿=呼叫端沒 poll,丟棄 */
    s_state = BG_IDLE;
}

static uint8_t write_cs(uint8_t size)
{
    switch (size) { case 1: return 0x2F; case 2: return 0x2B;
                    case 3: return 0x27; case 4: return 0x23; default: return 0; }
}

void sdo_bg_step_canopen(void)
{
    if (s_state == BG_WAIT) {                       /* 只計逾時,回應走 on_frame */
        if (++s_wait_steps >= s_timeout_steps)
            respond(SDO_BG_TIMEOUT, 0, 0);
        return;
    }
    if (!spsc_pop(&s_req_q, &s_cur)) return;        /* 無待辦 */

    co_frame_t f = {0};
    f.id  = (uint16_t)(CO_COBID_SDO_RX_BASE + s_cur.node);
    f.dlc = 8;
    f.data[0] = s_cur.is_write ? write_cs(s_cur.size) : 0x40;
    if (f.data[0] == 0) { respond(SDO_BG_ABORT, 0, 0); return; }
    f.data[1] = (uint8_t)(s_cur.index & 0xFF);
    f.data[2] = (uint8_t)(s_cur.index >> 8);
    f.data[3] = s_cur.sub;
    if (s_cur.is_write) {
        f.data[4] = (uint8_t)(s_cur.value & 0xFF);
        f.data[5] = (uint8_t)((s_cur.value >> 8) & 0xFF);
        f.data[6] = (uint8_t)((s_cur.value >> 16) & 0xFF);
        f.data[7] = (uint8_t)((s_cur.value >> 24) & 0xFF);
    }
    if (co_bxcan_send((co_bus_t)s_cur.bus, &f) != CO_OK) {
        respond(SDO_BG_TIMEOUT, 0, 0);              /* mailbox 滿,下窗重試由呼叫端決定 */
        return;
    }
    s_wait_steps = 0;
    s_state = BG_WAIT;
}

bool sdo_bg_on_frame(co_bus_t bus, const co_frame_t *f)
{
    if (s_state != BG_WAIT) return false;
    if ((uint8_t)bus != s_cur.bus) return false;
    if (f->id != (uint16_t)(CO_COBID_SDO_TX_BASE + s_cur.node)) return false;
    if (f->dlc < 4) return false;
    /* index/sub 必須吻合（避免撿到殘留的舊回應） */
    uint16_t idx = (uint16_t)(f->data[1] | (f->data[2] << 8));
    if (idx != s_cur.index || f->data[3] != s_cur.sub) return false;

    uint8_t cs = f->data[0];
    uint32_t v = (uint32_t)f->data[4] | ((uint32_t)f->data[5] << 8)
               | ((uint32_t)f->data[6] << 16) | ((uint32_t)f->data[7] << 24);
    if (cs & 0x80)       respond(SDO_BG_ABORT, 0, v);           /* abort code */
    else if (cs == 0x60) respond(SDO_BG_OK, s_cur.size, 0);     /* 寫成功 */
    else {                                                      /* 讀回 */
        uint8_t size = (cs == 0x4F) ? 1 : (cs == 0x4B) ? 2 : 4;
        if (size == 1) v &= 0xFF;
        if (size == 2) v &= 0xFFFF;
        respond(SDO_BG_OK, size, v);
    }
    return true;
}

bool sdo_bg_take_req(sdo_bg_req_t *out) { return spsc_pop(&s_req_q, out); }

void sdo_bg_respond_ext(uint32_t tag, int8_t status, uint8_t size, uint32_t value)
{
    sdo_bg_rsp_t rsp = { .tag = tag, .status = status,
                         .size = size, .value = value };
    (void)spsc_push(&s_rsp_q, &rsp);
}
