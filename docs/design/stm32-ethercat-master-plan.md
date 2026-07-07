# STM32F746 板端 EtherCAT（CoE）主站 — 深度規劃（WP-SE）

> 分支：`feature/stm32-ethercat-master`（基底 = `h-sdo-bg-escalation` ＋併入 `ecat-backend-sim` 的 DC 鎖相 PI）
> 定位：四條主站路線（PC/STM32 × CANopen/EtherCAT）的第 4 條，也是唯一尚未動工的一條。
> 本文件是 `ethercat-coe-master-plan.md` **WP-E4（F746 移植）的展開**，並補齊 SIL/HIL 分層與 harness 復用評估。

---

## 1. 目標與範圍

把已在 PC 端驗證過的 EtherCAT（CoE, CiA 402）主站堆疊，移植到 **Nucleo-F746ZG 板端 bare-metal**，
達成 **1 kHz、雙臂 14 軸 EYOU PHU** 週期控制：

- 板端跑 SOEM 主站：掃鏈 → PREOP 組態 → SAFEOP → OP，DC-Synchron 同步。
- 復用既有 `ec_master.h` 門面（三後端之一：`ec_master_soem.c`）與 harness/loop engine（H0–H5 全套）。
- 硬體零改造：Nucleo-F746ZG 板載 ETH MAC（RMII）+ LAN8742A PHY + RJ45，**主站不需要 ESC**
  （ESC 是從站晶片；已在 `ethercat-coe-master-plan.md` §4.3 定案）。

**不在範圍**：task-space IK 上板效能調優（沿用既有 `control/`，量測後再議）、EtherCAT 線纜冗餘（單埠做不到）。

## 2. 為什麼 EYOU 的 CANopen 資產幾乎全部可以搬 — CoE 對照表

EYOU PHU 的 EtherCAT 走 **CoE（CANopen over EtherCAT）**：應用層物件字典與 CiA402 狀態機
**跟我們已完成的 CANopen 主站完全同一套**，只有傳輸層換掉。逐層對照：

| 層 | CANopen（已完成） | EtherCAT CoE（本計畫） | 復用程度 |
| --- | --- | --- | --- |
| 物件字典 | 0x6040/0x6041/0x607A/0x6064/0x6060/0x6061… | **同一組索引，一個都不變** | 100%（`phu_od.py`、bring-up 排錯知識直接沿用） |
| 驅動狀態機 | CiA402（`canopen/cia402.c`） | **同一份 `cia402.c`**，H5 已證 agents 零修改跑雙協定 | 100% |
| 服務資料 | SDO（8-byte CAN 幀，分段） | CoE SDO（mailbox，可 expedited/分段，更大 payload） | 呼叫端 100%（`ec_coe_read/write` 門面已定）；底層由 SOEM 提供 |
| 週期資料 | PDO（COB-ID 0x180+n…） | Process Data（0x1600/0x1A00 重映射 + 0x1C12/0x1C13 assignment，僅 PREOP 可改，每 PDO ≤6 entries，超過報 E405） | 映射內容同、組態手續不同 |
| 網路管理 | NMT（reset→PreOp→Start） | ESM（INIT→PREOP→SAFEOP→OP，AL control/status） | 序列對映清楚（見 §7.1 母計畫） |
| 同步 | SYNC 幀（0x080）＋鎖存 | **DC SYNC0**（分散時鐘，硬體級，µs 抖動） | 概念同、實作換 `ec_dc_pll` |
| 緊急事件 | EMCY（0x080+n） | CoE Emergency（mailbox） | EMCY→safe stop 條款（G5）沿用 |
| 節點定址 | node-id（0x26A0 佈建） | 鏈上位置定址／configured address | 佈建腳本觀念沿用 |
| 錯誤碼 | E401–E405/E301/E131（原廠手冊） | **同一張表**（E404=週期非 500 µs 整數倍、E405=PDO 超 6 entries） | 100% |

結論：**要新寫的只有「底層傳輸」**（板端網卡驅動 + SOEM 移植），應用層與監督層照搬。

## 3. 既有資產盤點（本分支已就位 vs 要從別的分支搬）

### 3.1 本分支已就位（h-sdo-bg-escalation ＋ ecat-backend-sim 合併，5102 單元測試 PASS）

| 資產 | 位置 | 說明 |
| --- | --- | --- |
| loop engine + 相位排程 | `firmware/engine/loop_engine.c` | overrun SKIP、WCET 預算、host+arm 雙編譯 |
| harness 監督層 | `firmware/engine/harness.c` | §5.2 升級策略：miss 視窗、降頻退避、bus 重啟、agent 停用 |
| agents | `firmware/app/app_agents.c`、`app_io_agents.c` | H1 四 agent，bus-agnostic |
| bus 抽象 | `firmware/engine/bus_if.h` + `app/bus_canopen.c`/`bus_ecat.c` | H5 vtable，`--bus ethercat` 已可跑 sim |
| EtherCAT 門面 | `firmware/ecat/ec_master.h` | init/op/exchange/coe_read/write/health，一拍延遲模型 |
| fake 後端（SIL） | `firmware/ecat/ec_master_sim.c` | 接 phu_sim CiA402 模型，WKC/掉軸/使能免硬體全驗 |
| DC 鎖相 PI | `firmware/ecat/ec_dc_pll.c` | ±200/-500 ppm 漂移收斂 \|err\|≤3 µs（SIL 已證） |
| 三環 ring | `engine/spsc_ring.c`、`eng_log.c`、`eng_trace.c` | cmd/telemetry/log/trace，RT 分域 |
| SDO 背景通道 | `firmware/app/sdo_bg.c` | RUN 中讀寫 OD 不擾動 PDO 流量 → CoE mailbox 天然對應 |
| CiA402 + 控制鏈 | `canopen/cia402.c`、`control/*`、`safety/*` | 雙協定共用 |
| F746 板端工程 | `firmware/board/`、Makefile bring-up | CANopen 版可編可燒（develop 已證） |

### 3.2 要從其他分支搬入

| 資產 | 來源分支 | 搬法 |
| --- | --- | --- |
| SOEM 完整原始碼 | `linux-rt-ethercat-master`（`third_party/SOEM`，commit a2cff83） | SE2 直接 checkout 該路徑 |
| EYOU ESI 檔 + 原廠參考主站（gallop_ws） | 同上（`third_party/eyou_esi`） | SE2 一併帶入（PDO 出廠映射對照） |
| SAFEOP 診斷工具（wd_read/phu_state） | 同上（`tools/`） | HIL-2 排錯用 |
| PHU17 真機特性化紀錄 | 同上（docs/changes 07-07 四輪診斷） | 文件引用即可 |

## 4. 關鍵前置風險：0x2100 控制權鎖（**HIL-2 的硬性阻塞**）

2026-07-07 真機四輪診斷定案（commit 6347f95）：關節控制權在 CANopen（0x2100=2）時，
**EtherCAT 側所有 SDO 寫入被拒（abort 0x06010000），連 0x2100 本身都改不了**（唯讀被動），
這正是 SAFEOP 彈跳的根因。含義：

- 板端 EtherCAT 主站對「現態關節」**只能讀不能寫**——OP/使能都不可能。
- 切控制權**只能從 CANopen 或 UART 側做**：用既有 `tools/provision_joint.sh`（WP-C0.3）
  經 CANable 寫 0x2100=1 並存檔，之後 EtherCAT 才拿得到控制權。
- 因此 HIL-2（真 PHU）前必須排一個 **SE-P0 佈建步驟**，且 SIL/HIL-1 完全不受此限
  （這也是本計畫把假從站排在真機前面的原因）。

## 5. 工作分解（WP-SE）

> 命名沿用母計畫 WP-E；SE = STM32 EtherCAT。每項附驗收。

| WP | 內容 | 驗收 | 狀態 |
| --- | --- | --- | --- |
| **SE0 資產整併** | 開分支、併 DC PLL、衝突解掉、全套 HOST 測試 | WSL ctest 5102 PASS | ✅ 本次完成 |
| **SE1 SIL 基線報告** | `pc_master --bus ethercat @1kHz`（sim 後端）＋ trace ring 報表：p99、四相位 WCET、escalation 演練（掉軸/WKC 短少/DC 漂移注入） | SIL-B 報告一份（docs/changes），數字進 §8 預算表 | ✅ 2026-07-07（四相位 p99≈14µs；RT 數字見 07-06 報告） |
| **SE2 vendor 搬入** | 從 linux-rt 分支帶 `third_party/SOEM`、`third_party/eyou_esi`、SAFEOP 工具（✅）；SOEM core 以 **arm-none-eabi 編過** | arm build 連結成功（.map 檢視 Flash/RAM footprint） | ✅ 2026-07-07（38KB Flash / 62KB RAM） |
| **SE3 nicdrv_stm32f7** | F746 ETH MAC 驅動：RMII+LAN8742A 初始化、DMA descriptor ring 放 **MPU non-cacheable region**（D-cache 一致性）、raw frame（EtherType 0x88A4）送收、**不經 LwIP** | HIL-0：直連 PC NIC，Wireshark 看到板子送的 EtherCAT 幀；board→PC→board 迴環延遲量測 | ✅ 2026-07-08 HIL-0+**HIL-1** 過（板端對 ecat_slave.py 2 軸：OP+CSP WKC 零漏；DWT LAR/ReleaseTx 修正）。⚠ 100M link 不穩暫以 10M 繞道，HIL-2 前須解（線/供電） |
| **SE4 osal_baremetal + 1 kHz tick** | SOEM osal（tick 計時、無 RTOS busy-wait/中斷混合）、TIM 1 kHz 週期源、`ec_dc_pll` 接真 `ec_DCtime`（`ec_master_dc_error_us()` 由 SOEM 導出） | 板端空鏈掃描不當機；tick 抖動 VCP trace 報表 p99 < 20 µs | ⬜ |
| **SE5 ec_master_soem 後端** | 寫 `firmware/ecat/ec_master_soem.c` 接門面（PC 端先開發：Linux raw socket 跑同一份 .c，**先在 PC 對假從站全驗，再上板**——延續「同套韌體多後端」模式） | SIL-C：PC 上 SOEM 後端對 `ecat_slave.py` 走完 init→OP→CSP 點動 | ⬜ |
| **SE6 板端單軸 OP** | SE3+SE4+SE5 合體上板：板端對「假從站或 ESC 評估板」PREOP 重映射（≤6 entries）→SAFEOP→OP→CiA402 使能→CSP 點動 | HIL-1 驗收；E404/E405 清零 | ⬜ |
| **SE-P0 真機佈建** | `provision_joint.sh` 經 CANable 把 PHU 0x2100=2→1（CANopen 側寫），存檔重上電，EtherCAT 側確認可寫 | `ec_coe_write` 0x6060 成功（解除 §4 阻塞） | ⬜ |
| **SE7 真機單軸** | 板端 ↔ 1 顆 PHU17：OP + DC-Synchron + CSP 點動；示波器量 SYNC0 vs 主站發幀抖動 | HIL-2：抖動 < 10 µs、位置跟隨無 E404 | ⬜ |
| **SE8 多軸擴展** | 7 軸單臂 → 14 軸雙臂；PDO 頻寬複核（§5.2 母計畫）、WKC 健康度接 safety、STO 鏈、供電（樹形 + PD50 泄放防 E131） | HIL-3：14 軸 1 kHz 軌跡跟隨 demo；escalation 真機演練 | ⬜ |
| **SE9 收尾** | 3D viewer/ws_server 接板端 telemetry、文件、`canopen-vs-ethercat.md` 決策定案更新、GPL 授權註記 | 全棧 demo + docs/changes | ⬜ |

里程碑：**MSE1** = SE1+SE2（SIL 基線 + arm 可連結）→ **MSE2** = SE3（板端 raw frame 通，硬體可行性拍板點）→
**MSE3** = SE6（板端單軸 OP 對假從站）→ **MSE4** = SE7（真機單軸）→ **MSE5** = SE8（14 軸）。

## 6. SIL / HIL 分層策略

### 6.1 SIL（軟體在環）— 三層，全部免板免馬達

| 層 | 內容 | 依託 | 狀態 |
| --- | --- | --- | --- |
| **SIL-A 單元測試** | ctest 全套（engine/harness/ecat/cia402/DC PLL…） | `firmware/tests/`（Windows 缺 `sys/mman.h`，**全套跑 WSL/Linux**；MinGW 只能跑不含 rt_selfcheck 的子集） | ✅ 5102 PASS |
| **SIL-B 閉環模擬** | `pc_master --bus ethercat`：ec_master_sim + phu_sim（8 模式 CiA402）+ 漂移模型 + DC PLL + harness escalation | H5/WP-L2.2 成果 | ✅ 可跑，SE1 補 1 kHz 驗收報告 |
| **SIL-C 協定在環** | PC 上真 SOEM 堆疊 ↔ **軟體假從站 `ecat_slave.py`**（見 §6.3），veth pair 或雙 NIC 對接；走完整 ESM/CoE/過程資料 | SE5 新作 | ⬜ |

### 6.2 HIL（硬體在環）— 四級，逐級加真

| 層 | 拓撲 | 驗什麼 | 前置 |
| --- | --- | --- | --- |
| **HIL-0 板端自檢** | Nucleo 直連 PC NIC + Wireshark | nicdrv 送收、DMA/cache 正確性、空鏈失敗路徑 | SE3 |
| **HIL-1 假從站** | Nucleo ↔ PC（`ecat_slave.py` + 復用 `phu_motor.py` 8 模式模型） | ESM 全流程、CoE SDO、PDO 重映射、CiA402 使能、看門狗/掉軸 | SE6 |
| **HIL-1′ 真 ESC（選配，強烈建議）** | Nucleo ↔ LAN9252 評估板（EasyCAT，約 NT$1.5–2k）或 AX58100 EVB | **真 DC/SYNC0 時序**——軟體假從站給不了的：示波器量抖動、DC PLL 真收斂 | SE6 |
| **HIL-2 真機單軸** | Nucleo ↔ PHU17 ×1 | 原廠韌體相容性、E404/E405 排錯、力/位真實回授 | **SE-P0（0x2100 切換）** |
| **HIL-3 多軸** | 7 軸單臂 → 14 軸雙臂菊鏈 | 頻寬、WKC 健康、供電壓降（E131）、STO | HIL-2 |

### 6.3 假從站 `ecat_slave.py` 設計（HIL-1 核心新作，對應 C1 的 `can_slave.py`）

- **復用**：`phu_motor.py` 的 CiA402 全狀態機 + 8 模式馬達模型**一行不改**（read_od/write_od/apply_controlword/step 介面本來就 bus-agnostic）。
- **新寫**：EtherCAT datagram 層（raw socket 綁 PC NIC，解析 0x88A4：APRD/APWR/LRW…、AL control/status 狀態機、
  mailbox CoE SDO、FMMU/SM 的最小子集）。單從站、線形拓撲，約略對應 can_slave.py 的 SDO/NMT/PDO 純函式層。
- **已知限制（要寫進驗收報告）**：軟體從站無 ESC 硬體 on-the-fly 轉發——frame 延遲毫秒級、無真 DC。
  所以 HIL-1 只驗「協定正確性」，**時序驗證交給 HIL-1′ 或 HIL-2**。
- `web_monitor.py` SSE 看板可原樣掛在 phu_motor 模型上（監看層不碰 bus）。

## 7. Harness agent 復用評估（結論：可以用，缺口只有三個）

之前做的 harness/agent/loop engine（`docs/design/harness-agent-loop-engine-plan.md`，WP-H0–H5）**就是為這一步設計的**：

**直接可用（零修改）**
- H5 已定案 `bus_if_t` vtable，`bus_ecat.c` 後端存在且 agents 零修改跑雙協定（07-05 已驗）。
- 監督層 escalation（miss 視窗/降頻退避/bus 重啟/agent 停用）語意 bus-agnostic；`bus_health_t` 已預留 WKC/AL 欄位（proto[0..2]）。
- SDO 背景通道 `sdo_bg` 的「非 RT 佇列 + HOUSEKEEP 分片」直接對應 CoE mailbox 的非週期性本質。
- log/trace ring：SE4 的板端抖動驗收就靠 trace ring + `trace_report.py`。

**三個缺口（排進 WP）**
1. **eng_port 板端 1 kHz**：loop engine 的 tick 源目前板端是 TIM6 500 Hz（CANopen 版）；要改 1 kHz 並以
   `eng_phase_trim`（H 交叉盤點時預留的 DC 鎖相鉤子）接 `ec_dc_pll` 輸出 → SE4。
2. **ec_master_soem.c 不存在**：門面三後端只有 sim 寫完；SOEM 真後端是 SE5 主體。
3. **板端 trace 讀出**：PC 端 trace 走檔案，板端需 VCP dump 通道（沿用 CANopen bring-up 的 VCP 115200 路徑）→ SE4 驗收工具。

## 8. 資源預算與風險

| 風險 | 等級 | 對策 |
| --- | --- | --- |
| **0x2100 控制權鎖**（§4） | 高（阻塞 HIL-2） | SE-P0 走 CANopen 側佈建；SIL/HIL-1 先行不受阻 |
| SOEM GPLv2 | 中（產品化） | 開發/研究無虞；產品化前法務確認或換 Acontis/自研（母計畫 §6.1 已註記） |
| F746 CPU 餘裕：14 軸 exchange + PLL + 控制鏈 @1 kHz | 中 | SE1 先在 PC 量四相位 WCET；板端 SE4 trace 實測；不夠則 joint-space 上板、task-space 留 PC（架構已支援） |
| D-cache 與 ETH DMA 一致性 | 中（經典地雷） | descriptor + buffer 進 MPU non-cacheable region；SE3 驗收含壓力測試 |
| SOEM Flash/RAM footprint（1 MB/320 KB） | 低–中 | SE2 連結後看 .map；SOEM 可裁（只留 CoE/DC） |
| 單埠無冗餘、斷鏈即全停 | 低（已知限制） | WKC 健康 → escalation safe stop；不做冗餘 |
| 軟體假從站時序不真 → HIL-1 過了真機仍炸 | 中 | HIL-1′ 買一片 LAN9252 評估板補真 ESC 時序 |
| 週期非 500 µs 整數倍 → E404 | 低 | 1 kHz 在規格內；`0x60C2` bring-up 核對表（母計畫 §7.3） |

## 9. 驗證方式總表

- **每個 WP 的驗收欄**（§5）即為過關條件；SIL 全綠才准進下一級 HIL。
- 量化門檻：tick 抖動 p99 < 20 µs（板端 trace）、SYNC0 對主站幀抖動 < 10 µs（示波器）、
  DC PLL |err| ≤ 3 µs（SIL 已達）、WKC 缺軸 → 50 ms 內 safe stop（沿用 CANopen 看門狗驗收）。
- 每完成一個 WP 在 `docs/changes/` 留驗收紀錄（含量測數字），並更新本文件狀態欄。

## 10. 關聯文件

- 母計畫：`docs/design/ethercat-coe-master-plan.md`（WP-E、PDO 映射、錯誤碼表、DC 設計）
- Harness：`docs/design/harness-agent-loop-engine-plan.md`（H0–H5）
- PC 端 RT 實戰：`linux-rt-ethercat-master` 分支 docs/changes（PHU17 特性化、SAFEOP 四輪診斷、0x2100 定案）
- CANopen HIL 前例：`docs/design/sim-fake-hardware.md`（C1：can_slave.py/phu_motor.py/web_monitor.py）
- 選型背景：`docs/design/canopen-vs-ethercat.md`、`docs/design/eyou-phu-motor-analysis.md`
