# C1 上位機 web 看板（14 軸即時遙測）

## 變更摘要

新增 `firmware/sim_py/web_monitor.py`：純標準庫（`http.server` + SSE）的 web 即時看板，
把雙臂 **14 軸**從站的位置 / 扭矩 / 電流 / CiA402 狀態推到瀏覽器即時顯示。後端零第三方
相依、單檔內嵌前端（vanilla JS，`EventSource` 訂閱 `/events`）。

兩種資料來源：

- `--demo`：後端自驅 14 顆模擬關節（CSP 正弦），**無任何硬體**即可看畫面動。
- 真機（C1）：F746 主站透過 CANable 驅動 PC 上的 `CiA402Slave`，看板顯示從站即時狀態。

## 動機 / 背景

C1 路線（F746 主站 + PC 假從站）下，「web 前端後端看 data」需自行實作。選 SSE + 標準庫，
避免引入 Flask/websockets，`python web_monitor.py --demo` 即可跑，降低使用門檻；真機時
與 [`can_slave.py`](../../firmware/sim_py/can_slave.py) 共用同一套 `CiA402Slave`/`PhuMotor`。

## 影響範圍

| 檔案 | 變更 |
| ---- | ---- |
| `firmware/sim_py/web_monitor.py` | **新增**。`TelemetryHub`（14 軸狀態 + demo/CAN 驅動）、`http.server` SSE、內嵌雙臂看板 HTML |

- 關節配置依 CLAUDE.md：每臂 J1,J2=PHU20 / J3,J4=PHU17 / J5,J6,J7=PHU14。
- 重用既有 `can_slave.CiA402Slave` 與 `phu_motor.PhuMotor`（含本批次新增的 PV 模型），無新增相依。
- 純讀取/顯示，不改韌體、不影響硬體行為。

## 驗證方式

### 已完成（demo，無硬體）

```bash
cd firmware/sim_py
python web_monitor.py --demo --port 8087
# 瀏覽器開 http://127.0.0.1:8087
```

- `GET /` 回傳看板 HTML。
- `GET /events` 持續串出 SSE JSON（14 軸 pos/vel/sw/state/torque/current + rx/tx 計數）。
- 以 Playwright 實際渲染截圖：雙臂分欄、14 卡即時更新、CiA402 狀態徽章顯示 operation-enabled。
- 唯一 console 訊息為 `favicon.ico` 404（無害）。

### 真機（C1，待硬體）

```bash
python web_monitor.py --interface gs_usb --channel 0 --bitrate 1000000
```
F746 主站經 CANable 驅動從站，看板顯示各軸即時遙測。

## 關聯

- [C1：CANable + Python 假從站](./2026-07-01-c1-canable-python-slave.md)（同批，提供 `CiA402Slave`）
- [WP6 安全 + WP7 上位機](./2026-06-25-wp6-safety-wp7-host.md)（上位機遙測欄位概念）
- 後續可加：時間序列曲線、SDO 寫入/下命令面板、與 `host_if` 二進位協定對接。
