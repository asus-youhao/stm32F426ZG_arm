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

## 設計文件

| 文件                                                          | 說明                                  |
| ------------------------------------------------------------ | ------------------------------------- |
| [Dual-Arm Low-Level Control](./design/dual-arm-low-level-control.md) | STM32F746ZG 雙臂低階控制需求/架構概述 |
| [CAN/CAN-FD Bus Architecture](./design/can-bus-architecture.md) | CAN-FD 支援評估與雙臂匯流排拓樸/頻寬   |
| [EYOU PHU Motor Analysis](./design/eyou-phu-motor-analysis.md) | EYOU PHU 關節選型與 CAN 類型判定（依原廠 datasheet） |
| [CANopen vs EtherCAT](./design/canopen-vs-ethercat.md) | CANopen / EtherCAT / CoE 比較與通訊選型 |
| [Dual-Arm Control Plan](./design/dual-arm-control-plan.md) | 雙臂控制工作分解（joint/task-space 1kHz、整合） |
