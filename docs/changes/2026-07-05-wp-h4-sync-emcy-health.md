# WP-H4：SYNC 同步鎖存（G3）+ EMCY 條款（G5）+ 匯流排健康儀表（G6）

- 日期：2026-07-05
- 分支：`feature/harness-loop-engine`
- 類型：feat + test

## 變更摘要

落地方案 C（`linux-canopen-master-plan.md`）三個軟體差距：

1. **G3 SYNC 同步鎖存**：
   - `dual_arm_set_sync(true)`（須在 init 前）→ init 對每軸寫 RPDO1/TPDO1
     transmission type=1（`0x1400/0x1800:02`）；`dual_arm_tick()` 於 BUS_TX
     開頭對每條活 bus 先發 SYNC（`co_pdo_send_sync()`，COB 0x080）再發 RPDO。
   - 從站於 SYNC 邊緣統一鎖存目標/回傳 TPDO → 軸間 skew 從幀序列化散布
     （~1.5 ms）壓到 SYNC 抖動等級（方案 C §5.1 的 tick 結構）。
   - `pc_master --sync` 開啟；預設 off（EYOU 對 type=1 支援待 WP-C2.2 實測，
     不支援則維持 async——設計文件既定的退路）。
   - sim 對應：`phu_sim` 支援 transmission type SDO 與 SYNC 鎖存語意
     （sync 模式下 RPDO 只暫存、SYNC 才套用+回 TPDO）；`co_bxcan_sim`
     廣播 SYNC 給全部節點。
2. **G5 EMCY**（新增 `firmware/canopen/co_emcy.[ch]`）：
   - 解析 `0x081..0x0FF`（code/register/vendor），per-node 快取 + pending
     latch + per-bus 計數；`dual_arm_pump_rx()` 路由。
   - safety 新增 **EMCY 條款**：`safety_report_emcy()`——非零故障碼鎖存
     → `SYS_FAULT`（safe stop）；code `0x0000`（error reset）解除。app 安全
     步驟（`app_main_tick` 與 `app_agents` 兩份逐字一致）以 `co_emcy_take()`
     餵入。
3. **G6 健康儀表**：
   - `firmware/engine/bus_if.h`：bus-agnostic `bus_health_t`（tx_drop/
     rx_lost/err_events/link_ok/sync_ok/load_pct/proto[4]）。
   - `dual_arm_frame_counts()`：per-bus 成功收發幀累計。
   - HealthAgent（`app_io_agents.c`，divisor 100）：busload 估算
     （幀數 × ~130 bits ÷ 視窗，對照 §5.2 預算表）、EMCY 計數、heartbeat
     逾時軸數、SYNC 模式旗標 → health ring；pc_master 狀態列顯示
     `L load=..% emcy=..`。

## 動機 / 背景

使用者指示「做完 H4 繼續 C 的 G3/G5/G6」。三者正是方案 C 上真機前必備的
軟體面：無 SYNC 則 14 軸鎖存 skew 不可控；無 EMCY 則真馬達故障碼靜默丟失；
無儀表則 90% 負載運行等於盲飛。

## 影響範圍

- 新增：`canopen/co_emcy.[ch]`、`engine/bus_if.h`、`tests/test_h4.c`
- 修改：`app/dual_arm.[ch]`（sync/計數/EMCY 路由）、`safety/safety.[ch]`
  （EMCY 條款）、`app/app_main.c`+`app/app_agents.c`（安全步驟餵 EMCY）、
  `app/app_io_agents.[ch]`（HealthAgent）、`canopen/co_pdo.[ch]`（send_sync）、
  `sim/phu_sim.[ch]`+`sim/co_bxcan_sim.c`（SYNC 語意+故障注入）、
  `pc/pc_master_main.c`（--sync+儀表）、兩份 Makefile、`pc/README.md`
- 預設行為不變：sync off 時幀流與先前完全一致（test_agents 逐幀 diff 迴歸
  通過）；EMCY 條款只在真的收到 EMCY 幀時作用。

## H4 範圍內「明確遞延」項

| 項 | 去向 |
| --- | --- |
| `bus_if_t` vtable | H5（第二後端出現時定案簽名，`bus_if.h` 註明） |
| SDO 背景通道（RUN 中讀參數） | H5 前置或獨立小 WP |
| 降頻退避（§5.2 deactivate→reactivate） | 併入 H3/H4 後續（需 RT 真機驗證切換中斷） |
| SocketCAN error frame / bus-off 偵測 | WP-C2 真機期（`link_ok` 目前恆 1，已註明） |
| `sim_py/can_slave.py` 的 SYNC 支援 | vcan SIL 恢復時補（C 假硬體已覆蓋語意） |

## 驗證方式

- `firmware/tests`：**4768 檢查 0 失敗**。`test_h4`：EMCY 單元＋注入→
  `SYS_FAULT`→error reset 復歸；busload 滿視窗估算落在 75–100%（理論 ~91%）；
  SYNC 模式 300 tick 恰 300 個 SYNC、SYNC 永遠先於 RPDO、TPDO 恰 300×7
  只在 SYNC 邊緣、14 軸使能與運動照常。
- H1 迴歸：`test_agents` 逐幀 diff 通過（預設 async 幀流不變）。
- `firmware/pc`：建置零警告；`--sync` 旗標與儀表狀態列就緒（真機/vcan 實測
  依 WP-C2 計畫）。

## 關聯

- 前置：`ac664c7`（WP-H2）
- 設計：`docs/design/harness-agent-loop-engine-plan.md` §3.2/§6、§9 WP-H4；
  `docs/design/linux-canopen-master-plan.md` §5.1/§6（G3/G5/G6）
- 銜接：WP-C2.2（EYOU 對 transmission type=1 的真機實測）、WP-C2.3（skew
  量測，candump -H 硬體時戳）
