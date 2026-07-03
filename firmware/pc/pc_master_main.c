/**
 * @file    pc_master_main.c
 * @brief   PC 端 CANopen 主站 — 與 board/main.c 同一套 L1–L4 全棧,底層走 SocketCAN
 *
 * 用途：不接 Nucleo 板也能測完整韌體邏輯。
 *   行程A（本程式）：app_main 全棧 @500Hz,vcan0=左臂、vcan1=右臂
 *   行程B（sim_py/can_slave.py ×2）：每條 bus 模擬 7 顆 EYOU PHU CiA402 從站
 *
 * 流程對齊 board/main.c：
 *   （選配 --bringup）單軸 bring-up → app_main_init() → 500Hz tick 迴圈
 *   差異：TIM6 ISR 換成 clock_nanosleep 絕對時間週期;UART log 換成 stdout;
 *         多了 stdin 互動命令（j/e/p/q）方便手動測試。
 */
#include "canopen.h"
#include "co_bxcan_socketcan.h"
#include "dual_arm.h"
#include "dual_arm_ctrl.h"
#include "task_space.h"
#include "control_rate.h"
#include "bringup.h"
#include "stm32f7xx_hal.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

/* app_main.c 對外 API（無公用標頭,與 board/main.c 同樣以 extern 取用） */
void app_main_init(void);
void app_main_tick(void);
void app_set_estop(int active);
const char *app_sys_state(void);
void app_joint_move(int joint, float rad);
void app_set_mode(da_mode_t m);
void app_get_left_pose(pose_t *p);
void app_get_right_pose(pose_t *p);

/* bringup.c 的弱連結 log → 導到 stdout */
void bringup_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

#define TICKS_PER_SEC ((uint64_t)CONTROL_HZ)   /* CONTROL_HZ 為 float,取整用 */

static volatile sig_atomic_t s_quit = 0;
static void on_sigint(int sig) { (void)sig; s_quit = 1; }

static void usage(const char *argv0)
{
    printf("用法: %s [--left IF] [--right IF|none] [--bringup NODE] [--seconds N]\n"
           "  --left IF      左臂 SocketCAN 介面（預設 vcan0）\n"
           "  --right IF     右臂 SocketCAN 介面（預設 vcan1;'none' 停用 → 單臂）\n"
           "  --bringup N    先對左臂 node N 跑 WP2 單軸 bring-up（SDO 驗證 + 轉動）\n"
           "  --seconds N    跑 N 秒後自動結束（0=直到 Ctrl-C;預設 0）\n"
           "互動命令（stdin）：\n"
           "  j <idx> <rad>  關節點到點（idx 0..13）\n"
           "  e <0|1>        急停 off/on\n"
           "  p              印出雙臂末端位姿\n"
           "  q              離開\n", argv0);
}

static void print_status(uint64_t tick, uint32_t late_max_us)
{
    printf("[app] t=%llus sys=%s J0 sw=0x%04X pos=%ld tgt=%ld drops=%lu late_max=%uus\n",
           (unsigned long long)(tick / TICKS_PER_SEC), app_sys_state(),
           g_jstate[0].statusword,
           (long)g_jstate[0].pos_actual, (long)g_jstate[0].target_pos,
           (unsigned long)dual_arm_tx_drops(), (unsigned)late_max_us);
    fflush(stdout);
}

static void print_poses(void)
{
    pose_t L, R;
    app_get_left_pose(&L);
    app_get_right_pose(&R);
    printf("  左末端 xyz=(%.3f, %.3f, %.3f)  右末端 xyz=(%.3f, %.3f, %.3f)\n",
           L.p[0], L.p[1], L.p[2], R.p[0], R.p[1], R.p[2]);
    fflush(stdout);
}

/* 非阻塞讀 stdin 一行;有完整命令回 true */
static bool poll_stdin(char *line, size_t cap)
{
    fd_set rf;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&rf);
    FD_SET(STDIN_FILENO, &rf);
    if (select(STDIN_FILENO + 1, &rf, NULL, NULL, &tv) <= 0) return false;
    if (!fgets(line, (int)cap, stdin)) { s_quit = 1; return false; }
    return true;
}

static void handle_cmd(const char *line)
{
    int idx;
    float rad;
    int v;
    if (sscanf(line, "j %d %f", &idx, &rad) == 2 && idx >= 0 && idx < 14) {
        app_joint_move(idx, rad);
        printf("  → J%d move_to %.3f rad\n", idx, rad);
    } else if (sscanf(line, "e %d", &v) == 1) {
        app_set_estop(v);
        printf("  → estop %s\n", v ? "ON" : "OFF");
    } else if (line[0] == 'p') {
        print_poses();
    } else if (line[0] == 'q') {
        s_quit = 1;
    } else if (line[0] != '\n') {
        printf("  未知命令（j/e/p/q）\n");
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *left = "vcan0", *right = "vcan1";
    int bringup_node = 0;
    long run_seconds = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--left") && i + 1 < argc)         left = argv[++i];
        else if (!strcmp(argv[i], "--right") && i + 1 < argc)   right = argv[++i];
        else if (!strcmp(argv[i], "--bringup") && i + 1 < argc) bringup_node = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) run_seconds = atol(argv[++i]);
        else { usage(argv[0]); return (strcmp(argv[i], "--help") == 0) ? 0 : 2; }
    }
    if (!strcmp(right, "none")) right = "";

    signal(SIGINT, on_sigint);

    co_socketcan_set_ifname(CO_BUS_LEFT, left);
    co_socketcan_set_ifname(CO_BUS_RIGHT, right);

    printf("=== PC CANopen 主站（SocketCAN）===\n");
    printf("左臂=%s  右臂=%s  週期=%u us（%u Hz）\n",
           left, right[0] ? right : "(停用)",
           (unsigned)CONTROL_DT_US, (unsigned)TICKS_PER_SEC);

    /* （選配）WP2 單軸 bring-up：SDO 驗證 + 轉動,同 board main 開機自檢 */
    if (bringup_node > 0) {
        bringup_report_t rep;
        co_status_t st = bringup_single_axis(CO_BUS_LEFT, (uint8_t)bringup_node,
                                             5000, 1000, &rep);
        printf("=== bring-up result = %d (0=OK) ===\n", st);
        printf("  deviceType=0x%08lX baud=%lu node=%lu\n",
               (unsigned long)rep.device_type, (unsigned long)rep.baudrate_bps,
               (unsigned long)rep.node_id_read);
        printf("  pos_before=%ld pos_after=%ld moved=%d\n",
               (long)rep.pos_before, (long)rep.pos_after, rep.moved);
    }

    /* L1–L4 全棧初始化（NMT reset → PDO 映射 → CSP → 使能） */
    printf("\n=== app phase: init L1-L4 stack ===\n");
    app_main_init();
    int present = dual_arm_present_count();
    printf("dual_arm present joints = %d / 14%s\n", present,
           present ? "" : "  (init FAILED — 從站沒起來?)");
    if (present == 0) return 1;

    /* 500 Hz 控制迴圈：clock_nanosleep 絕對時間,量測遲到抖動 */
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    uint64_t tick = 0;
    uint32_t late_max_us = 0;
    uint64_t tick_limit = (run_seconds > 0)
                        ? (uint64_t)run_seconds * TICKS_PER_SEC : 0;
    char line[128];

    printf("控制迴圈啟動（Ctrl-C 或 'q' 結束）。命令：j <idx> <rad> / e <0|1> / p / q\n");
    while (!s_quit && (tick_limit == 0 || tick < tick_limit)) {
        next.tv_nsec += (long)CONTROL_DT_US * 1000L;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long late_us = (now.tv_sec - next.tv_sec) * 1000000L
                     + (now.tv_nsec - next.tv_nsec) / 1000L;
        if (late_us > 0 && (uint32_t)late_us > late_max_us)
            late_max_us = (uint32_t)late_us;

        app_main_tick();
        tick++;

        if (tick % TICKS_PER_SEC == 0) {              /* 每秒狀態 */
            print_status(tick, late_max_us);
            late_max_us = 0;
        }
        if (tick % (TICKS_PER_SEC / 50) == 0 && poll_stdin(line, sizeof(line)))
            handle_cmd(line);                          /* 50Hz 輪詢 stdin */
    }

    printf("\n結束：ticks=%llu drops=%lu sys=%s\n",
           (unsigned long long)tick, (unsigned long)dual_arm_tx_drops(),
           app_sys_state());
    return 0;
}
