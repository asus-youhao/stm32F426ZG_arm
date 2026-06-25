/**
 * @file    task_space.c
 * @brief   L3 Task-space 控制器實作
 */
#include "task_space.h"
#include "joint_space.h"
#include <string.h>
#include <math.h>

void ts_init(task_arm_t *ta, const arm_kin_t *kin, const ik_cfg_t *ik,
             int joint_base, const float q_init[ARM_DOF])
{
    ta->kin = *kin;
    ta->ik = *ik;
    ta->joint_base = joint_base;
    memcpy(ta->q, q_init, sizeof(ta->q));
    ta->has_target = false;
    kin_fk(&ta->kin, ta->q, &ta->target);  /* 預設目標=目前位姿 */
}

void ts_set_target(task_arm_t *ta, const pose_t *target)
{
    ta->target = *target;
    ta->has_target = true;
}

void ts_sync_q(task_arm_t *ta, const float q_fb[ARM_DOF])
{
    memcpy(ta->q, q_fb, sizeof(ta->q));
}

float ts_tick_1khz(task_arm_t *ta)
{
    if (!ta->has_target) return 0.0f;

    /* IK 單步：朝目標前進,更新 ta->q */
    float err = ik_step(&ta->kin, &ta->ik, &ta->target, ta->q);

    /* 寫入 joint_space 設定點（限位由 joint_space 夾制） */
    for (int i=0;i<ARM_DOF;i++)
        js_set_setpoint(ta->joint_base + i, ta->q[i]);

    return err;
}

void ts_get_pose(const task_arm_t *ta, pose_t *out)
{
    kin_fk(&ta->kin, ta->q, out);
}
