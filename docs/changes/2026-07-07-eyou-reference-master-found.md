# 發現既有 EYOU EtherCAT 參考主站 + ESI（gx701 gallop_ws）

- 日期：2026-07-07
- 分支：`feature/linux-rt-ethercat-master`
- 類型：discovery + vendor（ESI 納入）+ docs

## 摘要

盤點 gx701 家目錄時，在使用者**另一個專案** `~/gallop_ws/src/` 發現
針對本專案同款 EYOU/PHU 馬達的既有 EtherCAT 資產：

- `eyou-motor-master/ethercat_position_control.py`（4116 行，**pysoem**，
  已能跑 PP/CSP，Windows+npcap 開發）——一支**可用的 EYOU EtherCAT 主站**
- `eyou-motor-master/EYOU_ServoModule.xml`（**ESI 檔**）+
  通訊手冊 v1.06 PDF
- `phu_controller_ros2/`（PHU 的 ROS2 控制器,亦含 esi/）

## 三個決定性結論（解 P0 SAFEOP 卡點）

1. **確認根因 = `0x2100`**：參考主站**整支不碰 0x2100**，用標準
   SAFEOP→OP 即能到 OP → 它跑的時候馬達必為 `0x2100=1`（EtherCAT）。
   我們這顆讀到 `2`（CANopen）——**唯一差異**。→ **Route A（切
   0x2100=1）確定可行**，等同用使用者自己的可用程式反證。
2. **PDO 非差異點**：參考主站重映射的 10 個 entries 與出廠映射
   **逐項相同**（0x6040/0x6060/0x607A/0x6081/0x60FF/0x240D/0x6071/
   0x6083/0x6084/0x6087）→ 我們用出廠映射的方向正確。
3. **真實位置單位**：**52953088 counts = 360°（輸出圈）= 524288 × 101**
   （編碼器 19-bit × 減速比）。位置物件（0x607A/0x6064）以**馬達側
   counts 含齒輪**計。修正先前「524288 counts/輸出圈」的假設——
   `robot_config` 的 `counts_per_rad` 換真機值時應為 `52953088/(2π)`。
   （附帶：先前 ±2900 counts 點動實際僅 ~0.02° 輸出,更安全。）

## 納入 repo 的東西

- `third_party/eyou_esi/EYOU_ServoModule.xml`（196 KB）：原廠裝置描述檔
  （Vendor `#x1097` Jiangsu Yiyou、ProductCode `#x00010002`、Rev 1——與
  PHU17 實讀 identity 一致）。原廠 ESI 屬可散布的裝置描述,供
  TwinCAT 對照 / SOEM/IgH ESI-based 組態。**因此 EYOU 詢問單的
  「索取 ESI」一題可移除**。

## 未納入（僅記指標）

`eyou-motor-master` / `phu_controller_ros2` 是使用者**另一個專案**
（各有 .git、pysoem、ROS2 相依），不複製進本 repo；作為參考主站的
**位置指標**記於記憶 [[eyou-reference-master]]。日後把 SOEM/IgH 真後端
接進 `ec_master.h` 門面時,其 bring-up 序列（PREOP SDO 重映射 →
SAFEOP → 送一幀有效輸出 → OP）為現成範本。

## 影響範圍

- 新增：`third_party/eyou_esi/EYOU_ServoModule.xml`、本文件
- 修正認知：位置單位 52953088（非 524288）；ESI 已有;0x2100 根因坐實

## 關聯

- 診斷鏈：`2026-07-07-phu17-safeop-diagnosis.md`（三輪 → 0x2100=2）
- 用途：Route A 執行後,可直接用此參考主站（pysoem 在 Linux 亦可,
  換 `--adapter` 為 Linux 介面名）對打驗證,或照其序列補我方後端
