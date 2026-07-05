# harness 升級策略完整化（§5.2 策略表逐格落地）

- 日期：2026-07-05
- 分支：`feature/h-sdo-bg-escalation`
- 類型：feat + test

## 變更摘要

1. **harness 策略引擎**（`engine/harness.[ch]`）——`hn_cfg_t` 新增策略欄位，
   `hn_supervise()` 依 §5.2 策略表升級：
   - **miss 率視窗**：每次 supervise 計算 (miss+overrun+skip)/ticks，
     超過 `miss_pct_max`（預設 5%）→ 降頻或 SAFE_STOP。
   - **降頻退避**：`degrade_dt_us` 設定時第一次超標走
     `hn_change_rate()`——`deactivate → 改 dt → reactivate`（§5.2 降頻
     語意，非無縫；agent 生命週期完整走過），`on_rate_changed` 通知平台；
     **僅一次機會**，再犯 → SAFE_STOP。未設定 → 直接 SAFE_STOP（原行為）。
   - **bus link 條款**：`hn_feed_health()` 餵入健康快照；link 掉 →
     `restart_bus` 嘗試一次，持續掉 `bus_fail_checks` 次 → SAFE_STOP；
     恢復 → 計數清零、允許再次重啟。
   - **非關鍵 agent 停用**：`noncritical[]` 名單內 agent 連續超預算
     ≥ `agent_over_n`（預設 10）→ `eng_agent_set_enabled(0)` 停用
     （第一層 on_fault 自降級無效之後的第二層）。
2. **engine**：`enabled[]` 旗標 + `eng_agent_set_enabled/enabled()` API，
   run_phase 跳過停用 agent。
3. **pc_master**：health 快照回填 `hn_feed_health`（link 條款接線；
   CANopen 後端 link_ok 目前恆 1，SocketCAN error frame 解析仍為
   後續工作——條款先就緒）。

## 動機 / 背景

§5.2 策略表原本只落地「SAFE_STOP」一格；降頻退避（方案 C EMCY 風暴
對策）、bus 重啟、非關鍵 agent 停用是交叉盤點確認的規格，本次補齊。

## 影響範圍

- 修改：`engine/harness.[ch]`、`engine/loop_engine.[ch]`、
  `pc/pc_master_main.c`；新增 `tests/test_escalation.c`
- 預設行為不變：新欄位全 0 時 = 原「超標即 SAFE_STOP」（g3 迴歸測試把關）。

## 驗證方式

`firmware/tests` 4956 檢查 0 失敗；test_escalation 覆蓋策略表逐格：
overrun 升級→降頻一次→再犯 SAFE_STOP、miss 率視窗（非連續 miss）觸發、
無降頻設定直停（迴歸）、link 掉→重啟→持續掉 SAFE_STOP→恢復清零、
非關鍵 agent 停用（runs 凍結、關鍵 agent 照跑、系統續 RUN）、
降頻生命週期完整性（deact/act 計數、dt 生效、通知、基準重錨定）。
pc_master 建置零警告。

## 關聯

- 設計：`harness-agent-loop-engine-plan.md` §5.2（策略表 + 降頻語意）
- 前置：`301eeec`（SDO 背景通道）、WP-H2 harness 核心
- 待真機：link_ok 的 SocketCAN error frame 實源（bus-off 偵測）
