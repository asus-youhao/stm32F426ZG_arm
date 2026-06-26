/** @file test_ik.c — FK→IK 往返收斂測試 */
#include "test_framework.h"
#include "kinematics.h"
#include "ik.h"
#include "robot_config.h"

void test_ik(void)
{
    const arm_kin_t *k = robot_left_kin();
    ik_cfg_t cfg = { .lambda = 0.03f, .step_gain = 0.6f,
                     .max_iters = 500, .pos_tol = 1e-3f, .rot_tol = 5e-3f };

    /* 真值姿態 */
    float q_true[ARM_DOF] = {0.2f, 0.4f, -0.3f, 0.9f, 0.1f, -0.4f, 0.3f};
    pose_t target;
    kin_fk(k, q_true, &target);

    /* 從擾動起點求解,應收斂到同一「位姿」(7-DoF 冗餘,q 可不同) */
    float q[ARM_DOF];
    for (int i = 0; i < ARM_DOF; i++) q[i] = q_true[i] + 0.15f;

    int ok = ik_solve(k, &cfg, &target, q);
    CHECK(ok);

    pose_t got;
    kin_fk(k, q, &got);
    float e[6];
    kin_pose_error(&target, &got, e);
    float pos = sqrtf(e[0]*e[0]+e[1]*e[1]+e[2]*e[2]);
    float rot = sqrtf(e[3]*e[3]+e[4]*e[4]+e[5]*e[5]);
    CHECK(pos < 2e-3f);
    CHECK(rot < 1e-2f);

    /* 單步應使誤差下降 */
    float q2[ARM_DOF];
    for (int i = 0; i < ARM_DOF; i++) q2[i] = q_true[i] + 0.1f;
    pose_t p0; kin_fk(k, q2, &p0);
    float e0[6]; kin_pose_error(&target, &p0, e0);
    float n0 = 0; for (int i=0;i<6;i++) n0 += e0[i]*e0[i];
    float n1 = ik_step(k, &cfg, &target, q2);   /* 回傳前一狀態誤差範數 */
    pose_t p1; kin_fk(k, q2, &p1);
    float e1[6]; kin_pose_error(&target, &p1, e1);
    float n1after = 0; for (int i=0;i<6;i++) n1after += e1[i]*e1[i];
    CHECK(n1after < sqrtf(n0)*sqrtf(n0) + 1e-9f);  /* 誤差不增 */
    (void)n1;
}
