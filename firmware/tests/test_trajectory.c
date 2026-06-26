/** @file test_trajectory.c — 軌跡插值測試 */
#include "test_framework.h"
#include "trajectory.h"

void test_trajectory(void)
{
    /* 梯形：0 → 1 rad,vmax=1, amax=4, dt=1ms */
    traj_t tr;
    traj_plan_trap(&tr, 0.0f, 1.0f, 1.0f, 4.0f, 0.001f);
    CHECK(tr.active);

    float prev = 0.0f, vpeak = 0.0f;
    int guard = 0;
    while (traj_active(&tr) && guard++ < 100000) {
        float q = traj_step(&tr);
        CHECK(q >= -1e-4f && q <= 1.0f + 1e-3f);     /* 不超界 */
        CHECK(q >= prev - 1e-4f);                     /* 單調遞增 */
        if (fabsf(tr.v_cmd) > vpeak) vpeak = fabsf(tr.v_cmd);
        prev = q;
    }
    CHECK(!traj_active(&tr));
    CHECK_NEAR(traj_step(&tr), 1.0f, 1e-3f);          /* 終點到達 */
    CHECK(vpeak <= 1.0f + 0.05f);                      /* 限速 */

    /* 五次多項式：0 → 2 rad,T=1s */
    traj_t q5;
    traj_plan_quintic(&q5, 0.0f, 2.0f, 1.0f, 0.001f);
    float first = traj_step(&q5);
    CHECK_NEAR(first, 0.0f, 0.02f);                    /* 起點附近,起始速度~0 */
    CHECK_NEAR(q5.v_cmd, 0.0f, 0.05f);
    /* 跑到中點 t≈0.5 → 對稱應 ≈1.0 */
    for (int i = 0; i < 499; i++) traj_step(&q5);
    CHECK_NEAR(q5.q_cmd, 1.0f, 0.05f);
    /* 跑完 */
    int guard2 = 0;
    while (traj_active(&q5) && guard2++ < 100000) traj_step(&q5);
    CHECK_NEAR(q5.q_cmd, 2.0f, 1e-3f);                 /* 終點 */
    CHECK_NEAR(q5.v_cmd, 0.0f, 0.05f);                 /* 終速~0 */

    /* 退化：起點=終點 */
    traj_t z;
    traj_plan_trap(&z, 0.5f, 0.5f, 1.0f, 4.0f, 0.001f);
    CHECK(!z.active);
    CHECK_NEAR(traj_step(&z), 0.5f, 1e-6f);
}
