# 2026-07-07 STM32 板端 EtherCAT 主站 — 開分支 + 深度規劃（WP-SE）

## 變更摘要

1. 開新分支 `feature/stm32-ethercat-master`，補齊四條主站路線（PC/STM32 × CANopen/EtherCAT）中
   唯一尚未動工的「STM32 EtherCAT 主站」。
2. 分支基底選 `feature/h-sdo-bg-escalation`（firmware 最完整：harness/engine H0–H5、ec_master 門面、
   SDO 背景通道、log/trace ring、開機自檢、設定檔外部化），再 merge `feature/ecat-backend-sim`
   唯一分岔的 commit c753f57（`ec_dc_pll` DC 鎖相 PI + sim 漂移模型 + test_dc_pll）。
3. 衝突解法＝聯集（`docs/README.md` 索引、`firmware/tests/Makefile`/`test_main.c` 測試清單、
   `firmware/app/bus_ecat.c` include）。
4. 新增設計文件 `docs/design/stm32-ethercat-master-plan.md`：WP-SE0～SE9 工作分解、
   SIL 三層（A 單元測試 / B 閉環模擬 / C 協定在環）× HIL 四級（0 板端自檢 / 1 假從站 /
   1′ 真 ESC 評估板 / 2 真機單軸 / 3 多軸）、CoE↔CANopen 資產對照表、harness agent 復用評估。

## 動機 / 背景

- EYOU PHU 關節的 EtherCAT 走 **CoE（CANopen over EtherCAT）**：物件字典、CiA402 狀態機、
  錯誤碼表與已完成的 CANopen 主站**同一套**，應用層可 100% 復用，只需新寫板端傳輸層
  （nicdrv_stm32f7 + SOEM bare-metal 移植）。
- PC 端 EtherCAT 路線（linux-rt）已把 E1–E3 大半做掉（SOEM vendor、DC PLL SIL、PHU 真機特性化），
  板端路線從 WP-E4 展開即可，起點很高。
- 真機診斷（commit 6347f95）定案：0x2100 控制權在 CANopen 時 EtherCAT 側 SDO 全鎖 →
  規劃中明確把「SE-P0 CANopen 側佈建」列為 HIL-2 的硬性前置，SIL/HIL-1 先行不受阻。

## 影響範圍

- 新分支 `feature/stm32-ethercat-master`（未動 develop）。
- 合併帶入 `firmware/ecat/ec_dc_pll.{c,h}`、`firmware/tests/test_dc_pll.c`；
  `bus_ecat.c` 同時掛 sdo_bg 與 dc_pll/loop_engine include。
- 純文件新增：`docs/design/stm32-ethercat-master-plan.md`、本檔、`docs/README.md` 索引。
- 無硬體行為變更（規劃階段；後續 SE3 起會動 ETH 腳位/MPU/TIM，屆時各自出變更文件）。

## 驗證方式

- WSL 全套單元測試：`make && ./unit_tests` → **5102 檢查、0 失敗、PASS**（含新併入的 test_dc_pll）。
- Windows/MinGW 已知限制：`rt_selfcheck.c` 需 `sys/mman.h`，全套測試請在 WSL/Linux 跑（規劃文件 §6.1 已註記）。
- `gcc -fsyntax-only` 驗證解衝突後的 `bus_ecat.c`/`test_main.c`。

## 關聯

- Branch：`feature/stm32-ethercat-master`
- Merge commit：4631ee1（h-sdo-bg-escalation ⊕ ecat-backend-sim）
- 母計畫：`docs/design/ethercat-coe-master-plan.md`（WP-E）
- 根因定案：commit 6347f95（0x2100 控制權鎖）
