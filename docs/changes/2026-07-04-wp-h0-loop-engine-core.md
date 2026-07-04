# WP-H0：loop engine 核心 + SPSC ring + 單元測試

- 日期：2026-07-04
- 分支：`feature/harness-loop-engine`
- 類型：feat（新增模組，未接入既有控制路徑）

## 變更摘要

依 `docs/design/harness-agent-loop-engine-plan.md` §9 WP-H0，新增
`firmware/engine/`（平台無關、零 malloc、零鎖）：

| 檔案 | 內容 |
| --- | --- |
| `eng_port.h` | 平台移植層：僅 `port_now_us()` 一個函式（PC=clock_gettime、F746=DWT、測試=假時鐘） |
| `agent.h` | agent 統一介面：生命週期（configure/activate/deactivate，非 RT）+ 四相位 callback（read/compute/write/housekeep，RT）+ `on_fault`；`eng_cfg_t` 執行期設定 |
| `loop_engine.[ch]` | RT 週期執行器：遲到分級（正常/miss/overrun）、**overrun SKIP 政策**（不補跑、deadline 重錨定 now+dt、連續 N 次通知 harness）、divisor/phase_offset 多速率錯峰、每 agent 各相位耗時 max + compute histogram + WCET 預算（連續超標 → `AG_FAULT_BUDGET`）、activate 失敗反序回滾 |
| `spsc_ring.[ch]` | 單生產者/單消費者 lock-free ring（C11 atomics、free-running head/tail、容量 2 的冪），RT↔非 RT 域交界用 |

測試：`firmware/tests/test_engine.c`（`test_engine` + `test_spsc` 兩個 RUN），
掛入既有 `tests/Makefile` 與 `test_main.c`。

## 動機 / 背景

設計文件盤點的痛點 P1–P7 中，P2（無相位）、P3（無 per-module 預算）、
P4（overrun 補跑 burst）、P7（頻率編譯期寫死）都在 engine 層解；
本 WP 先落地 engine 核心與跨域佇列，H1 才把 `app_main_tick()` 拆成 agent。

## 影響範圍

- 新增 `firmware/engine/`（5 檔）與 `firmware/tests/test_engine.c`。
- 修改 `firmware/tests/Makefile`、`firmware/tests/test_main.c`（掛新測試）。
- **未接入任何既有控制路徑**：`pc_master`、`board/main.c`、`app_main_tick()`
  行為完全不變；不影響硬體行為。

## 驗證方式

- `cd firmware/tests && make && ./unit_tests`：**4697 檢查、0 失敗（PASS）**，
  其中 engine/spsc 覆蓋：
  - 生命週期：configure/activate 順序、activate 中途失敗反序回滾、
    ACTIVE 中禁註冊；
  - 相位順序：所有 read → 所有 compute → 所有 write → housekeep，
    同相位依註冊順序；
  - divisor/phase_offset：div4/off1 於 tick 1,5 執行、div2 於 0,2,4,6；
  - 遲到分級：warn≤late<dt 記 miss 且 deadline 照排；late≥dt 記 overrun、
    skipped=late/dt、deadline 重錨定 now+dt、**agent 只多跑一次（無 burst）**；
  - 升級：連續 3 次 overrun 通知 harness 一次；復原後新 streak 再通知；
  - 預算：連續 3 次超 WCET → `on_fault(AG_FAULT_BUDGET)` 一次、
    histogram/max/total 正確、達標歸零重計；
  - SPSC：非 2 冪容量拒絕、滿/空邊界、FIFO、1000 次回繞、交錯滿載。
- ARM 交叉編譯把關：`arm-none-eabi-gcc -mcpu=cortex-m7 -std=c11 -Wall -Wextra`
  編譯 `loop_engine.c`、`spsc_ring.c` 無警告（防 PC/F746 漂移）。

## 關聯

- 設計文件：`docs/design/harness-agent-loop-engine-plan.md`（§3/§4/§9 H0）
- 後續：WP-H1 將以 SIL candump 逐幀 diff 為驗收，把 `app_main_tick()`
  拆成 Bus/Axis/Motion/Safety/Telemetry/Health/Command agent。
