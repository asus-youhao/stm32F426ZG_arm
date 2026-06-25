# CANopen vs EtherCAT（含 CoE）— EYOU PHU 通訊選擇

- 文件層級：需求 / 通訊選型
- 依據：`doc_EYOU/EYou-PHU&RHU系列关节CANopen与EtherCAT通信手册v1.06-20260430.pdf`

## 1. 兩者本質

| 項目     | CANopen                          | EtherCAT                                   |
| -------- | -------------------------------- | ------------------------------------------ |
| 底層     | CAN 匯流排（本關節為 CAN 2.0B / Classic, 預設 1 Mbps） | 工業乙太網（100 Mbps 全雙工,「飛讀飛寫」） |
| 拓樸     | 匯流排 (bus)                     | 菊鏈 (daisy-chain) / 線型                  |
| 同步     | 軟體（SYNC 物件）, 抖動較大      | **硬體分散式時鐘 DC**, 抖動極低            |
| 吞吐     | ≤1 Mbps（共享）                  | 高,單週期可更新多軸                       |
| 設定模型 | 物件字典 OD（CiA 301/402）       | 同樣用 OD,透過 **CoE** 存取               |
| 適用     | 中低頻 / 結構簡單 / 成本低       | 高頻多軸同步 / 力控 / 1 kHz+               |

## 2. EYOU PHU 同時支援，且支援 CoE

原廠通訊手冊 §1.2 明確說明:

> 「在 EtherCAT 中,可以透過 **CoE（CANopen over EtherCAT）** 方式,使用熟悉的 CANopen 物件字典模型來設定設備,有效結合 EtherCAT 的高性能與 CANopen 的設定便利性。」

亦即 PHU 的 **同一套 CiA 402 物件字典**,可在兩種傳輸層上使用:

```
            ┌── 傳輸層 A：CAN（Classic CANopen, ≤1Mbps）
CiA 402 OD ─┤
            └── 傳輸層 B：EtherCAT（CoE = CANopen over EtherCAT, DC 同步）
```

手冊章節佐證:
- §2 CANopen:OD、NMT 狀態機、SDO、PDO、EMCY、Heartbeat（CiA 301）。
- §3 EtherCAT:ESM 狀態機、Mailbox（CoE）、Process Data（PDO via SM/FMMU）、**DC 分散式時鐘同步**。
- §4 運動控制:PP / PV / PT / **CSP / CSV / CST**（力控版 **CSF**）。
- §6 TwinCAT 主站設定（ESI、PDO 配置）。

## 3. 對「1 kHz 雙臂高頻控制」的選擇

| 控制需求                          | 建議傳輸層         | 理由                                            |
| --------------------------------- | ------------------ | ----------------------------------------------- |
| 位置/速度,中低頻（≤幾百 Hz）     | CANopen（CAN）     | 簡單、成本低;一臂一路 bus                       |
| **1 kHz joint/task-space、力控**  | **EtherCAT（CoE + DC + CSP/CSF）** | DC 硬體同步、低抖動、單週期可更新 14 軸 |

**結論:目標既是 1 kHz 雙臂 joint-space / task-space + 力控,通訊主路徑應採 EtherCAT（CoE）**;CANopen 適合早期單關節 bring-up 與低頻測試。

## 4. 對 MCU 的硬體含意

- 走 **CANopen**:需 CAN 控制器。F746 為 bxCAN（Classic,可用）;若要 CAN-FD 餘裕用 FDCAN（H743/G4）。
- 走 **EtherCAT（建議）**:MCU 需外接 **EtherCAT 從站控制器 ESC**(如 LAN9252 經 SPI/外部匯流排)或選用內建 ESC 的方案;主站側則用 PC/工控機 + TwinCAT/IgH EtherCAT Master,或具 EtherCAT 主站能力的控制器。

> 重要待決議:本專案的「主站」是放在 **MCU(STM32)** 還是 **PC/工控機**?
> - EtherCAT 主站常見於 PC（TwinCAT / IgH）。若 STM32 當 EtherCAT **主站**較不典型;STM32 較適合當 EtherCAT **從站** 或 CANopen 主站。
> - 這會直接決定系統架構,請見 `dual-arm-control-plan.md` 的待決議項。

## 參考來源

- 通訊手冊 `doc_EYOU/EYou-PHU&RHU系列关节CANopen与EtherCAT通信手册v1.06-20260430.pdf`（§1.2、§2、§3、§4、§6）
