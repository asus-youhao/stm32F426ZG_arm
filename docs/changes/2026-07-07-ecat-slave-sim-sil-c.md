# 2026-07-07 PC 端 EtherCAT 假從站 ecat_slave.py + SIL-C 真 SOEM 對打通過

## 變更摘要

1. 新增 `firmware/sim_py/ecat_slave.py`（規劃 §6.3 的 HIL-1 核心 / SIL-C 從站側）：
   PC 端 EtherCAT(CoE) 假 PHU 從站，**CiA402/馬達物理復用 `phu_motor.PhuMotor` 一行不改**，
   新寫的只有 EtherCAT 傳輸層：
   - ESC 暫存器空間模擬（4KB regs + 8KB 過程/信箱 RAM），支援 BRD/BWR/APRD/APWR/FPRD/FPWR/LRD/LWR/LRW
   - SII EEPROM 模擬（身分/信箱/SM 參數取自原廠 ESI `EYOU_ServoModule.xml`：
     Vendor 0x1097、Product 0x00010002、SM0/1 mailbox @0x1000/0x1080×0x80、SM2@0x1100、SM3@0x1400），
     含 General category（CoE details）與 SM category
   - ESM AL 狀態機（INIT→PREOP→SAFEOP→OP、非法跳轉拒絕 0x0011、SM 未配置 0x001D）
   - CoE mailbox SDO expedited（正確 size bits；abort 依 ETG 走 SDOREQ service）
   - PDO 重映射 0x1600/0x1A00 + 0x1C12/0x1C13 + 0x1C00（>6 entries → abort 0x06040042 ≒ 真機 E405；
     非 PREOP 改映射 → abort 0x08000022 ≒ ESM Changed）
   - FMMU 邏輯定址 + LRW（WKC 語意：讀+1 寫+2）、SM 看門狗（斷流 → SAFEOP + 0x001B + 馬達失能）
   - 多從站菊鏈（`--axes N`）、raw socket 模式（AF_PACKET）、`--selftest` 離線模式
2. 新增 `tools/ecat_bringup/sil_csp_test.c`：真 SOEM（v2）主站驗收程式 =
   未來 `ec_master_soem.c` 的流程雛形（掃鏈→CoE 重映射→SAFEOP→OP→CiA402 使能→1kHz CSP）。
3. 新增 `tools/ecat_bringup/run_sil_c.sh`：一鍵重現（veth + SOEM cmake 建置 + 從站 + 驗收）。

## 動機 / 背景

WP-SE 規劃（`docs/design/stm32-ethercat-master-plan.md` §6）要求板端上板前先有
SIL-C（真 SOEM 協定在環）與 HIL-1（板子對假從站）。EYOU 走 CoE = CANopen over EtherCAT，
物件字典/CiA402 與既有 CANopen 資產同一套，因此假從站只需補傳輸層，
與 C1 路線的 `can_slave.py` 一一對應。

## 影響範圍

- 純新增（`firmware/sim_py/ecat_slave.py`、`tools/ecat_bringup/sil_csp_test.c`、`run_sil_c.sh`）；
  未動任何既有程式與硬體行為。
- `docs/design/stm32-ethercat-master-plan.md`：SE2 狀態更新（vendor 已入庫）。

## 驗證方式

1. **離線 selftest**（Windows/Linux 免網卡）：`python ecat_slave.py --selftest`
   → **26 檢查 0 失敗 PASS**（掃鏈 WKC、SII、非法 AL 跳轉、SDO 讀寫/RO abort、
   E405 等效、重映射、SAFEOP/OP、使能三步 0x21→0x23→0x27、CSP 跟隨、看門狗）。
2. **SIL-C 真 SOEM 對打**（WSL Ubuntu 22.04, veth pair, SOEM v2.0 cmake 建置）：
   - `slaveinfo ecm0`：2 從站、身分/信箱/SM2@1100/SM3@1400/FMMU 全對、乾淨 SAFEOP
   - `sil_csp_test ecm0 2`：**19 檢查 0 失敗 PASS**
   - `sil_csp_test ecm0 14`（雙臂全配）：**67 檢查 0 失敗 PASS**，500 週期 @1kHz WKC 零漏
3. 對打過程抓到並修正 3 個真實協定問題（離線 selftest 抓不到的）：
   EtherType 網路序、SII 缺 SM category（SOEM 拿不到 SM2/SM3 位址 → 0x001D）、
   abort 需走 SDOREQ service（否則 SOEM 誤判成功）——**這正是 SIL-C 層存在的價值**。

## 已知限制

- 軟體從站無 ESC 硬體 on-the-fly 轉發：不驗時序/DC（交 HIL-1' LAN9252 評估板或 HIL-2 真機）。
- DC 暫存器（0x0900 區）僅佔位；SII features 回報「無 DC」讓 SOEM 跳過 DC 組態。
- SOEM v2.0 需 cmake ≥3.28（WSL 22.04 用 `pip3 install cmake` 解）。

## 關聯

- Branch：`feature/stm32-ethercat-master`
- 規劃：`docs/design/stm32-ethercat-master-plan.md`（§5 SE2/SE5、§6.3 假從站設計）
- 對照前例：`docs/changes/2026-07-02`（C1 CANopen HIL：can_slave.py）
