/**
 * @file    trajectory.h
 * @brief   單關節軌跡插值（梯形速度 / 五次多項式）
 *
 * 在 1 kHz tick 下,每次呼叫 step() 取得下一個位置設定點（rad）。
 */
#ifndef TRAJECTORY_H
#define TRAJECTORY_H

#include <stdbool.h>

typedef enum { TRAJ_TRAP, TRAJ_QUINTIC } traj_type_t;

typedef struct {
    traj_type_t type;
    float q0, qf;        /* 起點/終點 (rad) */
    float vmax, amax;    /* 限制（梯形用） */
    float T;             /* 總時程 (s) */
    float t;             /* 已經過時間 (s) */
    float dt;            /* tick 週期 (s)，如 0.001 */
    bool  active;
    /* 五次多項式係數 */
    float a0,a1,a2,a3,a4,a5;
    /* 梯形分段 */
    float ta, tc;        /* 加速時間、等速時間 */
    float dir;
    float q_cmd, v_cmd;  /* 目前輸出位置/速度 */
} traj_t;

/** @brief 規劃梯形軌跡（自動依 vmax/amax 算 T）。 */
void traj_plan_trap(traj_t *tr, float q0, float qf, float vmax, float amax, float dt);

/** @brief 規劃五次多項式軌跡（指定總時程 T,起終速度/加速度為 0）。 */
void traj_plan_quintic(traj_t *tr, float q0, float qf, float T, float dt);

/**
 * @brief 前進一個 tick。
 * @return 目前位置設定點 (rad)。完成後 active=false 並維持 qf。
 */
float traj_step(traj_t *tr);

/** @brief 是否仍在執行。 */
static inline bool traj_active(const traj_t *tr) { return tr->active; }

#endif /* TRAJECTORY_H */
