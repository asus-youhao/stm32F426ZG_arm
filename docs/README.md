# 文件索引

本目錄收錄專案所有文件。**每次改動都需在 [`changes/`](./changes) 新增一份變更文件**，並更新本索引。

## 文件規範

- 變更文件命名：`changes/YYYY-MM-DD-<簡短描述>.md`
- 必含章節：變更摘要、動機/背景、影響範圍、驗證方式、關聯
- 詳細規範見 [CLAUDE.md](../CLAUDE.md) 的「文件撰寫規範」。

## 變更紀錄

| 日期       | 文件                                                                 | 摘要                              |
| ---------- | ------------------------------------------------------------------- | --------------------------------- |
| 2026-06-25 | [初始化專案文件與 Git Flow](./changes/2026-06-25-init-project-docs.md) | 建立 CLAUDE.md、Git Flow 與文件規範 |
| 2026-06-25 | [STM32F746ZG 雙臂低階控制](./changes/2026-06-25-stm32f746zg-dual-arm-control.md) | 新增雙臂低階控制需求/架構文件 |
| 2026-06-25 | [CAN/CAN-FD 匯流排架構](./changes/2026-06-25-can-bus-architecture.md) | 評估 CAN-FD 支援與雙臂頻寬/拓樸 |
| 2026-06-25 | [CANopen 支援說明](./changes/2026-06-25-canopen-support-note.md) | 補充 CANopen / CANopen FD 支援情況 |
| 2026-06-25 | [EYOU PHU 馬達分析](./changes/2026-06-25-eyou-phu-motor-analysis.md) | 判定 PHU CANopen 為 Classic CAN |
| 2026-06-25 | [雙臂配置與控制規劃](./changes/2026-06-25-dual-arm-config-and-plan.md) | 關節表、CoE 說明、雙臂控制工作分解 |
| 2026-06-25 | [協定與模式 HTML 速覽](./changes/2026-06-25-protocols-modes-html.md) | 新增可瀏覽的協定/控制模式速覽頁 |
| 2026-06-25 | [F746 CANopen 韌體](./changes/2026-06-25-f746-canopen-firmware.md) | 雙臂雙 CAN channel CANopen 通訊骨架 |
| 2026-06-25 | [CubeMX 整合 + WP2 bring-up](./changes/2026-06-25-cubemx-integration-wp2-bringup.md) | 接線範本與單軸 bring-up 測試 |
| 2026-06-25 | [WP3 Joint-space 控制器](./changes/2026-06-25-wp3-joint-space.md) | 軌跡插值 + 14 軸 1kHz 設定點 |
| 2026-06-25 | [WP4 Task-space 控制器](./changes/2026-06-25-wp4-task-space.md) | FK/Jacobian/DLS-IK + 笛卡爾控制 |
| 2026-06-25 | [WP5 雙臂整合](./changes/2026-06-25-wp5-dual-arm-integration.md) | L4 協同 + L1–L4 全棧串接 |
| 2026-06-25 | [假硬體模擬器(C)](./changes/2026-06-25-sim-fake-hardware.md) | PC 全棧 + 模擬 PHU 從站資料流 |
| 2026-06-25 | [WP6 安全 + WP7 上位機](./changes/2026-06-25-wp6-safety-wp7-host.md) | 安全狀態機/急停 + 命令遙測協定 |
| 2026-06-25 | [Python 假硬體](./changes/2026-06-25-python-fake-hardware.md) | 馬達讀寫 + 扭矩/電流 + URDF-ready |
| 2026-06-25 | [完整OD+測試上位機](./changes/2026-06-25-full-od-test-host.md) | 全物件字典假馬達 + CANopen 讀寫上位機 |
| 2026-06-25 | [前端統一接後端(WS)](./changes/2026-06-25-ws-unified-frontend.md) | host_ui/can_monitor 連同一 Python 假硬體 |
| 2026-06-25 | [修復 500Hz/看門狗/可建置](./changes/2026-06-25-fix-500hz-watchdog-buildable.md) | 三項嚴重問題修復 + CMake 目標建置 |
| 2026-06-25 | [單元測試](./changes/2026-06-25-unit-tests.md) | trajectory/kinematics/IK/協定/CANopen 測試 |
| 2026-06-26 | [F746 Makefile bring-up 專案](./changes/2026-06-26-f746-makefile-bringup-build.md) | 可編譯/可燒的 Nucleo-F746ZG bring-up + CAN 時序修正 |
| 2026-07-01 | [C1：CANable + Python 假從站](./changes/2026-07-01-c1-canable-python-slave.md) | F746 主站對打 PC 假 CiA402 從站（python-can）+ PV 模型 |
| 2026-07-01 | [C1 上位機 web 看板](./changes/2026-07-01-web-monitor-14axis.md) | 標準庫 SSE 即時看板，畫雙臂 14 軸 pos/扭矩/電流/狀態 |
| 2026-07-01 | [全面忠實 CiA402 模擬器](./changes/2026-07-01-full-cia402-simulator.md) | 8 模式 + 完整狀態機 + Homing + 故障 + 手冊抽出 OD + web 控制台 |
| 2026-07-01 | [Web CANopen 資料流](./changes/2026-07-01-web-can-dataflow.md) | 選取 motor 的即時 rx/tx 幀流面板（RPDO/TPDO/SDO 解析）|
| 2026-07-02 | [bringup_decode 跨平台(WSL/Ubuntu)](./changes/2026-07-02-bringup-decode-linux-slcan.md) | 補回 can_slave + slcan 自動偵測 + WSL/Ubuntu 啟動腳本 |

## 設計文件

| 文件                                                          | 說明                                  |
| ------------------------------------------------------------ | ------------------------------------- |
| [Dual-Arm Low-Level Control](./design/dual-arm-low-level-control.md) | STM32F746ZG 雙臂低階控制需求/架構概述 |
| [CAN/CAN-FD Bus Architecture](./design/can-bus-architecture.md) | CAN-FD 支援評估與雙臂匯流排拓樸/頻寬   |
| [EYOU PHU Motor Analysis](./design/eyou-phu-motor-analysis.md) | EYOU PHU 關節選型與 CAN 類型判定（依原廠 datasheet） |
| [CANopen vs EtherCAT](./design/canopen-vs-ethercat.md) | CANopen / EtherCAT / CoE 比較與通訊選型 |
| [Dual-Arm Control Plan](./design/dual-arm-control-plan.md) | 雙臂控制工作分解（joint/task-space 1kHz、整合） |
| [CubeMX 整合說明](./design/firmware-cubemx-integration.md) | F746 CubeMX 設定與韌體接線 |
| [WP2 單軸 Bring-up](./design/wp2-single-axis-bringup.md) | SDO 驗證 + 單軸轉動測試流程 |
| [WP3 Joint-space](./design/wp3-joint-space.md) | 軌跡插值與 joint-space 控制器 |
| [WP4 Task-space](./design/wp4-task-space.md) | 運動學/Jacobian/IK 與笛卡爾控制 |
| [WP5 雙臂整合](./design/wp5-dual-arm-integration.md) | L4 協同與全棧串接 |
| [假硬體模擬器](./design/sim-fake-hardware.md) | PC 模擬 PHU 從站 + 全棧資料流 |
| [協定與模式速覽 (HTML)](./eyou-phu-protocols.html) | 可瀏覽的 EYOU PHU 協定/控制模式速覽 |
