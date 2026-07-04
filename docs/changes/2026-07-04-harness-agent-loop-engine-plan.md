# Harness / Agent / Loop Engine 架構強化規劃

- 日期：2026-07-04
- 分支：`feature/harness-loop-engine`（自 `feature/linux-canopen-master` 切出）
- 類型：docs（設計規劃，無程式碼變更）

## 變更摘要

新增 `docs/design/harness-agent-loop-engine-plan.md`：把「harness（非 RT 監督層）
/ agent（自治週期單元）/ loop engine（RT 週期執行器）」三層架構落到本專案的
PC（Ubuntu 24.04 Pro + PREEMPT_RT）主站與 STM32F746 同源韌體上，作為 CANopen
（方案 C，500 Hz）與 EtherCAT（方案 A/B，1 kHz）**共用**的程式組織層。

內容重點：

1. 現況七個痛點盤點（P1–P7）：`app_main_tick()` 單體管線、無相位分離、無
   per-module 預算、overrun 補跑 burst、故障全域化、RT 迴圈內 printf/stdin、
   頻率編譯期寫死。
2. Loop engine 強化：相位化（BUS_RX→LATCH→COMPUTE→BUS_TX→HOUSEKEEP）、
   overrun SKIP 政策（不補跑）、每 agent WCET 預算與 histogram、多速率
   divisor/錯峰、RT 執行緒調校門檻、trace ring 觀測。
3. Agent 目錄與遷移對照：BusAgent×2、AxisAgent×14、Motion/Safety/Telemetry/
   Health/CommandAgent；`app_main_tick()` 四步驟逐一對映到相位。
4. Harness：生命週期狀態機、升級策略表（per-axis 降級→臂→全系統）、SDO 背景
   通道、cmd/telemetry/log SPSC ring。
5. `bus_if_t` 統一抽象：同一 binary `--bus canopen|ethercat --rate 500|1000`。
6. F746 port 層對映（TIM6 tick 源、DWT 計時、縮小版 harness）。
7. 工作分解 WP-H0～H6 與驗收標準（H1 以 candump 逐幀 diff 作重構迴歸硬驗收）。

## 動機 / 背景

使用者提問「harness agent 要如何設計、如何強化 loop engine、如何應用在韌體」。
現有 loop engine（`pc_master_main.c` 的 clock_nanosleep 迴圈 + `app_main_tick()`
單體管線）在換 EtherCAT、per-axis 降級、抖動歸因上都會卡住；需要一層
bus-agnostic 的架構，讓方案 A/B/C 共用同一套控制程式。

## 影響範圍

- 僅新增文件：`docs/design/harness-agent-loop-engine-plan.md`、本檔、
  `docs/README.md` 索引。
- 不影響任何程式碼與硬體行為；實作將依 WP-H0～H6 另開變更。

## 驗證方式

- 文件審閱：與 `linux-canopen-master-plan.md`（G1–G6）、
  `linux-rt-ethercat-master-plan.md` §3（RT 調校）、WP6 安全語意交叉一致。
- 規劃內驗收標準見設計文件 §9（單元測試 / SIL candump diff / cyclictest 門檻）。

## 關聯

- 分支：`feature/harness-loop-engine`
- 相關文件：`docs/design/linux-canopen-master-plan.md`、
  `docs/design/linux-igh-ethercat-master-plan.md`、
  `docs/design/linux-rt-ethercat-master-plan.md`、
  `docs/design/wp6-safety-wp7-host.md`
- 相關程式碼：`firmware/pc/pc_master_main.c`、`firmware/app/app_main.c`、
  `firmware/app/control_rate.h`
