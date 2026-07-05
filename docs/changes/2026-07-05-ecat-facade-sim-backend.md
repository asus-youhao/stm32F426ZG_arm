# EtherCAT 門面 + fake 後端（WP-L3.1 / L3.5 / WP-H5 前置）

- 日期：2026-07-05
- 分支：`feature/ecat-backend-sim`（自 `feature/harness-loop-engine` 切出——
  依賴 engine/`bus_if.h` 與 H4 的 phu_sim SYNC 鎖存，方案 A/B 分支上沒有這些碼；
  待 harness 系列合回 develop 後歸隊）
- 類型：feat + test（純軟體，零 EtherCAT 硬體）

## 變更摘要

1. **`firmware/ecat/ec_master.h`**——EtherCAT 主站門面（WP-L3.1/I3.1 定案初版）：
   `init(掃鏈→PREOP) → coe_read/write(組態) → op(OP+DC) → 每週期
   set_output → exchange → get_input`（一拍延遲模型）；`expected_wkc()`
   供失軸判定；`ec_master_health()` 餵 H4 的 `bus_health_t`
   （proto[0]=實得 WKC、[1]=期望 WKC、[2]=AL 概況）。
   同一門面三後端：`ec_master_sim.c`（本次）/ `ec_master_soem.c`（WP-L）/
   `ec_master_igh.c`（WP-I）。
2. **`firmware/ecat/ec_master_sim.c`**——fake 後端（WP-L3.5）：
   - 從站 CiA402 行為**單一來源**：重用 `phu_sim`（與 CANopen 假從站同一份
     模型），行程內以 CAN 幀編碼往返——CoE 本就是 CANopen over EtherCAT。
   - process data 交換 = RPDO 暫存 + SYNC 廣播鎖存（= DC-Synchron 類比，
     與 H4/G3 同一機制）；WKC 模型：每顆健在從站 3（LRW 讀1+寫2）。
   - 故障注入鉤子（`ec_master_sim.h`）：`phu_ecat_set_offline()` 模擬拔線
     （WKC 缺 3、輸入凍結）、`phu_ecat_node()` 直接注入故障狀態。
3. **`firmware/tests/test_ecat.c`**——模擬 WP-L1 bring-up 全序列（免硬體）：
   掃鏈 14 軸、CoE 讀 0x1000=0x00020192、0x6060=8 寫+讀回、未 OP 拒交換、
   **同一份 `cia402.c` 使能邏輯零修改跑在 EtherCAT 門面上**（bus-agnostic
   實證）、一拍延遲語意（set_output 不動輸入）、CSP 跟隨（步進 ≤max_step、
   收斂）、掉軸/回線 WKC 與 health 全驗。

## 動機 / 背景

使用者問「WP-L/I 一定要實機才能動嗎」——答案是否：門面定案、fake 後端、
免硬體 CI（L3.1/L3.5）都是純軟體，且是 WP-H5（EtherCAT 後端掛 engine）的
直接前置。本次讓 EtherCAT 路徑擁有與 CANopen 同級的 SIL 迴歸能力。

## 影響範圍

- 新增：`firmware/ecat/ec_master.h`、`ec_master_sim.[ch]`、
  `firmware/tests/test_ecat.c`
- 修改：`firmware/tests/Makefile`、`test_main.c`（掛新測試）
- 不影響既有 CANopen 路徑與 pc_master 行為；不涉硬體。

## 驗證方式

- `firmware/tests`：`make && ./unit_tests` → **4887 檢查 0 失敗**
  （含全部既有測試迴歸）。
- 未做（明列）：SOEM vendor tree 引入（WP-L1.1，需下載源碼樹/決定
  submodule 策略）；IgH 對 6.8-rt 編譯驗證（WP-I0.4，需 24.04 RT 機）；
  真機掃鏈以後的一切（WP-L1.2+）。

## 關聯

- 設計：`docs/design/ethercat-coe-master-plan.md` §6.2（門面）、
  `linux-rt-ethercat-master-plan.md` §5.2（一拍延遲）+ WP-L3、
  `harness-agent-loop-engine-plan.md` §6（bus_health_t）+ WP-H5
- 前置：`8f89c8b`（WP-H4：phu_sim SYNC 鎖存、bus_health_t）
- 下一步：WP-H5 以 `bus_if_t` 把本門面掛上 loop engine（`--bus ethercat`）；
  或 WP-I0.4（IgH 編譯拍板，需 RT 真機）
