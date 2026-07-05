# WP-C0.6：EYOU 原廠確認清單（CAN FD / SYNC / 佈建細節）

- 日期：2026-07-05
- 分支：`feature/linux-canopen-master`
- 類型：docs

## 變更摘要

新增 `docs/design/eyou-canfd-inquiry.md`：給 EYOU 技術窗口的正式確認清單，
三組共 14 題——A) CAN FD data phase（升級路存廢、波特率、CiA 1301 vs 廠商
自訂、開啟方式）、B) SYNC/transmission type=1 支援（G3 前提、skew 規格）、
C) 佈建/校正順帶確認（0x2025 解析度、EtherCAT 模式下 CAN SDO 可達性、
存檔行為、EMCY 廠商欄位、運行中 SDO 節流）。每題附「為什麼重要」與
回覆後的動作對照表。

## 動機 / 背景

WP-C0.6 DoD「書面回覆歸檔」。清單不依賴任何硬體，可立即寄出；A 組答案
決定方案 C 天花板（500 Hz 備援 vs 1 kHz 正式候選），B 組決定 WP-C2.2
的做法，C2 決定佈建腳本（WP-C0.3）首步走 CAN 還是 UART。

## 影響範圍

- 僅新增文件。回覆後回填本清單並視結果開 FD 評估支線。

## 驗證方式

- 與 `linux-canopen-master-plan.md` §3.1/§5.1/§5.2、`provision_joint.sh`
  流程交叉核對，問題覆蓋全部「待原廠確認」標記項。

## 關聯

- 設計：`docs/design/linux-canopen-master-plan.md`（C0.6、C2.2、G3/G5）
- 工具：`tools/provision_joint.sh`（C2 的答案影響其流程分支）
