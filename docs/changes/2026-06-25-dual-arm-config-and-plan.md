# 雙臂關節配置、CANopen/EtherCAT 說明與控制工作規劃

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

1. 依原廠 datasheet 更正 `docs/design/eyou-phu-motor-analysis.md`（CAN 類型:硬體 CAN FD 埠 / 協定 Classic CANopen 1 Mbps）。
2. `CLAUDE.md` 新增「雙臂硬體配置」節與每臂 7 軸關節對應表。
3. 新增 `docs/design/canopen-vs-ethercat.md`:CANopen 與 EtherCAT 比較,確認支援 **CoE（CANopen over EtherCAT）**。
4. 新增 `docs/design/dual-arm-control-plan.md`:雙臂控制工作分解（WP0–WP7,含 joint-space / task-space 1 kHz / 雙臂整合）。
5. 更新 `docs/README.md` 索引。

## 動機 / 背景

使用者提供 EYOU 原廠 PDF,並要求:釐清 CANopen/EtherCAT 與 CoE 支援、記錄雙臂關節配置於 CLAUDE.md、規劃雙臂控制工作項目（joint-space、task-space 1 kHz、整合）。

## 重要結論

- PHU 同時支援 CANopen 與 EtherCAT,且支援 **CoE**（共用 CiA 402 物件字典）。
- 1 kHz 雙臂 task-space/力控 → 通訊主路徑建議 **EtherCAT-CoE（DC 同步）**;CANopen 適合 bring-up/低頻。
- 關節配置（每臂 7 軸）:J1/J2 肩 PHU20、J3 肩 yaw PHU17、J4 肘 PHU17、J5–J7 腕 PHU14。
- 控制架構分層 L0–L4,工作分解 WP0–WP7,里程碑 M1–M5。

## 待決議（已記於 plan）

1. 通訊主路徑（CANopen vs EtherCAT-CoE）。
2. 主站位置（STM32 vs PC/SBC）— 影響整體架構與 MCU 選型。
3. MCU 最終選型（F746 / H743 / +LAN9252）。
4. 機構 DH/URDF 與慣量參數。

## 影響範圍

- 修改:`CLAUDE.md`、`docs/design/eyou-phu-motor-analysis.md`、`docs/README.md`
- 新增:`docs/design/canopen-vs-ethercat.md`、`docs/design/dual-arm-control-plan.md`
- 不影響韌體程式碼（仍為文件/規劃階段）。

## 驗證方式

- 對照原廠手冊 §1.2（CoE）、§2/§3/§4（OD/EtherCAT/控制模式）、規格書（接口/控制模式）確認內容正確。

## 關聯

- `eyou-phu-motor-analysis.md`、`canopen-vs-ethercat.md`、`dual-arm-control-plan.md`
