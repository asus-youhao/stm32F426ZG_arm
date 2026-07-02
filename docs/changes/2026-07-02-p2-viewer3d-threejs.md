# P2：viewer3d.html — three.js 3D 檢視器（TF 場景 + 馬達零點紅線）

> 分支：`feature/3d-humanoid-visualizer`　對應設計：[3D 人形視覺化](../design/3d-humanoid-visualizer.md) §5, §7(P2)

## 變更摘要

新增前端 3D 檢視器，將後端 14 軸即時遙測以人形 TF 骨架呈現：

- **新增 `firmware/ui/viewer3d.html`**（three.js，ES module）：
  - 瀏覽器端解析 URDF（`DOMParser`）→ 建 **Z-up TF 場景圖**：每個 joint 一個
    origin 群（定子）+ 繞軸 pivot（轉子），child link 掛在 pivot 下 → 父子關係即 TF。
  - **馬達**：依型號（PHU20/17/14）以圓柱幾何沿轉軸渲染（P3 換 CAD 網格）。
  - **馬達零點紅線**：轉子上紅色徑向標線 + 定子上灰色參考刻痕；`q=0` 對齊，
    轉動時夾角即角度（達成需求）。
  - **手臂垂下＝零**：`q=0` 時雙臂沿 −Z 垂下（瀏覽器 FK 實測 L_hand=(0,0.20,−0.08)）。
  - 即時遙測（WS 20Hz）驅動關節；**目標姿態半透明殘影**（qTarget）；**扭矩上色**。
  - OrbitControls、地面格線、世界/關節 TF 座標軸（可開關）、末端位置 HUD。
  - 側欄：連線狀態、使能/急停、姿態 preset（垂下/示範彎曲）、顯示開關、
    Kp 設定（`set_config`）、14 軸 Jog 滑桿（依 soft limit 範圍，送 `jog`）。
  - `model_rev` 改變時自動重載模型（設定熱更新）。
- **vendored three.js（離線）** 於 `firmware/ui/vendor/`：`three.module.js`、
  `OrbitControls.js`、`STLLoader.js`、`GLTFLoader.js`；`firmware/ui/utils/BufferGeometryUtils.js`。
  透過 import map 將 `three` 映到本地檔，不依賴 CDN。
- **後端啟動姿態修正**：`ws_server.Sim.goal` 預設由舊示範彎曲姿改為**全零（手臂垂下）**，
  符合零點慣例。

## 動機 / 背景

需求：3D 人形、依 TF 擺放馬達、馬達零點紅線、初始手臂垂下＝零。前端原為 2D Canvas
且無 3D。故以 three.js 建 TF 骨架，並把「零點」在視覺與啟動姿態上都對齊「手臂垂下」。

## 影響範圍

- 新增：`firmware/ui/viewer3d.html`、`firmware/ui/vendor/*`、`firmware/ui/utils/BufferGeometryUtils.js`
- 修改：`firmware/sim_py/ws_server.py`（`Sim.goal` 預設全零；HTTP 埠自動避讓：
  偏好埠被占用時於 +20 範圍內找空埠並印出實際埠）。
- 不影響 MCU 韌體；不動既有 `host_ui*.html` / `can_monitor*.html`。

## 驗證方式（實際執行）

1. 語法：抽出 module JS，`node --check` → OK。
2. 啟動 `python3 ws_server.py`（HTTP 偏好 8090，因本機占用自動改用 8091/8095）。
3. 於瀏覽器載入 `http://localhost:<port>/ui/viewer3d.html`（本次用 preview 工具實跑）：
   - 連線成功、模型 `dual_arm_humanoid` rev 1、14 軸 Jog 滑桿建立、**console 無錯誤**。
   - 按「手臂垂下(零)」→ 馬達回零，HUD 末端 **L=(0.00, 0.20, −0.08)、R=(0.00, −0.20, −0.08)**，
     與 Python `urdf_loader` FK 完全一致。
   - 截圖確認：人形骨架、各關節馬達圓柱、**紅色零點紅線**、目標殘影、扭矩上色皆正常。

## 待補 / 風險

- 馬達與連桿為幾何 fallback，P3 導入 CAD 網格（STL/glTF）後替換。
- 3 顆肩關節同點，視覺上呈叢集（結構正確，之後可用網格美化）。
- HTTP/WS 為開發用途、無認證。

## 關聯

- 分支：`feature/3d-humanoid-visualizer`；前置：[P1 後端模型/設定端點](./2026-07-02-p1-ws-server-model-endpoints.md)
- 下一步：P3/P4 — CAD 網格載入 fallback + 側欄設定面板擴充（擺放/零位/極限）。
