# 新增 CAN / CAN-FD 通訊匯流排架構評估文件

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增設計文件 [`docs/design/can-bus-architecture.md`](../design/can-bus-architecture.md),評估雙臂(7+7 軸,共 14 軸)低階控制的 CAN 通訊方案,並回答「STM32F746 能否支援兩個 CAN-FD」。

## 動機 / 背景

使用者規劃以 CAN-FD 控制雙臂各 7 軸,需釐清 STM32F746 的 CAN 能力與頻寬是否足夠。

## 重要結論

- **STM32F746ZG 不支援 CAN-FD**,僅內建 2 × bxCAN(Classic CAN 2.0B,≤ 1 Mbps)。
- 需 CAN-FD 時建議改用 **STM32H743ZI**(2 × FDCAN,腳位接近)或 STM32G4,或 F746 外接 MCP2518FD。
- 拓樸建議:**一臂一路 CAN/CAN-FD**(方案 A),負載與故障隔離佳。
- 頻寬粗估:Classic CAN 單路帶一臂約 ≤ 400 Hz;1 kHz 等級閉迴路需 CAN-FD。

## 影響範圍

- 新增檔案:`docs/design/can-bus-architecture.md`
- 更新 `docs/README.md` 索引。
- 不影響韌體程式碼或硬體行為(文件階段)。

## 驗證方式

- 確認文件內容與選型結論正確、列出待決議項。

## 關聯

- 延續 `2026-06-25-stm32f746zg-dual-arm-control.md`,補充通訊層細節。
