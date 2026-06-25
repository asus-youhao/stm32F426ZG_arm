/**
 * @file    joint_space.c
 * @brief   L2 Joint-space 控制器實作
 */
#include "joint_space.h"
#include <string.h>
#include <math.h>

static float s_dt = 0.001f;
static js_joint_cfg_t s_cfg[JS_TOTAL_JOINTS];
static traj_t s_traj[JS_TOTAL_JOINTS];
static float  s_cmd[JS_TOTAL_JOINTS];   /* 目前命令 (rad) */
static float  s_fb[JS_TOTAL_JOINTS];    /* 回授 (rad) */
static bool   s_direct[JS_TOTAL_JOINTS]; /* true=外部直接設定點模式 */

static float clampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

void js_init(float dt, const js_joint_cfg_t cfg[JS_TOTAL_JOINTS])
{
    s_dt = dt;
    memcpy(s_cfg, cfg, sizeof(s_cfg));
    memset(s_traj, 0, sizeof(s_traj));
    memset(s_cmd, 0, sizeof(s_cmd));
    memset(s_fb, 0, sizeof(s_fb));
    memset(s_direct, 0, sizeof(s_direct));
}

int32_t js_rad_to_counts(int j, float rad)
{
    return (int32_t)lrintf((rad + s_cfg[j].offset_rad) * s_cfg[j].counts_per_rad);
}
float js_counts_to_rad(int j, int32_t counts)
{
    return ((float)counts / s_cfg[j].counts_per_rad) - s_cfg[j].offset_rad;
}

void js_move_to(int j, float q)
{
    if (j < 0 || j >= JS_TOTAL_JOINTS) return;
    q = clampf(q, s_cfg[j].q_min, s_cfg[j].q_max);
    s_direct[j] = false;
    traj_plan_trap(&s_traj[j], s_cmd[j], q, s_cfg[j].vmax, s_cfg[j].amax, s_dt);
}

void js_move_to_quintic(int j, float q, float T)
{
    if (j < 0 || j >= JS_TOTAL_JOINTS) return;
    q = clampf(q, s_cfg[j].q_min, s_cfg[j].q_max);
    s_direct[j] = false;
    traj_plan_quintic(&s_traj[j], s_cmd[j], q, T, s_dt);
}

void js_set_setpoint(int j, float q)
{
    if (j < 0 || j >= JS_TOTAL_JOINTS) return;
    s_direct[j] = true;
    s_traj[j].active = false;
    s_cmd[j] = clampf(q, s_cfg[j].q_min, s_cfg[j].q_max);
}

void js_update_feedback(int j, int32_t counts)
{
    if (j < 0 || j >= JS_TOTAL_JOINTS) return;
    s_fb[j] = js_counts_to_rad(j, counts);
}

void js_tick_1khz(int32_t out_counts[JS_TOTAL_JOINTS])
{
    for (int j = 0; j < JS_TOTAL_JOINTS; j++) {
        if (!s_direct[j]) {
            if (s_traj[j].active) s_cmd[j] = traj_step(&s_traj[j]);
        }
        /* 限位保險夾制（即使外部直接設定也保護） */
        s_cmd[j] = clampf(s_cmd[j], s_cfg[j].q_min, s_cfg[j].q_max);
        if (out_counts) out_counts[j] = js_rad_to_counts(j, s_cmd[j]);
    }
}

float js_get_cmd(int j)
{ return (j >= 0 && j < JS_TOTAL_JOINTS) ? s_cmd[j] : 0.0f; }

bool js_all_idle(void)
{
    for (int j = 0; j < JS_TOTAL_JOINTS; j++)
        if (!s_direct[j] && s_traj[j].active) return false;
    return true;
}
