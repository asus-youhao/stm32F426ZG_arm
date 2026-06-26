/** @file test_kinematics.c — FK 一致性 + Jacobian 數值微分對拍 */
#include "test_framework.h"
#include "kinematics.h"
#include "robot_config.h"

/* 以有限差分驗證幾何 Jacobian 的線速度部分（rows 0..2）。 */
void test_kinematics(void)
{
    const arm_kin_t *k = robot_left_kin();
    float q[ARM_DOF] = {0.1f, 0.3f, -0.2f, 0.7f, 0.4f, -0.5f, 0.2f};

    /* FK 應為有限值 */
    pose_t pe;
    kin_fk(k, q, &pe);
    for (int i = 0; i < 3; i++) CHECK(isfinite(pe.p[i]));

    /* 旋轉矩陣應近似正交：R*R^T ≈ I */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float s = 0;
            for (int m = 0; m < 3; m++) s += pe.R[i*3+m]*pe.R[j*3+m];
            CHECK_NEAR(s, (i == j) ? 1.0f : 0.0f, 1e-3f);
        }

    /* Jacobian 線速度欄 vs 有限差分 dPos/dq */
    float J[6*ARM_DOF];
    kin_jacobian(k, q, J);

    const float h = 1e-5f;
    for (int i = 0; i < ARM_DOF; i++) {
        float qa[ARM_DOF], qb[ARM_DOF];
        for (int m = 0; m < ARM_DOF; m++) { qa[m] = q[m]; qb[m] = q[m]; }
        qa[i] += h; qb[i] -= h;
        pose_t pa, pb;
        kin_fk(k, qa, &pa);
        kin_fk(k, qb, &pb);
        for (int r = 0; r < 3; r++) {
            float fd = (pa.p[r] - pb.p[r]) / (2.0f * h);
            CHECK_NEAR(J[r*ARM_DOF + i], fd, 1e-2f);
        }
    }
}
