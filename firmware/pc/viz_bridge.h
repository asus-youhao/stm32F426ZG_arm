/**
 * @file    viz_bridge.h
 * @brief   pc_master ↔ ws_server 視覺化橋（UDP,非 RT;項目 4）
 *
 * 資料面：主執行緒把 telemetry ring 的快照轉 JSON,發給最近一個來訊的
 * 對端（ws_server --bridge 每秒送 "hello" 訂閱,順便當保活）。
 * 命令面：對端送與 stdin 相同的文字命令（"j 3 0.4"、"e 1"…）,
 * 由 pc_master 的同一個 handle_cmd 解析 → cmd ring → RT 域。
 * RT 路徑零觸碰：socket 只活在主執行緒。
 */
#ifndef VIZ_BRIDGE_H
#define VIZ_BRIDGE_H

#include "app_io_agents.h"
#include <stdbool.h>
#include <stddef.h>

/** @brief 開 UDP socket（bind 127.0.0.1:port,non-blocking）。失敗回 -1。 */
int viz_open(int port);

/**
 * @brief 收一個 datagram（非阻塞）。任何來訊都更新對端位址（訂閱）;
 *        若內容是命令行（非 "hello"）,複製進 line 並回 true。
 */
bool viz_poll_cmd(char *line, size_t cap);

/** @brief 遙測快照 → JSON → 送對端（尚無對端時 no-op）。 */
void viz_send_tele(const app_tele_t *t, const char *sys_str);

void viz_close(void);

#endif /* VIZ_BRIDGE_H */
