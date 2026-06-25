/**
 * @file    kinematics.h
 * @brief   7-DoF 手臂運動學（DH 正運動學 + 幾何 Jacobian）
 *
 * 姿態以 position(3) + rotation matrix(3x3, row-major 9) 表示。
 * 標準 DH：T_i = Rz(theta_i) * Tz(d_i) * Tx(a_i) * Rx(alpha_i)
 */
#ifndef KINEMATICS_H
#define KINEMATICS_H

#include <stdbool.h>

#define ARM_DOF 7

typedef struct {
    float a;            /* 連桿長度 */
    float alpha;        /* 連桿扭轉 (rad) */
    float d;            /* 連桿偏移 */
    float theta_off;    /* 關節角偏移 (rad) */
} dh_t;

typedef struct {
    dh_t dh[ARM_DOF];   /* 7 軸 DH 參數 */
} arm_kin_t;

typedef struct {
    float p[3];         /* 末端位置 x,y,z */
    float R[9];         /* 末端旋轉矩陣 (row-major) */
} pose_t;

/** @brief 正運動學：關節角 q[7] → 末端位姿。 */
void kin_fk(const arm_kin_t *k, const float q[ARM_DOF], pose_t *out);

/**
 * @brief 幾何 Jacobian（6x7,row-major）。
 * 前 3 列為線速度,後 3 列為角速度。
 */
void kin_jacobian(const arm_kin_t *k, const float q[ARM_DOF], float J[6*ARM_DOF]);

/** @brief 位姿誤差：6 維（位置差 3 + 旋轉差 3,以軸角近似）。 */
void kin_pose_error(const pose_t *desired, const pose_t *current, float err6[6]);

#endif /* KINEMATICS_H */
