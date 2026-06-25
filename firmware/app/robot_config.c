/**
 * @file    robot_config.c
 * @brief   機器人參數設定（佔位值,待 WP0.4 實機填入）
 */
#include "robot_config.h"
#include <math.h>

#define PI_F 3.14159265358979f
/* EYOU 輸出端 19-bit → 524288 counts/rev → counts/rad（實際以驅動 user-unit 為準） */
#define CPR_19BIT (524288.0f / (2.0f*PI_F))

/* 14 軸 joint_space 設定（左 0..6, 右 7..13）。限位/速度為保守佔位值。 */
static js_joint_cfg_t s_js[JS_TOTAL_JOINTS];

/* 7-DoF 擬人臂 DH（S-R-S 結構）佔位參數。單位：公尺 / 弧度。
 * TODO(WP0.4)：以實機機構（連桿長度、扭轉、偏移）取代。 */
static arm_kin_t s_left = {{
    /*    a       alpha        d       theta_off */
    { 0.0f,  -PI_F/2,  0.10f,   0.0f },   /* J1 肩 */
    { 0.0f,   PI_F/2,  0.0f,    0.0f },   /* J2 肩 */
    { 0.0f,  -PI_F/2,  0.30f,   0.0f },   /* J3 肩 yaw（上臂） */
    { 0.0f,   PI_F/2,  0.0f,    0.0f },   /* J4 肘 */
    { 0.0f,  -PI_F/2,  0.28f,   0.0f },   /* J5 腕（前臂） */
    { 0.0f,   PI_F/2,  0.0f,    0.0f },   /* J6 腕 */
    { 0.0f,   0.0f,    0.08f,   0.0f },   /* J7 腕（末端） */
}, { 0.0f, 0.20f, 0.0f }};                /* 左肩基座：世界 +Y 0.20m */
static arm_kin_t s_right; /* 右臂於 init 複製左臂（實機可鏡像） */

static ik_cfg_t s_ik = {
    .lambda = 0.05f,
    .step_gain = 0.5f,
    .max_iters = 100,
    .pos_tol = 0.001f,   /* 1 mm */
    .rot_tol = 0.01f,    /* ~0.57° */
};

static float s_q_init[ARM_DOF] = { 0.0f, 0.3f, 0.0f, 0.7f, 0.0f, 0.5f, 0.0f };

static int s_inited = 0;
static void ensure_init(void)
{
    if (s_inited) return;
    s_right = s_left;                 /* 佔位：右臂同左臂 DH（實機請填鏡像 DH） */
    s_right.base_p[1] = -0.20f;       /* 右肩基座：世界 -Y 0.20m（與左臂分開 0.4m） */
    for (int j=0;j<JS_TOTAL_JOINTS;j++){
        s_js[j].counts_per_rad = CPR_19BIT;
        s_js[j].q_min = -PI_F;       /* 佔位限位 ±180° */
        s_js[j].q_max =  PI_F;
        s_js[j].vmax  =  2.0f;       /* rad/s 佔位 */
        s_js[j].amax  =  8.0f;       /* rad/s^2 佔位 */
        s_js[j].offset_rad = 0.0f;
    }
    s_inited = 1;
}

const js_joint_cfg_t *robot_js_cfg(void){ ensure_init(); return s_js; }
const arm_kin_t *robot_left_kin(void){ ensure_init(); return &s_left; }
const arm_kin_t *robot_right_kin(void){ ensure_init(); return &s_right; }
const ik_cfg_t *robot_ik_cfg(void){ return &s_ik; }
const float *robot_q_init(void){ return s_q_init; }
