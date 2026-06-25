# CubeMX 整合說明 + WP2 單軸 bring-up

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

1. 新增 `firmware/integration_example.c`：CubeMX 接線範本（HAL_CAN RX 回呼分流、TIM6 1kHz、main 啟動順序）。
2. 新增 `firmware/test/bringup.[ch]`：單軸 bring-up 測試（SDO 讀寫驗證 + Profile Velocity 單軸轉動 + 位移確認）。
3. 新增 `docs/design/firmware-cubemx-integration.md`：CubeMX 設定（CAN1/CAN2、TIM6、時脈、位元時序計算、NVIC、接線片段）。
4. 新增 `docs/design/wp2-single-axis-bringup.md`：bring-up 流程、安全前提、逐步驗收標準、常見問題。
5. 更新 `docs/README.md` 索引。

## 動機 / 背景

使用者要求補 CubeMX 整合/範例接線,並開始 WP2 單軸 bring-up（SDO 讀寫驗證 + 單軸轉動）。

## L0 / L1 狀態說明

- L0（通訊:bxCAN/SDO/NMT/PDO）與 L1（CiA402）**程式碼完成,尚未硬體驗證**。
- 本次新增的整合範本與 bring-up 測試即為「驗證 L0/L1」的工具;通過 WP2 後 L0/L1 才算 bring-up 驗證完成。

## 設計重點

- bring-up 採 **Profile Velocity（PV, 0x6060=3）** 做單軸轉動,最易觀察。
- 使能輪詢 CiA402 狀態字推進（fault reset→0x06→0x07→0x0F）。
- `bringup_log()` 弱連結,可覆寫為 printf（UART/SWO）看逐步輸出。
- 位元時序提供 APB1=45/54/42 MHz 三組範例,需依實際時脈樹挑選。

## 影響範圍

- 新增:`firmware/integration_example.c`、`firmware/test/bringup.[ch]`、兩份 design 文件。
- 更新:`docs/README.md`。
- 韌體仍為骨架/測試工具,實機驗證待 CubeMX 專案整合後進行。

## 驗證方式

- bring-up 程式對照 CANopen/CiA402 標準與 EYOU 手冊（0x1000/0x6041/0x26A0/0x26A1/0x6064/0x6060/0x60FF）檢查。
- 實機:依 `wp2-single-axis-bringup.md` 逐步驗收。

## 關聯

- `firmware-cubemx-integration.md`、`wp2-single-axis-bringup.md`、`dual-arm-control-plan.md`
