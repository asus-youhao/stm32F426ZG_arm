# WP-H1：app_main_tick() agent 化重構（行為不變）+ 重複 init 殘留狀態修正

- 日期：2026-07-04
- 分支：`feature/harness-loop-engine`
- 類型：refactor + fix + test

## 變更摘要

1. **agent 化重構（WP-H1 本體）**：新增 `firmware/app/app_agents.[ch]`，把
   `app_main_tick()` 的四步驟拆成四個 agent 掛上 WP-H0 的 loop engine：

   | Agent | 相位 | 對應原步驟 |
   | --- | --- | --- |
   | `bus_rx` | cycle_read | 步驟 1 前半 `dual_arm_pump_rx()` |
   | `motion` | cycle_read / compute / write | 步驟 1 後半回灌、步驟 3 L4→L2、步驟 4 前半 set_target |
   | `safety` | cycle_compute | 步驟 2 看門狗 + safe stop 判定 |
   | `bus_tx` | cycle_write（最後） | 步驟 4 後半 `dual_arm_tick()` PDO 下發 |

   註冊順序保證各相位內執行序列與原單體版完全一致。`app_main_tick()` 原樣保留
   （board/main.c 與 pc_master 尚未切換，切換屬 WP-H2）。

2. **逐幀 diff 硬驗收**：新增 `firmware/tests/test_agents.c`。在 C 假硬體
   （`co_bxcan_sim` + `phu_sim`，新增 `sim_bus_set_tap()` 幀記錄鉤子＝確定性
   candump）上，以同一刺激序列（關節命令、急停/復歸）各跑 1200 tick：
   - A) 舊版重跑兩輪 → 驗證確定性（diff 法的前提）
   - B) 舊版 vs agent 化 → **33,858 幀逐 byte 比對一致**

3. **修正：重複 init 的殘留狀態 bug（A 輪抓到的真 bug）**：
   - `dual_arm_init()` 原本不清 `g_jstate[]`／`s_safe_stop`／`s_tx_drops`。
     行程內第二次 init（harness 生命週期的 bus 重啟情境）時，殘留的
     `enabled=true`/`statusword=0x27` 會讓主站對剛重置的從站**直接下
     CW_ENABLE_OP(0x0F)，跳過 CiA402 使能交握**。現於 init 開頭全部清零。
   - 配套新增 `co_pdo_reset()`、`co_nmt_reset_cache()`（清回授/心跳快取），
     由 `dual_arm_init()` 呼叫。
   - `app_main.c` 新增 `app_ctrl()`/`app_is_ready()` 內部存取（app_agents 用）。

## 動機 / 背景

依 `docs/design/harness-agent-loop-engine-plan.md` §4.2 與 §9（WP-H1）：重構
必須「行為不變」，驗收方式即逐幀 diff。diff 法先驗自身確定性，意外抓出
重複 init 的殘留狀態問題——這正是 harness 生命週期（重啟/重配置）上線前
必須修掉的。

## 影響範圍

- 新增：`firmware/app/app_agents.[ch]`、`firmware/tests/test_agents.c`
- 修改：`firmware/app/dual_arm.c`（init 清零）、`firmware/app/app_main.c`
  （內部存取 API）、`firmware/canopen/co_pdo.[ch]`、`co_nmt.[ch]`（reset API）、
  `firmware/sim/co_bxcan_sim.c`（frame tap）、`firmware/tests/`（掛新測試；
  `test_engine.c` 假時鐘改共用全域 `g_fake_now_us`）
- **執行期行為**：單次 init 的行為完全不變（清零的都是 BSS 初值）；
  唯一行為變化是「重複 init」從錯誤（跳過交握）變為正確（完整交握）。
- 不影響硬體腳位/時脈設定。

## 驗證方式

- `firmware/tests`：`make && ./unit_tests` → **4716 檢查 0 失敗**
  （含 test_agents 兩階段逐幀 diff）。
- `firmware/pc`：`make` pc_master 建置通過（dual_arm.c 修改相容）。
- F746 target build 未驗：`firmware/Makefile` HAL 路徑為 Windows 路徑
  （`C:/Users/u/STM32Cube/...`），本機無法建置——既有問題，與本次無關。

## 關聯

- 前置：commit `f77abf0`（WP-H0 loop engine 核心）
- 設計：`docs/design/harness-agent-loop-engine-plan.md` §4、§9
- 下一步：WP-H2（harness 生命週期 + pc_master 切換到 engine 驅動 +
  cmd/telemetry ring 取代迴圈內 stdin/printf）
