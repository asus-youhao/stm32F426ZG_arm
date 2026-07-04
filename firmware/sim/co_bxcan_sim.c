/**
 * @file    co_bxcan_sim.c
 * @brief   虛擬 CAN bus（取代 co_bxcan.c 的硬體層）
 *
 * 每條 bus 掛 7 個模擬 PHU 從站（node 1..7）。主站 co_bxcan_send() 的 frame
 * 會被路由到對應從站,從站回應 frame 立即注入主站 RX 佇列,供 co_bxcan_recv() 取得。
 * 可開啟 frame 記錄以觀察資料流。
 */
#include "co_bxcan.h"
#include "phu_sim.h"
#include <stdio.h>
#include <string.h>

#define NODES_PER_BUS 7

static phu_node_t s_nodes[CO_BUS_COUNT][NODES_PER_BUS + 1]; /* index by node id 1..7 */

/* 主站 RX 佇列 */
#define RXQ 256
typedef struct { co_frame_t b[RXQ]; int head, tail; } rxq_t;
static rxq_t s_rx[CO_BUS_COUNT];

/* 統計與記錄 */
static int s_log_enable = 0;
unsigned long g_sim_tx_count[CO_BUS_COUNT];
unsigned long g_sim_rx_count[CO_BUS_COUNT];

void sim_bus_set_log(int en) { s_log_enable = en; }

/* frame tap：測試用逐幀記錄（等效 candump）。dir_tx=1 主站→從站。 */
static void (*s_tap)(co_bus_t bus, int dir_tx, const co_frame_t *f) = 0;
void sim_bus_set_tap(void (*fn)(co_bus_t, int, const co_frame_t *)) { s_tap = fn; }

static const char *frame_kind(uint16_t id)
{
    if (id == 0x000) return "NMT";
    if (id >= 0x600 && id <= 0x67F) return "SDO-req";
    if (id >= 0x580 && id <= 0x5FF) return "SDO-rsp";
    if (id >= 0x200 && id <= 0x27F) return "RPDO1";
    if (id >= 0x180 && id <= 0x1FF) return "TPDO1";
    if (id >= 0x700 && id <= 0x77F) return "HB";
    return "?";
}

static void log_frame(co_bus_t bus, const char *dir, const co_frame_t *f)
{
    (void)bus;
    if (!s_log_enable) return;
    printf("    [%s %-7s] id=0x%03X dlc=%d data=", dir, frame_kind(f->id), f->id, f->dlc);
    for (int i = 0; i < f->dlc; i++) printf("%02X ", f->data[i]);
    printf("\n");
}

static void rx_push(co_bus_t bus, const co_frame_t *f)
{
    rxq_t *q = &s_rx[bus];
    int n = (q->head + 1) % RXQ;
    if (n == q->tail) return;
    q->b[q->head] = *f; q->head = n;
}

/* 故障注入（測試用）：把任意 frame 塞進主站 RX 佇列（如模擬從站發 EMCY） */
void sim_bus_inject_rx(co_bus_t bus, const co_frame_t *f)
{
    if (bus >= CO_BUS_COUNT || !f) return;
    rx_push(bus, f);
    g_sim_rx_count[bus]++;
    if (s_tap) s_tap(bus, 0, f);
}

co_status_t co_bxcan_init(co_bus_t bus)
{
    if (bus >= CO_BUS_COUNT) return CO_ERR_PARAM;
    s_rx[bus].head = s_rx[bus].tail = 0;
    for (int id = 1; id <= NODES_PER_BUS; id++)
        phu_init(&s_nodes[bus][id], (uint8_t)id);
    return CO_OK;
}

co_status_t co_bxcan_send(co_bus_t bus, const co_frame_t *f)
{
    if (bus >= CO_BUS_COUNT || !f) return CO_ERR_PARAM;
    g_sim_tx_count[bus]++;
    log_frame(bus, "TX", f);
    if (s_tap) s_tap(bus, 1, f);

    co_frame_t out[2];
    int nr = 0;

    if (f->id == CO_COBID_NMT) {
        /* 廣播給全部節點 */
        for (int id = 1; id <= NODES_PER_BUS; id++)
            (void)phu_on_frame(&s_nodes[bus][id], f, out, 2);
    } else if (f->id == CO_COBID_SYNC) {
        /* SYNC 廣播：同步模式的從站於此鎖存並回 TPDO（WP-H4/G3） */
        for (int id = 1; id <= NODES_PER_BUS; id++) {
            nr = phu_on_frame(&s_nodes[bus][id], f, out, 2);
            for (int i = 0; i < nr; i++) {
                rx_push(bus, &out[i]);
                g_sim_rx_count[bus]++;
                log_frame(bus, "RX", &out[i]);
                if (s_tap) s_tap(bus, 0, &out[i]);
            }
        }
        nr = 0;
    } else {
        /* 取出目標 node id（低 7 位） */
        int id = f->id & 0x7F;
        if (id >= 1 && id <= NODES_PER_BUS) {
            nr = phu_on_frame(&s_nodes[bus][id], f, out, 2);
            for (int i = 0; i < nr; i++) {
                rx_push(bus, &out[i]);
                g_sim_rx_count[bus]++;
                log_frame(bus, "RX", &out[i]);
                if (s_tap) s_tap(bus, 0, &out[i]);
            }
        }
    }
    return CO_OK;
}

bool co_bxcan_recv(co_bus_t bus, co_frame_t *out)
{
    if (bus >= CO_BUS_COUNT || !out) return false;
    rxq_t *q = &s_rx[bus];
    if (q->tail == q->head) return false;
    *out = q->b[q->tail];
    q->tail = (q->tail + 1) % RXQ;
    return true;
}

void co_bxcan_on_rx(co_bus_t bus, const co_frame_t *f) { (void)bus; (void)f; }
