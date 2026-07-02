# P5：3D 檢視器 × 真實 CAN 整合（測 F746 主站，PC 當假 CiA402 從站）

> 分支：`feature/3d-humanoid-visualizer`　對應設計：[3D 人形視覺化](../design/3d-humanoid-visualizer.md)

## 變更摘要

讓 3D 人形檢視器能顯示**真實 CANopen/CiA402 交握**下的馬達動作——情境①：
F746 當 CANopen 主站，PC 透過 CANable 當 14/7 顆假 CiA402 從站，3D 即時反映主站指令。

- `firmware/sim_py/ws_server.py` 新增**真實 CAN 模式**：
  - `--interface / --channel / --bitrate / --nodes` 參數（`argparse`；沿用位置參數 `ws_port http_port`）。
  - `can_loop()`：以 `python-can` 開 bus，用 `can_slave.CiA402Slave.handle_frame` 回應主站；
    每個模擬 node 綁到 `sim.M[node-1]`（**共用同一顆 `PhuMotor`**），因此馬達 `q` 直接反映
    真實 RPDO/SDO 指令，遙測與 3D 立即同步。
  - CAN 模式下自驅控制迴圈自動停用（改由主站驅動）；收到/送出的幀計入既有 `frames`/`cobids`/`tx`/`rx`。
  - 遙測新增 `source` 欄位（`sim` 或 `canable:<if>@<ch>`）。
- `firmware/ui/viewer3d.html`：HUD 顯示資料來源 `源：sim / canable`。

### 為何是「小橋接」而非重寫
兩條假硬體線（`ws_server` 3D 與 C1 `web_monitor`/`can_slave`）**本來就共用 `phu_motor.py`**。
本次只是把 `ws_server` 的馬達交給 `CiA402Slave.handle_frame` 在真實 bus 上驅動，
其餘 3D/遙測管線完全沿用。

## 影響範圍

- 修改：`firmware/sim_py/ws_server.py`（新增 CAN 模式、argparse、source）、
  `firmware/ui/viewer3d.html`（HUD 顯示 source）。
- 相依：真實 CAN 模式需 `python-can`（未裝時優雅提示，server 仍以其餘功能運行）。
- 純軟體模式（不加 `--interface`）行為不變；既有 UI 相容。
- 不影響 MCU 韌體。

## 使用方式

```bash
cd firmware/sim_py
# 純軟體 3D（情境②，無硬體）
python3 ws_server.py

# 真實 CAN：PC 當假從站測 F746 主站（情境①）
python3 ws_server.py 8765 8090 --interface slcan --channel COM11 --nodes 1:PHU20,2:PHU20,3:PHU17,4:PHU17,5:PHU14,6:PHU14,7:PHU14
# 瀏覽器開 http://localhost:<http-port>/ui/viewer3d.html → F746 一動，3D 手臂跟著動
```
> 單一 CANable 一次接一條 bus（一支手臂 7 顆 node）；雙臂需兩個介面或 F746 雙通道。

## 驗證方式（實際執行）

1. 語法：`node --check` viewer 模組 → OK。
2. **虛擬 CAN 端到端**（`python-can` `virtual` bus，無需硬體）：模擬主站對 node1 送
   SDO(設 CSP 0x6060=8) + RPDO1(使能 0x06→0x07→0x0F + 目標 0.5rad) →
   `sim.M[0]` 進入 **OP_ENABLED、mode=8(CSP)、q=0.462→0.5rad**，`source=canable:virtual@tbus`，
   frames/cobids 正確累計。→ 證明主站指令會驅動 3D 所讀的同一份馬達狀態。
3. **純軟體回歸**：瀏覽器載入 viewer3d，連線正常、`源：sim`、14 軸幾何、**無 console error**。

## 待補 / 風險

- 真實硬體（CANable + F746）尚未實測；虛擬 bus 已驗證邏輯路徑。
- 單 bus 下 node id 1..7 對應左臂 `sim.M[0..6]`；雙臂同時需兩介面。
- 情境③（真 EYOU 馬達）：把「假從站算出的 q」換成「被動讀 TPDO/SDO 0x6064 真編碼器」即可共用 3D。

## 關聯

- 分支：`feature/3d-humanoid-visualizer`；前置：[P2 3D 檢視器](./2026-07-02-p2-viewer3d-threejs.md)、
  [P1 後端端點](./2026-07-02-p1-ws-server-model-endpoints.md)
- 共用模型：`phu_motor.py`、`can_slave.py`（CiA402 從站）
