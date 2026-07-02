# 3D 人形視覺化設計規劃（TF 骨架 + 馬達擺放 + 零點紅線）

## 變更摘要

新增設計文件 [`docs/design/3d-humanoid-visualizer.md`](../design/3d-humanoid-visualizer.md)，
規劃把現有 Python 假硬體（14 軸雙臂、CANopen/CiA402、20Hz WebSocket 遙測）從 2D Canvas
升級為互動式 **3D 人形視覺化**。核心決策：

- **模型來源**：自寫 URDF（link/joint/origin/axis/limit）作為前後端唯一真相來源。
- **外觀**：匯入 CAD 網格（STL/glTF），未到貨前以基本幾何（圓柱/膠囊）替代。
- **UI**：新開專用 3D 頁面 `viewer3d.html`（three.js），連同一 `ws_server`，側欄含設定面板。
- **零點慣例**：所有關節 q=0 時雙臂沿 −Z **自然垂下**（人站立手臂貼身）。
- **馬達零點紅線**：每顆馬達轉子畫紅色徑向標線，對齊定子參考刻痕即機械零點。

文件涵蓋：座標/TF 慣例、3-1-3 擬人臂零位定義、資料流（前端 FK + 後端 model/config 端點 +
靜態 HTTP server）、three.js 場景圖對映 TF、紅線設計、設定 UI 架構、分 P0–P5 落地路線、風險。

## 動機 / 背景

使用者希望把假硬體升級成「有 3D、人形 TF、依 TF 擺放馬達」的模擬器，並要求馬達零點有紅色
標線、初始姿態為手臂自然垂下（人體垂下即零點）。此文件為後續實作前的深度規劃。

## 影響範圍

- 僅新增文件，**未改動任何程式碼或硬體行為**。
- 規劃階段將影響（實作時）：`firmware/sim_py/`（`urdf_loader.py`、`ws_server.py`、新增 `model/`）
  與 `firmware/ui/`（新增 `viewer3d.html` 與 vendored three.js）。

## 驗證方式

- 文件審閱。實作各階段（P0–P5）於各自變更文件中定義驗收（見設計文件 §7）。

## 關聯

- 分支：`develop`
- 設計文件：[3D 人形視覺化](../design/3d-humanoid-visualizer.md)
- 相關：[假硬體模擬器](../design/sim-fake-hardware.md)、[雙臂控制規劃](../design/dual-arm-control-plan.md)
