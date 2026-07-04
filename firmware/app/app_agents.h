/**
 * @file    app_agents.h
 * @brief   WP-H1：把 app_main_tick() 拆成 agent 掛上 loop engine
 *
 * 使用：app_main_init() 成功後呼叫 app_agents_register()，
 * 之後由 eng_configure/eng_activate/eng_tick 驅動（取代 app_main_tick）。
 * 行為與 app_main_tick() 逐幀一致（驗收見 tests/test_agents.c）。
 */
#ifndef APP_AGENTS_H
#define APP_AGENTS_H

#include "loop_engine.h"

/** @brief 把四個 agent（bus_rx/safety/motion/bus_tx）依序註冊進 engine；回 0 成功。 */
int app_agents_register(loop_engine_t *e);

#endif /* APP_AGENTS_H */
