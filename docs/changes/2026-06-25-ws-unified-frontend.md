# 前端統一接同一份 Python 假硬體（WebSocket）

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

把 `host_ui` 與 `can_monitor` 兩個前端統一接到**同一份 Python 假硬體**(`phu_motor.py` 的真實 OD),不再各用自己的 JS 模型:

- `firmware/sim_py/ws_server.py`：**純標準庫 WebSocket 伺服器**(RFC6455 自實作,無第三方相依)。內含 14 顆 PhuMotor + ~500Hz 控制迴圈 + 命令處理 + 20Hz 遙測廣播。
- `firmware/ui/host_ui_ws.html`：連線版上位機（控制 + 馬達遙測 + OD 讀取）。
- `firmware/ui/can_monitor_ws.html`：連線版 CAN 監控（COB-ID 表 + node 狀態 + 幀串流）。

命令：enable / estop / jog / mode / read / write（OD 讀寫）。
遙測：每顆馬達 cw/sw/狀態/目標/實際/扭矩/電流 + 最近 CAN 幀 + COB-ID 表。

## 動機 / 背景

先前 `host_ui.html` 與 `can_monitor.html` 各自內嵌 JS 假馬達,與 Python 模型重複且會發散。改為統一後端。

## 驗證結果

- 純標準庫 WebSocket 測試客戶端連線：收到 14 顆馬達遙測;送 enable+move 後 L_J4 OP_ENABLED、力 3.12 N·m / 電流 0.78 A（後端 phu_motor.py 真實值）。
- 以內建 Chromium 載入 `can_monitor_ws.html`：顯示「已連線」,COB-ID 表/狀態/力電流為後端即時數據。
- 兩前端連同一 server → 同一份數據。

## 影響範圍

- 新增 `ws_server.py`、`host_ui_ws.html`、`can_monitor_ws.html`;更新 sim_py/README。
- 舊的離線版 `host_ui.html` / `can_monitor.html` 保留（無需後端的展示用）。

## 限制

- WebSocket 為教學用最小實作（單片 text frame、未處理分片/ping）。
- 仍為假硬體;真機應走 WP7 host_if 二進位協定或 SocketCAN 閘道。

## 關聯

- `python-fake-hardware.md`、`full-od-test-host.md`、`sim-fake-hardware.md`
