/**
 * L3-15 · 2 連桿平面手臂 FK/IK + 直線軌跡（對照 learn/l3-adv.html 第 15 節）
 *
 * 7-DoF 太大不好教,先用 2 連桿把「FK → 數值 IK(Jacobian) → task-space 直線」
 * 的完整迴路走一遍 — firmware/control/ 的 kinematics/ik/trajectory 是同一套
 * 思路的 7 軸版。
 */
#include <stdio.h>
#include <math.h>

#define L1 0.30
#define L2 0.25

static void fk(double q1, double q2, double *x, double *y)
{
    *x = L1 * cos(q1) + L2 * cos(q1 + q2);
    *y = L1 * sin(q1) + L2 * sin(q1 + q2);
}

/* 數值 IK：Newton 法,解 J·dq = e（2x2 直接反解 + 奇異點保護 + 步長夾限）
 * J = [[-L1 s1 - L2 s12, -L2 s12],[L1 c1 + L2 c12, L2 c12]] */
static int ik(double tx, double ty, double *q1, double *q2)
{
    for (int it = 0; it < 100; it++) {
        double x, y; fk(*q1, *q2, &x, &y);
        double ex = tx - x, ey = ty - y;
        if (ex * ex + ey * ey < 1e-16) return it;
        double s1 = sin(*q1), c1 = cos(*q1);
        double s12 = sin(*q1 + *q2), c12 = cos(*q1 + *q2);
        double j11 = -L1 * s1 - L2 * s12, j12 = -L2 * s12;
        double j21 =  L1 * c1 + L2 * c12, j22 =  L2 * c12;
        double det = j11 * j22 - j12 * j21;
        if (fabs(det) < 1e-9) det = det >= 0 ? 1e-9 : -1e-9;  /* 奇異點保護 */
        double d1 = ( j22 * ex - j12 * ey) / det;
        double d2 = (-j21 * ex + j11 * ey) / det;
        if (d1 >  0.2) d1 =  0.2;  if (d1 < -0.2) d1 = -0.2;  /* 大步夾限 */
        if (d2 >  0.2) d2 =  0.2;  if (d2 < -0.2) d2 = -0.2;
        *q1 += d1; *q2 += d2;
    }
    return -1;
}

int main(void)
{
    double q1 = 0.5, q2 = 0.5;                          /* 初始姿態 */
    double x0 = 0.35, y0 = 0.10, x1 = 0.25, y1 = 0.35;  /* 直線起訖 */
    int N = 10;

    printf("task-space 直線 (%.2f,%.2f) -> (%.2f,%.2f), %d 個插補點\n",
           x0, y0, x1, y1, N);
    printf("%4s %8s %8s %10s %10s %6s\n", "k", "x", "y", "q1(deg)", "q2(deg)", "iter");
    for (int k = 0; k <= N; k++) {
        double t = (double)k / N;
        double tx = x0 + (x1 - x0) * t, ty = y0 + (y1 - y0) * t;
        int it = ik(tx, ty, &q1, &q2);   /* 用上一點的解當初值 → 快又連續 */
        double vx, vy; fk(q1, q2, &vx, &vy);
        printf("%4d %8.4f %8.4f %10.2f %10.2f %6d  err=%.1e\n",
               k, tx, ty, q1 * 180 / M_PI, q2 * 180 / M_PI, it,
               hypot(vx - tx, vy - ty));
    }
    printf("重點: 每個插補點用前一點解當初值 — CSP 每 2ms 一點,IK 永遠只差一小步。\n");
    return 0;
}
