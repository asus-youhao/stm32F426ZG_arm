# 2026-07-07 · 學習手冊對照範例碼（learn/examples/）

## 變更摘要

為教學站每一節（L1~L3 共 18 節）與每個里程碑（專案 A WP2→WP7、專案 B M1→M6）
補上獨立可跑的最小範例，共 30 份：

- `learn/examples/common/`：`hal_stub.h`（L1 免板模擬）、`fake_slave.py`
  （純 stdlib SocketCAN 假 CANopen/CiA402 關節）、`can_util.h`、`vcan_setup.sh`
- `learn/examples/{l1,l2,l3,project-a,project-b}/…`：各節/各里程碑範例
- `learn/examples/Makefile` + `README.md`：`make` 全建置、`make run` 跑零依賴組、
  `make cansim` 跑 vcan 對打組
- 五個教學頁每節/每里程碑加「💻 對照範例」連結；index 補範例入口說明

## 動機 / 背景

使用者確認需要「每個里程碑都有 example code」（比照 esp32_search learn 站的
`learn/examples/` 規格）。

## 影響範圍

- 純新增 `learn/examples/`；教學 HTML 只增加 note 區塊，不動 firmware。

## 驗證方式

- `make`：23 個 C 目標 `-Wall -Wextra` 零警告。
- `make run`：13 個零依賴範例全過（含 cia402 斷言、units 往返驗證、
  wp4 軌跡誤差 4.8e-13、watchdog buggy/fixed 對照）。
- Python `py_compile` ×4、`bash -n` ×5、wp7 stdin 互動實測通過。
- vcan 類需 `sudo common/vcan_setup.sh` 後 `make cansim`（本機無免密 sudo 未實跑）。

## 關聯

- Branch：`feature/stm32-learn-tree`
- 前一筆：`2026-07-07-stm32-learn-tree-site.md`（教學站本體）
- 說明檔：`learn/docs/08-examples.md`
