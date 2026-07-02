# Web 上位機：選取 motor 的 CANopen rx/tx 資料流

## 變更摘要

`web_monitor.py` 新增右側「CAN 資料流」面板：即時顯示**目前選取那顆 motor** 的 CANopen
幀流（RX/TX、COB-ID、依 COB-ID 解析的種類、資料 hex）。點任一關節卡即切換追蹤對象。

- 真機模式：直接記錄 F746↔從站之間 python-can 收到/送出的每一幀。
- demo 模式：無實體 bus，故對追蹤的 motor 產生等效幀交換（~20Hz RPDO1→TPDO1 + ~1Hz SDO 輪詢）。

方向採「從站視角」：**RX = 主站→從站**（RPDO1 / SDO-req / NMT）、**TX = 從站→主站**
（TPDO1 / SDO-rsp / HB）。

## 動機 / 背景

使用者要在 web 上看到真實 CANopen rx/tx 資料流，先只顯示一顆選取的 motor。

## 影響範圍

| 檔案 | 變更 |
| ---- | ---- |
| `firmware/sim_py/web_monitor.py` | 加 frame ring buffer + `frame_kind()` COB-ID 解析 + `log_frame()`；`apply_command` 加 `trace` 動作（切追蹤對象）；demo 加 `_demo_exchange()` 產生幀；`run_can` 記錄真實 bus 幀；前端加資料流面板與渲染；`faultreset` 改為完整重新使能 |

附帶修正：

- SSE handler 例外加入 `ConnectionAbortedError`/`OSError`（瀏覽器斷線不再噴 traceback）。
- `do_GET` 路由忽略 query string（`/?v=2` 不再 404）。

## 驗證方式（demo，已完成）

```bash
cd firmware/sim_py
python web_monitor.py --demo --port 8181   # 避開被占用的 8080/8090
```

Playwright 實測：選取 R_J3 後，面板即時顯示 `RX RPDO1 0x203` ↔ `TX TPDO1 0x183` 週期交換
與 `RX SDO-req 0x603 → TX SDO-rsp 0x583` 輪詢，COB-ID 對應 node3 正確，RX/TX 顏色區分。

## 關聯

- [全面忠實 CiA402 模擬器](./2026-07-01-full-cia402-simulator.md)、[C1 web 看板](./2026-07-01-web-monitor-14axis.md)
- 後續：多 motor 同時追蹤、SDO/PDO 解碼成物件名稱、幀流匯出 .log / candump 格式。
