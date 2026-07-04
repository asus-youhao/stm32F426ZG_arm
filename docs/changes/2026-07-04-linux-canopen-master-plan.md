# 2026-07-04 Linux PC CANopen 主站（真實 14 軸）深度規劃 — 方案 C

## 變更摘要

新增 `docs/design/linux-canopen-master-plan.md`：把既有的 PC 端 CANopen 主站 `firmware/pc/pc_master`（`feature/pc-canopen-master` 分支成果，目前只打過 vcan 假從站）升級到**驅動真實 14 顆 EYOU PHU** 的深度規劃。內容：

- 定位（方案 C）：Classic CAN 1 Mbps 物理天花板 = **500 Hz**（1 kHz 不存在）；價值是「最快摸到真馬達」（MC1 = 全專案首次真機閉環）、EtherCAT 風險對沖、協定對照組、非力控中速應用
- 差距分析 G1–G6：真 CAN 硬體、RT 化、SYNC 同步鎖存、從站佈建、EMCY 解析、匯流排儀表
- CAN 硬體選型：CANable(slcan) 淘汰 → 刷 candleLight(gs_usb) 過渡 → **PCIe 雙通道 CAN 卡為雙臂目標**（含 txqueuelen/取樣點/IRQ 綁核配套）
- 從站佈建 SOP：出廠 `0x2100=1(EtherCAT)` 必須切 2、node-id `0x26A0` 逐顆設 1..7、`0x2130` 存檔（保存中禁斷電）、腳本化 `provision_joint.sh`
- SYNC 設計：SYNC producer + PDO transmission type=1 同步鎖存，軸間 skew 從 ~1.5 ms 壓到 <100 µs；含 EYOU 支援度不明時的 async 退路
- 頻寬精算表：500 Hz ≈ 95% 負載（運行中禁 SDO）；預設 **400 Hz 檔位**留餘裕，`control_rate.h` 檔位化
- 深度工作分解 **WP-C0～C5**（每項含 DoD）＋里程碑 MC1～MC5；MC5 產出 CANopen(500Hz) vs EtherCAT(1kHz) 三方案對比報告
- 新增軟體件：`co_emcy.c`（EMCY 解析餵 safety）、bus-off 自動恢復、canbusload 儀表

## 動機 / 背景

- 使用者詢問「PC 端主站走 CANopen 的有沒有」：**有**（`firmware/pc/pc_master`，SocketCAN，已驗證於 vcan），但缺「接真實馬達」的規劃——本文件補齊該缺口。
- 四條路線中這是唯一「程式碼已完成、只差真硬體」的路徑，適合當真機首發與 EtherCAT 方案的對照組/備援。

## 影響範圍

- 新增：`docs/design/linux-canopen-master-plan.md`、本變更文件
- 修改：`docs/README.md`（索引）
- **純文件變更**。路線全景：STM32 板端（暫緩）/ A=22.04+SOEM / B=24.04+IgH / **C=Linux+CANopen 500Hz（本分支）**

## 驗證方式

- 文件審閱：頻寬預算與 SYNC 機制對照 doc_EYOU 通訊手冊 §2（CANopen）與 CiA 301；佈建參數（0x2100/0x26A0/0x2130）出自手冊摘讀
- 規劃可行性驗收點：MC1（第一顆真關節閉環）與 MC2（RT+SYNC 品質、USB vs PCIe 對比）

## 關聯

- 分支：`feature/linux-canopen-master`（自 `feature/linux-igh-ethercat-master` 切出，僅為文件疊加；程式碼基底在 `feature/pc-canopen-master`）
- 既有成果：`docs/changes/2026-07-03-pc-master-socketcan.md`、`docs/changes/2026-07-03-ui-integration-monitor.md`
- 相關規劃：`docs/design/linux-rt-ethercat-master-plan.md`、`docs/design/linux-igh-ethercat-master-plan.md`、`docs/design/ethercat-coe-master-plan.md`
