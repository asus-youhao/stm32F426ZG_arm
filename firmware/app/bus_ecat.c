/**
 * @file    bus_ecat.c
 * @brief   bus_if_t 的 EtherCAT 後端（WP-H5）——走 ec_master.h 門面
 *
 * 底下的 ec_master 後端由連結期決定：ec_master_sim.c（免硬體 SIL）/
 * ec_master_soem.c（方案 A）/ ec_master_igh.c（方案 B）——本檔不變。
 *
 * 一拍延遲對映（linux-rt-ethercat-master-plan.md §5.2）：
 *   pump_rx（BUS_RX 相位）= ec_master_exchange()：送出上週期輸出 +
 *   收本次 DC/SYNC 鎖存的回授 → g_jstate
 *   tick（BUS_TX 相位）   = 使能步進/安全覆寫 → ec_axis_set_output
 *   （只寫暫存,下個 pump_rx 的 exchange 才上線）
 * 使能/安全語意與 dual_arm_tick() 逐句對齊（同一份 cia402.c）。
 */
#include "bus_if.h"
#include "dual_arm.h"      /* g_jstate / 關節數常數（資料面共用） */
#include "ec_master.h"
#include "cia402.h"
#include <string.h>

#define NJ (ARM_COUNT * JOINTS_PER_ARM)

static bool     s_safe_stop;
static uint16_t s_safe_cw = 0x0002;
static uint8_t  s_fault_latch[NJ];   /* CiA402 fault 邊緣偵測 */

static int be_init(void)
{
    memset(g_jstate, 0, sizeof(g_jstate));
    memset(s_fault_latch, 0, sizeof(s_fault_latch));
    s_safe_stop = false;
    s_safe_cw = 0x0002;

    int n = ec_master_init(NJ);
    if (n < 1) return -1;

    /* PREOP 組態：CSP 模式（PDO 佈局由後端內建/啟動 SDO 處理） */
    for (int a = 0; a < n; a++)
        if (ec_coe_write(a, 0x6060, 0, 8)) return -1;
    if (ec_master_op()) return -1;

    /* 首次交換：目標初值 = 實際位置（避免使能瞬間跳動,同 dual_arm_init 步驟5） */
    (void)ec_master_exchange();
    for (int a = 0; a < n; a++) {
        ec_in_t in;
        ec_axis_get_input(a, &in);
        g_jstate[a].present    = true;
        g_jstate[a].statusword = in.statusword;
        g_jstate[a].pos_actual = in.pos_actual;
        g_jstate[a].target_pos = in.pos_actual;
        g_jstate[a].controlword = CW_SHUTDOWN;
    }
    return 0;
}

static int be_present(void)
{
    int n = 0;
    for (int j = 0; j < NJ; j++)
        if (g_jstate[j].present) n++;
    return n;
}

static void be_pump_rx(void)
{
    (void)ec_master_exchange();
    for (int a = 0; a < NJ; a++) {
        if (!g_jstate[a].present) continue;
        ec_in_t in;
        ec_axis_get_input(a, &in);
        g_jstate[a].fb_fresh = (ec_axis_fresh(a) != 0);
        if (g_jstate[a].fb_fresh) {
            g_jstate[a].statusword = in.statusword;
            g_jstate[a].pos_actual = in.pos_actual;
            g_jstate[a].enabled =
                (cia402_decode(in.statusword) == DS_OPERATION_ENABLED);
        }
    }
}

static void be_set_target(uint8_t j, int32_t counts)
{
    if (j < NJ) g_jstate[j].target_pos = counts;
}

static void be_tick(void)
{
    ec_out_t out;
    for (int a = 0; a < NJ; a++) {
        joint_state_t *js = &g_jstate[a];
        if (!js->present) continue;

        if (s_safe_stop) {                      /* WP6 安全覆寫,同 dual_arm_tick */
            out.controlword = s_safe_cw;
            out.target_pos  = js->pos_actual;
            ec_axis_set_output(a, &out);
            continue;
        }
        js->controlword = js->enabled ? CW_ENABLE_OP
                                      : cia402_enable_step(js->statusword);
        out.controlword = js->controlword;
        out.target_pos  = js->enabled ? js->target_pos : js->pos_actual;
        ec_axis_set_output(a, &out);
    }
}

static void be_safe_stop(bool on, uint16_t cw)
{
    s_safe_stop = on;
    s_safe_cw = cw;
}

static uint32_t be_tx_drops(void) { return 0; }   /* process image 無丟幀概念 */

/* CANopen 用 EMCY;EtherCAT 對等物 = CiA402 fault 邊緣（診斷碼 0x603F 屬
 * SDO 背景通道,遞延）。進入 fault 報非零碼、離開報 0x0000（復歸）。 */
static bool be_take_fault(int j, uint16_t *code)
{
    if (j < 0 || j >= NJ || !g_jstate[j].present) return false;
    cia402_state_t st = cia402_decode(g_jstate[j].statusword);
    uint8_t f = (st == DS_FAULT || st == DS_FAULT_REACTION) ? 1 : 0;
    if (f == s_fault_latch[j]) return false;
    s_fault_latch[j] = f;
    if (code) *code = f ? 0xFF00u : 0x0000u;     /* 0xFF00=裝置特定(佔位) */
    return true;
}

static void be_health(int bus_idx, uint32_t window_us, bus_health_t *out)
{
    (void)window_us;
    if (bus_idx == 0) {
        ec_master_health(out);
    } else {                                    /* EtherCAT 單鏈：第二 bus 空槽 */
        memset(out, 0, sizeof(*out));
        out->link_ok = 1;
        out->sync_ok = 1;
    }
}

const bus_if_t g_bus_ecat = {
    .name          = "ethercat",
    .init          = be_init,
    .present_count = be_present,
    .pump_rx       = be_pump_rx,
    .set_target    = be_set_target,
    .tick          = be_tick,
    .set_safe_stop = be_safe_stop,
    .tx_drops      = be_tx_drops,
    .take_fault    = be_take_fault,
    .health        = be_health,
};
