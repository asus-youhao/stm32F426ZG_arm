# P1：後端 ws_server 擴充（模型/設定端點 + 靜態 HTTP + 遙測 q）

> 分支：`feature/3d-humanoid-visualizer`　對應設計：[3D 人形視覺化](../design/3d-humanoid-visualizer.md) §4, §7(P1)

## 變更摘要

擴充 `firmware/sim_py/ws_server.py`，讓前端 3D 檢視器能取得模型、驅動骨架、雙向改設定：

- **靜態 HTTP 伺服器**（`http.server.ThreadingHTTPServer`，預設埠 8080）：
  serve `firmware/` 目錄，附 CORS 與 no-store 標頭。
  - 3D 檢視器：`http://localhost:8080/ui/viewer3d.html`
  - 模型：`http://localhost:8080/sim_py/model/dual_arm.urdf`
  - 設定：`http://localhost:8080/sim_py/model/robot_config.json`
  - 解決前端以 `file://` 無法 fetch/module import 的限制。
- **啟動時載入** `model/robot_config.json`，把 `control.Kp`/`Kd_ratio` 套到 14 顆馬達，
  記錄 `model_rev` 與各軸 `home_offset`。
- **新增 WebSocket 命令**：
  - `set_config`：整包 `{config:{...}}` 或 dotted-path `{path:"control.Kp", value:150}`；
    寫回 `robot_config.json`、`model_rev++`、即時重新套用（Kp/Kd）。
  - `preset`：`{name:"home_arms_down"|"demo_bend"}` 套用具名姿態到各軸目標。
- **遙測 JSON 擴充**（相容既有欄位，只新增）：每顆馬達加 `q`(rad)、`qTarget`(rad)、
  `home`(rad)；頂層加 `model_rev`（前端據此決定是否重新 fetch 模型/設定）。

## 動機 / 背景

3D 檢視器需要：(1) 能拿到 URDF/設定；(2) 每幀取得關節角度以驅動 TF 骨架；
(3) 在 UI 改馬達擺放/零位/控制參數並持久化。原 `ws_server` 只廣播 counts 級遙測、
且前端只能 `file://` 開啟無法 fetch。故新增 HTTP 服務與 model/config 命令與 rad 級遙測。

## 影響範圍

- 修改：`firmware/sim_py/ws_server.py`（新增 HTTP thread、config 載入/套用、set_config/preset、
  遙測欄位）。
- 依賴：`model/robot_config.json`（P0 產出）。
- **相容性**：遙測只「新增」欄位、命令只「新增」種類，既有 `host_ui_ws.html` /
  `can_monitor_ws.html` 不受影響。
- 不影響 MCU 韌體。埠：新增對外 TCP **8080**（HTTP，可用 argv[2] 覆寫）。

## 驗證方式

```bash
cd firmware/sim_py
# 單元：Sim 設定/preset/遙測
python3 -c "import ws_server; s=ws_server.Sim(); t=s.telemetry(); \
print('rev',t['model_rev'],'has q/qTarget/home', all(k in t['motors'][0] for k in ('q','qTarget','home'))); \
s.set_config({'path':'control.Kp','value':150}); print('Kp',s.M[0].Kp,'rev',s.model_rev); \
s.apply_preset('demo_bend'); print('goal[1,3]',s.goal[1],s.goal[3])"

# 端到端：啟動並打 HTTP
python3 ws_server.py 8765 8080 &
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:8080/sim_py/model/dual_arm.urdf   # 200
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:8080/ui/viewer3d.html             # 200（P2 後）
```
實測：HTTP 均回 200；config 正確回 `rev=1 Kp=120.0`；遙測含 `q/qTarget/home/model_rev`；
`set_config` 使 Kp=150、rev+1 並寫檔；`preset demo_bend` 正確設定各軸目標。
（測試後已將 `robot_config.json` 還原為預設 rev=1 / Kp=120。）

## 待補 / 風險

- `home_offset` 目前僅出現在遙測供前端顯示/畫零點基準，尚未接入 CiA402 `0x607C` 控制路徑。
- HTTP server 為開發用途（無認證），僅供區網模擬。

## 關聯

- 分支：`feature/3d-humanoid-visualizer`；前置：[P0 URDF 模型+FK](./2026-07-02-p0-urdf-model-fk.md)
- 下一步：P2 `viewer3d.html`（three.js）依 `get_model` 建 TF 場景、遙測 `q` 驅動、紅色零點標線。
