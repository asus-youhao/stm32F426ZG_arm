# P3/P4：CAD 網格載入（fallback）+ 零位校正設定面板

> 分支：`feature/3d-humanoid-visualizer`　對應設計：[3D 人形視覺化](../design/3d-humanoid-visualizer.md) §6, §7(P3/P4)

## 變更摘要

- **P3 — CAD 網格載入**（`firmware/ui/viewer3d.html`）：
  - 導入 vendored `STLLoader` / `GLTFLoader`。
  - `config.meshes[model]` 有檔名時，非同步載入 `model/meshes/<檔名>`（`.stl` / `.glb` / `.gltf`），
    載入成功即隱藏圓柱、換上網格；檔名為 `null` 或載入失敗 → **保留圓柱幾何 fallback**。
  - 新增 `firmware/sim_py/model/meshes/README.md` 說明放置與設定方式。
- **P4 — 零位校正設定面板**：
  - 側欄新增「零位校正 (home offset)」面板：每軸一個數字輸入（度），
    變更即送 `set_config { path:"joints.<name>.home_offset", value:<rad> }`，
    後端寫回 `robot_config.json` 並 `model_rev++` → 前端自動重載模型。
  - 每顆馬達的**定子灰色參考刻痕**依 `home_offset` 繞軸旋轉，視覺上代表「校正後機械零」。
- 小幅：`viewer3d.html` 加 `window.__dbg`（scene/live/ghost 與 mesh 統計）供除錯。

## 動機 / 背景

需求含「依 TF 擺放馬達」與「馬達零點紅線 / 零位」。P2 已用圓柱呈現，本階段補上
真實 CAD 網格的載入路徑（未到貨前自動 fallback），並提供在 UI 校正每軸零位的設定介面
（對應 CiA402 `0x607C` Home Offset 的視覺化）。

## 影響範圍

- 修改：`firmware/ui/viewer3d.html`（mesh 載入、校正面板、debug hook）
- 新增：`firmware/sim_py/model/meshes/README.md`
- `firmware/sim_py/model/robot_config.json`：格式整理（pretty-print），內容為預設值
  （rev 1、home_offset 全 0、meshes 全 null）。
- 不影響 MCU 韌體。

## 驗證方式（實際於瀏覽器執行）

1. 語法：`node --check` viewer 模組 → OK。
2. **Mesh 載入路徑**：暫時放入測試 `test_cube.stl` 並將 `meshes.PHU14` 指向它、重載頁面：
   `window.__dbg.counts` = `{cyl:12, mesh:6, box:31}` → 6 顆 PHU14 馬達的圓柱成功換成 STL
   （其餘 8 顆馬達 + 4 段連桿仍為圓柱）。測畢還原為 `null`、移除測試檔。
3. **Fallback 路徑**：`meshes` 全 `null` 重載：`{cyl:18, mesh:0}`（14 馬達 + 4 連桿圓柱），
   **console 無錯誤**。
4. **零位校正**：於面板將 `L_J1_shoulder` 設 10° → 後端寫入 `home_offset=0.1745 rad`、
   `model_rev` 由 1→2、前端自動重載。測畢還原為 0。
5. 截圖確認：`demo_bend` preset 下雙臂前彎、紅色零點紅線、校正面板 14 列皆正常。

## 待補 / 風險

- 尚無真實 PHU CAD 檔；到貨後放入 `model/meshes/` 並於 config 填檔名即可。
- 「馬達擺放（origin/連桿長度）」的即時編輯尚未做（需寫回 URDF）；目前擺放由 URDF 決定，
  設定面板先覆蓋零位/控制/顯示。可列後續。
- `home_offset` 目前為視覺與 config 持久化；接入實機 `0x607C` 控制路徑待 P1 之後補。

## 關聯

- 分支：`feature/3d-humanoid-visualizer`；前置：[P2 3D 檢視器](./2026-07-02-p2-viewer3d-threejs.md)
- 下一步：整體架構 HTML 總覽頁。
