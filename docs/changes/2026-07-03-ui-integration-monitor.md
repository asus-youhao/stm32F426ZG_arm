# UI 整合：ws_server 監聽模式——3D 動畫 + CAN 資料流看真實主站交握

> 分支：`feature/pc-canopen-master`　前置：[PC 端 CANopen 主站](./2026-07-03-pc-master-socketcan.md)

## 變更摘要

讓瀏覽器 UI（3D 人形動畫 + CAN 資料流面板）反映 **pc_master（C 韌體主站）在真實
vcan bus 上的交握**。架構採「旁聽者」而非「兼職從站」：

- 主站：`pc_master`（L0–L4 C 堆疊）
- 從站：`can_slave.py` ×14（物理模擬，經 2026-07-03 step 修正）
- **`ws_server.py --monitor`（新增）**：第三方旁聽 vcan0+vcan1，把 TPDO 回授鏡射到
  馬達狀態（→ 3D 動畫）、把所有幀鏡射到資料流面板;不參與交握。

`ws_server.py` 變更：

- 新增 `--monitor` 與 `--channel2`（雙 bus:bus0=左臂 M[0..6]、bus1=右臂 M[7..13]）。
  TPDO1 → `q`/`statusword`,RPDO1 → `controlword`/`target_counts`,
  SDO/NMT/HB 進資料流。幀記錄依 (bus,COB-ID) 取樣 20Hz（500Hz×28 幀全記太貴），
  tx/rx 計數器仍逐幀累計。
- **修潛在 bug**：`MODE=="can"`（假從站模式）下無人推進馬達物理——CSP 目標下去
  q 永遠不動。現於 `control_loop` 以實測 dt 步進（monitor 模式不步進,位置來自 bus）。
- 遙測狀態解碼改 `sw_state()`（CiA402 位元遮罩）：真主站的 statusword 帶
  bit10/12 旗標（如 0x1427），原整值查表會顯示 "?"。
- 新增 `firmware/pc/run_demo_ui.sh` 一鍵 UI demo：檢查系統級 vcan →
  起從站+監聽伺服器 → 印 UI 網址 → pc_master 連續示範動作（雙臂揮手循環）。

## 為何用監聽模式（而非沿用 P5 的 ws_server 當從站）

1. 職責分離：從站物理只有一份（can_slave.py，已驗證），ws_server 純顯示。
2. 負載分散：14 軸 500Hz 交握由兩個從站行程扛，監聽行程只鏡射+取樣。
3. **通往真馬達**：接真 EYOU 關節時（P5 情境③），同一個 `--monitor` 直接旁聽
   真編碼器 TPDO，3D 即顯示實機姿態——不需要再寫新程式。

## 影響範圍

- `firmware/sim_py/ws_server.py`：新增 monitor 模式;sim 模式行為不變
  （回歸已測）;can 假從站模式多了物理步進（原為 bug）。
- `firmware/pc/run_demo_ui.sh`（新增）、`firmware/pc/README.md`（用法）。
- UI HTML 零修改——viewer3d/can_monitor_ws 本來就吃同一份遙測格式。

## 使用方式

```bash
sudo ./firmware/pc/setup_vcan.sh   # 一次性；瀏覽器要連 HTTP，需系統級 vcan
./firmware/pc/run_demo_ui.sh      # 起全部；瀏覽器開印出的網址
#   3D 動畫    http://localhost:8090/ui/viewer3d.html
#   CAN 資料流 http://localhost:8090/ui/can_monitor_ws.html
```

## 驗證方式（實際執行）

1. **端到端**（`unshare -rn` + `ip link set up lo`;無需 sudo）：
   從站×14 + `ws_server --monitor` + `pc_master` 連續動作，WebSocket 探針斷言：
   - `viewer3d.html`/`can_monitor_ws.html` HTTP 200
   - `source = monitor:socketcan@vcan0+vcan1`
   - **q 隨時間變化的軸 = [0,3,7,10]**（正是下命令的軸 → 3D 有動畫）
   - 14 軸全 `OP_ENABLED`（sw_state 正確解 0x1427 等旗標值）
   - 資料流含 RPDO1+TPDO1、36 個 COB-ID、雙 bus tx/rx 各 ~45k 幀
   - 主站同時 **0 丟幀**（監聽不影響交握）
2. **sim 模式回歸**：無參數啟動，`source=sim`、14 軸 OP_ENABLED、HTTP 200。
3. `can_slave.py --selftest/--modetest`：通過（前次已驗，本次未動）。

## 待補 / 風險

- monitor 每 bus 一執行緒逐幀解析（14k 幀/s 總量），Python 單行程約占半核;
  更多軸或更高頻率時可改 BCM/epoll 或 filter。
- `run_demo_ui.sh` 需系統級 vcan（namespace 內的 HTTP 瀏覽器連不到）;
  無 sudo 環境可用終端版 `run_demo.sh`。

## 關聯

- 分支：`feature/pc-canopen-master`
- 前置：[PC 主站](./2026-07-03-pc-master-socketcan.md)、[P5 3D×真實CAN](./2026-07-03-p5-canopen-realbus-integration.md)
- UI：[P2 3D 檢視器](./2026-07-02-p2-viewer3d-threejs.md)、[Web CAN 資料流](./2026-07-01-web-can-dataflow.md)
