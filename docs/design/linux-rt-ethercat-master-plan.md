# Ubuntu 22.04 Pro + PREEMPT_RT EtherCAT 主站深度規劃 — 雙臂 14 軸 1 kHz

- 文件層級：架構 / 實作規劃
- 狀態：草案（draft）
- 分支：`feature/linux-rt-ethercat-master`
- 前置文件：`ethercat-coe-master-plan.md`（EtherCAT 協定面設計——PDO 映射、DC 規則、CiA402 序列、EYOU 故障碼——**全部沿用**，本文件不重複，只寫 Linux 主站特有部分）
- 決策變更：**STM32F746 板端主站暫緩**（保留為未來選項）；主站改為 **Ubuntu 22.04 Pro（PREEMPT_RT 即時內核）x86 PC/工控機**，直接驅動雙臂 14 顆 EYOU PHU 關節（CoE, CiA 402, CSP @1 kHz）

---

## 1. 目標與定位

| 項目 | 內容 |
| --- | --- |
| 主站 | x86 PC / 工控機，Ubuntu 22.04 LTS + Ubuntu Pro **realtime-kernel**（5.15-rt, PREEMPT_RT） |
| 從站 | EYOU PHU ×14（單鏈：左臂 J1–J7 → 右臂 J1–J7），CoE + DC-Synchron |
| 控制 | 既有 L1–L4 控制堆疊（joint/task-space、雙臂協同、safety）**原封重用**，1 kHz |
| 模式 | CSP 起步；力控版後續切 CST/CSF（PDO 佈局已預留，見前置文件 §5.1） |
| 與 STM32 版的關係 | 協定層（`ec_master.h` 抽象、PDO/DC/CiA402 設計）共用；未來要回 STM32 只需換 nicdrv/osal 移植層 |

**為什麼 PC 主站是合理的第一步**（原 `canopen-vs-ethercat.md` §4 待決議的另一種拍板）：

1. EtherCAT 主站生態以 PC 為主流（TwinCAT / IgH / SOEM），除錯工具鏈完整（Wireshark EtherCAT dissector、ethercat CLI、ftrace）。
2. 本 repo 已驗證「同套韌體跑 PC」模式（`firmware/pc/pc_master` + SocketCAN），這次只是把底層從 vcan 換成真網卡。
3. 1 kHz × 14 軸對 x86 是輕載，把風險集中在「RT 調校 + EtherCAT 協定打通」兩件事上，不用同時打 MCU 移植這第三場仗。
4. 上位機（ws_server、3D 視覺化、未來 ROS 2）與主站同機，遙測零成本。

## 2. 硬體選型（WP-L0 採購依據）

### 2.1 主機

- x86 工控機或桌機，**4 核以上**（2 核跑一般負載、2 核隔離給 RT），8 GB RAM 足夠。
- **BIOS 要求**：可關閉 Intel SpeedStep/TurboBoost/C-states、可關 SMT（Hyper-Threading）、無不可關閉的 SMI 大戶（劣質 BIOS 的 SMI 是 RT 最大殺手，驗收靠 `hwlatdetect`）。

### 2.2 網卡（關鍵零件）

| 等級 | 型號 | 說明 |
| --- | --- | --- |
| 建議 | **Intel i210 / i211（igb）、i225 / i226（igc）** | EtherCAT 社群最常用，中斷行為乾淨、驅動成熟；i225/i226 另有 TSN LaunchTime（進階選項） |
| 可用 | Intel e1000e（板載常見） | 可跑，抖動略大 |
| 避免 | Realtek 板載、**任何 USB 網卡** | 中斷/DMA 行為不可控，USB 路徑抖動毫秒級 |

EtherCAT 專用一張獨立網卡（不跑 IP），管理/上網用另一張。RJ45 → JST GHS 5 pin 轉接線同前置文件 §4.3。

### 2.3 從站側

接線、供電（雙臂額定 45.4 A@48V、PD50 泄放、PHU20/17 禁電源菊鏈）、STO 鏈、IN→OUT 方向決定軸序——**全部沿用前置文件 §4**，此處不重複。

## 3. Ubuntu 22.04 Pro + PREEMPT_RT 環境

### 3.1 即時內核安裝

```bash
sudo pro attach <token>            # Ubuntu Pro（個人 5 台機器免費）
sudo pro enable realtime-kernel    # 安裝 linux-image-realtime（5.15-rt）
sudo reboot
uname -a                           # 應含 PREEMPT_RT
```

注意：啟用後 apt 會跟蹤 realtime 內核系列；**內核升級後所有 RT 驗收測試要重跑**（列入 WP-L7 回歸清單）。NVIDIA 專有驅動與 RT 內核相容性差，主站機不裝獨顯或用 nouveau/內顯。

### 3.2 RT 調校基線（WP-L0 產出：一鍵設定腳本 `firmware/rt/setup_rt.sh`）

以 4 核機、隔離 CPU2/CPU3 為例：

- **內核參數**（`/etc/default/grub`）：
  `isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 irqaffinity=0-1 skew_tick=1 intel_pstate=disable processor.max_cstate=1 intel_idle.max_cstate=0 nosoftlockup`
- **CPU**：governor=performance、關 TurboBoost（頻率抖動）、BIOS 關 SMT。
- **IRQ 綁定**：EtherCAT 網卡 IRQ → CPU2（與 cyclic thread 的 CPU3 相鄰但不同核）；其餘 IRQ 留 CPU0-1。
- **執行緒優先權配置**（SCHED_FIFO）：

| 執行緒 | CPU | 優先權 | 職責 |
| --- | --- | --- | --- |
| NIC IRQ thread（`irq/xx-igb`） | 2 | 85 | 收包必須搶在 cyclic 之前完成 |
| cyclic（1 kHz 控制迴圈） | 3 | 80 | exchange + safety + L2–L4 |
| housekeeping | 0-1 | 20 | SDO 背景讀、統計 |
| host/ws_server、log | 0-1 | 一般（CFS） | 遙測、3D 視覺化、命令 |

- **行程**：`mlockall(MCL_CURRENT|MCL_FUTURE)`、預觸碰堆疊、禁 page fault 於 RT 路徑；RT 路徑內**禁 malloc/檔案 I/O/println**（log 走 lock-free ring buffer 由非 RT 執行緒落盤）。
- **驗收基線（進 WP-L1 前必須達標）**：
  - `cyclictest -m -Sp90 -i 1000 -D 12h`：隔離核 **max latency < 50 µs**
  - `hwlatdetect --duration=2h`：SMI 造成的 hardware latency ≈ 0（>10 µs 即需換機/調 BIOS）

## 4. 主站堆疊選型：SOEM（主）vs IgH（備）

| 面向 | **SOEM**（建議主路徑） | IgH EtherCAT Master |
| --- | --- | --- |
| 形態 | user-space 函式庫 + raw socket | kernel module（ec_master）+ 使用者庫 |
| DC 支援 | 有（`ec_configdc` + 應用側漂移補償） | 有，較成熟 |
| 部署 | 編進我們的執行檔，零安裝 | 需 DKMS/內核模組，內核升級要重編 |
| 與本 repo 契合 | **與前置文件 `ec_master.h` 抽象直接共用；未來可回移 STM32** | 綁死 Linux，STM32 路徑作廢 |
| 生態 | 輕量、程式碼可讀 | LinuxCNC/ROS 2（ethercat_driver_ros2）主流 |
| 授權 | GPLv2（rt-labs 可購商業授權） | GPLv2 module + LGPL 使用者庫 |

**決策：SOEM 為主路徑**——延續前置規劃的抽象層投資，單一程式碼庫。若 WP-L2 抖動驗收過不了（raw socket 路徑不穩），**升級路徑是換 IgH + 原生 igb 驅動**，`ec_master.h` 門面不變、只重寫 `.c` 後端（此隔離正是抽象層存在的理由）。未來若上 ROS 2，IgH 生態是加分項，屆時再評估。

## 5. 軟體架構

### 5.1 目錄（延續三後端模式）

```
firmware/
├── ecat/                     # 前置文件 §6.2 的協定層（平台無關）
│   ├── ec_master.h/.c        # 門面：init/exchange/coe/health
│   ├── ec_config.h           # 14 軸鏈序、PDO 佈局、週期常數
│   ├── ec_dc.c               # DC 漂移補償（PI 鎖相）
│   └── soem/                 # SOEM vendor tree
├── rt/                       # 新增：Linux RT 主站
│   ├── rt_master_main.c      # 進入點：RT 環境自檢 + 執行緒建立 + 1kHz 迴圈
│   ├── rt_cyclic.c           # clock_nanosleep(TIMER_ABSTIME) 週期引擎 + 統計
│   ├── rt_setup.sh           # grub/IRQ/governor 一鍵調校
│   └── telemetry_ring.c      # RT→非RT lock-free 遙測環
└── pc/                       # 既有 CANopen pc_master（保留為退路）
```

L1–L4（`app/dual_arm.c`、`control/*`、`safety/*`、`host/*`）不動，`dual_arm.c` 依 `BUS_BACKEND=ecat` 走新後端——同前置文件 §6.2。

### 5.2 1 kHz 週期引擎與 DC 鎖相

```
每 cycle（CPU3, FIFO 80）:
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, next)   ← 絕對時間，無累積誤差
  ec_master_exchange()        ← ec_send/receive_processdata + WKC 檢查
  讀 ec_DCtime → PI 調節 next 的偏移                        ← 把喚醒時刻鎖到從站 DC 柵格
  safety（statusword/0x603F/WKC 連續失敗計數）
  da_ctrl_tick_1khz()         ← L4→L3→L2 產生 14 軸目標
  ec_axis_set_output()×14     ← 寫 process image，下一 cycle 開頭送出（一拍延遲模型）
  遙測快照 → lock-free ring
```

- 週期 1 ms（原廠規則：500 µs 整數倍，見前置文件 §5.3；`0x60C2`=1 ms，不符報 E404）。
- DC 參考時鐘 = 鏈上第一顆從站；主站不是 DC 柵格的主人，**是跟隨者**——PI 鎖相把 `clock_nanosleep` 喚醒點對齊 `ec_DCtime` 相位，SYNC0 offset 設在 exchange 之後（如 +250 µs）保證資料先到再觸發從站鎖存。
- 遙測/命令與 RT 迴圈之間只透過 lock-free ring + seqlock 快照，**RT 路徑無鎖、無系統呼叫**（除 clock_nanosleep 與 raw socket send/recv）。

### 5.3 與既有上位機/視覺化整合

`ws_server` + 3D 視覺化（`feature/3d-humanoid-visualizer` 成果）目前吃 CANopen 幀流；改為訂閱 `telemetry_ring` 的 1 kHz 快照（降採到 50–100 Hz 推 WebSocket）。`host_if` 命令路徑（點動、模式、estop）經非 RT 執行緒寫入命令信箱，cyclic 每週期取用。

## 6. 深度工作分解（WP-L）

> 每項含驗收標準（DoD）。順序即依賴順序；L0/L1 可並行採購與開發。

### WP-L0 — 環境與硬體建置

| # | 工作項 | 驗收（DoD） |
| --- | --- | --- |
| 0.1 | 主機採購/整備（4 核 x86、BIOS 關 C-states/Turbo/SMT） | BIOS 設定清單存檔 |
| 0.2 | Ubuntu 22.04 + `pro enable realtime-kernel` | `uname` 見 PREEMPT_RT |
| 0.3 | EtherCAT 專用 Intel NIC（i210/i225）安裝 | `ethtool -i` 確認 igb/igc |
| 0.4 | `rt_setup.sh`：grub 參數、IRQ affinity、governor、優先權 | 腳本冪等、重跑安全 |
| 0.5 | RT 基線量測 | `cyclictest` 12 h max < 50 µs；`hwlatdetect` 2 h ≈ 0 |
| 0.6 | 關節確認（尾碼 E/C、`0x2100`=1）、ESI 索取、RJ45→GHS 轉接線 | 1 顆 PHU14 可上電、ECAT STA 燈作動 |
| 0.7 | 48 V 電源 + STO 急停鏈（先單軸規模） | STO 斷開 → E301 |

### WP-L1 — SOEM 打通單軸（協定可行性）

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 1.1 | SOEM vendor tree 引入 + CMake `rt_master` 目標 | 建置零警告 |
| 1.2 | 掃鏈：`ec_config_init`、讀 `0x1018`/`0x1008`（"EYou"） | 從站數/identity 正確列印 |
| 1.3 | PREOP 組態：`0x6060=8`、PDO 重映射（前置文件 §5.1，≤6 entries）、`0x1C12/13`、`0x60C2=1ms` | 無 E405；SAFEOP 可讀回授 |
| 1.4 | SAFEOP→OP + CiA402 使能（重用 `cia402_enable_step()`；enforce 上電 >5 s、鬆閘後 ≥500 ms） | statusword 到 Operation enabled |
| 1.5 | CSP 點動：使能前 `0x607A`←`0x6064`，正弦 ±5° | 軸平滑跟隨、無 E404 |
| 1.6 | 故障注入：拔線/E403、STO/E301、fault reset | 排錯表（前置文件 §7.3）逐項覆核 |

### WP-L2 — DC 同步與抖動驗收（**遷移可行性拍板點**）

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 2.1 | `ec_configdc` + SYNC0=1 ms + offset 設計 | 從站進 DC-Synchron |
| 2.2 | 主站 PI 鎖相（`ec_DCtime` → `clock_nanosleep` 偏移） | 鎖定後相位誤差 σ < 10 µs |
| 2.3 | 抖動量測：cycle 時間直方圖（RT 內建統計）+ 示波器量 SYNC0 | 1 kHz 下 max jitter < 100 µs、WKC 全對 |
| 2.4 | 壓力共存測試：同機跑編譯/網頁/磁碟 IO | RT 指標不劣化 |
| 2.5 | （若 2.3 不過）IgH 備援路徑 spike | 換後端重測的成本報告 |

### WP-L3 — 程式碼整合（抽象層落地）

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 3.1 | `ec_master.h` 門面定案（前置文件 §6.2 API） | header review |
| 3.2 | `dual_arm.c` 雙後端（`BUS_BACKEND=ecat|canopen`），`g_jstate` 介面不變 | 兩種 build 皆過 |
| 3.3 | `cia402.c` 三個 `*_sdo()` 改走 `ec_coe_read/write` | 單元測試 |
| 3.4 | `control_rate.h` → 1 kHz（`CONTROL_HZ=1000`） | L2–L4 現有測試全綠 |
| 3.5 | fake `ec_master` 後端（接既有 `phu_sim` CiA402 模型） | HOST build ctest 全綠、免硬體 CI 可跑 |
| 3.6 | 單位換算校正：實機 SDO 讀 `0x2025`/`0x26A2-A3`，更新 `robot_config` | 讀回值寫入文件（19-bit=524288 cnt/rev 待實證） |
| 3.7 | `telemetry_ring` + RT 統計（cycle min/avg/max、WKC 錯誤、DC 相位） | 1 Hz 摘要列印 |

### WP-L4 — 單臂 7 軸

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 4.1 | 7 軸鏈佈線（IN→OUT 順序記錄）+ 樹形供電 + PD50 泄放 | 滿載母線電壓量測（無 E131） |
| 4.2 | 7 軸同時 OP + 使能序列（逐軸錯開鬆閘突波） | 14 秒內全臂 ready |
| 4.3 | joint-space 軌跡跟隨（既有 L2） | 跟隨誤差 vs 500 Hz CANopen 基線報告 |
| 4.4 | task-space（既有 L3 FK/IK/DLS）1 kHz 運算預算量測 | cyclic 總耗時 < 500 µs |
| 4.5 | 掉軸演練：運轉中斷開 J5 電源 | WKC 缺 1 → 50 ms 內全臂 safe stop |

### WP-L5 — 雙臂 14 軸

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 5.1 | 14 軸單鏈 + 雙臂供電（45.4 A 額定預算、分區保險） | 電源審查文件 |
| 5.2 | 全域 STO 急停鏈（雙通道、常閉串聯） | 任一急停 → 全 14 軸 E301 |
| 5.3 | L4 雙臂協同（既有 `dual_arm_ctrl`）@1 kHz | 雙臂鏡像/協同 demo |
| 5.4 | 長時間 soak：24 h 正弦運動 | WKC 錯誤 = 0、無熱降載、cyclictest 同步錄製 |

### WP-L6 — 上位機與視覺化整合

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 6.1 | `ws_server` 改吃 telemetry_ring（50–100 Hz 降採） | 3D 視覺化跟隨真實 14 軸 |
| 6.2 | `host_if` 命令信箱（點動/模式/estop）接 cyclic | 命令延遲 < 2 ms |
| 6.3 | 資料記錄器：1 kHz 全軸 pos/vel/tq → 二進位 log + 離線分析腳本 | 整定用資料集 |
| 6.4 | （選配）ROS 2 橋接評估 | 評估報告，不阻塞 |

### WP-L7 — 韌性與運維

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 7.1 | 開機自檢：RT 內核/隔離核/NIC/優先權不符 → 拒絕進 OP | 自檢清單 |
| 7.2 | 內核升級回歸程序（realtime kernel apt 更新後重跑 L0.5/L2.3） | runbook 文件 |
| 7.3 | 故障記錄：`0x603F`/`0x2672`/AL status 全量落盤 | 事後可追溯 |
| 7.4 | 力控前置（PHU-F）：CST/CSF 切換序列（使能前 `0x6071=0`）spike | 單軸 CST 演示 |

### 里程碑

1. **ML1 = L0+L1+L2**：單軸 1 kHz DC 同步達標（**拍板點**：抖動不過 → 切 IgH）
2. **ML2 = L3**：程式碼整合、免硬體測試鏈完備
3. **ML3 = L4**：單臂 7 軸 1 kHz
4. **ML4 = L5**：雙臂 14 軸 + 24 h soak
5. **ML5 = L6+L7**：全棧整合 + 運維化

## 7. 風險與對策

| 風險 | 等級 | 對策 |
| --- | --- | --- |
| BIOS SMI 不可關 → 毫秒級停頓 | 高 | L0.5 用 `hwlatdetect` 先驗機；不過直接換硬體（拍板點提前到採購期） |
| raw socket（SOEM）路徑抖動過大 | 中 | WP-L2.5 IgH 備援；`ec_master.h` 隔離使替換只動一個 .c |
| 內核/驅動升級破壞 RT 特性 | 中 | 鎖定內核版本 + WP-L7.2 升級回歸 runbook |
| GPLv2（SOEM/IgH module） | 研發低/產品化高 | 同前置文件：產品化前換商業授權（Acontis）或重談 |
| 14 軸鬆閘同時湧浪電流 | 中 | 使能序列逐軸錯開（L4.2）；電源餘裕 ≥1.5× |
| 供電菊鏈壓降 → E131 欠壓 | 中 | 樹形/直連供電 + 滿載電壓實測（L4.1） |
| PC 單點故障（無 MCU 韌體層兜底） | 中 | 硬體 STO 鏈獨立於 PC；從站側 following-error/逾時自保護參數（`0x6065/0x6066`）必設 |
| 週期/插補參數不符（E404） | 低 | 前置文件 §5.3 核對清單納入 L1.3 |

## 8. 驗證方式總表

| 層 | 工具 / 方法 | 標準 |
| --- | --- | --- |
| OS RT | cyclictest 12 h、hwlatdetect 2 h | max < 50 µs；SMI ≈ 0 |
| 匯流排 | WKC 計數、Wireshark EtherCAT dissector、示波器量 SYNC0 | 24 h WKC 錯誤 = 0；SYNC0 對齊 < 1 µs |
| 主站迴圈 | 內建 cycle 直方圖 + ftrace（wakeup latency） | max jitter < 100 µs @1 kHz |
| 控制 | 正弦跟隨誤差 vs 500 Hz CANopen 基線 | 誤差下降且無新振盪 |
| 安全 | 拔線/STO/掉軸/急停注入 | 50 ms 內 safe stop，全部有故障記錄 |
| 回歸 | HOST build ctest（fake ec_master 後端） | 免硬體全綠 |

## 9. 關聯文件

- **協定面設計（沿用）**：`ethercat-coe-master-plan.md` §4 硬體接線、§5 PDO/頻寬、§6.2 抽象層、§7 協定流程與排錯表
- 選型比較：`canopen-vs-ethercat.md`
- 總體計畫：`dual-arm-control-plan.md`（WP0.2 拍板變更：主站 = Linux RT PC；STM32 板端主站暫緩）
- 變更紀錄：`../changes/2026-07-04-linux-rt-ethercat-master-plan.md`
