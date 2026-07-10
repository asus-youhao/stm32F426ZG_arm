# EtherCAT（CoE）主站遷移深度規劃 — 從 CANopen 換到 EtherCAT

- 文件層級：架構 / 遷移規劃（回應 `dual-arm-control-plan.md` WP0.1/0.2/0.3 待決議）
- 狀態：草案（draft）
- 分支：`feature/ethercat-coe-master`
- 依據：
  - `doc_EYOU/EYou-PHU&RHU系列关节CANopen与EtherCAT通信手册v1.06-20260430.pdf`（§3 EtherCAT、§4 運動控制、§5 診斷、§6 TwinCAT、§7 OD）
  - `doc_EYOU/EYou-PHU关节模组用户手册v1.24-20260409.pdf`（§2/§4 電氣安裝、§5 維護）
  - `doc_EYOU/PHU系列规格书v1.14-20260424.pdf`
  - 現有韌體 `firmware/`（CANopen 主站全棧，見 §3 盤點）

---

## 1. 目標與範圍

把現有 **CANopen（Classic CAN, 500 Hz）主站** 換成 **EtherCAT（CoE, CiA 402）主站**，控制頻率提升到 **1 kHz**，目標 MCU 仍為 **STM32F746ZG**（Nucleo-144），對象為雙臂 14 軸 EYOU PHU 關節（PHU20×2 + PHU17×2 + PHU14×3 每臂）。

**不變的部分**：L2–L4 控制層（joint/task-space、雙臂協同）、safety、host 介面、CiA 402 狀態機純邏輯——CoE 用的就是同一套 CiA 402 物件字典。
**要換的部分**：L0 通訊層（bxCAN → EtherCAT 主站堆疊）與 L1 的 bring-up / 每-tick 收發模型。

## 2. 為什麼換：頻寬與同步的硬限制

現況 `firmware/app/control_rate.h` 明寫著降頻理由：Classic CAN @1 Mbps，單臂 7 軸每週期 14 個 frame，500 Hz 已吃掉約 90% 匯流排；1 kHz 必然超載。而且 CANopen SYNC 是軟體同步，抖動大。

EtherCAT 下同樣的資料量：

| 項目 | CANopen（現況） | EtherCAT（目標） |
| --- | --- | --- |
| 實體層 | 2× Classic CAN 1 Mbps（每臂一路） | 100 Mbps 全雙工，單鏈 14 軸 |
| 每週期資料 | 14 frame × 2 bus，逐幀仲裁 | 1–2 個 EtherCAT frame，「飛讀飛寫」 |
| 14 軸 PDO 線上時間 | ≈ 1.8 ms（超過 1 ms 週期） | 精簡映射後 out ≈ 170 B、in ≈ 240 B，**線上時間 < 60 µs** |
| 同步 | SYNC 物件（軟體，抖動 ~百 µs 級） | **DC 分散式時鐘 + SYNC0**（硬體，抖動 < 1 µs 級） |
| 週期上限 | 500 Hz（實測極限） | 原廠規格支援到 **2 kHz**（500 µs） |

原廠通訊手冊 §3.5 的關鍵規則：**主站週期必須是 500 µs 的整數倍**（1×, 2×, 3×…），即支援 500 µs / 1 ms / 1.5 ms / 2 ms…。**1 kHz 目標完全在規格內**；不符會報 E404（0xFF34）「指令插補週期不支援」。

## 3. 現有韌體盤點：留什麼、換什麼

現有分層（`docs/design/dual-arm-control-plan.md` L0–L4 的實際程式碼對應）：

| 層 | 檔案 | 遷移處置 |
| --- | --- | --- |
| L4–L2 控制 | `firmware/control/*`（dual_arm_ctrl / task_space / joint_space / kinematics / ik / trajectory / linalg） | **原封重用**（純運算，吃/吐 rad 與 counts） |
| 安全 | `firmware/safety/safety.c` | **原封重用**（只吃 statusword + 時戳） |
| 上位機 | `firmware/host/host_if.c` | **原封重用** |
| 參數 | `firmware/app/robot_config.c` | **原封重用**（單位換算表要按實機解析度校正，見 §7.4） |
| CiA402 | `firmware/canopen/cia402.c` | **純函式重用**：`cia402_decode()`、`cia402_enable_step()`、全部 CW/模式巨集。尾端三個 `*_sdo()` 函式改走 CoE mailbox |
| L1 雙臂抽象 | `firmware/app/dual_arm.c` | **改寫**：bring-up 序列（NMT→ESM）、tick 收發（逐幀→process image）。`g_jstate[14]` 結構保留 |
| 應用 tick | `firmware/app/app_main.c` | **小改**：pump_rx/tick 換成單次 process-data 交換；四步驟骨架不動 |
| 控制頻率 | `firmware/app/control_rate.h` | 500 Hz → **1 kHz**（`CONTROL_HZ=1000`, `CONTROL_DT_US=1000`） |
| L0 協定 | `firmware/canopen/co_{nmt,sdo,pdo}.c` | **整組替換**為 EtherCAT 主站堆疊（NMT→ESM、SDO→CoE mailbox、PDO→process image） |
| L0 硬體 | `firmware/canopen/co_bxcan.c` + board CAN 腳位/ISR | **移除**，換 ETH MAC 驅動 |
| 板端進入點 | `firmware/board/main.c` | TIM6 觸發改為 **DC 對齊的週期觸發**（見 §8.3），移除 CAN GPIO/ISR |

> 關鍵教訓：現有 porting 邊界是 `co_bxcan.h` 的 4 個函式（CAN frame 級、逐幀交易）。EtherCAT 是 **process image 級**（一次交換全部軸），語意不同——**不能只換一個 driver 檔**，要在 L0/L1 之間立一個新的「匯流排抽象」（§6.2），讓 CANopen 與 EtherCAT 兩條路徑並存、build 時選擇。

## 4. 硬體前提（採購/接線層，動工前必須確認）

### 4.1 關節端

- **訂購尾碼**：PHU 型號最後一碼 **E = EtherCAT、C = CANopen**（例：PHU20H-90-B-101-**E**）。板上兩種介面硬體都在，但出廠配置與隨附線束不同。**若現有關節是 C 尾碼，需向 EYOU 確認能否直接切換**（韌體參數 `0x2100` 出廠預設其實就是 1 = EtherCAT）。
- **控制權參數 `0x2100`**（P01.00）：0=UART、**1=EtherCAT（預設）**、2=CANopen。用 UART + EYouServoStudio 或 SDO 改，寫 `0x2130=1` 存 EEPROM。
- **ESI 檔**：`EYOU_ServoModule.xml`，需向原廠索取（Vendor ID / Product Code 手冊未公布，也可上線後 SDO 讀 `0x1018`）。

### 4.2 接線（用戶手冊 §4.3）

- **EtherCAT 埠**：每關節 **ECAT IN / ECAT OUT** 兩座 JST **BM05B-GHS-TBT**（5 pin：TX+ / TX− / RX+ / RX− / E_GND），獨立屏蔽雙絞線。
- **方向很重要**：In/Out 接反仍可通訊，但**主站看到的軸順序會跟物理順序不符**。EtherCAT 採位置尋址（無站號撥碼），軸順序 = IN→OUT 鏈接順序。雙臂 14 軸佈線務必固定方向並記錄。
- **拓撲（單鏈 14 軸）**：F746ZG 只有一個 ETH MAC → 一條鏈：

  `F746 ETH ➜ 左臂 J1 IN…J7 OUT ➜ 右臂 J1 IN…J7 OUT ➜ 開路末端`

  100 Mbps 對 14 軸綽綽有餘（§5 預算），不需要雙鏈。線纜冗餘（環網，`0x2677`）需要第二埠，F746 做不到，列為不啟用。
- **STO**：每關節 4 pin（STO1± / STO2±），DC24V 經常閉急停鏈；**兩路同時通電才能運行**。`0x253B` STO 故障模式**正常運行禁止設 0**（0 僅限調試）。
- **供電**：額定電流 @48V — PHU20 5 A、PHU17 3.8 A、PHU14 1.7 A → **單臂 22.7 A、雙臂 45.4 A（額定）**。PHU20/17 **不建議電源菊鏈**（壓降），用單軸直連或樹形；減速回灌需掛 **PD50 泄放模組**（PHU17/20 各建議外接 50 W / 5 Ω）。
- **UART 調試口保留**：與 EtherCAT 並存，運行時仍可作維修診斷；**嚴禁帶電插拔**（GND 電位差會打壞介面）。

### 4.3 主站端（STM32F746ZG）

**EtherCAT 主站不需要 ESC**（ESC 是從站晶片；主站只要能收發 raw Ethernet frame，EtherType 0x88A4）。Nucleo-F746ZG 板載 **ETH MAC（RMII）+ LAN8742A PHY + RJ45**，硬體零改造即可當主站——之前文件（`canopen-vs-ethercat.md` §4）提到的 LAN9252 只有「STM32 當從站」才需要，本規劃不採用。

RJ45 → JST GHS 5 pin 需要一條**轉接線**（RJ45 的 TX±/RX± 對應 ECAT IN 的 pin1-4，屏蔽接 E_GND），這是唯一要自製的硬體。

## 5. 通訊設計：PDO 映射與頻寬預算

### 5.1 精簡 PDO 映射（PREOP 階段用 CoE SDO 重映射）

出廠預設映射太肥（RxPDO 33 B + TxPDO 29 B/軸，14 軸 = 462+406 B），且含許多 1 kHz 用不到的欄位。每個 PDO **最多 6 entries**（超過報 E405/0xFF33），重映射寫 `0x1600`/`0x1A00` + assignment `0x1C12`/`0x1C13`（ESM Changed：僅 PREOP 可改）。

**RxPDO（0x1600，主→從，12 B/軸）**：

| 物件 | 大小 | 用途 |
| --- | --- | --- |
| 0x6040 Controlword | 2 B | 使能/快停/故障復位 |
| 0x607A Target position | 4 B | CSP 目標（絕對 plus） |
| 0x60B1 Velocity offset | 4 B | 速度前饋（軌跡微分，改善跟隨） |
| 0x6071 Target torque | 2 B | CST/CSF 備用；CSP 時恆 0 |

**TxPDO（0x1A00，從→主，15 B/軸）**：

| 物件 | 大小 | 用途 |
| --- | --- | --- |
| 0x6041 Statusword | 2 B | CiA402 狀態 + 安全監看 |
| 0x6064 Position actual | 4 B | 位置回授 |
| 0x606C Velocity actual | 4 B | 速度回授（現有 CANopen 版沒有，升級點） |
| 0x6077 Torque actual | 2 B | 扭矩回授（‰ 額定） |
| 0x603F Error code | 2 B | 故障碼（EtherCAT 無 EMCY，靠這個+`0x2672`） |
| 0x6061 Mode display | 1 B | 模式確認 |

`0x6060` 模式設定走 SDO（PREOP 設定一次），不佔 PDO。之後要上 CST/CSF 力控時**不需改映射**，只切 `0x6060` 並改用 0x6071 欄位。

### 5.2 頻寬預算（1 kHz）

- Process data：out 14×12 = 168 B、in 14×15 = 210 B。SOEM 以 logical addressing（LRW）打包，加上 frame/datagram overhead 後單一 frame < 500 B。
- **線上時間 ≈ 40 µs/cycle（100 Mbps）**，佔 1 ms 週期 4%。
- 即使之後 PDO 加倍（雙向都加前饋/診斷欄位），仍 < 10%。**單鏈 14 軸 @1 kHz 頻寬完全無虞**，2 kHz（500 µs）也在規格內，留作日後力控升級空間。

### 5.3 週期相關從站參數（bring-up 時核對）

| 物件 | 意義 | 設定 |
| --- | --- | --- |
| 0x60C2 Interpolation time period | 插補週期 | 1 ms（與主站週期一致） |
| 0x266C / 0x266D | 驅動內部指令週期指數/值 | 依原廠預設（125 µs 內部週期），異常時諮詢原廠 |
| 0x2675 | EtherCAT 位置補償延時（單位 125 µs） | 預設 2；跟隨誤差偏大時微調 |
| 0x1C32/0x1C33 | SM2/SM3 sync 參數 | DC-Synchron 模式下由主站設定 |

週期不匹配的症狀就是 **E404（0xFF34）**，這是 bring-up 最可能踩到的錯誤碼。

## 6. 軟體架構

### 6.1 主站堆疊選型：SOEM

| 選項 | 評估 |
| --- | --- |
| **SOEM（Simple Open EtherCAT Master, rt-labs）** ✅ | C 語言、可移植（osal/oshw 兩層 porting）、支援 CoE SDO/PDO/DC、有現成 STM32 bare-metal 社群移植可參考；PC 端（Linux raw socket）與板端共用同一套 API——完美延續本專案「同套韌體多後端」模式 |
| IgH EtherCAT Master | Linux kernel module，品質高但**只能跑 PC**，無法上 F746 |
| Acontis EC-Master（原廠手冊點名） | 商業授權，先不採；未來產品化可評估 |
| 自研精簡主站 | ESC 初始化、DC 補償、mailbox 狀態機工作量極大，不划算 |

> **授權注意**：SOEM 為 GPLv2（rt-labs 另售商業授權）。韌體整體連結 SOEM 即受 GPL 拘束——**產品化前需法務確認**；替代方案是屆時換 Acontis 或自研。開發/研究階段無虞。

### 6.2 新目錄與匯流排抽象

```
firmware/
├── ecat/                    # 新增：EtherCAT 主站層（L0'）
│   ├── ec_master.h/.c       # 對 L1 的門面：init/exchange/sdo/state
│   ├── ec_config.h          # 14 軸鏈序、PDO 佈局、週期常數
│   ├── ec_coe.c             # CoE SDO 讀寫（包 SOEM ec_SDOread/write）
│   ├── ec_dc.c              # DC 同步：SYNC0 設定 + 主站週期漂移補償
│   └── soem/                # SOEM 原始碼（子目錄 vendor 或 submodule）
├── ecat_port/
│   ├── nicdrv_stm32f7.c     # oshw：F746 ETH MAC + DMA descriptor 收發（不經 LwIP）
│   ├── osal_baremetal.c     # osal：時間戳（DWT/TIM）、無 OS 的等待
│   ├── nicdrv_rawsock.c     # oshw：Linux raw socket（PC 主站用，SOEM 內建）
│   └── osal_linux.c         # SOEM 內建
└── canopen/                 # 保留：CAN 路徑仍可 build（單軸調試/退路）
```

**L1 介面（`ec_master.h`）——process image 語意，一次交換全部軸**：

```c
ec_status_t ec_master_init(const ec_net_config_t *cfg);        /* INIT→PREOP→(remap)→SAFEOP→OP */
ec_status_t ec_master_exchange(void);                          /* 每 tick 一次：送出全部 RxPDO + 收回全部 TxPDO + WKC 檢查 */
void        ec_axis_set_output(int axis, uint16_t cw, int32_t target_pos, int32_t vel_ff, int16_t tq);
bool        ec_axis_get_input(int axis, uint16_t *sw, int32_t *pos, int32_t *vel, int16_t *tq, uint16_t *err);
ec_status_t ec_coe_write(int axis, uint16_t idx, uint8_t sub, const void *v, int n);  /* cia402_set_mode 等改呼叫這個 */
ec_status_t ec_coe_read (int axis, uint16_t idx, uint8_t sub, void *v, int n);
int         ec_master_health(void);                            /* WKC/AL status/丟包統計，餵 safety */
```

`dual_arm.c` 依 build flag（`BUS_BACKEND=ecat|canopen`）選擇後端；`g_jstate[14]`、`dual_arm_set_target()`、`dual_arm_set_safe_stop()` 等對控制層的介面**完全不變**，控制層無感。

### 6.3 每-tick 流程對比

現況（CANopen, 500 Hz）：`pump_rx()`（逐幀解析）→ safety → 控制 → `dual_arm_tick()`（14 次 `co_pdo_send_csp`）。

目標（EtherCAT, 1 kHz）：

1. `ec_master_exchange()` —— 單次 process data 交換（送上一 tick 算好的目標、收全部回授），檢查 WKC
2. safety —— statusword + `0x603F` + WKC/健康度（WKC 不符 = 有從站掉線 → safe stop）
3. `da_ctrl_tick_1khz()` —— L4→L3→L2 產生 14 軸目標（此函式名總算名符其實）
4. `ec_axis_set_output()` ×14 —— 寫入 process image，**下一 tick 週期開頭送出**

> 「先交換、再計算、下期送出」的一拍延遲模型是 EtherCAT 主站標準做法，讓交換時刻與 SYNC0 對齊、計算時間不影響同步精度。從站端 `0x2675` 位置補償延時即為此而設。

## 7. 關鍵協定流程（實作依據，全部出自原廠手冊）

### 7.1 Bring-up 序列（取代現有 NMT reset→PreOp→Start）

1. **INIT→PREOP**：SOEM `ec_config_init()` 掃鏈，核對從站數=14、`0x1018` identity
2. **PREOP 組態（CoE SDO）**：每軸 `0x6060=8`（CSP）→ 重映射 `0x1600`/`0x1A00`（§5.1，各 ≤6 entries）→ `0x1C12`/`0x1C13` assignment → `0x60C2`=1 ms
3. **DC 設定**：`ec_configdc()`，以**鏈上第一顆從站為參考時鐘**，SYNC0 週期 1 ms
4. **SAFEOP**：從站開始回傳輸入、輸出鎖定安全值——主站在此驗證回授合理性
5. **OP**：process data 全速；LED「ECAT STA」綠常亮
6. **CiA402 使能**（純函式 `cia402_enable_step()` 原樣重用）：`0x6040`: 0x06→0x07→0x0F
7. **時序約束（韌體要 enforce）**：上電到可使能 **T1 > 5 s**；使能後抱閘釋放 500 ms + **再等 ≥500 ms 才可發運動指令**；下使能後 ≥300 ms 才能再使能
8. **CSP 防跳**：使能前**必須 `0x607A` ← `0x6064`**（現有程式碼已有同樣邏輯，保留）；未來 CST/CSF：使能前 `0x6071=0`

### 7.2 ESM 與 CiA402 雙狀態機

EtherCAT 下不再有 NMT/Heartbeat/EMCY（`0x1016/0x1017/0x1014` 等物件明標 ×EtherCAT），改成：

- **鏈路層健康**：ESM 狀態（`0x2670` 可映 PDO）+ 每 tick WKC 核對 + AL status（`0x2672`）
- **軸層健康**：statusword（bit4=母線電壓正常、bit13=跟隨誤差）+ `0x603F` error code
- safety 的 50 ms 通訊逾時邏輯改綁 WKC 連續失敗計數（1 kHz 下 = 連續 50 次）

### 7.3 故障碼對照（bring-up 排錯表）

| 面板碼 | 0x603F | 意義 / 對策 |
| --- | --- | --- |
| E401 | 0xFF31 | EtherCAT 丟包 → 查線材屏蔽/接頭 |
| E402 | 0xFF32 | 未進 OP → 主站 ESM 流程問題 |
| E403 | 0xFF30 | 網線未接 |
| E404 | 0xFF34 | **插補週期不支援** → 核對週期為 500 µs 整數倍、`0x60C2` |
| E405 | 0xFF33 | **PDO 超過 6 entries** → 精簡映射 |
| E301 | 0x5454 | STO 觸發 → 查急停鏈 24 V |
| E131 | 0x3220 | 欠壓（<24 V 抱閘打不開）→ 查電源壓降（菊鏈供電症狀） |

### 7.4 單位換算（robot_config 校正）

- 位置/速度單位一律 plus（counts）；輸出端 19-bit 絕對編碼器 = **524288 counts/rev**（角度 = counts/524288×360°）。OD `0x2025` 預設值與規格書標稱不同（17-bit vs 19-bit），**以實機 SDO 讀回為準**。
- 減速比 `0x26A2/0x26A3`（如 101:1）、EtherCAT 電子齒輪 `0x2217/0x2218`（預設 1:1，建議維持，換算全放主站側）。
- 扭矩：`0x6077` 為 ‰ 額定，實際 mNm = (0x6077/1000)×`0x6076`；力控版另有 `0x2087` 模組額定轉矩（Nm, REAL）。
- 回零僅支援 **method 35**（當前位置設零）→ 絕對零位由主站管理（現有 3D 視覺化的 home offset 機制沿用），標定後 `0x2130=1` 存 EEPROM。

## 8. DC 同步與 F746 即時性設計

### 8.1 同步模式

採 **DC-Synchron**（從站以 SYNC0 硬體事件鎖拍，參考時鐘 = 第一顆從站）。

> ⚠️ **更正（2026-07-08 實機證實）**：DC 不是「偏好」而是 **CSP 動作的硬性
> 必要條件**。EYOU PHU 在 **free-run（`assign_activate=0`）下,drive 會進
> OperationEnabled、`0x607A` target 會 ramp,但內部 position demand 凍住、
> 實際位置完全不動、且無 fault**——過去 §5.3「先 free-run 再 DC」的 bring-up
> 假設錯誤,務必**從一開始就開 DC**（`assign_activate=0x300`、SYNC0 週期=控制
> 週期、每 cycle 由主站 sync reference/slave clocks）。開 DC 後馬達立即跟隨。
> gx701 實測：100 rpm 連續轉 16.6 馬達圈、追隨誤差穩定。配方見 `firmware/ecat/
> ec_config.h`,證據見 `../changes/2026-07-07-phu17-safeop-diagnosis.md`（六輪）。

### 8.2 主站週期漂移補償

F746 的 TIM 時基與從站 DC 時鐘會漂移。標準做法（SOEM 慣例）：

1. 每 cycle 從 process data 讀回 DC 時間（`ec_DCtime`）
2. 計算 `(ec_DCtime % cycletime)` 相對目標相位的偏差
3. PI 調節器微調下一次 TIM 週期（±數十 ns 級），把主站發幀時刻鎖到 DC 柵格上

F746 ETH MAC 具 IEEE 1588 硬體時戳，可作為進階選項（更精確的發幀時刻量測），第一版先用 SOEM 標準軟體補償即可。

### 8.3 板端執行模型（bare-metal，無 RTOS）

> **F746 EtherCAT 控制流程（WP-H6;沿用 gx701 實證配方）**：TIM 觸發的每個
> tick 必須**相位鎖定到 SYNC0**——TIM 週期 = SYNC0 週期,由 §8.2 的 DC PI
> （對映 `firmware/engine` 的 `eng_phase_trim_us` + `ec_dc_pll`）微調 TIM
> reload 把發幀時刻壓到 DC 柵格。每 tick：`ec_axis_set_output`（含 mode=8
> 寫進 RxPDO,**不走 SDO**）→ `ec_master_exchange`（送 RxPDO+收 TxPDO+WKC）→
> safety → 控制。init 時 `ec_master_op()` 內啟 DC（`assign_activate=0x300`）,
> **不重映射 PDO**（用出廠佈局,見 `ec_config.h`）。無 DC 則馬達不動。

- **TIM（1 kHz，最高優先權 ISR）**：觸發 `app_main_tick()`，內含 `ec_master_exchange()`。ETH DMA 收發都是 zero-copy descriptor 操作，單次交換（發幀+等回+解析）在 100 Mbps 下 < 100 µs
- **主迴圈（背景）**：host 介面、1 Hz 狀態列印、LED
- **預算**：1 ms 週期內——交換 ~0.1 ms + L2–L4 控制運算（現有 500 Hz 下實測餘裕大，M7 216 MHz + FPU）+ 安全檢查，預估 < 0.5 ms，餘裕 2 倍
- **記憶體**：SOEM context + 14 軸 IOmap（<1 KB）+ ETH DMA descriptors/buffers（~16 KB，放 SRAM1 並注意 D-cache MPU 設定——F7 的 ETH DMA 與 D-cache 一致性是已知坑，descriptor 區要設 non-cacheable）

## 9. PC 端與模擬策略（延續三後端模式）

現有「PORTABLE_SRC + 可插拔後端」三件套照搬：

| 後端 | CANopen（現況） | EtherCAT(目標) |
| --- | --- | --- |
| 板端 | `co_bxcan.c`（bxCAN） | `nicdrv_stm32f7.c`（ETH MAC） |
| PC 主站 | `co_bxcan_socketcan.c`（vcan） | **SOEM Linux raw socket**（需真網卡 + 真從站；建議先買 1 顆 PHU14 接 PC 驗證） |
| 模擬 | `co_bxcan_sim.c` + `phu_sim.c` | **`ec_master` 門面級 fake**：實作同一組 `ec_master.h` API，後面接現有 `phu_sim` 的 CiA402 模型（EtherCAT 線路級模擬成本過高，不做） |

開發順序建議 **PC 先行**：SOEM 在 Linux 上先跟真關節打通（等效現在的 `pc_master`），驗證 PREOP 組態、PDO 佈局、DC 參數、CiA402 序列全部正確後，同一套 `ec_master.c`/`ec_config.h` 原封搬上 F746，只換 nicdrv/osal。這正是本專案 CANopen 階段已驗證有效的路徑。

## 10. 工作分解（WP-E）

| WP | 工作項 | 產出 / 驗收 |
| --- | --- | --- |
| **E0 前置** | 確認關節尾碼 E/C 與 `0x2100`；向 EYOU 索取 ESI；RJ45→GHS 轉接線；SOEM 引入 vendor tree；法務確認 GPL | 硬體/授權檢核表 |
| **E1 PC 主站打通**（1 軸） | SOEM Linux + 1 顆 PHU：掃鏈、PREOP 重映射、SAFEOP→OP、CiA402 使能、CSP 點動 | 單軸轉動；E404/E405 清零 |
| **E2 DC 同步** | DC-Synchron 1 kHz、漂移補償、抖動量測（示波器量 SYNC0 vs 主站發幀） | 抖動報告（目標 < 10 µs） |
| **E3 抽象層** | `ec_master.h` 定案；`dual_arm.c` 改雙後端；`cia402.c` SDO 三函式接 CoE；`control_rate.h` → 1 kHz；fake 後端 + 單元測試 | HOST build 全綠（既有 ctest 擴充） |
| **E4 F746 移植** | nicdrv_stm32f7（ETH DMA + MPU non-cacheable）、osal_baremetal、TIM 1 kHz + PI 鎖相 | 板端對 1 軸 OP + CSP 點動 |
| **E5 多軸擴展** | 7 軸單臂 → 14 軸雙臂；供電（樹形 + PD50 泄放）；STO 鏈；WKC 健康度接 safety | 14 軸 1 kHz 軌跡跟隨 demo |
| **E6 收尾** | 3D 視覺化/ws_server 接新後端；文件、變更紀錄；`docs/design/canopen-vs-ethercat.md` 決策定案 | 全棧 demo + 文件 |

里程碑：**ME1** = E1+E2（單軸 1 kHz DC 同步，遷移可行性拍板點）→ **ME2** = E3+E4（板端單軸）→ **ME3** = E5（雙臂 14 軸）→ **ME4** = E6。

## 11. 風險與對策

| 風險 | 等級 | 對策 |
| --- | --- | --- |
| SOEM GPLv2 授權 vs 產品化 | 高（商業）/低（研發） | 研發期照用；產品化前換 Acontis 商業授權或自研（`ec_master.h` 抽象層已隔離，替換成本受控） |
| F7 ETH DMA + D-cache 一致性 | 中 | Descriptor/buffer 放 non-cacheable MPU 區；這是 F7 社群已知且有標準解的坑 |
| 現有關節為 C 尾碼、無法/難切 EtherCAT | 中 | E0 先向原廠確認；`0x2100` 預設=1 顯示硬體皆備，大概率參數即可切 |
| ESI / Vendor ID 拿不到 | 低 | SOEM 不強制 ESI（掃鏈 + SDO 讀 `0x1018` 即可組態）；ESI 主要給 TwinCAT 對照驗證用 |
| 1 kHz 下 L3/L4 運算超時 | 低 | 現 500 Hz 餘裕大；不足時 task-space 降半頻（500 Hz）、joint-space 保 1 kHz 的分頻架構 |
| 供電菊鏈壓降 → 欠壓抱閘打不開（E131） | 中 | PHU20/17 走單軸直連/樹形供電；掛 PD50 泄放；量測滿載母線電壓 |
| 週期/插補參數不符（E404） | 低 | §5.3 參數核對清單；週期嚴格取 500 µs 整數倍 |

## 12. 驗證方式

1. **協定層**：TwinCAT（手冊 §6 流程）先對關節做一次「官方主站」驗證，作為 SOEM 行為的對照組
2. **同步**：示波器量兩顆從站 SYNC0 對齊度、主站發幀抖動；連續 24 h WKC 錯誤計數
3. **控制**：既有單元測試（HOST build）+ fake 後端回歸；單軸→7 軸→14 軸正弦軌跡跟隨誤差對比 500 Hz CANopen 基線
4. **安全**：拔線（E403）、觸發 STO（E301）、模擬掉軸（WKC 缺 1）→ 全部須在 50 ms 內進 safe stop
5. **相容**：`BUS_BACKEND=canopen` 舊路徑仍可編譯執行（退路保留至 ME3 達成）

## 13. 關聯文件

- 選型比較：`canopen-vs-ethercat.md`（本文件將其 §4 待決議拍板為「F746 直接當 EtherCAT 主站，不需 ESC」）
- 總體工作計畫：`dual-arm-control-plan.md`（WP0.1→EtherCAT、WP0.2→STM32 主站+PC 先行驗證、WP0.3→維持 F746ZG）
- 馬達分析：`eyou-phu-motor-analysis.md`
- 變更紀錄：`../changes/2026-07-04-ethercat-migration-plan.md`
