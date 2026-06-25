/**
 * @file    dual_arm.c
 * @brief   雙臂 CANopen 控制實作
 *
 * 拓樸：左臂 7 軸於 bxCAN1（CO_BUS_LEFT）、右臂 7 軸於 bxCAN2（CO_BUS_RIGHT）,
 * 每條 bus 節點 ID 1..7。控制模式採 CSP（循環同步位置）,以 1 kHz 下發目標位置。
 */
#include "dual_arm.h"
#include "co_bxcan.h"
#include "co_nmt.h"
#include "co_pdo.h"
#include "co_sdo.h"
#include "stm32f7xx_hal.h"

#define NIDX(arm, j)  ((arm) * JOINTS_PER_ARM + (j))

/* 關節配置：對照 CLAUDE.md「每臂 7 軸」表 */
const joint_cfg_t g_joints[ARM_COUNT * JOINTS_PER_ARM] = {
    /* 左臂 — bxCAN1 */
    { CO_BUS_LEFT,  1, PHU20, "L_J1_Shoulder" },
    { CO_BUS_LEFT,  2, PHU20, "L_J2_Shoulder" },
    { CO_BUS_LEFT,  3, PHU17, "L_J3_ShoulderYaw" },
    { CO_BUS_LEFT,  4, PHU17, "L_J4_Elbow" },
    { CO_BUS_LEFT,  5, PHU14, "L_J5_Wrist1" },
    { CO_BUS_LEFT,  6, PHU14, "L_J6_Wrist2" },
    { CO_BUS_LEFT,  7, PHU14, "L_J7_Wrist3" },
    /* 右臂 — bxCAN2 */
    { CO_BUS_RIGHT, 1, PHU20, "R_J1_Shoulder" },
    { CO_BUS_RIGHT, 2, PHU20, "R_J2_Shoulder" },
    { CO_BUS_RIGHT, 3, PHU17, "R_J3_ShoulderYaw" },
    { CO_BUS_RIGHT, 4, PHU17, "R_J4_Elbow" },
    { CO_BUS_RIGHT, 5, PHU14, "R_J5_Wrist1" },
    { CO_BUS_RIGHT, 6, PHU14, "R_J6_Wrist2" },
    { CO_BUS_RIGHT, 7, PHU14, "R_J7_Wrist3" },
};
joint_state_t g_jstate[ARM_COUNT * JOINTS_PER_ARM];

/* 設定單一關節 PDO 映射為 CSP 所需內容（需於 Pre-Operational 狀態）。
 * RPDO1：Controlword(0x6040,16) + Target Position(0x607A,32)
 * TPDO1：Statusword(0x6041,16)  + Position Actual(0x6064,32)
 */
static co_status_t map_pdo_csp(co_bus_t bus, uint8_t node)
{
    co_status_t st;
    /* --- RPDO1 (0x1600 mapping, COB 0x200+node) --- */
    st = co_sdo_write(bus, node, 0x1600, 0x00, 0, 1, 50); if (st) return st; /* 清 count */
    st = co_sdo_write(bus, node, 0x1600, 0x01, 0x60400010, 4, 50); if (st) return st; /* CW u16 */
    st = co_sdo_write(bus, node, 0x1600, 0x02, 0x607A0020, 4, 50); if (st) return st; /* TgtPos i32 */
    st = co_sdo_write(bus, node, 0x1600, 0x00, 2, 1, 50); if (st) return st; /* count=2 */
    /* --- TPDO1 (0x1A00 mapping, COB 0x180+node) --- */
    st = co_sdo_write(bus, node, 0x1A00, 0x00, 0, 1, 50); if (st) return st;
    st = co_sdo_write(bus, node, 0x1A00, 0x01, 0x60410010, 4, 50); if (st) return st; /* SW u16 */
    st = co_sdo_write(bus, node, 0x1A00, 0x02, 0x60640020, 4, 50); if (st) return st; /* PosAct i32 */
    st = co_sdo_write(bus, node, 0x1A00, 0x00, 2, 1, 50); if (st) return st;
    return CO_OK;
}

co_status_t dual_arm_init(void)
{
    co_status_t st;

    /* 1) 初始化兩條 bxCAN channel */
    st = co_bxcan_init(CO_BUS_LEFT);  if (st) return st;
    st = co_bxcan_init(CO_BUS_RIGHT); if (st) return st;

    /* 2) 重置通訊 + 進入 Pre-Operational（廣播） */
    for (int b = 0; b < CO_BUS_COUNT; b++) {
        co_nmt_send((co_bus_t)b, CO_NMT_RESET_COMM, 0);
    }
    HAL_Delay(100);
    for (int b = 0; b < CO_BUS_COUNT; b++) {
        co_nmt_send((co_bus_t)b, CO_NMT_PRE_OP, 0);
    }
    HAL_Delay(20);

    /* 3) 逐軸：設模式 CSP、設 PDO 映射 */
    for (int i = 0; i < ARM_COUNT * JOINTS_PER_ARM; i++) {
        const joint_cfg_t *jc = &g_joints[i];
        st = cia402_set_mode(jc->bus, jc->node_id, MODE_CSP);
        if (st) return st;
        st = map_pdo_csp(jc->bus, jc->node_id);
        if (st) return st;
        g_jstate[i].controlword = CW_SHUTDOWN;
    }

    /* 4) 進入 Operational（開始 PDO 交換） */
    for (int b = 0; b < CO_BUS_COUNT; b++) {
        co_nmt_send((co_bus_t)b, CO_NMT_START, 0);
    }
    HAL_Delay(20);

    /* 5) 把目標位置初值設為當前實際位置（避免使能瞬間跳動） */
    dual_arm_pump_rx();
    for (int i = 0; i < ARM_COUNT * JOINTS_PER_ARM; i++) {
        uint16_t sw; int32_t pa;
        if (co_pdo_get_feedback(g_joints[i].bus, g_joints[i].node_id, &sw, &pa)) {
            g_jstate[i].target_pos = pa;
        }
    }
    return CO_OK;
}

void dual_arm_pump_rx(void)
{
    for (int b = 0; b < CO_BUS_COUNT; b++) {
        co_frame_t f;
        while (co_bxcan_recv((co_bus_t)b, &f)) {
            co_nmt_process_frame((co_bus_t)b, &f);
            co_pdo_process_frame((co_bus_t)b, &f);
        }
    }
}

void dual_arm_set_target(uint8_t idx, int32_t target_pos)
{
    if (idx < ARM_COUNT * JOINTS_PER_ARM) g_jstate[idx].target_pos = target_pos;
}

static bool s_safe_stop = false;
static uint16_t s_safe_cw = 0x0002; /* quick stop */
void dual_arm_set_safe_stop(bool on, uint16_t safe_cw)
{
    s_safe_stop = on;
    s_safe_cw = safe_cw;
}

static uint32_t s_tx_drops = 0;
uint32_t dual_arm_tx_drops(void) { return s_tx_drops; }

void dual_arm_tick(void)
{
    /* 1) 收進回授 */
    dual_arm_pump_rx();

    /* 2) 逐軸更新回授快取、推進使能、送出 CSP 目標 */
    for (int i = 0; i < ARM_COUNT * JOINTS_PER_ARM; i++) {
        const joint_cfg_t *jc = &g_joints[i];
        joint_state_t *js = &g_jstate[i];

        /* 新鮮度：序號變動才算「本 tick 真的收到新 TPDO」（看門狗用） */
        uint32_t seq = co_pdo_feedback_seq(jc->bus, jc->node_id);
        js->fb_fresh = (seq != js->fb_seq);
        js->fb_seq = seq;

        uint16_t sw; int32_t pa;
        if (co_pdo_get_feedback(jc->bus, jc->node_id, &sw, &pa)) {
            js->statusword = sw;
            js->pos_actual = pa;
            js->enabled = (cia402_decode(sw) == DS_OPERATION_ENABLED);
        }

        co_status_t st;

        /* WP6 安全停止覆寫：強制安全控制字、目標維持實際位置 */
        if (s_safe_stop) {
            st = co_pdo_send_csp(jc->bus, jc->node_id, s_safe_cw, js->pos_actual);
            if (st == CO_ERR_TX) s_tx_drops++;
            continue;
        }

        /* 推進 CiA402 使能狀態機（未使能時用回授狀態決定下一步 CW） */
        js->controlword = js->enabled ? CW_ENABLE_OP
                                      : cia402_enable_step(js->statusword);

        /* 尚未使能：維持目標 = 實際,避免跳動 */
        int32_t tgt = js->enabled ? js->target_pos : js->pos_actual;

        st = co_pdo_send_csp(jc->bus, jc->node_id, js->controlword, tgt);
        if (st == CO_ERR_TX) s_tx_drops++;   /* mailbox 滿 → 頻寬不足 */
    }
}
