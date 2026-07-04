# 2026-07-04 EtherCAT（CoE）主站遷移深度規劃

## 變更摘要

新增 `docs/design/ethercat-coe-master-plan.md`：把現有 CANopen（Classic CAN, 500 Hz）主站換成 **EtherCAT（CoE, CiA 402）主站** 的完整遷移規劃，MCU 維持 STM32F746ZG。內容涵蓋：

- 硬體前提：關節訂購尾碼 E/C、`0x2100` 控制權參數、ECAT IN/OUT 接線（JST BM05B-GHS-TBT pinout）、單鏈 14 軸拓撲、供電預算（雙臂額定 45.4 A@48V、PD50 泄放）、STO 鏈
- 主站硬體結論：**F746ZG 板載 ETH MAC + LAN8742A 直接當主站，不需 LAN9252 ESC**（ESC 僅從站需要）
- 軟體堆疊選型：**SOEM**（PC/板端共用，延續本專案三後端模式），含 GPLv2 授權風險註記
- 精簡 PDO 映射設計（RxPDO 12 B / TxPDO 15 B 每軸，遵守每 PDO ≤6 entries 的 E405 限制）與 1 kHz 頻寬預算（線上時間 <60 µs）
- DC-Synchron 同步設計：SYNC0 1 ms、主站 PI 鎖相漂移補償、原廠「週期須為 500 µs 整數倍」規則（E404）
- 現有韌體逐檔處置表（L2–L4 控制/safety/cia402 純函式原封重用；L0 整組替換；`dual_arm.c` 收發模型改 process image）
- 新抽象層 `ec_master.h` API 草案、bring-up 序列（ESM + CiA402 + 抱閘時序）、故障碼排錯表
- 工作分解 WP-E0～E6 與里程碑、風險對策、驗證方式

同時建立分支 `feature/ethercat-coe-master`（自 `feature/pc-canopen-master` 切出，因規劃以該分支的最新韌體狀態為盤點基準）。

## 動機 / 背景

- `dual-arm-control-plan.md` WP0.1/0.2/0.3 三項阻塞性待決議（通訊主路徑、主站位置、MCU 選型）一直未拍板。
- 現況 `control_rate.h` 因 Classic CAN 1 Mbps 頻寬被迫降到 500 Hz（1 kHz 會超載），且軟體 SYNC 抖動大；1 kHz 雙臂 joint/task-space + 未來力控（CSF）必須走 EtherCAT DC。
- 原廠三份文件（通訊手冊 v1.06 §3–§7、用戶手冊 v1.24、規格書 v1.14）已深入摘讀，關鍵規格（週期 500 µs 整數倍、PDO ≤6 entries、`0x2100` 預設=EtherCAT、19-bit 524288 counts/rev、E40x 故障碼群）全部落到文件內，規劃有據可依。

## 影響範圍

- 新增：`docs/design/ethercat-coe-master-plan.md`、本變更文件
- 修改：`docs/README.md`（索引）
- **純文件變更，無程式碼/硬體行為改動**。後續實作（WP-E1 起）才會動 `firmware/`
- 對既有設計文件的效力：`canopen-vs-ethercat.md` §4 的「主站位置」待決議由本規劃拍板（F746 直接當主站，PC + SOEM 先行驗證）

## 驗證方式

- 文件內所有協定數值（物件 index、故障碼、週期規則、pinout、電流）皆出自 doc_EYOU 三份 PDF 的對應章節，文中已標註來源
- 規劃本身的可行性驗收點設在里程碑 ME1（WP-E1+E2：PC + SOEM 對單顆 PHU 關節達成 1 kHz DC 同步）

## 關聯

- 分支：`feature/ethercat-coe-master`
- 相關設計文件：`docs/design/ethercat-coe-master-plan.md`（本次新增）、`docs/design/canopen-vs-ethercat.md`、`docs/design/dual-arm-control-plan.md`
- 原廠文件：`doc_EYOU/EYou-PHU&RHU系列关节CANopen与EtherCAT通信手册v1.06-20260430.pdf`、`doc_EYOU/EYou-PHU关节模组用户手册v1.24-20260409.pdf`、`doc_EYOU/PHU系列规格书v1.14-20260424.pdf`
