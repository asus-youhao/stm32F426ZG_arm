# 2026-07-04 方案 C 硬體具體化：PEAK PCAN（CAN FD 系列）

## 變更摘要

修改 `docs/design/linux-canopen-master-plan.md`，把方案 C 的 CAN 介面硬體從「泛用 PCIe CAN 卡」具體化為 **PEAK-System PCAN CAN FD 系列**：

- 選型階梯改為：PCAN-USB FD（單通道，bring-up）→ PCAN-USB Pro FD（雙通道 USB，佈建/功能驗證）→ **PCAN-PCIe FD 雙通道**（雙臂與效能量測本體）
- 新增 §3.1「CAN FD 的角色」：三層事實分離——關節硬體是 CAN FD 埠（規格書）、EYOU CANopen 協定現況走 **Classic 2.0B 固定 1 Mbps**（`0x26A1`）、PCAN FD 硬體向下相容。運行組態定為「FD 硬體、Classic 協定」，`ip link` **不開 `fd on`**
- 新增 FD 升級路徑分析：若原廠開放 data phase（如 1M/5M，CiA 1301 CANopen-FD 或廠商幀），§5.2 的 95% 匯流排負載降至 ~25%，1 kHz 進入可行區——列為 WP-C0.6「向 EYOU 書面確認」工作項，確認前一律按 Classic 預算
- Linux 驅動註記：`peak_usb`/`peak_pciefd` 均在主線內核，不需 PEAK out-of-tree 驅動或 PCAN-Basic
- WP-C0 工作項與驗收更新（0.1/0.2 指名 PCAN 型號 + `ethtool -i` 驗證、新增 0.6）；WP-C2.4 對比項改為 PCAN USB vs PCIe
- 風險表新增「誤開 `fd on` 打 Classic 從站造成 error frame 風暴」及對策（link 腳本固定 Classic）

## 動機 / 背景

使用者指定方案 C 使用 PCAN CAN-FD 介面。PCAN FD 系列同時滿足：主線內核 SocketCAN 原生驅動、硬體時戳（軸間 skew 量測必需）、雙通道單卡覆蓋雙臂，且 FD 能力與 EYOU 關節的 CAN FD 埠對齊——即使協定現況是 Classic，硬體投資不會因未來原廠開放 FD 而作廢。

## 影響範圍

- 修改：`docs/design/linux-canopen-master-plan.md`（§3、§3.1 新增、WP-C0、WP-C2.4、風險表）
- 新增：本變更文件；修改 `docs/README.md`（索引）
- **純文件變更**。採購面影響：BOM 由泛用 CAN 卡改為 PCAN 指定型號

## 驗證方式

- 文件審閱：Classic/FD 判定對照 `eyou-phu-motor-analysis.md` 與通訊手冊 §2（`0x26A1` 波特率表無 data-phase 物件）
- 實作驗收點：WP-C0.1/0.2（`ethtool -i` = peak_usb/peak_pciefd、`candump -H` 硬體時戳）、WP-C0.6（原廠 FD 支援書面回覆）

## 關聯

- 分支：`feature/linux-canopen-master`
- 前置：`docs/changes/2026-07-04-linux-canopen-master-plan.md`（方案 C 主規劃）
- 相關：`docs/design/eyou-phu-motor-analysis.md`、`docs/design/can-bus-architecture.md`
