# 補充 CANopen 支援說明

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

於 [`docs/design/can-bus-architecture.md`](../design/can-bus-architecture.md) 新增「3.5 CANopen 支援」一節,說明 CANopen 為軟體協定層、各 MCU 的支援情況與堆疊選項。

## 動機 / 背景

使用者詢問所選 MCU 是否支援 CANopen,需釐清「傳統 CANopen」與「CANopen FD」的差異及硬體前提。

## 重要結論

- CANopen 是上層軟體協定(CiA 301),非硬體功能;有 CAN 控制器 + 軟體堆疊即可運行。
- **STM32F746ZG 支援傳統 CANopen(bxCAN),但不支援 CANopen FD**(需 CAN-FD 硬體)。
- STM32H743ZI / G4 兩者皆支援(含 CANopen FD)。
- ST 無官方 stack;建議開源 **CANopenNode** 或商用堆疊。

## 影響範圍

- 修改檔案:`docs/design/can-bus-architecture.md`(新增 3.5 節)
- 不影響韌體程式碼或硬體行為(文件階段)。

## 驗證方式

- 確認新增章節內容正確,表格涵蓋傳統/FD 兩種情況。

## 關聯

- 延續 `2026-06-25-can-bus-architecture.md`。
