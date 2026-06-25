# 修復三項嚴重問題：500Hz / 看門狗 / 可建置目標韌體

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

針對深度分析指出的三項嚴重問題進行修復。

### 1. 頻寬：1 kHz → **500 Hz** + 丟幀偵測
- 新增 `firmware/app/control_rate.h`（`CONTROL_HZ=500`、`CONTROL_DT=0.002`），並附頻寬計算依據。
- `app_main` 改用 `CONTROL_DT`；TIM6 週期改 2 ms（500 Hz）。
- `dual_arm.c`：檢查 `co_pdo_send_csp` 回傳值，mailbox 滿（CO_ERR_TX）累計丟幀 → `dual_arm_tx_drops()`。
- 理由：Classic CAN @1Mbps 單臂 7 軸 1 kHz = 14000 frame/s 超載；500 Hz = 7000/s 為可行邊界。1 kHz 須走 EtherCAT。

### 2. 通訊看門狗修復（WP6 安全 bug）
- 原 bug：`app_main` 每 tick 無條件 `safety_report_joint`，`last_ms` 永遠刷新 → 失聯偵測失效。
- 修法：
  - `co_pdo` 加回授序號 `seq` + `co_pdo_feedback_seq()`（每收到 TPDO 遞增）。
  - `dual_arm` 比較序號 → `joint_state_t.fb_fresh`（本 tick 是否真的收到新回授）。
  - `app_main` **僅對 `fb_fresh` 的軸**更新看門狗時間戳。
  - `safety.c`：`last_ms==0`（從未收過回授）不視為失聯；已活過再失聯則會被偵測 → FAULT。

### 3. 可建置目標韌體
- 新增 `firmware/CMakeLists.txt`：
  - HOST 路徑（系統 gcc）→ 建模擬器 `phu_sim_demo`（已驗證可 configure+build+run）。
  - TARGET 路徑（交叉）→ 建 `firmware.elf/.bin/.hex`（需 arm-none-eabi + CubeMX HAL）。
- 新增 `firmware/cmake/gcc-arm-none-eabi.cmake`（Cortex-M7F 工具鏈）。
- 新增 `firmware/linker/STM32F746ZGTx_FLASH.ld`（1MB Flash / 320KB RAM）。
- 新增 `firmware/target/README.md`（CubeMX HAL/startup 放置與建置/燒錄流程）。

## 影響範圍

- 修改：`app_main.c`、`dual_arm.[ch]`、`co_pdo.[ch]`、`safety.c`、`sim/sim_main.c`、`firmware/README.md`、wp2-5 文件（函式改名 `dual_arm_tick`）。
- 新增：`control_rate.h`、`CMakeLists.txt`、`cmake/`、`linker/`、`target/README.md`。
- 函式更名：`dual_arm_tick_1khz` → `dual_arm_tick`。

## 驗證方式

- HOST：`cmake -S firmware -B firmware/build && cmake --build firmware/build && ./firmware/build/phu_sim_demo`，
  資料流正常、丟幀=0（模擬無 mailbox 限制）。
- `cd firmware/sim && make` 亦通過。
- TARGET：CMake 設定正確，待 arm-none-eabi + CubeMX HAL 方能實際產出 .bin。

## 仍未解（需硬體 / 後續）

- 硬體 STO 整合、實機 HIL 與效能基準。
- C/Python 運動學重複實作（建議加交叉對拍測試）。
- 機構 DH / 馬達參數仍為佔位值。

## 關聯

- 深度分析（對話）、`can-bus-architecture.md`、`canopen-vs-ethercat.md`、`wp6-safety-wp7-host.md`
