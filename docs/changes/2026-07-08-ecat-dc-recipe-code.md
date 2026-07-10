# 把 DC 可動配方落地成程式碼：ec_config + 真 SOEM/IgH 後端 + F746 流程

- 日期：2026-07-08
- 分支：`feature/h-sdo-bg-escalation`（EtherCAT 整合碼所在）
- 類型：feat（真後端）+ fix（mode-via-PDO）+ docs

## 變更摘要

把 2026-07-08 在 gx701 實測「讓 PHU17 轉動」的配方（DC 是 CSP 必要、
不重映射 PDO、mode 走 PDO、0x2100=1、0x253B=0 旁路 STO）從診斷文件
落地成**程式碼與設計流程**：

1. **`firmware/ecat/ec_config.h`（新）**：配方單一真相來源——DC
   `assign_activate=0x300`、`SYNC0=控制週期`、`EC_REMAP_PDO=0`、出廠 PDO
   位元組偏移、CiA402 序列、STO 調試、單位 52953088=524288×101、身分
   0x1097/0x10002。含六輪血淚註解。sim/SOEM/IgH/F746 一律引用。
2. **`firmware/ecat/ec_master_igh.c`（新）**：真 IgH 後端,由實測轉動的
   `tools/ecat_bringup/igh_spin.c` 重構成 `ec_master.h` 門面。不重映射、
   DC 每 cycle sync、mode 寫 RxPDO。
3. **`firmware/ecat/ec_master_soem.c`（新）**：真 SOEM 後端（同配方；到
   SAFEOP 已驗,完整 DC 轉動待 gx701 覆核——IgH 已驗）。`ec_soem_set_ifname`
   或 `EC_IFNAME` 指定網卡。
4. **`bus_ecat.c` 修真 bug**：移除 `ec_coe_write(0x6060,0,8)`——mode 是
   PDO-mapped,SDO 寫回 0x06010000（實測）。改由後端每週期於 RxPDO 寫 mode=8。
5. **`ec_master.h` 契約更新**：註明 DC 必要 / 不重映射 / mode 走 PDO / 0x2100=1。
6. **F746 控制流程（設計 §8）**：DC 從「偏好」更正為「CSP 動作必要」；
   板端 TIM tick 須相位鎖定 SYNC0（DC PI = eng_phase_trim + ec_dc_pll）；
   更正 §5.3/§8.1「先 free-run 再 DC」的錯誤假設。
7. **`pc/Makefile`**：加 `pc_master_igh` / `pc_master_soem` 目標（換後端 .c +
   鏈接對應庫,路徑變數 IGH_DIR/SOEM_DIR）。

## 動機

先前 EtherCAT 不動被誤判為 STO 硬體閘;實測證實真因是**缺 DC**（EYOU CSP
嚴格依賴 SYNC0,free-run 下 drive 到 OperationEnabled 但內部 demand 凍住）。
此配方若只留在診斷文件,下次寫真後端會再踩坑 → 落地成 `ec_config.h` +
真後端 + 設計流程,成為唯一真相來源。

## 影響範圍

- 新增：`ecat/ec_config.h`、`ecat/ec_master_igh.c`、`ecat/ec_master_soem.c`、本文件
- 修改：`app/bus_ecat.c`、`ecat/ec_master.h`、`pc/Makefile`、
  `docs/design/ethercat-coe-master-plan.md` §5.3/§8
- **sim 路徑與板端建置零影響**（新後端 .c 需 ecrt/soem 庫,不進 tests/pc
  預設 Makefile;pc_master_igh/soem 為選配目標）。

## 驗證方式

- 本地 `firmware/tests` **5083 檢查 0 失敗**、`pc_master`（sim）建置零警告
  ——bus_ecat 改動未破壞既有。
- `pc_master_igh` 於 gx701 對真 IgH 編譯（整合檢核,見 commit 訊息）。
- 配方本身：igh_spin.c 已於 gx701 實測轉動（100 rpm × 16.6 馬達圈）。

## 關聯

- 診斷六輪：`2026-07-07-phu17-safeop-diagnosis.md`
- 配方源：`tools/ecat_bringup/igh_spin.c`（實測轉動）
- 設計：`design/ethercat-coe-master-plan.md` §6.2/§8
