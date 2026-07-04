# WP-C0.3：關節佈建 SOP 腳本 provision_joint.sh

- 日期：2026-07-05
- 分支：`feature/linux-canopen-master`（cherry-pick 自 harness 分支 d4d02db,歸位）
- 類型：feat（工具腳本，無韌體變更）

## 變更摘要

新增 `tools/provision_joint.sh`：把 `linux-canopen-master-plan.md` §4 的佈建
SOP 腳本化，讓一顆出廠生關節在 5 分鐘內完成 CANopen 佈建：

1. **偵測目前節點**：聽 heartbeat（0x701..0x77F）3 秒自動判定 node-id
   （可 `--current-id` 覆蓋）；若目標 node-id 已在匯流排上有 heartbeat
   → 判定撞車直接中止（強制「一次一顆」）。
2. **0x2100=2** 切控制權到 CANopen；若 SDO 無回應（關節仍在出廠 EtherCAT
   模式）→ 明確提示改走 EYouServoStudio(UART)/EtherCAT 路徑。
3. **0x26A0=<new-id>** 設 node-id（1..7）。
4. **存檔**：0x2130=1，失敗退路 0x1010:01="save"（0x65766173）；等 4 秒，
   全程提示「嚴禁斷電」。`--skip-save` 可乾跑排練。
5. **重上電驗證**：等新 node-id 的 heartbeat（60 秒逾時）→ SDO 讀回
   0x1008 名稱、0x2025 解析度（≠524288 時警告需更新 robot_config）、
   0x26A2/A3 減速比、0x26A1 波特率。
6. **佈建清冊**：附加一列到 `tools/provision_ledger.csv`
   （date,interface,node_id,label,name,resolution,gear,baud）；`--label`
   記部位（如 L_J4_Elbow），貼標籤由人工完成。

實作要點：

- 純 bash + can-utils（cansend/candump），無其他依賴。
- SDO expedited 讀寫自帶實作；**寫入大小自動探測**——先讀該物件，依回應
  command specifier（0x4F/0x4B/0x43 → 1/2/4 byte）選寫入 cmd（0x2F/0x2B/0x23），
  避免手冊未載明物件大小時猜錯被 abort。
- `--dry-run`：不需真介面，印出全部將送的 frame（教學/審查用）。

## 動機 / 背景

方案 C 差距 G4：真關節出廠 `0x2100=1`（EtherCAT）、node-id 全是 1，不佈建
就上 bus 必撞。SOP 腳本化 + 清冊是 WP-C0.3 的 DoD（「一顆生關節 5 分鐘
佈建完」）。

## 影響範圍

- 新增：`tools/provision_joint.sh`（+ 執行時自動生成 `tools/provision_ledger.csv`）
- 不影響韌體與現有程式。

## 驗證方式

- `bash -n` 語法通過；`--dry-run` 全流程輸出 SDO 幀已逐幀核對
  （0x600+node / expedited cmd / little-endian index+data 正確）。
- **待真機驗**（WP-C0.4 首顆關節時）：0x2100/0x26A0/0x2130 的實際物件大小
  與寫入生效、heartbeat 等待流程、0x2025=524288 實證。本機無 CAN 硬體
  亦無免密 sudo（vcan 建不了），無法對假從站實測。

## 關聯

- 設計：`docs/design/linux-canopen-master-plan.md` §4（SOP 本文）、§7 WP-C0.3
- 銜接：WP-C0.4（首顆真關節佈建）、WP-C0.5（0x2025/0x26A2-A3 實測 →
  robot_config 校正）
