/**
 * @file    ik.c
 * @brief   DLS 逆運動學實作
 */
#include "ik.h"
#include "linalg.h"
#include <math.h>

float ik_step(const arm_kin_t *k, const ik_cfg_t *cfg,
              const pose_t *desired, float q[ARM_DOF])
{
    pose_t cur;
    kin_fk(k, q, &cur);

    float e[6];
    kin_pose_error(desired, &cur, e);

    float J[6*ARM_DOF];
    kin_jacobian(k, q, J);

    /* JJt = J * J^T  (6x6) */
    float Jt[ARM_DOF*6];
    la_transpose(J, Jt, 6, ARM_DOF);
    float JJt[6*6];
    la_matmul(J, Jt, JJt, 6, ARM_DOF, 6);

    /* JJt += λ² I */
    float lam2 = cfg->lambda * cfg->lambda;
    for (int i=0;i<6;i++) JJt[i*6+i] += lam2;

    /* inv(JJt) */
    float JJti[6*6];
    if (!la_inverse(JJt, JJti, 6)) {
        /* 極端奇異,放棄這步 */
        float n=0; for(int i=0;i<6;i++) n+=e[i]*e[i]; return sqrtf(n);
    }

    /* tmp = inv(JJt) * e   (6) */
    float tmp[6];
    la_matvec(JJti, e, tmp, 6, 6);

    /* dq = J^T * tmp   (7) */
    float dq[ARM_DOF];
    la_matvec(Jt, tmp, dq, ARM_DOF, 6);

    for (int i=0;i<ARM_DOF;i++) q[i] += cfg->step_gain * dq[i];

    float n=0; for(int i=0;i<6;i++) n+=e[i]*e[i];
    return sqrtf(n);
}

bool ik_solve(const arm_kin_t *k, const ik_cfg_t *cfg,
              const pose_t *desired, float q[ARM_DOF])
{
    for (int it=0; it<cfg->max_iters; it++){
        (void)ik_step(k, cfg, desired, q);
        pose_t cur; kin_fk(k, q, &cur);
        float e[6]; kin_pose_error(desired, &cur, e);
        float pos = sqrtf(e[0]*e[0]+e[1]*e[1]+e[2]*e[2]);
        float rot = sqrtf(e[3]*e[3]+e[4]*e[4]+e[5]*e[5]);
        if (pos < cfg->pos_tol && rot < cfg->rot_tol) return true;
    }
    return false;
}
