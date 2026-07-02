# bringup_decode 跨平台化（WSL / Ubuntu 可跑，slcan）

## 變更摘要

- 補回缺失的相依模組 `firmware/sim_py/can_slave.py`（原本只存在於 worktree，主目錄沒有，
  導致 `bringup_decode.py` 一執行就 `ModuleNotFoundError: No module named 'can_slave'`）。
- 將 `firmware/sim_py/bringup_decode.py` 改為**跨平台**：
  - `--channel` 預設值改為依作業系統決定 —— Windows 沿用 `COM11`；Linux / WSL 自動偵測
    第一個 `/dev/ttyACM*` 或 `/dev/ttyUSB*`（CANable/slcan 常見）。
  - 開埠失敗時，依環境（Windows / Ubuntu / WSL）印出對應的排錯步驟（含 WSL 的 usbipd 掛載指引）。
  - 新增 `_is_wsl()` / `_default_channel()` / `_port_help()` 輔助函式。
- 新增兩支一鍵啟動腳本（皆走 slcan / USB 序列埠）：
  - `firmware/sim_py/run_wsl.sh` —— 自動偵測裝置；找不到時提示 usbipd 掛載步驟；自動裝相依套件。
  - `firmware/sim_py/run_ubuntu.sh` —— 自動偵測裝置；檢查 dialout 寫入權限；自動裝相依套件。

## 動機 / 背景

原本 `bringup_decode.py` 硬編碼 `--channel COM11`，只能在 Windows 端跑。使用者希望在
**WSL 與原生 Ubuntu** 也能執行。經確認 CAN 轉接器在 Linux 端一律以 **slcan（USB 序列埠）**
方式呈現（`/dev/ttyACM0` 之類），因此兩種環境的傳輸層相同，差異只在「USB 如何接到 Linux」：

- Ubuntu：插上即為 `/dev/ttyACM0`。
- WSL：USB 不會自動進來，需先用 `usbipd-win` 於 Windows 端 `attach`，之後同樣是 `/dev/ttyACM0`。

此外發現 `can_slave.py` 未進主目錄，屬於「一執行就爆」的阻斷性缺失，一併補回。

## 影響範圍

- `firmware/sim_py/can_slave.py`（新增；由 worktree 補回，內容未改）
- `firmware/sim_py/bringup_decode.py`（修改：跨平台預設通道 + 友善錯誤）
- `firmware/sim_py/run_wsl.sh`（新增）
- `firmware/sim_py/run_ubuntu.sh`（新增）
- 純 PC 端工具，**不影響任何 MCU 韌體行為**（腳位/時脈/周邊皆未動）。
- Windows 端既有用法 `python bringup_decode.py --channel COM11` 行為不變。

## 驗證方式

於 WSL（Ubuntu 22.04, Python 3.10）驗證：

```bash
cd firmware/sim_py
python3 -c "import bringup_decode"          # import 全通（含補回的 can_slave）
python3 bringup_decode.py                   # 無 --channel → 自動抓 /dev/ttyACM0，
                                            #   無裝置時印出 WSL usbipd 排錯指引
bash run_wsl.sh                             # 無裝置 → 印 usbipd 掛載步驟
```

實機驗證（待硬體）：
- Ubuntu：插上 CANable → `./run_ubuntu.sh` → 按 F746 RESET → 應印出主站 bring-up 序列。
- WSL：先 `usbipd attach --wsl --busid <BUSID>` → `./run_wsl.sh` → 同上。

## 關聯

- 分支：`develop`
- 相關：`firmware/sim_py/`（Python 假硬體 / CANopen 從站模擬），
  參見 [Python 假硬體](./2026-06-25-python-fake-hardware.md)、
  [完整OD+測試上位機](./2026-06-25-full-od-test-host.md)。
