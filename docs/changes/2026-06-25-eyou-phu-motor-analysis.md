# 新增 EYOU PHU 馬達選型與 CANopen 類型分析

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增設計文件 [`docs/design/eyou-phu-motor-analysis.md`](../design/eyou-phu-motor-analysis.md),判定 EYOU PHU14/17/20 關節的 CANopen 屬於 **Classic CAN（CAN 2.0B）** 而非 CAN-FD,並更新對 MCU 選型的影響。

## 動機 / 背景

使用者提供 EYOU PHU 馬達 BOM（ECAT/CAN 介面）,要求查證其 CANopen 是 FD 還是 Classic。

## 重要結論

- EYOU PHU 提供 CANopen（`-C`）與 EtherCAT（`-E`）兩版本。
- **CANopen 版為 Classic CAN（CAN 2.0B,CiA 301/402）,非 CAN-FD。**
- 故 **不需要 CAN-FD 硬體**,STM32F746 的 bxCAN 即相容;先前 F746 無 FDCAN 的疑慮對此馬達不成立。
- 限制回到 Classic CAN 1 Mbps 頻寬 → 建議一臂一路（7 軸/路）。
- 高頻需求改走 PHU EtherCAT 版（+ 外接 EtherCAT 從站晶片）。

## 影響範圍

- 新增檔案:`docs/design/eyou-phu-motor-analysis.md`
- 更新 `docs/README.md` 索引。
- 不影響韌體程式碼或硬體行為（文件/選型階段）。

## 信心 / 限制

- EYOU 官網與 RobotShop 對自動抓取回 403,未能讀取 PDF datasheet 逐字。結論依經銷商列表與此級距關節通用規格判定,信心高,建議向原廠索取 PHU 通訊手冊最終確認 bit rate 與物件字典。

## 驗證方式

- 確認文件結論、BOM 對應與待確認清單正確。

## 關聯

- 延續 `2026-06-25-can-bus-architecture.md` 與 `2026-06-25-canopen-support-note.md`。
