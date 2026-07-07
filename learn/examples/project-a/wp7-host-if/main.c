/**
 * 專案A M6(WP7) · 上位機文字協定解析（對照 project-dual-arm.html M6）
 *
 * firmware/host/host_if.c 的教學簡化版：stdin 讀命令 → 呼叫控制 API。
 * 跑法：make run 之後輸入命令，或
 *   printf "STATE\nL 0.30 0.20\nSTATE\nESTOP\nSTATE\nQUIT\n" | ./demo
 */
#include <stdio.h>
#include <string.h>

static double lx = 0.35, ly = 0.10;    /* 左臂末端目標(2D 教學版) */
static int    estop = 0;

static void cmd_state(void)
{
    printf("< sys=%s left_target=(%.3f, %.3f)\n",
           estop ? "ESTOP" : "RUNNING", lx, ly);
}

int main(void)
{
    char line[128];
    printf("命令: L <x> <y> | STATE | ESTOP | RESET | QUIT\n");
    while (fgets(line, sizeof line, stdin)) {
        double x, y;
        if (sscanf(line, "L %lf %lf", &x, &y) == 2) {
            if (estop) { printf("< REJECT: estop 中不收運動命令\n"); continue; }
            lx = x; ly = y;                  /* 真機: app_set_left_pose() */
            printf("< OK L target=(%.3f, %.3f)\n", lx, ly);
        } else if (!strncmp(line, "STATE", 5)) {
            cmd_state();                     /* 真機: app_sys_state()+app_get_*_pose */
        } else if (!strncmp(line, "ESTOP", 5)) {
            estop = 1;                       /* 真機: app_set_estop(1) */
            printf("< OK estop\n");
        } else if (!strncmp(line, "RESET", 5)) {
            estop = 0;
            printf("< OK reset\n");
        } else if (!strncmp(line, "QUIT", 4)) {
            break;
        } else {
            printf("< ERR unknown: %s", line);
        }
    }
    printf("bye — 完整版含 UART DMA 環形緩衝,見 firmware/host/host_if.c\n");
    return 0;
}
