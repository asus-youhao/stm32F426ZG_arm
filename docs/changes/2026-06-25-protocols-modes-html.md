# 新增 EYOU PHU 協定與控制模式 HTML 速覽

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增 `docs/eyou-phu-protocols.html`:可用瀏覽器檢視的單頁速覽,整理 EYOU PHU 的:
- 支援通訊協定（CANopen / EtherCAT-CoE）
- 完整控制模式與 `0x6060` 數值（PP=1, PV=3, PT=4, HM=6, CSP=8, CSV=9, CST=10, CSF=13）
- 人形雙臂建議使用的 mode

## 重要結論（人形雙臂建議 mode）

- 主路徑:**EtherCAT(CoE)+DC 同步**。
- 位置型任務:**CSP（8）**;順應/力控:**CST（10）/ CSF（13）**。
- Profile 模式（PP/PV/PT）讓驅動器本地規劃,不適合 1 kHz 集中式雙臂協同。
- CANopen（Classic CAN ≤1Mbps）保留給單軸 bring-up 與低頻測試。

## 依據（原廠手冊 v1.06）

- §4.3 運行模式表（0x6060 對應）。
- §4.6 循環同步模式 CSP/CSV/CST/CSF（含基本操作步驟與相關物件）。
- §4.7 輪廓模式 PP/PV/PT、回零 HM（方式 35）。

## 影響範圍

- 新增:`docs/eyou-phu-protocols.html`
- 更新:`docs/README.md` 索引。
- 不影響韌體程式碼。

## 驗證方式

- 對照手冊各模式章節確認 0x6060 數值與描述正確。

## 關聯

- `canopen-vs-ethercat.md`、`dual-arm-control-plan.md`、`eyou-phu-motor-analysis.md`
