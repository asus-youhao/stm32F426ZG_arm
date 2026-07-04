# Linux PC CANopen 主站（真實 14 軸）深度規劃 — 方案 C

- 文件層級：架構 / 實作規劃
- 狀態：草案（draft）
- 分支：`feature/linux-canopen-master`
- 前置成果：`feature/pc-canopen-master` 分支的 **`firmware/pc/pc_master` 已存在且驗證過**（SocketCAN、同套 L0–L4 韌體、500 Hz、vcan×2 對打 14 顆假從站、ws_server 3D 監看）——本文件規劃的是把它從「vcan demo」升級到「**驅動真實 14 顆 EYOU PHU**」
- 沿用文件：
  - `linux-rt-ethercat-master-plan.md` §3——RT 調校方法論（隔離核、IRQ 綁定、SCHED_FIFO、cyclictest 驗收）**原封沿用**
  - `ethercat-coe-master-plan.md` §4——關節供電/STO/線材（與傳輸層無關的部分）
  - `can-bus-architecture.md`、`eyou-phu-motor-analysis.md`——CAN 頻寬與 Classic CAN 判定

---

## 1. 定位：方案 C 是什麼、不是什麼

| 分支 | 方案 | 頻率上限 | 定位 |
| --- | --- | --- | --- |
| `feature/linux-rt-ethercat-master` | A：22.04 + SOEM | 1–2 kHz | 主路徑候選 |
| `feature/linux-igh-ethercat-master` | B：24.04 + IgH | 1–2 kHz | 主路徑候選 |
| **本分支** | **C：Linux + CANopen（SocketCAN）** | **500 Hz（物理上限）** | **真機首發路徑 + EtherCAT 風險對沖 + 低頻備援** |

**誠實的物理限制**：EYOU 的 CANopen 是 Classic CAN 固定 1 Mbps（關節硬體有 CAN FD 埠，但 CANopen 協定面走 Classic）。每軸每週期 RPDO+TPDO 各一幀，單臂 7 軸 @500 Hz ≈ 90% 匯流排負載——**1 kHz 在這條路上不存在**，這正是 `control_rate.h` 降到 500 Hz 的原因。因此方案 C 的價值不是取代 EtherCAT，而是：

1. **最快摸到真馬達**：`pc_master` 程式碼已完成並驗證，只差真硬體介面——是四條路裡「今天就能接第一顆真關節」的最短路徑。
2. **風險對沖**：EtherCAT 側若卡在 ESI/原廠支援/IgH 內核相容，雙臂開發不停擺。
3. **協定對照組**：同一顆關節先用 CANopen 摸清 CiA402 行為（使能時序、單位、抱閘），EtherCAT bring-up 時問題二分（協定層 vs 傳輸層）立判。
4. 500 Hz joint-space 對「非力控、中速動作」已足夠，可先做出完整雙臂 demo。

## 2. 現況盤點與差距分析

### 2.1 已有（`feature/pc-canopen-master`）

- `firmware/pc/pc_master`：SocketCAN 後端（`co_bxcan_socketcan.c` 實作 `co_bxcan.h` 四函式）、真牆鐘 HAL、`clock_nanosleep(TIMER_ABSTIME)` 500 Hz 迴圈 + 遲到統計、stdin 互動命令、`--bringup N` 單軸自檢、單臂降級（`--right none`）。
- 完整協定棧：NMT reset/PreOp/Start、SDO 組態（模式+PDO 映射 `0x1600/0x1A00`）、RPDO1/TPDO1（Controlword+TargetPos / Statusword+PosActual，各 6 B）、heartbeat 監看、CiA402 使能、安全狀態機（50 ms 逾時→safe stop）。
- 監看鏈：`ws_server --monitor` 旁聽 bus → 3D 動畫 + 幀流面板。
- 已知量測：非 RT 核心下 tick 遲到 max ~2.5 ms @500 Hz（README 自述）。

### 2.2 差距（本規劃要補的）

| # | 差距 | 說明 |
| --- | --- | --- |
| G1 | **真 CAN 硬體** | 現在只跑 vcan；唯一實體介面 CANable 走 slcan（ASCII 序列化 + USB），500 Hz×14 軸絕對不夠 |
| G2 | **RT 化** | 2.5 ms 遲到會吃掉半個控制週期；需 PREEMPT_RT + 優先權/隔離 |
| G3 | **SYNC 同步機制** | 現在 RPDO 直發（等效 async）；14 軸無共同鎖存時刻，軸間 skew = 幀序列化時間（~1.5 ms 散布） |
| G4 | **從站佈建程序** | 真關節出廠 `0x2100=1（EtherCAT）`、node-id 全是 1——不改就上 bus 必撞 |
| G5 | **EMCY / 錯誤處理** | 現有棧無 EMCY（`0x80+node`）解析；真馬達的故障碼（E1xx/E2xx/E3xx）會靜默丟失 |
| G6 | **匯流排健康監控** | 無 busload/error counter 監看；90% 負載運行必須有儀表 |

## 3. 真 CAN 硬體選型（G1，關鍵決策）

需求：**2 通道**（左/右臂各一路）Classic CAN 1 Mbps、SocketCAN 原生驅動、低且穩定的 TX 延遲（500 Hz 下每 bus 每週期要塞 7 幀 RPDO，序列化本身 ~112 µs/幀）。

| 等級 | 介面 | 評估 |
| --- | --- | --- |
| ❌ 淘汰 | **CANable(slcan)**（現有） | ASCII 編碼 + USB CDC，單幀來回毫秒級；只留給「單軸手動調試」 |
| ⚠️ 過渡 | CANable 刷 **candleLight(gs_usb)**、PCAN-USB、Kvaser Leaf | 原生 SocketCAN，但 USB 批次傳輸抖動 0.1–1 ms 級；單臂 7 軸勉強、雙臂 14 軸 @500 Hz 臨界。手上已有 CANable → **刷 candleLight 是零成本第一步** |
| ✅ 目標 | **PCIe/M.2 雙通道 CAN 卡**（PEAK PCAN-PCIe FD 2ch / Kvaser PCIe 2xCAN / Advantech 等） | MMIO+中斷直達、硬體時戳、TX 延遲 µs 級；SocketCAN 原生驅動（peak_pciefd/kvaser_pciefd 在主線內核）；一張卡解決雙臂 |

配套設定：`ip link set can0 type can bitrate 1000000`、`txqueuelen 32`（預設 10 太小，7 幀突發+重傳餘裕）、取樣點採 CiA 建議 87.5%、CAN 卡 IRQ 綁定到隔離核旁（沿用 RT 方法論的 IRQ affinity 表）。

接線（用戶手冊 §4.3）：關節側 3-pin JST GHS（CAN-H / CAN-L / E_GND），**每顆關節只有一個 CAN 連接器**（菊鏈靠線束在連接器內貫通）；**控制器端與鏈末端各一顆 120 Ω 終端電阻**；雙絞+屏蔽，總線 ≤ 25 m。

## 4. 從站佈建程序（G4，第一次接真機的 SOP）

真關節出廠狀態對 CANopen 是「不能直接用」的，佈建 SOP（做成 `tools/provision_joint.sh`）：

1. **切控制權**：`0x2100` = 2（CANopen）——出廠預設 1=EtherCAT。經 UART（EYouServoStudio）或先以 EtherCAT/SDO 寫入。
2. **設 node-id**：`0x26A0` = 1..7（左右臂各自 1..7，出廠全是 1）→ **逐顆單獨上線設定**（一次只接一顆新關節，避免 node-id 撞車）。
3. **存檔**：`0x2130=1`（或 `0x1010:01="save"`，約 3 s，**保存中嚴禁斷電**）→ 重上電生效。
4. **驗證**：`candump can0` 應見 `0x701..0x707` heartbeat；SDO 讀 `0x1008`="EYou"、`0x2025`（解析度實測）、`0x26A2/A3`（減速比）記入 `robot_config` 校正表。
5. 貼標籤（臂別/軸號/node-id），登記於佈建清冊文件。

> 波特率固定 1 Mbps（`0x26A1`），不動。每關節佈建一次即可（EEPROM 保存）。

## 5. 同步與頻寬設計（G3 + 精算）

### 5.1 SYNC 同步鎖存

現況 RPDO 直發等效 transmission type 255（async），從站收到即用，14 軸鎖存時刻散布 = 幀序列化順序。改為標準 CANopen 同步模型：

- 主站每週期先發 **SYNC（COB 0x080）**，`0x1005/0x1006`（communication cycle = 2000 µs）寫入從站。
- RPDO/TPDO transmission type 改 **1（synchronous cyclic）**：從站在下一個 SYNC 邊緣統一鎖存目標/回傳回授——**軸間 skew 從 ~1.5 ms 壓到 SYNC 抖動等級（RT 化後 <100 µs）**。
- 主站 tick 結構變為：`SYNC 發出 → 收上週期 TPDO → 算控制 → 發 RPDO（下個 SYNC 前送完）`——與 EtherCAT 版的一拍延遲模型同構，控制層無感。
- 需確認 EYOU 對 SYNC-triggered PDO 的支援細節（通訊手冊 §2 CANopen 章 PDO 傳輸類型表）；若僅支援 async，退而求其次用「RPDO 發送順序固定 + 週期起點對齊」並如實記錄 skew。

### 5.2 頻寬精算（每臂一路 1 Mbps）

| 項目 | 幀 | bits/幀（含 stuffing 估計） | @500 Hz 佔用 |
| --- | --- | --- | --- |
| SYNC | 1 | ~65 | 3.3% |
| RPDO1 ×7（6 B） | 7 | ~130 | 45.5% |
| TPDO1 ×7（6 B） | 7 | ~130 | 45.5% |
| Heartbeat ×7（1 Hz）+ EMCY 餘裕 | — | — | ~1% |
| **合計** | | | **~95%** |

結論：**500 Hz 是天花板且無 SDO 餘裕**。設計對策：

- 運行中 SDO（診斷輪詢）**禁止**——診斷資料改映入 TPDO 或降到停機時查。
- 保留 **400 Hz 運行檔位**（負載 ~76%，留 EMCY/重傳餘裕）作為預設，500 Hz 作為 benchmark 檔位；`control_rate.h` 做成可設定。
- TPDO 若要加速度/扭矩回授（+2 B）→ 頻寬不夠，只能犧牲頻率（333 Hz）——文件明定取捨表，不臨場亂調。

## 6. 軟體工作項（G2/G5/G6）

- **RT 化**：`pc_master` 進程套 RT 方法論（mlockall、SCHED_FIFO 80、隔離核、CAN IRQ 綁核）；tick 遲到統計已內建，驗收 max < 100 µs @RT 核。
- **EMCY 解析**（新增 `co_emcy.c`）：`0x080+node` 幀 → error code + register + 廠商欄位，對照手冊表 5-2（E1xx 電流/溫度、E2xx 編碼器、E301 STO、E4xx 通訊）落盤並餵 safety（EMCY = 立即 safe stop 條款）。
- **匯流排儀表**：週期統計 TX drop（已有 `dual_arm_tx_drops`）、RX 逾時軸別、`canbusload` 整合遙測；error passive/bus-off 偵測（SocketCAN error frames `CAN_ERR_*` 開啟並解析）→ bus-off 自動恢復程序（`restart-ms 100`）。
- **SYNC producer**：高精度發送（RT 執行緒內首發），TPDO 收集窗設計。
- **ws_server 監看鏈沿用**：`--monitor` 直接旁聽 can0/can1（原生介面名已支援 `co_socketcan_set_ifname`）。

## 7. 深度工作分解（WP-C）

> 每項含 DoD。C0/C1 用現有 CANable（刷 candleLight）即可開始，不等 PCIe 卡到貨。

### WP-C0 — 硬體與佈建

| # | 工作項 | 驗收（DoD） |
| --- | --- | --- |
| 0.1 | CANable 刷 candleLight（gs_usb） | `ip link` 見原生 can0（非 slcan0） |
| 0.2 | PCIe 雙通道 CAN 卡採購/安裝 | `candump -H` 硬體時戳可用 |
| 0.3 | `provision_joint.sh`（§4 SOP 腳本化：0x2100=2、node-id、save、驗證） | 一顆生關節 5 分鐘佈建完 |
| 0.4 | 首顆真關節佈建 + 終端電阻 + 供電/STO（沿用 EtherCAT 版 §4） | heartbeat 出現於 candump |
| 0.5 | `0x2025`/`0x26A2-A3` 實測 → `robot_config` 校正 | 校正表 + 文件（19-bit=524288 待實證） |

### WP-C1 — 單軸真機（pc_master 首次接真馬達）

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 1.1 | `pc_master --left can0 --right none --bringup 1` 對真關節 | SDO 全過、CiA402 使能、PV 轉動 moved=1 |
| 1.2 | 使能時序 enforce（上電 >5 s、鬆閘後 ≥500 ms 才動、下使能 ≥300 ms） | 時序違規測試不再出錯 |
| 1.3 | CSP 正弦 ±5° @500 Hz（非 RT 先跑） | 平滑、無跟隨誤差故障 |
| 1.4 | EMCY 解析（`co_emcy.c`）+ 故障注入（STO/E301、拔 CAN/E4xx） | 故障碼正確落盤 + safe stop |
| 1.5 | 真機 vs `can_slave.py` 行為差異記錄 | 假從站修正清單（提升日後模擬保真度） |

### WP-C2 — RT 化與 SYNC（品質拍板點）

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 2.1 | RT 環境沿用（22.04 Pro RT 機）+ pc_master RT 化 | tick 遲到 max < 100 µs @12 h |
| 2.2 | SYNC producer + transmission type=1 下發 | 示波器/時戳：兩軸 TPDO 對 SYNC 對齊 |
| 2.3 | 軸間 skew 量測（candump -H 硬體時戳） | skew 報告：SYNC 模式 vs async 模式對比 |
| 2.4 | USB(candleLight) vs PCIe 卡延遲對比 | 對比報告 → 雙臂用卡定案 |

### WP-C3 — 單臂 7 軸

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 3.1 | 7 顆佈建（node 1–7）+ 菊鏈佈線 + 雙端終端 | 7 heartbeat、canbusload 實測 vs §5.2 預算 |
| 3.2 | 400 Hz/500 Hz 檔位化（`control_rate.h` 參數化） | 兩檔位皆 present 7/7 |
| 3.3 | joint-space 軌跡跟隨（既有 L2） | 跟隨誤差基線報告（供 EtherCAT 對比） |
| 3.4 | 掉軸/bus-off 演練（運轉中拔一軸、短接 CAN-H/L） | 50 ms 內 safe stop + bus-off 自動恢復 |

### WP-C4 — 雙臂 14 軸

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 4.1 | 第二路 can1 + 右臂 7 顆佈建 | present 14/14 |
| 4.2 | 雙 bus SYNC 對齊（同一 tick 先後發、記錄兩臂 skew） | 臂間 skew < 200 µs |
| 4.3 | L4 雙臂協同 demo @400 Hz + 24 h soak | TX drop=0、EMCY=0、無熱降載 |
| 4.4 | ws_server/3D 接真雙臂 | 監看鏈全通 |

### WP-C5 — 收尾與交叉驗證

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 5.1 | 與 EtherCAT 方案（A/B）同動作腳本對比：跟隨誤差/軸間 skew/頻率上限 | 三方案對比報告（最終架構定案輸入） |
| 5.2 | 佈建清冊 + runbook + 文件 | docs 齊備 |

### 里程碑

1. **MC1 = C0+C1**：**第一顆真關節被本 repo 韌體轉起來**（全專案首次真機閉環，四條路裡最早）
2. **MC2 = C2**：RT + SYNC 品質定案（含 USB vs PCIe 對比）
3. **MC3 = C3**：單臂 7 軸 @400/500 Hz
4. **MC4 = C4**：雙臂 14 軸 + soak
5. **MC5 = C5**：三方案對比報告

## 8. 風險與對策

| 風險 | 等級 | 對策 |
| --- | --- | --- |
| 90–95% 匯流排負載無餘裕（重傳/EMCY 風暴即超載） | 高 | 預設 400 Hz 檔位；運行中禁 SDO；bus 儀表常駐；EMCY 風暴→自動降頻/safe stop |
| USB CAN 介面抖動不可控 | 中 | 僅過渡用；PCIe 卡為雙臂前提（WP-C2.4 數據定案） |
| EYOU 對 SYNC/transmission type=1 支援不明 | 中 | WP-C2.2 實測；不支援則退 async + 固定發送順序，如實記錄 skew |
| `0x2100`/node-id 佈建失誤（save 時斷電、node 撞車） | 中 | SOP 腳本化 + 一次一顆 + 佈建清冊；save 期間 UPS/穩定供電 |
| 500 Hz 天花板被誤當長期方案 | 低 | 文件明定定位（§1）：力控/1 kHz 一律走 EtherCAT |
| 供電/STO/鬆閘湧浪 | — | 全部沿用 EtherCAT 版 §4 與 WP-L4 對策 |

## 9. 驗證方式

- **匯流排**：`canbusload -r` 實測 vs §5.2 預算表；`candump -H` 硬體時戳量 SYNC→TPDO 對齊與軸間 skew；error counter/bus-off 注入測試
- **迴圈**：pc_master 內建遲到統計（RT 前後對比）；cyclictest 同步錄製
- **控制**：同一動作腳本在（C）500 Hz CANopen vs（A/B）1 kHz EtherCAT 的跟隨誤差對比——三方案報告
- **安全**：STO/拔線/掉軸/bus-off 全項 50 ms safe stop；EMCY 落盤可追溯
- **回歸**：vcan + `can_slave.py` 假從站鏈保留為免硬體 CI（WP-C1.5 的差異修正回饋進假從站）

## 10. 關聯文件

- 既有成果：`../changes/2026-07-03-pc-master-socketcan.md`（pc_master 本體）、`../changes/2026-07-03-ui-integration-monitor.md`（監看鏈）
- 沿用：`linux-rt-ethercat-master-plan.md`（RT 方法論）、`ethercat-coe-master-plan.md` §4（供電/STO/接線）
- 背景：`can-bus-architecture.md`（頻寬）、`eyou-phu-motor-analysis.md`（Classic CAN 判定）、`canopen-vs-ethercat.md`
- 變更紀錄：`../changes/2026-07-04-linux-canopen-master-plan.md`
