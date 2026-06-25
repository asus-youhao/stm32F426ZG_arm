# 致動器選型分析 — EYOU PHU 系列(CANopen 類型判定）

- 對象：EYOU Robotics（意优科技 / Jiangsu EYOU Robotics）PHU14 / PHU17 / PHU20 整合式伺服關節
- 文件層級：需求 / 選型分析
- 狀態：草案（draft，待原廠 datasheet 逐字確認 baud rate）

## 1. 結論摘要

- EYOU PHU 為人形機器人用「諧波減速一體化關節」,提供 **兩種通訊版本**:`-C` = **CANopen**、`-E` = **EtherCAT**（截圖中 `ECAT/CAN` 欄即指此）。
- **PHU 的 CANopen 為傳統 Classic CAN（CAN 2.0B,≤ 1 Mbps）,非 CAN-FD / CANopen FD。** 採 CiA 301 + 馬達 CiA 402 profile。
- 因此 **無需 CAN-FD 硬體** → **STM32F746ZG 的 2× bxCAN 即相容**,不必為了 FD 改用 H743。
- 需要更高即時頻寬時,EYOU 官方路線是改用 **EtherCAT 版**,而非 CAN-FD。

> ⚠️ 信心說明:EYOU 官網與 RobotShop 對自動抓取回 403,未能直接讀取 PDF datasheet 逐字。以上依多個經銷商產品列表與此級距關節通用規格判定;**建議向原廠索取 PHU 通訊手冊**確認確切 bit rate 與物件字典（OD/PDO 對應）。

## 2. 產品概況

- 廠商：Eyou Robot Technology Co., Ltd.（en.eyoubot.com）
- 系列:PHU = 輕量諧波關節（另有 PH、PP 規劃系列）
- 規格（搜尋彙整）:19-bit 雙磁編、24–48V、背隙 15 arcsec、噪音 < 60dB@30cm、-20~60°C。
- 命名:型號尾碼 `-C` = CANopen 版、`-E` = EtherCAT 版。

## 3. 通訊類型判定

| 問題                         | 判定                                   |
| ---------------------------- | -------------------------------------- |
| CANopen 跑在哪種 CAN?       | **Classic CAN（CAN 2.0B）**            |
| 是否 CAN-FD / CANopen FD?   | **否**                                 |
| 應用層 profile               | CiA 301 + CiA 402（馬達運動控制）      |
| 高頻寬替代版本               | EtherCAT（CoE）版（`-E`）              |

依據:所有公開來源一致標示 "CANopen",未見任何 "CAN-FD / CANopen FD" 字樣;此級距人形關節業界普遍為 Classic CANopen，高頻需求改走 EtherCAT。

## 4. 對 MCU 選型的影響

- 既然 PHU CANopen 版為 **Classic CAN**,先前「F746 不支援 CAN-FD」**不再是阻礙** —— bxCAN 跑 Classic CANopen 完全相容。
- 真正限制回到 **Classic CAN 1 Mbps 頻寬**:建議 **一臂一路**（7 軸/路）。
- 拓樸:`bxCAN1 → 左臂 7 軸`,`bxCAN2 → 右臂 7 軸`,跑 CiA 402,搭 **CANopenNode** 軟體堆疊。

| 需求情境                                   | 建議方案                                            |
| ------------------------------------------ | --------------------------------------------------- |
| PHU CANopen 版 + 中低頻（位置/速度模式）   | **STM32F746 + 2× bxCAN**,足夠,不需換 MCU          |
| 7 軸/路 1 kHz 高頻力矩閉迴路               | Classic CAN 吃緊 → 採 PHU **EtherCAT 版** + F746 外接 EtherCAT 從站晶片（如 LAN9252） |

## 5. 待原廠確認 / 待決議

1. PHU CANopen 版確切 **CAN bit rate**（是否 1 Mbps）與是否可調。
2. CiA 402 支援的 **operation mode**（PP / PV / CSP / CST / CSV…）。
3. 每軸 PDO 配置與更新率上限（決定單路可掛幾軸、可跑多快）。
4. 目標控制頻率 → 定 F746(CANopen) 或改 EtherCAT 版。
5. 供電:24V 或 48V?（影響功率與線束）

## 6. BOM 對應（依使用者截圖）

| 型號   | 數量/臂 | 介面      |
| ------ | ------- | --------- |
| PHU20  | 2       | ECAT/CAN  |
| PHU17  | 2       | ECAT/CAN  |
| PHU14  | 3       | ECAT/CAN  |
| 合計   | **7**   | （一臂 7 軸,雙臂共 14 軸） |

## 參考來源

- Eyou Robot Technology（官方）: https://en.eyoubot.com/
- EYOU 公司簡介 PDF: https://www.daonautomation.com/upload/file/카다로그%20EYOU%20Company%20profile%20V1.07.pdf
- RobotShop EYOU 系列: https://www.robotshop.com/collections/eyou
