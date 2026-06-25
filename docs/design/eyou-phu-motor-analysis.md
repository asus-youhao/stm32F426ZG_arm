# 致動器選型分析 — EYOU PHU 系列(CAN 類型判定）

- 對象：EYOU Robotics（意优科技 / Jiangsu EYOU Robotics）PHU 系列整合式伺服關節
- 文件層級：需求 / 選型分析
- 狀態：**已依原廠 datasheet 更正(v2)**
- 依據文件（`doc_EYOU/`）：
  - `PHU系列规格书v1.14-20260424.pdf`
  - `EYou-PHU&RHU系列关节CANopen与EtherCAT通信手册v1.06-20260430.pdf`
  - `EYou-PHU关节模组用户手册v1.24-20260409.pdf`

> ⚠️ 更正說明：本文件第一版（依網路經銷商資料）誤判 PHU 為「非 CAN-FD、無需 FD 硬體」。取得原廠 datasheet 後更正如下。

## 1. 結論摘要（依原廠 datasheet）

| 層級             | 結論                                                              |
| ---------------- | --------------------------------------------------------------- |
| **實體接口/收發器** | **CAN FD 等級**：規格書「驅動器接口」明列 `CAN FD（In, Out）`、`EtherCAT（In, Out）`、`STO`、`電源 DC in/out` |
| **CANopen 協定運作** | **以 Classic CAN 運作**：對象字典 `0x26A1` CAN 波特率預設 **1 Mbps**,僅支援 500k/250k/125k/100k/50k/20k（皆 ≤1Mbps,傳統速率,無 FD 資料相） |
| 應用層 profile   | 標準 **CiA 301 + CiA 402**（OD、NMT、SDO、PDO、EMCY、Heartbeat），節點 ID 1–127（`0x26A0`） |
| 高頻路徑         | **EtherCAT（CoE）**：支援 CSP/CSV/CST、力控 CSF、DC 分散式時鐘同步 |

白話:**接頭是 CAN-FD 規格的硬體,但文件記載的 CANopen 目前跑在傳統 CAN（1 Mbps）上。**

## 2. 通訊關鍵參數（自通訊手冊 v1.06）

- `0x26A0` Node-ID：CAN 總線節點位址,範圍 1–127(0x01–0x7F),改後需重新上電。
- `0x26A1` CAN 波特率：預設 **1,000,000 bps**;可選 500k/250k/125k/100k/50k/20k bps。
- 控制模式(規格書 + 手冊):PP、PV、PT、CSP、CSV(力控版另有 CSF/力控模式)。
- 通訊協定欄：**EtherCAT / CANopen**(同一硬體兩種模式)。
- 全文未出現 "CAN FD / CANopen FD / Mbps 資料相" 等 FD 協定字樣 → CANopen 為傳統模式。

## 3. 使用者 BOM 對應（截圖)

一臂 7 軸:

| 截圖型號 | 對應規格書型號     | 數量/臂 | 介面      | 峰值外部容許扭矩(N.m) |
| -------- | ------------------ | ------- | --------- | --------------------- |
| PHU20    | PHU-20H-90         | 2       | CAN FD/ECAT | 158–182               |
| PHU17    | PHU-17H-80         | 2       | CAN FD/ECAT | 86–134                |
| PHU14    | PHU-14H-70         | 3       | CAN FD/ECAT | 43–66                 |
| 合計     |                    | **7**   |           | 雙臂共 **14 軸**       |

（大關節置於肩/肘基座、小關節靠手腕,符合 7-DoF 手臂配置。供電 24–48V,內建驅動器,雙絕對編碼器 19-bit。）

## 4. 對 MCU 選型的影響（更正後）

1. **要走 CANopen**:對方是 **Classic CANopen @1Mbps** → **STM32F746 的 2× bxCAN 即可驅動**,協定相容。
2. **但建議選 FDCAN MCU**:關節埠為 CAN-FD 等級,且雙臂 7 軸/路 @1Mbps 頻寬吃緊 → 採 **STM32H743ZI（2× FDCAN）** 或 STM32G4 更保險（FDCAN 向下相容傳統 CAN;未來若開放 FD 也接得上）。
3. **要高頻(1 kHz+ 力矩 / CSP / 力控)**:改走關節的 **EtherCAT** 介面 + MCU 外接 EtherCAT 從站晶片(如 LAN9252),用 CoE + DC 同步。

### 拓樸建議

```
方案 1（CANopen，中低頻）：
  MCU bxCAN/FDCAN1 ─► 左臂 7 軸（Node 1..7）   @1Mbps Classic CANopen
  MCU bxCAN/FDCAN2 ─► 右臂 7 軸（Node 1..7）

方案 2（EtherCAT，高頻力控）：
  MCU + EtherCAT 從站 ─► 菊鏈 14 軸（DC 同步, CSP/CSF）
```

## 5. 待原廠確認

1. 「CAN FD 接口」是否支援以 **CAN-FD 幀（BRS/64B）** 運行 CANopen,或僅 CAN-FD 收發器跑傳統幀?(影響是否需 FDCAN 主控)
2. CANopen 模式下單路可掛幾軸並維持目標控制頻率?(PDO 數量 / 更新率上限)
3. EtherCAT 模式的最小循環週期(DC)?

## 參考來源

- 規格書 `doc_EYOU/PHU系列规格书v1.14-20260424.pdf`(驅動器接口、控制模式、電氣規格)
- 通訊手冊 `doc_EYOU/EYou-PHU&RHU系列关节CANopen与EtherCAT通信手册v1.06-20260430.pdf`(0x26A0 Node-ID、0x26A1 CAN 波特率、CiA 402 模式)
- 官方:https://en.eyoubot.com/
