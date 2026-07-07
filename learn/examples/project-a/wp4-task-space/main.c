/**
 * 專案A M3(WP4) · task-space 直線 → IK → counts（對照 project-dual-arm.html M3）
 *
 * 把 L3-15 的 2 連桿 IK 接上 WP3 的 500Hz 節拍：末端走直線,
 * 每 2ms 產生一次關節角並轉 counts — 這正是 app_main_tick() 第 3 步在做的事。
 */
#include <stdio.h>
#include <math.h>

#define L1c 0.30
#define L2c 0.25
#define CPR (524288.0 / (2.0 * M_PI))

static void fk(double q1, double q2, double *x, double *y)
{
    *x = L1c * cos(q1) + L2c * cos(q1 + q2);
    *y = L1c * sin(q1) + L2c * sin(q1 + q2);
}

/* Newton 法：解 J·dq = e（2x2 直接反解 + 奇異點保護 + 步長夾限） */
static void ik_step(double tx, double ty, double *q1, double *q2)
{
    for (int it = 0; it < 100; it++) {
        double x, y; fk(*q1, *q2, &x, &y);
        double ex = tx - x, ey = ty - y;
        if (ex * ex + ey * ey < 1e-16) return;
        double s1 = sin(*q1), c1 = cos(*q1);
        double s12 = sin(*q1 + *q2), c12 = cos(*q1 + *q2);
        double j11 = -L1c * s1 - L2c * s12, j12 = -L2c * s12;
        double j21 =  L1c * c1 + L2c * c12, j22 =  L2c * c12;
        double det = j11 * j22 - j12 * j21;
        if (fabs(det) < 1e-9) det = det >= 0 ? 1e-9 : -1e-9;
        double d1 = ( j22 * ex - j12 * ey) / det;
        double d2 = (-j21 * ex + j11 * ey) / det;
        if (d1 >  0.2) d1 =  0.2;  if (d1 < -0.2) d1 = -0.2;
        if (d2 >  0.2) d2 =  0.2;  if (d2 < -0.2) d2 = -0.2;
        *q1 += d1; *q2 += d2;
    }
}

int main(void)
{
    double q1 = 0.5, q2 = 0.5;
    double x0 = 0.35, y0 = 0.10, x1 = 0.25, y1 = 0.35;
    int N = 500;                                   /* 1 秒 @500Hz */
    double max_err = 0;

    printf("末端直線 %d 個 2ms 插補點,每 100ms 印一次:\n", N);
    for (int k = 0; k <= N; k++) {
        double t = (double)k / N;
        ik_step(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, &q1, &q2);
        double vx, vy; fk(q1, q2, &vx, &vy);
        double err = hypot(vx - (x0 + (x1 - x0) * t), vy - (y0 + (y1 - y0) * t));
        if (err > max_err) max_err = err;
        if (k % 50 == 0)
            printf("t=%4dms  J1=%+8d J2=%+8d counts  err=%.2e m\n",
                   k * 2, (int)(q1 * CPR), (int)(q2 * CPR), err);
    }
    printf("最大軌跡誤差 %.2e m %s\n", max_err,
           max_err < 1e-4 ? "✅ (7軸版見 firmware/control/ik.c)" : "❌ IK 沒收斂");
    return !(max_err < 1e-4);
}
