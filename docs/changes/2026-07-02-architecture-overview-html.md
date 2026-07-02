# 整體架構總覽 HTML

> 分支：`feature/3d-humanoid-visualizer`　對應設計：[3D 人形視覺化](../design/3d-humanoid-visualizer.md)

## 變更摘要

新增 `firmware/ui/architecture.html`：一頁式、離線可看的整體架構總覽，涵蓋 P0–P4 成果：

1. 系統總覽 + 圖例（紅色零點紅線 / 定子參考 / 扭矩上色 / 目標殘影）
2. **資料流 SVG 圖**：後端 `ws_server`（Sim/500Hz、model 載入、WS:8765、HTTP:8090）
   ↔ 前端 `viewer3d`（fetch 模型→TF 場景→遙測驅動→控制/設定 UI）
3. 座標系與「手臂垂下＝零點」、3-1-3 擬人臂 TF 鏈
4. 關節↔馬達對應表（14 軸）
5. 模組職責表
6. WebSocket 協定（命令 + 遙測 schema）
7. 實作路線圖（P0–P4 狀態）
8. 如何執行

純內嵌 CSS/SVG、無外部相依，可直接 `http://localhost:<port>/ui/architecture.html` 開啟。

## 動機 / 背景

使用者要求「做完最後給我整體架構的 HTML」。此頁總結整個 3D 人形模擬器的後端/前端/
資料流/座標零點/協定/檔案結構，供快速理解與交接。

## 影響範圍

- 新增：`firmware/ui/architecture.html`（純文件頁，無行為影響）。
- 不影響 MCU 韌體與既有程式。

## 驗證方式

- 以 preview 於瀏覽器載入 `ui/architecture.html`，確認版面、SVG 資料流圖、各表格正常呈現。

## 關聯

- 分支：`feature/3d-humanoid-visualizer`
- 前置：[P0](./2026-07-02-p0-urdf-model-fk.md) · [P1](./2026-07-02-p1-ws-server-model-endpoints.md) ·
  [P2](./2026-07-02-p2-viewer3d-threejs.md) · [P3/P4](./2026-07-02-p3-p4-mesh-and-settings.md)
- 設計文件：[3D 人形視覺化](../design/3d-humanoid-visualizer.md)
