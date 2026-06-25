# 假硬體模擬器（C）— 全棧 + 模擬 PHU 從站

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增 `firmware/sim/`:於 PC 編譯執行整個 L1–L4 韌體,接虛擬 CAN bus 與模擬 PHU CANopen 從站,印出資料流。
- `phu_sim.[ch]`：模擬 PHU 從站（SDO/NMT/PDO/CiA402 + 動力學）。
- `co_bxcan_sim.c`：虛擬 CAN bus（取代 bxCAN 硬體層,frame 記錄/統計）。
- `hal_shim.c` + `stm32f7xx_hal.h`：主機 HAL 替身。
- `sim_main.c`：情境腳本 + 資料流列印。
- `Makefile`、`.gitignore`。
- 為支援雙臂分離,`kinematics` 新增基座位移 `base_p`;`robot_config` 設左/右肩基座 ±Y 0.2m;`app_main` 加觀測 API。

## 動機 / 背景

使用者無硬體,要求依 EYOU PDF 做「假硬體」通訊並觀察資料流。

## 實測結果

- 編譯通過、實際執行。
- CANopen 交握（NMT/SDO/PDO 映射）frame 正確。
- task-space IK 收斂：左臂 X+0.05 達成。
- BIMANUAL 雙臂協同：右臂跟隨左臂。
- 雙 channel CAN 流量對稱（TX≈RX≈10566）。

## 影響範圍

- 新增 `firmware/sim/`。
- 修改 `kinematics.[hc]`（base_p）、`robot_config.c`（基座偏移）、`app_main.c`（觀測 API 改讀 s_dc）。

## 驗證方式

- `cd firmware/sim && make && ./phu_sim_demo`,對照預期資料流。

## 關聯

- `sim-fake-hardware.md`、`dual-arm-control-plan.md`
