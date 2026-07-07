/**
 * 專案A M2(WP3) · joint-space 限速限加速度軌跡（對照 project-dual-arm.html M2）
 *
 * 7 軸各自從 0 rad 走到目標角，梯形速度曲線 @500Hz，輸出 counts。
 * 對照 repo：firmware/control/trajectory.c / joint_space.c 的教學簡化版。
 * 純計算可直接跑：make run
 */
#include <stdio.h>
#include <math.h>

#define AXES   7
#define DT     0.002                 /* 500 Hz = CONTROL_DT */
#define VMAX   1.0                   /* rad/s */
#define AMAX   4.0                   /* rad/s^2 */
#define CPR    (524288.0 / (2.0 * M_PI))   /* counts per rad(輸出端 19-bit) */

typedef struct { double q, v, target; } axis_t;

/* 每 tick 一步：限加速度逼近限速,再限速逼近目標(教學版 S 曲線近似) */
static void traj_step(axis_t *a)
{
    double dist = a->target - a->q;
    double dir = dist >= 0 ? 1.0 : -1.0;
    /* 減速距離 v^2/2a: 快到了就開始煞車 */
    double v_des = (fabs(dist) < a->v * a->v / (2 * AMAX)) ? 0.0 : dir * VMAX;
    double dv = v_des - a->v;
    double dv_max = AMAX * DT;
    if (dv >  dv_max) dv =  dv_max;
    if (dv < -dv_max) dv = -dv_max;
    a->v += dv;
    a->q += a->v * DT;
}

int main(void)
{
    axis_t ax[AXES];
    double targets[AXES] = { 0.5, -0.3, 0.8, 1.2, -0.6, 0.4, -1.0 };
    for (int j = 0; j < AXES; j++) ax[j] = (axis_t){ 0, 0, targets[j] };

    printf("7 軸梯形軌跡 @500Hz (VMAX=%.1f AMAX=%.1f), 每 100ms 印一次\n", VMAX, AMAX);
    for (int t = 0; t <= 1000; t++) {              /* 2 秒 */
        for (int j = 0; j < AXES; j++) traj_step(&ax[j]);
        if (t % 50 == 0) {
            printf("t=%4dms |", t * 2);
            for (int j = 0; j < AXES; j++)
                printf(" J%d=%+7d", j + 1, (int)(ax[j].q * CPR));
            printf(" (counts)\n");
        }
    }
    int done = 1;
    for (int j = 0; j < AXES; j++)
        if (fabs(ax[j].q - ax[j].target) > 1e-3) done = 0;
    printf("%s\n", done ? "全部軸到位,速度曲線平滑 ✅ (真機: 這串 counts 就是 CSP 目標)"
                        : "還沒到位 — 調 VMAX/AMAX 或加時間");
    return !done;
}
