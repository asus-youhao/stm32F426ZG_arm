/**
 * @file    dual_arm_ctrl.c
 * @brief   L4 雙臂協同控制實作
 */
#include "dual_arm_ctrl.h"
#include "joint_space.h"
#include <math.h>
#include <string.h>

/* pose 合成：out = A ∘ B（先 B 再 A，於 A 座標系下）
 *   out.R = A.R * B.R
 *   out.p = A.R * B.p + A.p
 */
static void pose_compose(const pose_t *A, const pose_t *B, pose_t *out)
{
    float R[9];
    for (int i=0;i<3;i++)
        for (int j=0;j<3;j++){
            float s=0;
            for (int k=0;k<3;k++) s+=A->R[i*3+k]*B->R[k*3+j];
            R[i*3+j]=s;
        }
    float p[3];
    for (int i=0;i<3;i++)
        p[i]=A->R[i*3+0]*B->p[0]+A->R[i*3+1]*B->p[1]+A->R[i*3+2]*B->p[2]+A->p[i];
    memcpy(out->R,R,sizeof(R));
    memcpy(out->p,p,sizeof(p));
}

static float ee_distance(const dual_arm_ctrl_t *dc)
{
    pose_t pl, pr;
    ts_get_pose(&dc->left, &pl);
    ts_get_pose(&dc->right, &pr);
    float dx=pl.p[0]-pr.p[0], dy=pl.p[1]-pr.p[1], dz=pl.p[2]-pr.p[2];
    return sqrtf(dx*dx+dy*dy+dz*dz);
}

void da_ctrl_init(dual_arm_ctrl_t *dc,
                  const task_arm_t *left, const task_arm_t *right,
                  da_mode_t mode, float min_ee_distance)
{
    dc->left = *left;
    dc->right = *right;
    dc->mode = mode;
    dc->min_ee_distance = min_ee_distance;
    dc->safety_hold = false;
    /* 預設相對位姿：右目前相對左目前（單位旋轉 + 位移） */
    memset(&dc->rel_right_in_left, 0, sizeof(pose_t));
    dc->rel_right_in_left.R[0]=dc->rel_right_in_left.R[4]=dc->rel_right_in_left.R[8]=1.0f;
}

void da_ctrl_set_mode(dual_arm_ctrl_t *dc, da_mode_t mode){ dc->mode = mode; }
void da_ctrl_set_left_target(dual_arm_ctrl_t *dc, const pose_t *p){ ts_set_target(&dc->left, p); }
void da_ctrl_set_right_target(dual_arm_ctrl_t *dc, const pose_t *p){ ts_set_target(&dc->right, p); }
void da_ctrl_set_relative(dual_arm_ctrl_t *dc, const pose_t *rel){ dc->rel_right_in_left = *rel; }

void da_ctrl_sync_feedback(dual_arm_ctrl_t *dc, const float q_fb14[14])
{
    ts_sync_q(&dc->left,  &q_fb14[0]);
    ts_sync_q(&dc->right, &q_fb14[7]);
}

float da_ctrl_tick_1khz(dual_arm_ctrl_t *dc, int32_t out_counts[14])
{
    /* 1) BIMANUAL：右臂目標 = 左臂目前位姿 × 相對變換 */
    if (dc->mode == DA_MODE_BIMANUAL) {
        pose_t left_pose, right_target;
        ts_get_pose(&dc->left, &left_pose);
        pose_compose(&left_pose, &dc->rel_right_in_left, &right_target);
        ts_set_target(&dc->right, &right_target);
    }

    /* 2) 安全：自碰撞 / 最小末端距離檢查 */
    float d = ee_distance(dc);
    dc->safety_hold = (dc->min_ee_distance > 0.0f && d < dc->min_ee_distance);

    /* 3) 兩臂 task_space 各走一步（保持中則不推進 IK，維持目前設定點） */
    float err = 0.0f;
    if (!dc->safety_hold) {
        err += ts_tick_1khz(&dc->left);
        err += ts_tick_1khz(&dc->right);
    } else {
        /* 維持目前關節設定點（不前進） */
        for (int i=0;i<ARM_DOF;i++){
            js_set_setpoint(dc->left.joint_base + i,  dc->left.q[i]);
            js_set_setpoint(dc->right.joint_base + i, dc->right.q[i]);
        }
    }

    /* 4) joint_space → counts（交由上層送 dual_arm_set_target / dual_arm_tick） */
    js_tick_1khz(out_counts);
    return err;
}
