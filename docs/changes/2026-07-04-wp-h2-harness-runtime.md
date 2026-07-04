# WP-H2：harness 監督層 + cmd/telemetry ring + pc_master 切 engine 驅動

- 日期：2026-07-04
- 分支：`feature/harness-loop-engine`
- 類型：feat + refactor + test

## 變更摘要

1. **harness 核心**（`firmware/engine/harness.[ch]`，platform-free）：
   生命週期 `BOOT→CONFIGURED→RUN→SAFE_STOP→SHUTDOWN`（DEGRADED 策略屬
   WP-H4）；`hn_supervise()` 監督兩件事——engine tick 心跳停滯（連續 N 次
   無進展）與連續 overrun 升級（接 `eng_cfg_t.on_escalate`），觸發即呼叫
   `enter_safe_stop` callback（非 RT、可阻塞、可直接對 bus 動作）。
2. **IO agents**（`firmware/app/app_io_agents.[ch]`）：
   - cmd ring（16 格）：非 RT push → CommandAgent（divisor 10）於 HOUSEKEEP
     消化（JOINT_MOVE/ESTOP/MODE），每 tick 上限 4 筆。
   - telemetry ring（64 格）：TelemetryAgent（divisor 5，@500 Hz≈100 Hz）
     產生快照（tick、sys 狀態、J0 sw/pos/tgt、tx drops、late/miss、雙臂末端
     位姿）；ring 滿丟新留舊並計數——消費端須定期抽取。
   - 兩個 agent 只掛 HOUSEKEEP，不碰 bus——H1 逐幀 diff 驗收對其依然成立。
3. **pc_master 切換 engine 驅動**（`firmware/pc/pc_master_main.c` 重寫）：
   - RT 執行緒：`clock_nanosleep(TIMER_ABSTIME)` 睡到 `eng_next_deadline_us()`
     → `eng_tick()`；嘗試 SCHED_FIFO 80 + `mlockall`（無權限降級並警告）。
     **RT 路徑零 printf/零 stdin**（解 P6）。
   - 主執行緒（harness）：50 Hz 輪詢 stdin → 解析後 push cmd ring；抽
     telemetry ring 印 1 Hz 狀態列；10 Hz `hn_supervise`。
   - 最後防線：監督觸發 SAFE_STOP 時 `safety_set_estop` + 直接廣播 NMT stop
     （RT 執行緒已死也停得下來）。操作員 `e 1/0` 則走 cmd ring → RT 域
     safety quick-stop（**可逆**，與 NMT stop 分開，避免 e 0 復歸不了）。
   - `--rate 100..1000` 執行期頻率（解 P7；對應 WP-C3.2 400/500 檔位化），
     配套 `app_main_init_hz(float)`（`app_main.c`，`js_init` 用 dt=1/hz）。
   - `port_now_us()` 落在 `hal_linux.c`（與 RT 迴圈共用 CLOCK_MONOTONIC 時基）。
4. **測試**（`firmware/tests/test_harness.c`）：harness 狀態轉移守門、
   心跳停滯→SAFE_STOP（含不重複觸發）、overrun 升級→SAFE_STOP、
   IO agents E2E（完整 app 棧 + C 假硬體：cmd ring 下 JOINT_MOVE 目標移動、
   telemetry 快照 tick/位姿合理、estop 經 ring 生效）。

## 動機 / 背景

設計文件 §5（harness）與 WP-H2：printf/stdin 在 RT tick 路徑上（痛點 P6）、
頻率編譯期寫死（P7）、無監督層。本次把 RT / 非 RT 分域落地，pc_master 從
單執行緒單體迴圈變成 engine 驅動的雙執行緒架構。

## 影響範圍

- 新增：`firmware/engine/harness.[ch]`、`firmware/app/app_io_agents.[ch]`、
  `firmware/tests/test_harness.c`
- 修改：`firmware/pc/pc_master_main.c`（重寫）、`firmware/pc/Makefile`
  （+engine/agents、-lpthread）、`firmware/pc/hal_linux.c`（port_now_us）、
  `firmware/pc/README.md`、`firmware/app/app_main.c`（app_main_init_hz）、
  `firmware/tests/`（掛新測試）
- 行為變化：pc_master 狀態列改由遙測快照驅動（內容等價）；`--seconds` 改為
  牆鐘秒數（原為 tick 數換算，語意相同）；新增 `--rate`。控制語意（使能、
  safety、safe stop）不變。
- 不影響板端/硬體行為（F746 port 屬 WP-H6）。

## 驗證方式

- `firmware/tests`：`make && ./unit_tests` → **4741 檢查 0 失敗**（新增
  test_harness 25 項；含 H1 逐幀 diff 迴歸，確認 IO agents 不影響 bus 行為）。
- `firmware/pc`：`make` 零警告；`--help`／`--rate 50`（拒絕）／無 vcan 時
  BUS_UP 優雅失敗（rc=1）已實測。
- **待真機/有權環境驗**（本機無免密 sudo 建不了 vcan）：vcan SIL 全流程、
  「stdout 塞住 tick 不受影響」實測（結構上已保證：RT 執行緒無 printf）、
  SCHED_FIFO 抖動數據（屬 WP-H3 驗收）。

## 關聯

- 前置：`f77abf0`（WP-H0）、`c872e55`（WP-H1）、`7306750`（交叉盤點）
- 設計：`docs/design/harness-agent-loop-engine-plan.md` §2/§5、§9 WP-H2
- 下一步：WP-H3（RT 真機抖動驗收 + trace ring dump 工具）或
  WP-H4（bus_if_t/bus_health_t、SYNC 相位化、EMCY）
