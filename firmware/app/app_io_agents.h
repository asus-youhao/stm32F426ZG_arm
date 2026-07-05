/**
 * @file    app_io_agents.h
 * @brief   WP-H2：RT ↔ 非 RT 交界的命令/遙測 agent（SPSC ring）
 *
 * 設計見 docs/design/harness-agent-loop-engine-plan.md §5.3：
 *   cmd ring（非 RT push → CommandAgent 於 HOUSEKEEP 相位消化）
 *   telemetry ring（TelemetryAgent 於 HOUSEKEEP 產生 → 非 RT pop）
 * printf/stdin 從此完全離開 RT 路徑（解 P6）。
 * 註冊需在 app_agents_register() 之後（HOUSEKEEP 相位順序在 bus 之後）。
 */
#ifndef APP_IO_AGENTS_H
#define APP_IO_AGENTS_H

#include "loop_engine.h"
#include "bus_if.h"
#include <stdbool.h>

/* ---- 命令（非 RT → RT）---- */
enum { APP_CMD_JOINT_MOVE = 1,  /* idx=關節 0..13, val=rad */
       APP_CMD_ESTOP,           /* val!=0 → 急停 on */
       APP_CMD_MODE };          /* idx=da_mode_t */

typedef struct {
    uint8_t op;
    uint8_t idx;
    float   val;
} app_cmd_t;

/* ---- 遙測快照（RT → 非 RT）---- */
#define APP_TELE_NJ 14      /* 雙臂 14 軸（ARM_COUNT × JOINTS_PER_ARM） */
typedef struct {
    uint64_t tick;          /* engine tick 計數 */
    uint8_t  sys_state;     /* sys_state_t */
    uint16_t sw[APP_TELE_NJ];            /* 每軸 statusword */
    int32_t  pos[APP_TELE_NJ];           /* 每軸實際 counts */
    int32_t  tgt[APP_TELE_NJ];           /* 每軸目標 counts */
    uint32_t tx_drops;      /* dual_arm 發送丟棄 */
    uint32_t late_max_us;   /* engine 遲到最大值（累計） */
    uint64_t miss;          /* miss + overrun 合計 */
    float    lpos[3], rpos[3]; /* 左/右末端位置（m） */
} app_tele_t;

/* ---- bus 健康快照（RT → 非 RT;WP-H4/G6）---- */
typedef struct {
    uint8_t      bus;    /* co_bus_t */
    bus_health_t h;
} app_health_t;

/**
 * @brief 註冊 CommandAgent（divisor 10）、TelemetryAgent（divisor 5）、
 *        HealthAgent（divisor 100,per-bus busload/EMCY/heartbeat 儀表）。
 * @param e 已 eng_init 的 engine（讀 stats 與 dt）。
 */
int app_io_register(loop_engine_t *e);

/* 非 RT 端 API */
bool     app_io_cmd_push(const app_cmd_t *c);   /* 滿回 false */
bool     app_io_tele_pop(app_tele_t *t);        /* 空回 false */
bool     app_io_health_pop(app_health_t *h);    /* 空回 false */
uint32_t app_io_tele_drops(void);               /* RT 端 ring 滿丟棄計數 */

#endif /* APP_IO_AGENTS_H */
