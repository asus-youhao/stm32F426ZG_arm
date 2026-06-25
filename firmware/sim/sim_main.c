/**
 * @file    sim_main.c
 * @brief   假硬體模擬主程式 — 跑整個 L1–L4 韌體 + 模擬 PHU 從站,印出資料流。
 *
 * 情境：
 *   1) app_main_init()：bring-up（NMT/SDO 設定/PDO 映射）— 開 frame log 看 CANopen 交握。
 *   2) 跑 N 個 1kHz tick：兩臂從 0 收斂到初始姿態。
 *   3) 對左臂下笛卡爾目標（末端 +X 5cm）→ 觀察 IK→關節→counts→CAN→回授 的資料流。
 *   4) 切 BIMANUAL 模式,右臂跟隨左臂相對位姿。
 */
#include <stdio.h>
#include <string.h>
#include "dual_arm.h"
#include "joint_space.h"
#include "task_space.h"
#include "dual_arm_ctrl.h"
#include "kinematics.h"

/* app_main.c */
void app_main_init(void);
void app_main_tick(void);
void app_set_left_pose(const pose_t *p);
void app_set_mode(da_mode_t m);
void app_get_left_pose(pose_t *p);
void app_get_right_pose(pose_t *p);
const task_arm_t *app_get_left_arm(void);
int  app_safety_hold(void);

/* co_bxcan_sim.c */
void sim_bus_set_log(int en);
extern unsigned long g_sim_tx_count[2], g_sim_rx_count[2];

static void print_header(void)
{
    printf("\n  tick |   L_J1   L_J2   L_J3   L_J4  (rad) |  L_EE(x,y,z) m        |  R_EE(x) | hold\n");
    printf("  -----+-----------------------------------+-----------------------+----------+-----\n");
}

static void print_row(int tick)
{
    const task_arm_t *la = app_get_left_arm();
    pose_t lp, rp;
    app_get_left_pose(&lp);
    app_get_right_pose(&rp);
    printf("  %5d | %6.3f %6.3f %6.3f %6.3f | %6.3f %6.3f %6.3f | %7.3f  |  %d\n",
           tick, la->q[0], la->q[1], la->q[2], la->q[3],
           lp.p[0], lp.p[1], lp.p[2], rp.p[0], app_safety_hold());
}

int main(void)
{
    printf("=== EYOU PHU 雙臂 假硬體模擬（CANopen over 虛擬 bxCAN）===\n\n");

    printf("[1] app_main_init()：CANopen 交握（NMT / SDO 設定 / PDO 映射）\n");
    printf("    （以下為左臂 node1 等 CAN frame,僅顯示初始化期間前段）\n");
    sim_bus_set_log(1);
    app_main_init();
    sim_bus_set_log(0);
    printf("    [init done] L1–L4 全棧就緒\n");

    printf("\n[2] 1kHz 控制啟動：尚未下目標 → 安全保持於目前姿態（PDO 持續交換）\n");
    print_header();
    for (int t = 1; t <= 300; t++) {
        app_main_tick();
        if (t % 50 == 0) print_row(t);
    }

    printf("\n[3] 對左臂下笛卡爾目標（末端 +X 0.05m）→ 觀察資料流\n");
    pose_t tgt; app_get_left_pose(&tgt);
    printf("    目前 L_EE = (%.3f, %.3f, %.3f),目標 X += 0.05\n",
           tgt.p[0], tgt.p[1], tgt.p[2]);
    tgt.p[0] += 0.05f;
    app_set_left_pose(&tgt);
    print_header();
    for (int t = 301; t <= 900; t++) {
        app_main_tick();
        if (t % 100 == 0) print_row(t);
    }

    printf("\n[4] 切 BIMANUAL 模式（右臂跟隨左臂相對位姿）\n");
    app_set_mode(DA_MODE_BIMANUAL);
    tgt.p[1] += 0.05f;              /* 左臂再動,觀察右臂連動 */
    app_set_left_pose(&tgt);
    print_header();
    for (int t = 901; t <= 1500; t++) {
        app_main_tick();
        if (t % 100 == 0) print_row(t);
    }

    printf("\n[5] CAN 流量統計（frame 數）\n");
    printf("    左臂 bus(CAN1): TX=%lu  RX=%lu\n", g_sim_tx_count[0], g_sim_rx_count[0]);
    printf("    右臂 bus(CAN2): TX=%lu  RX=%lu\n", g_sim_tx_count[1], g_sim_rx_count[1]);
    printf("\n=== 模擬結束 ===\n");
    return 0;
}
