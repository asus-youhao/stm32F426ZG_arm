# 08 · 對照範例碼（learn/examples/）

每節 / 每里程碑一份最小可跑範例，共 30 份（總表見 `learn/examples/README.md`）。

## 分層策略（免硬體優先）

| 層 | 手法 | 依賴 |
| --- | --- | --- |
| L1 | `common/hal_stub.h` 迷你 HAL 替身，同一份 main.c host/target 雙用 | 無 |
| L2 / 專案A | vcan(SocketCAN) + `common/fake_slave.py`（純 stdlib 假 CANopen/CiA402 關節：HB/SDO/RPDO/TPDO/狀態機） | `sudo vcan_setup.sh` 一次 |
| L3 | 純邏輯（狀態機/單位/IK/看門狗）直接編譯；EtherCAT 骨架無 SOEM 編 dry-run | 無 |
| 專案B | 體檢腳本 + pysoem/SOEM 骨架（真從站才跑得動的明確標註） | 真機類 |

## 驗證紀錄（2026-07-07）

- `make`：23 個 C 目標全部 `-Wall -Wextra` 零警告編譯通過。
- `make run`：13 個零依賴範例全數通過（cia402 斷言、units 往返、wp4 軌跡誤差 4.8e-13、
  watchdog buggy/fixed 對照、wp3 全軸到位…）。
- Python ×4 `py_compile` 通過、shell ×5 `bash -n` 通過、wp7 stdin 協定互動實測。
- vcan 對打類（l2/06、wp5、wp6）因本機無免密 sudo 未實跑，指令：
  `sudo common/vcan_setup.sh && make cansim`。

## 設計備註

- IK 用 2 連桿 Newton 法（J 反解 + 奇異點保護 + 步長夾限）— 初版 Jacobian 轉置法
  收斂太慢（誤差停在 1e-4 級），已更正。
- `fake_slave.py` 刻意用 `socket.AF_CAN` 純 stdlib，不引入 python-can 依賴。
- 專案 B 的 m5 骨架把 52953088 counts/圈寫成常數並在 dry-run 印出與 524288 的對照，
  呼應 L3-14 的單位教訓。
