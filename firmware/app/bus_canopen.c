/**
 * @file    bus_canopen.c
 * @brief   bus_if_t 的 CANopen 後端（WP-H5）——包裝既有 dual_arm L1
 *
 * 純轉接層：呼叫順序與內容跟 app_agents 直呼 dual_arm_* 時完全相同,
 * H1 的逐幀 diff 驗收（test_agents）持續把關「包裝零行為差異」。
 * per-bus 健康儀表自 app_io_agents 移入（協定特有邏輯歸後端,H5 原則）。
 */
#include "bus_if.h"
#include "dual_arm.h"
#include "co_emcy.h"
#include "co_nmt.h"

static uint32_t s_last_tx[CO_BUS_COUNT], s_last_rx[CO_BUS_COUNT];

static int bc_init(void)
{
    for (int b = 0; b < CO_BUS_COUNT; b++) s_last_tx[b] = s_last_rx[b] = 0;
    return (dual_arm_init() == CO_OK) ? 0 : -1;
}

static bool bc_take_fault(int j, uint16_t *code)
{
    return co_emcy_take(g_joints[j].bus, g_joints[j].node_id, code);
}

/* busload 估算：視窗內成功收發幀數 × ~130 bits/幀（6B PDO 含 stuffing,
 * linux-canopen-master-plan.md §5.2）÷ 視窗 µs;1 Mbps → 1 bit/µs。 */
static void bc_health(int bus_idx, uint32_t window_us, bus_health_t *out)
{
    co_bus_t b = (co_bus_t)bus_idx;
    uint32_t tx, rx;
    dual_arm_frame_counts(b, &tx, &rx);
    uint32_t d = (tx - s_last_tx[bus_idx]) + (rx - s_last_rx[bus_idx]);
    s_last_tx[bus_idx] = tx;
    s_last_rx[bus_idx] = rx;

    /* 該 bus present 軸中 heartbeat 逾時（heartbeat 1Hz → 取 2s）數 */
    uint32_t stale = 0;
    for (int j = 0; j < ARM_COUNT * JOINTS_PER_ARM; j++)
        if (g_jstate[j].present && g_joints[j].bus == b &&
            co_nmt_node_age_ms(b, g_joints[j].node_id) > 2000u)
            stale++;

    out->tx_drop    = dual_arm_tx_drops();      /* 全域累計 */
    out->rx_lost    = stale;
    out->err_events = co_emcy_count(b);
    out->link_ok    = 1;   /* SocketCAN error frame 解析屬後續工作 */
    out->sync_ok    = dual_arm_sync_enabled() ? 1 : 0;
    out->load_pct   = (uint16_t)((uint64_t)d * 130u * 100u / window_us);
    for (int i = 0; i < 4; i++) out->proto[i] = 0;
}

const bus_if_t g_bus_canopen = {
    .name          = "canopen",
    .init          = bc_init,
    .present_count = dual_arm_present_count,
    .pump_rx       = dual_arm_pump_rx,
    .set_target    = dual_arm_set_target,
    .tick          = dual_arm_tick,
    .set_safe_stop = dual_arm_set_safe_stop,
    .tx_drops      = dual_arm_tx_drops,
    .take_fault    = bc_take_fault,
    .health        = bc_health,
};
