/**
 * @file    trajectory.c
 * @brief   單關節軌跡插值實作
 */
#include "trajectory.h"
#include <math.h>

void traj_plan_trap(traj_t *tr, float q0, float qf, float vmax, float amax, float dt)
{
    tr->type = TRAJ_TRAP;
    tr->q0 = q0; tr->qf = qf; tr->dt = dt; tr->t = 0.0f;
    tr->vmax = fabsf(vmax); tr->amax = fabsf(amax);
    float dist = qf - q0;
    tr->dir = (dist >= 0.0f) ? 1.0f : -1.0f;
    float D = fabsf(dist);

    if (D < 1e-9f || tr->vmax < 1e-9f || tr->amax < 1e-9f) {
        tr->active = false; tr->q_cmd = qf; tr->v_cmd = 0; tr->T = 0; return;
    }

    tr->ta = tr->vmax / tr->amax;                 /* 達到 vmax 所需時間 */
    float Da = 0.5f * tr->amax * tr->ta * tr->ta; /* 加速段距離 */
    if (2.0f * Da > D) {
        /* 三角形剖面（達不到 vmax） */
        tr->ta = sqrtf(D / tr->amax);
        tr->tc = 0.0f;
    } else {
        float Dc = D - 2.0f * Da;
        tr->tc = Dc / tr->vmax;
    }
    tr->T = 2.0f * tr->ta + tr->tc;
    tr->active = true; tr->q_cmd = q0; tr->v_cmd = 0;
}

void traj_plan_quintic(traj_t *tr, float q0, float qf, float T, float dt)
{
    tr->type = TRAJ_QUINTIC;
    tr->q0 = q0; tr->qf = qf; tr->T = T; tr->dt = dt; tr->t = 0.0f;
    if (T < 1e-6f) { tr->active = false; tr->q_cmd = qf; tr->v_cmd = 0; return; }
    float h = qf - q0;
    /* 邊界：v、a 起終為 0 → 標準五次係數 */
    tr->a0 = q0; tr->a1 = 0.0f; tr->a2 = 0.0f;
    tr->a3 = 10.0f * h / (T*T*T);
    tr->a4 = -15.0f * h / (T*T*T*T);
    tr->a5 = 6.0f * h / (T*T*T*T*T);
    tr->active = true; tr->q_cmd = q0; tr->v_cmd = 0;
}

float traj_step(traj_t *tr)
{
    if (!tr->active) return tr->qf;
    tr->t += tr->dt;

    if (tr->t >= tr->T) {
        tr->active = false; tr->q_cmd = tr->qf; tr->v_cmd = 0.0f;
        return tr->qf;
    }

    if (tr->type == TRAJ_QUINTIC) {
        float t = tr->t;
        float t2=t*t, t3=t2*t, t4=t3*t, t5=t4*t;
        tr->q_cmd = tr->a0 + tr->a1*t + tr->a2*t2 + tr->a3*t3 + tr->a4*t4 + tr->a5*t5;
        tr->v_cmd = tr->a1 + 2*tr->a2*t + 3*tr->a3*t2 + 4*tr->a4*t3 + 5*tr->a5*t4;
        return tr->q_cmd;
    }

    /* 梯形 */
    float t = tr->t, s;
    float a = tr->amax, ta = tr->ta;
    if (t < ta) {
        s = 0.5f * a * t * t;
        tr->v_cmd = a * t;
    } else if (t < ta + tr->tc) {
        float vmax = a * ta;
        s = 0.5f * a * ta * ta + vmax * (t - ta);
        tr->v_cmd = vmax;
    } else {
        float vmax = a * ta;
        float td = t - ta - tr->tc;
        float Sa = 0.5f * a * ta * ta;
        s = Sa + vmax * tr->tc + (vmax * td - 0.5f * a * td * td);
        tr->v_cmd = vmax - a * td;
    }
    tr->q_cmd = tr->q0 + tr->dir * s;
    tr->v_cmd *= tr->dir;
    return tr->q_cmd;
}
