# 3D 人形視覺化 + TF 骨架 + 馬達擺放：設計規劃

> 狀態：**設計草案（待實作）**　範圍：`firmware/sim_py/`（後端）與 `firmware/ui/`（前端）
> 決策基準：**自寫 URDF ＋ 匯入 CAD 網格（STL/glTF）＋ 新開專用 3D 頁面（three.js）**

## 1. 目標與需求

把現有的 Python 假硬體（14 軸雙臂、CANopen/CiA402 模型、20 Hz WebSocket 遙測）
從 2D Canvas 平面示意，升級為**可互動的 3D 人形視覺化**：

1. **3D 人形骨架**：以 URDF 定義的 TF（transform）樹呈現雙臂 7-DoF ×2 = 14 軸。
2. **馬達依 TF 擺放**：每顆 PHU 馬達（PHU14/17/20）依關節在 TF 樹上的位置/朝向渲染，
   外觀採匯入的 CAD 網格（STL/glTF），未有網格前以基本幾何（圓柱/膠囊）替代。
3. **馬達零點紅色標線**：每顆馬達在其機械零點畫一條**紅色徑向標線**；轉子上的紅線
   對齊定子上的固定參考刻痕時代表 `q = 0`。轉動時紅線隨之偏移，直觀顯示角度與零位。
4. **零點姿態 = 手臂垂下**：所有關節 `q = 0` 時，雙臂沿 −Z **自然垂下**（人站立、手臂
   貼身下垂），如同人體自然放鬆姿勢。
5. **設定 UI 軟體架構**：一份「機器人描述（URDF + 設定 JSON）」作為**前後端唯一真相來源**，
   可在 UI 調整馬達擺放、零位校正、關節極限、控制參數、顯示選項，並雙向同步。

### 非目標（本階段不做）
- 不做碰撞偵測 / 物理引擎（僅運動學 + 現有 `PhuMotor` 動態）。
- 不做即時 IK 拖曳末端（可列為後續；後端已有 `ik_step` 可接）。

---

## 2. 座標系與 TF 慣例（關鍵）

| 項目 | 定義 |
| --- | --- |
| 世界座標 | **Z 向上、X 向前、Y 向左**（右手系，重力 −Z，與後端 `kinematics.py` 一致） |
| `world` → `base_link` | 人形軀幹/骨盆原點 |
| 左肩掛載 | `base_link` → `L_shoulder_mount`，origin ≈ `(0, +0.20, Z_sh)`（沿用 `robot.py` base_p 的 ±0.20） |
| 右肩掛載 | `base_link` → `R_shoulder_mount`，origin ≈ `(0, −0.20, Z_sh)` |
| 角度單位 | rad（前端顯示可切 deg）；counts ↔ rad 用 `CPR = 524288/2π` |

> three.js 預設 Y-up；本專案統一 **Z-up**。作法：場景根節點套一個 `world` 群組，
> 用 `THREE.Object3D` 的旋轉把 Z-up 模型對映到 three.js，或直接以 Z-up 建構並調整相機/格線。
> 於前端集中一處處理，其餘節點皆以 Z-up 語意操作。

### 2.1 「手臂垂下 = 零點」的 7-DoF 定義（3-1-3 擬人臂）

依 CLAUDE.md 關節表（J1/J2 肩、J3 肩偏航、J4 肘、J5/J6/J7 腕）採標準 **3-1-3** 擬人構型。
於 URDF 中選定各關節 origin 與 axis，使 **q=0 時所有連桿沿 −Z 向下堆疊**：

| Joint | 功能 | 轉軸(zero 姿態) | q=0 意義 |
| ----- | --- | --- | --- |
| J1 | 肩 pitch（前後擺） | Y | 上臂垂直向下 |
| J2 | 肩 roll（外展/內收） | X | 貼身不外張 |
| J3 | 肩 yaw（沿上臂旋轉） | Z（沿下垂上臂） | 不旋轉 |
| J4 | 肘 flexion | Y | 前臂與上臂共線、續向下 |
| J5 | 腕 pron/supination | Z（沿下垂前臂） | 不旋轉 |
| J6 | 腕 flexion | Y | 手掌延伸向下 |
| J7 | 腕 deviation | X | 不偏擺 |

- 結果：`q = [0,0,0,0,0,0,0]` → 整條手臂為一直線沿 −Z，即「人體垂下」姿勢。
- **與現況差異**：`robot.py` 目前 `Q_INIT = [0,0.3,0,0.7,0,0.5,0]`（微彎示範姿）。
  新慣例下 **home/rest = 全 0 = 手臂垂下**；原本的彎曲姿改為一個具名「展示 preset」。
- 這條零位需對齊 EYOU PHU 的**機械零點**：實機用 CiA402 `0x607C Home Offset` 校正，
  UI 的「零位校正」即寫入該 offset；3D 的紅色標線代表此機械零點（見 §5.3）。

---

## 3. 機器人描述：唯一真相來源

新增兩份檔案，前後端共用：

```
firmware/sim_py/model/
├── dual_arm.urdf         # link/joint/origin/axis/limit + <visual> 引用網格
├── robot_config.json     # 可在 UI 調整的疊加設定（零位offset、Kp/Kd、顯示選項…）
└── meshes/               # PHU14/17/20 及連桿的 STL/glTF（待提供；缺檔時前端用基本幾何）
```

- **URDF** 定義結構性、不常改的東西：關節樹、origin、axis、limit、視覺網格引用。
- **robot_config.json** 定義可在 UI 即時調整、需持久化的東西：每軸 `home_offset`、
  `soft_limit`、控制參數（`Kp/Kd`）、顯示開關（TF 軸、紅線、目標殘影、扭矩上色）。
- 兩者合併後即為前端建場景、後端算 FK/limit 的依據。

`urdf_loader.py` 現況只抓 `<joint>` 名稱；需**重寫為完整解析**：link、joint parent/child、
`<origin xyz rpy>`、`<axis xyz>`、`<limit lower upper>`、`<visual><mesh>`，輸出一棵可被
FK 遍歷的樹（並保留與現有 `JOINT_MAP` 的 node/bus/model 對應）。

---

## 4. 軟體架構與資料流

```
              ┌───────────────────────── 瀏覽器 (viewer3d.html, three.js) ──────────────────────────┐
              │  1) 啟動時 GET 模型：URDF+config+meshes                                              │
              │  2) 依 TF 樹建 Object3D 階層（馬達=網格/圓柱、連桿=connector、紅色零點標線）          │
   HTTP :8080 │  3) 每 20Hz 收遙測 → 設定每個 joint 的 rotation(沿其 axis) = q(actual)               │
  (靜態檔) ───┤     目標 q 以半透明「殘影」疊加；扭矩→顏色                                            │
              │  4) 設定面板改動 → 送 config 命令 → 後端持久化 + 廣播新模型 → 前端重建/更新           │
   WS :8765   └──────────────────────────────────────────────────────────────────────────────────────┘
              ▲ get_model / telemetry / config 讀寫                     ▲ enable/jog/mode/read/write（沿用）
              │                                                         │
   ┌──────────┴─────────────────── ws_server.py（擴充）──────────────────┴──────────┐
   │  • 既有：14×PhuMotor、500Hz 控制、20Hz 遙測、CANopen 命令                       │
   │  • 新增：載入 model/（URDF+config）→ 提供 get_model；telemetry 增 q_actual/q_target│
   │  • 新增：get_config / set_config（改動寫回 robot_config.json 並廣播）             │
   │  • 新增：極小靜態 HTTP server（serve firmware/ui/ 與 model/），解決 file:// 限制  │
   └────────────────────────────────────────────────────────────────────────────────┘
```

### 4.1 FK 放前端（thin backend）
- 後端**只送 14 軸角度**（actual 由 `m.q` 得、target 由 `m.target_counts/CPR` 得），
  前端依已載入的 TF 樹自行設定每個關節節點的旋轉。這是最貼近 TF 的作法、遙測負載最小、
  且能一次驅動整條骨架（不需後端逐 link 算全域變換）。
- 後端既有的 `ArmKin.fk()` 仍保留供末端位姿顯示 / 未來 IK 拖曳。

### 4.2 遙測 JSON 擴充（相容既有欄位）
`ws_server.telemetry()` 的每個 motor 物件增加：
```jsonc
{ "name":"L_J4", "q":0.734, "qTarget":0.800, /* rad */
  "torque":12.3, "current":2.4, "state":"OP_ENABLED", /* …既有欄位不動 */ }
```
新增頂層（僅在載入模型後）：`"model_rev": <int>`（模型版本；改設定後 +1，前端據此決定是否重建）。

### 4.3 新增 WebSocket 命令
| 命令 | 參數 | 作用 |
| --- | --- | --- |
| `get_model` | — | 回傳 URDF(字串) + config(JSON) + mesh 檔清單 |
| `get_config` | — | 回傳目前 robot_config.json |
| `set_config` | `path`, `value`（或整包 `config`） | 改設定→寫檔→重建 FK/limit→`model_rev++`→廣播 |

---

## 5. 前端 3D 視覺化設計（three.js）

### 5.1 交付與相依
- 新檔 `firmware/ui/viewer3d.html` + `firmware/ui/js/`（three.js、OrbitControls、URDF 解析、場景建構）。
- **three.js 在地化 vendored**（放 `firmware/ui/vendor/`），不依賴 CDN——嵌入式開發常在離線環境。
- 靠後端新增的靜態 HTTP server 提供（module import + fetch URDF/mesh 需 http，不能 file://）。

### 5.2 場景圖對映 TF 樹
- 逐一把 URDF 的 link/joint 建成巢狀 `THREE.Object3D`；joint 節點掛一個「旋轉子節點」，
  每幀只更新該節點繞其 `axis` 的角度 = `q`。父子關係即 TF，天然正確。
- 馬達本體：有 mesh 就載 STL/glTF；無 mesh 時依型號用圓柱（PHU20>17>14 尺寸遞減）。
- 連桿：兩關節間以 connector（長方體/膠囊）連接。
- 輔助：地面格線、每個 frame 的 TF 座標軸（RGB=XYZ，可開關）、末端位姿標記。

### 5.3 馬達零點紅色標線（重點需求）
- 每顆馬達的**轉子**上貼一條紅色徑向標線（細長方體/貼圖），位於該關節 local `q=0` 方位。
- 定子（父側殼體）畫一個固定的**參考刻痕**。`q=0` 時兩者對齊。
- 關節旋轉時，紅線隨轉子繞 axis 轉動 → 與參考刻痕的夾角即目前角度，零位一目了然。
- 語意上這條紅線 = 機械零點；搭配「零位校正」寫 `0x607C Home Offset` 後，紅線即代表校正後的零。

### 5.4 狀態呈現
- **實際 vs 目標**：實際姿態實體渲染；目標姿態（`qTarget`）以半透明殘影疊加。
- **扭矩上色**：沿用現有綠→紅映射（`torque/peak`），套在馬達本體材質。
- **狀態**：非 OP_ENABLED / ESTOP / fault 時，馬達外框變色或閃爍。

---

## 6. 設定 UI 軟體架構

`viewer3d.html` 側欄分頁面板，全部以 `robot_config.json` 為單一狀態來源、經 `set_config` 雙向同步：

| 面板 | 可調項 | 寫入位置 | 影響 |
| --- | --- | --- | --- |
| 馬達擺放 | 各關節 origin(xyz/rpy)、axis、連桿長度、馬達型號 | URDF/config | 前端重建骨架、後端重算 FK |
| 零位校正 | 每軸 `home_offset`（對應 `0x607C`） | config → OD | 移動零點；紅線基準隨之移動 |
| 關節極限 | 每軸 soft `lower/upper` | config | 前端超限標紅、後端夾限 |
| 控制參數 | `Kp/Kd`、扭矩上限 | config → `PhuMotor` | 動態行為 |
| 顯示選項 | TF 軸、紅線、目標殘影、扭矩上色、deg/rad | 前端本地 | 純顯示 |
| 動作 preset | 儲存/載入具名姿態（含「手臂垂下=全0」與示範彎曲姿） | config | 一鍵擺位 |

改動流程：UI 調整 → `set_config` → 後端寫檔 + `model_rev++` + 廣播 → 前端據 `model_rev` 熱更新。

---

## 7. 分階段落地路線（每階段獨立可驗、皆補變更文件）

| 階段 | 產出 | 驗收 |
| --- | --- | --- |
| **P0 模型真相來源** | `model/dual_arm.urdf`（手臂垂下=零）、`robot_config.json`；重寫 `urdf_loader.py` 完整解析；新增 `fk_from_urdf` 或以 URDF 樹取代 DH | CLI 印出 q=0 時各 link 全域座標，雙臂沿 −Z 垂直 |
| **P1 後端擴充** | 靜態 HTTP server；`get_model`/`get_config`/`set_config`；telemetry 增 `q/qTarget`、`model_rev` | 瀏覽器可 GET 到 URDF；WS 收到含 q 的遙測 |
| **P2 3D 檢視器** | `viewer3d.html` + vendored three.js；TF 場景圖、馬達（幾何 fallback）、**紅色零點標線**、OrbitControls、格線/TF 軸 | 連上後端，14 軸隨遙測即時動；q=0 手臂垂下、紅線對齊 |
| **P3 CAD 網格** | 載入 PHU14/17/20 STL/glTF（待提供），取代幾何 | 馬達外觀為真實 CAD |
| **P4 設定 UI** | 側欄各面板 + `set_config` 雙向同步 + 熱更新 + preset | 調 origin/零位/極限即時反映；重開後保留 |
| **P5 整合/文件** | 導覽連結、README、change docs | 三個頁面（控制/CAN/3D）共用同一後端 |

---

## 8. 待補資訊 / 風險

- **CAD 網格檔**：需你提供 PHU14/17/20（及連桿）之 STL/glTF；未到貨前 P2 用基本幾何，P3 再替換。
- **真實幾何數值**：目前 DH 為占位符。手臂垂下零位需要各連桿長度、肩/肘/腕間距——
  若無實測，先用合理估值，實機到貨再校。
- **機械零點對齊**：3D 紅線零位必須對應 EYOU 出廠/校正後的編碼器零，靠 `0x607C` 校正流程確認。
- **Z-up ↔ three.js Y-up**：集中在場景根處理，避免各節點各自轉換出錯。
- **離線相依**：three.js 一律 vendored，不用 CDN。
- **與現有 UI 相容**：新頁獨立，不動 `host_ui*.html` / `can_monitor*.html`；遙測只「新增」欄位不改既有，確保舊頁不壞。

---

## 9. 關聯

- 後端模型：`firmware/sim_py/robot.py`、`kinematics.py`、`phu_motor.py`、`ws_server.py`、`urdf_loader.py`
- 前端：`firmware/ui/host_ui_ws.html`、`can_monitor_ws.html`（沿用其 WS 連線與 CSS 變數慣例）
- 設計背景：[假硬體模擬器](./sim-fake-hardware.md)、[雙臂控制規劃](./dual-arm-control-plan.md)、
  [EYOU PHU 馬達分析](./eyou-phu-motor-analysis.md)
