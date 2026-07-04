/**
 * @file    phu_sim.c
 * @brief   模擬 EYOU PHU 關節（CANopen 從站）實作
 */
#include "phu_sim.h"
#include <string.h>

#define DEVICE_TYPE_402  0x00020192u   /* CiA402 servo device profile */

void phu_init(phu_node_t *n, uint8_t node_id)
{
    memset(n, 0, sizeof(*n));
    n->node_id    = node_id;
    n->nmt_state  = CO_NODE_BOOTUP;
    n->statusword = 0x0040;             /* Switch on disabled */
    n->mode       = 8;                  /* 預設 CSP */
    n->max_step   = 5000;               /* 每 tick 最大位移(counts)，模擬限速 */
}

/* 依 controlword 推進 CiA402 狀態機,更新 statusword。 */
static void apply_controlword(phu_node_t *n, uint16_t cw)
{
    n->controlword = cw;
    if (cw & 0x0080) {                  /* fault reset */
        n->statusword = 0x0040; n->enabled = false; return;
    }
    switch (cw & 0x000F) {
        case 0x0006: n->statusword = 0x0021; n->enabled=false; break; /* shutdown→ready */
        case 0x0007: n->statusword = 0x0023; n->enabled=false; break; /* switch on */
        case 0x000F: n->statusword = 0x0027; n->enabled=true;  break; /* enable op */
        case 0x0000: n->statusword = 0x0040; n->enabled=false; break; /* disable volt */
        default: break;
    }
}

/* SDO expedited 讀：固定以 0x43(4B) 回應。 */
static int sdo_read_resp(phu_node_t *n, uint16_t idx, uint8_t sub,
                         co_frame_t *out)
{
    uint32_t v = 0;
    switch (idx) {
        case 0x1000: v = DEVICE_TYPE_402; break;
        case 0x6041: v = n->statusword; break;
        case 0x6061: v = (uint32_t)(uint8_t)n->mode; break;
        case 0x6064: v = (uint32_t)n->actual_pos; break;
        case 0x607A: v = (uint32_t)n->target_pos; break;
        case 0x60FF: v = (uint32_t)n->target_vel; break;
        case 0x26A0: v = n->node_id; break;
        case 0x26A1: v = 1000000u; break;     /* 1 Mbps */
        default:     v = 0; break;
    }
    out->id = (uint16_t)(CO_COBID_SDO_TX_BASE + n->node_id);
    out->dlc = 8;
    out->data[0] = 0x43;
    out->data[1] = (uint8_t)(idx & 0xFF);
    out->data[2] = (uint8_t)(idx >> 8);
    out->data[3] = sub;
    out->data[4] = (uint8_t)(v & 0xFF);
    out->data[5] = (uint8_t)((v >> 8) & 0xFF);
    out->data[6] = (uint8_t)((v >> 16) & 0xFF);
    out->data[7] = (uint8_t)((v >> 24) & 0xFF);
    return 1;
}

/* SDO 寫:更新對應物件,回 0x60。 */
static int sdo_write_resp(phu_node_t *n, uint16_t idx, uint8_t sub,
                          uint32_t val, co_frame_t *out)
{
    switch (idx) {
        case 0x6040: apply_controlword(n, (uint16_t)val); break;
        case 0x6060: n->mode = (int8_t)val; break;
        case 0x607A: n->target_pos = (int32_t)val; break;
        case 0x60FF: n->target_vel = (int32_t)val; break;
        /* WP-H4/G3：RPDO1/TPDO1 transmission type（sub 0x02）=1 → SYNC 鎖存 */
        case 0x1400:
        case 0x1800:
            if (sub == 0x02) n->sync_mode = (val == 1);
            break;
        /* 0x1600/0x1A00 PDO 映射、0x6083/0x6084 等：接受即可 */
        default: break;
    }
    out->id = (uint16_t)(CO_COBID_SDO_TX_BASE + n->node_id);
    out->dlc = 8;
    out->data[0] = 0x60;
    out->data[1] = (uint8_t)(idx & 0xFF);
    out->data[2] = (uint8_t)(idx >> 8);
    out->data[3] = sub;
    out->data[4]=out->data[5]=out->data[6]=out->data[7]=0;
    return 1;
}

/* TPDO1 回授：[SW u16][actual pos i32] */
static int emit_tpdo1(const phu_node_t *n, co_frame_t *out)
{
    out->id = (uint16_t)(CO_COBID_TPDO1_BASE + n->node_id);
    out->dlc = 6;
    out->data[0] = (uint8_t)(n->statusword & 0xFF);
    out->data[1] = (uint8_t)(n->statusword >> 8);
    out->data[2] = (uint8_t)(n->actual_pos & 0xFF);
    out->data[3] = (uint8_t)((n->actual_pos >> 8) & 0xFF);
    out->data[4] = (uint8_t)((n->actual_pos >> 16) & 0xFF);
    out->data[5] = (uint8_t)((n->actual_pos >> 24) & 0xFF);
    return 1;
}

int phu_on_frame(phu_node_t *n, const co_frame_t *in,
                 co_frame_t *out, int max_out)
{
    if (max_out < 1) return 0;
    uint16_t id = in->id;

    /* SYNC（0x080）：同步模式從站在 SYNC 邊緣套用上次 RPDO、回 TPDO（G3） */
    if (id == CO_COBID_SYNC) {
        if (!n->sync_mode || n->nmt_state != CO_NODE_OPERATIONAL) return 0;
        if (n->rpdo_pending) {
            apply_controlword(n, n->pend_cw);
            n->target_pos = n->pend_tp;
            n->rpdo_pending = false;
        }
        phu_step_dynamics(n);
        return emit_tpdo1(n, out);
    }

    /* NMT（0x000）：data[0]=cmd, data[1]=node(0=broadcast) */
    if (id == CO_COBID_NMT && in->dlc >= 2) {
        if (in->data[1] == 0 || in->data[1] == n->node_id) {
            switch (in->data[0]) {
                case CO_NMT_START:      n->nmt_state = CO_NODE_OPERATIONAL; break;
                case CO_NMT_PRE_OP:     n->nmt_state = CO_NODE_PRE_OP; break;
                case CO_NMT_STOP:       n->nmt_state = CO_NODE_STOPPED; break;
                case CO_NMT_RESET_COMM:
                case CO_NMT_RESET_NODE: n->nmt_state = CO_NODE_PRE_OP;
                                        n->statusword = 0x0040; n->enabled=false; break;
            }
        }
        return 0; /* NMT 無回應 */
    }

    /* SDO 請求（0x600+node） */
    if (id == (uint16_t)(CO_COBID_SDO_RX_BASE + n->node_id) && in->dlc >= 4) {
        uint8_t  cs  = in->data[0];
        uint16_t idx = (uint16_t)(in->data[1] | (in->data[2] << 8));
        uint8_t  sub = in->data[3];
        if (cs == 0x40) {
            return sdo_read_resp(n, idx, sub, out);
        } else {
            uint32_t val = (uint32_t)in->data[4]
                         | ((uint32_t)in->data[5] << 8)
                         | ((uint32_t)in->data[6] << 16)
                         | ((uint32_t)in->data[7] << 24);
            return sdo_write_resp(n, idx, sub, val, out);
        }
    }

    /* RPDO1（0x200+node）：[CW u16][target pos i32]
       async（預設）：立即套用 + 回 TPDO1（等效 transmission type 255）
       sync 模式    ：只暫存,下個 SYNC 才鎖存（transmission type 1） */
    if (id == (uint16_t)(CO_COBID_RPDO1_BASE + n->node_id) && in->dlc >= 6) {
        uint16_t cw = (uint16_t)(in->data[0] | (in->data[1] << 8));
        int32_t tp = (int32_t)((uint32_t)in->data[2]
                   | ((uint32_t)in->data[3] << 8)
                   | ((uint32_t)in->data[4] << 16)
                   | ((uint32_t)in->data[5] << 24));
        if (n->sync_mode) {
            n->pend_cw = cw;
            n->pend_tp = tp;
            n->rpdo_pending = true;
            return 0;
        }
        apply_controlword(n, cw);
        n->target_pos = tp;
        phu_step_dynamics(n);
        return emit_tpdo1(n, out);
    }

    return 0;
}

void phu_step_dynamics(phu_node_t *n)
{
    if (!n->enabled) return;
    if (n->mode == 8 || n->mode == 1) {     /* CSP / PP：朝目標位置收斂 */
        int32_t d = n->target_pos - n->actual_pos;
        if (d >  n->max_step) d =  n->max_step;
        if (d < -n->max_step) d = -n->max_step;
        n->actual_pos += d;
    } else if (n->mode == 9 || n->mode == 3) { /* CSV / PV：以速度積分（dt=1ms） */
        n->actual_pos += n->target_vel / 1000;
    }
}
