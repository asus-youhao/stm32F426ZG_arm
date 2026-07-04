# Ubuntu 24.04 Pro + PREEMPT_RT + IgH EtherCAT Master 深度規劃 — 雙臂 14 軸 1 kHz

- 文件層級：架構 / 實作規劃（與 SOEM 版平行的替代方案）
- 狀態：草案（draft）
- 分支：`feature/linux-igh-ethercat-master`
- 前置文件：
  - `ethercat-coe-master-plan.md`——**協定面設計沿用**（EYOU PDO 映射、DC「500 µs 整數倍」規則、CiA402 使能/抱閘時序、E40x 排錯表、接線/供電/STO）
  - `linux-rt-ethercat-master-plan.md`——**RT 調校方法論沿用**（CPU 隔離、IRQ 綁定、優先權配置、cyclictest/hwlatdetect 驗收、無鎖 RT 路徑守則）
- 本文件差異點：OS 換 **Ubuntu 24.04 LTS**（realtime-kernel 6.8-rt）、主站堆疊換 **IgH EtherCAT Master（EtherLab, kernel-space）**，並評估與 **ROS 2 Jazzy** 生態的銜接

---

## 1. 定位：與 SOEM 版的關係

三條規劃分支的分工：

| 分支 | 主站 | 堆疊 | 定位 |
| --- | --- | --- | --- |
| `feature/ethercat-coe-master` | STM32F746ZG | SOEM（bare-metal） | 板端主站（暫緩） |
| `feature/linux-rt-ethercat-master` | Ubuntu 22.04 Pro RT | SOEM（user-space raw socket） | PC 主站方案 A |
| **本分支** | **Ubuntu 24.04 Pro RT** | **IgH（kernel module）** | **PC 主站方案 B** |

兩條 PC 方案共用 `ec_master.h` 抽象門面（前置文件 §6.2）：L1–L4 控制堆疊、`dual_arm.c`、safety、上位機全部相同，**只有門面後端的 `.c` 不同**。方案 A/B 都做到 ML1（單軸 1 kHz DC 同步）後對比抖動數據再定案，或直接以本方案為主——選 IgH 的理由見 §2。

## 2. 為什麼選 IgH（相對 SOEM）

| 面向 | IgH 優勢 | 代價 |
| --- | --- | --- |
| 資料路徑 | **kernel module + 原生 EtherCAT 網卡驅動**（`ec_igb` 等）：幀收發不經一般網路堆疊、不經 user-space raw socket，抖動天花板更低 | 需維護內核模組（DKMS） |
| DC 支援 | 成熟：`ecrt_master_sync_reference_clock` / `sync_slave_clocks` API 內建參考時鐘同步 | — |
| 工具鏈 | **`ethercat` CLI**：`slaves`/`upload`/`download`/`cstruct`/`states`/`foe_write`——SDO 調試、PDO 結構自動生成、**FoE 韌體升級**（EYOU v1.23 支援）全部免寫程式 | — |
| 生態 | LinuxCNC、**ROS 2 `ethercat_driver_ros2`（基於 IgH）**；Ubuntu 24.04 = ROS 2 Jazzy LTS 原生平台，日後上 ROS 2 幾乎零阻力 | — |
| 多程序 | 主站常駐（systemd `ethercat.service`），多個應用可各自 request master | — |
| 授權 | kernel module GPLv2 + **使用者庫 libethercat LGPL**——應用程式連結 LGPL 庫，**比 SOEM(GPLv2) 對閉源應用更友善** | module 本身仍 GPL（不影響應用） |
| 風險 | 內核 6.8 支援需用 GitLab `stable-1.6` 近期版；**原生驅動對新 NIC（igc/i225）覆蓋不全** | 見 §7 風險表 |

一句話：SOEM 勝在可攜（能回 STM32）、零安裝；**IgH 勝在 Linux 上的即時性上限、工具鏈與 ROS 2 生態**。既然 STM32 已暫緩、主站確定落在 Linux PC，IgH 是更「工業正規軍」的選擇。

## 3. 平台：Ubuntu 24.04 Pro + realtime-kernel

```bash
sudo pro attach <token>
sudo pro enable realtime-kernel        # 6.8-rt（PREEMPT_RT）
sudo reboot && uname -a                # 應含 PREEMPT_RT
```

相對 22.04（5.15-rt）的差異與注意點：

- 6.8-rt 的 PREEMPT_RT patch 已幾乎全數進主線，品質與維護性更好；RT 調校參數（isolcpus/nohz_full/rcu_nocbs/irqaffinity/優先權表）**與 SOEM 版 §3.2 完全相同**，`rt_setup.sh` 直接共用。
- **IgH 模組必須能對 6.8 內核編譯**：官方 release 落後內核，需取 GitLab `gitlab.com/etherlab.org/ethercat` 的 `stable-1.6` 分支（持續跟進新內核）。這是本方案第一個技術驗證點（WP-I0.4）。
- 24.04 = ROS 2 **Jazzy** LTS 目標平台（選配整合，WP-I6.4）。
- 驗收基線同 SOEM 版：`cyclictest` 12 h max < 50 µs、`hwlatdetect` 2 h ≈ 0，未達標換硬體。

### NIC 與原生驅動對應（採購前先查對）

| NIC | 內核驅動 | IgH 原生驅動 | 建議 |
| --- | --- | --- | --- |
| **Intel i210/i211** | igb | **`ec_igb` 有** | ✅ 首選（原生驅動成熟） |
| Intel 82574 等 | e1000e | `ec_e1000e` 有 | ✅ 可用 |
| Intel i225/i226 | igc | 上游**未必有**（依版本） | 若無原生驅動 → generic 模式先行 |
| Realtek r8169 | r8169 | 有（品質一般） | 避免 |

**generic driver 退路**：IgH 可用任何內核網卡驅動跑 generic 模式（幀走標準 net 層），抖動較差但功能完整——原生驅動缺席時的過渡方案，非終態。

## 4. IgH 主站架構

### 4.1 部署形態

```
┌─ user space ─────────────────────────────────────────────┐
│ rt_master（我們的應用, SCHED_FIFO 80, CPU3）               │
│   L4-L2 控制 │ safety │ ec_master.h 門面 → libethercat(LGPL)│
│ ws_server / host_if / logger（CFS, CPU0-1）                │
├─ kernel space ───────────────────────────────────────────┤
│ ec_master.ko（EtherCAT 主站, /dev/EtherCAT0）              │
│ ec_igb.ko（原生網卡驅動, 取代 igb 綁定該網卡）              │
└──────────────────────────────────────────────────────────┘
```

- 安裝：DKMS 打包 `ec_master` + `ec_igb`；`/etc/ethercat.conf` 設 `MASTER0_DEVICE=<MAC>`、`DEVICE_MODULES="igb"`；systemd `ethercat.service` 開機常駐；udev rule 開放 `/dev/EtherCAT0` 給控制程式使用者組。
- 該網卡被 `ec_igb` 接管後**從系統網路消失**（不再有 IP），天然隔離。

### 4.2 應用 API 對映（`ec_master.h` 門面 → ecrt_*）

| 門面（不變） | IgH 後端實作 |
| --- | --- |
| `ec_master_init()` | `ecrt_request_master(0)` → `ecrt_master_create_domain` → 逐軸 `ecrt_master_slave_config(alias=0, position=0..13, VID/PC)` → `ecrt_slave_config_pdos(EC_END, syncs)`（PDO 映射用 `ethercat cstruct` 從實機自動生成 `ec_sync_info_t[]`，對照前置文件 §5.1 精簡佈局）→ startup SDO：`ecrt_slave_config_sdo8(0x6060,0,8)`、`0x60C2` 等 → `ecrt_slave_config_dc(assign_activate=0x0300, SYNC0=1ms, shift)` → `ecrt_activate` → 取 domain 資料指標 |
| `ec_master_exchange()` | `ecrt_master_receive` + `ecrt_domain_process` →（讀輸入/寫輸出於 domain image）→ `ecrt_domain_queue` + `ecrt_master_application_time(now)` + `ecrt_master_sync_reference_clock` + `ecrt_master_sync_slave_clocks` + `ecrt_master_send` |
| `ec_axis_set_output/get_input` | domain image 內 offset 讀寫（offset 由 `ecrt_slave_config_reg_pdo_entry` 註冊取得） |
| `ec_coe_read/write` | 週期外：`ecrt_master_sdo_upload/download`；週期內非同步：`ecrt_sdo_request` |
| `ec_master_health()` | `ecrt_domain_state`（WKC）、`ecrt_master_state`（slaves responding / AL states）、`ecrt_master_link_state` |

### 4.3 DC 同步模型（與 SOEM 版的差異）

SOEM 版：主站是「跟隨者」，PI 鎖相把 `clock_nanosleep` 對齊從站 DC。
IgH 版：**主站是「發號者」**——每週期把 `CLOCK_MONOTONIC`（或 TAI）餵給 `ecrt_master_application_time()`，IgH 負責把參考時鐘（預設第一顆 DC 從站）同步到應用時間、再把其餘從站同步到參考時鐘。主站側只需保證 `clock_nanosleep` 絕對時間喚醒的自身抖動夠小（RT 調校已保證）。

- SYNC0 = 1 ms（500 µs 整數倍規則不變，違反報 E404）、`assign_activate=0x0300`（SYNC0 致能，實值以 ESI/`ethercat cstruct` 輸出為準）、SYNC0 shift 設在 exchange 之後（如 +250 µs）。
- 進階（6.8-rt 已支援）：改餵 `CLOCK_TAI` + i210 硬體時戳，把 DC 對齊到系統 PTP 時基——多主站/感測器融合才需要，第一版不做。

### 4.4 每-tick 流程

與 SOEM 版 §5.2 相同的一拍延遲模型，僅 exchange 內部換成 §4.2 的 ecrt 序列。1 kHz、RT 路徑無鎖無配置、遙測走 lock-free ring——全部沿用。

## 5. EYOU 從站專屬對接（IgH 工具鏈加成）

- **PDO 重映射**：接上實機後 `ethercat cstruct -p 0` 直接吐出現行 SM/PDO C 結構；把它改成前置文件 §5.1 的精簡佈局（RxPDO 12 B / TxPDO 15 B，每 PDO ≤6 entries 防 E405），差異用 startup SDO 或 `ecrt_slave_config_pdos` 下發。
- **SDO 調試**：`ethercat upload/download -p <n> 0x2100 0`（控制權）、`0x2025`（實機解析度確認 19-bit=524288）、`0x26A2/A3`（減速比）——免寫任何程式即可完成 WP-L3.6 的單位校正實測。
- **FoE 韌體升級**：`ethercat foe_write -p <n> firmware.bin`（先 `ethercat states -p <n> BOOT`，密碼 0x87654321 流程見用戶手冊 §5.8）——14 顆關節批次升級腳本化。
- **健康監控**：`ethercat slaves`（AL state + 位置）、`0x2670`/`0x2672`（映 TxPDO 或週期外輪詢）、`ethercat reg_read` 讀 ESC 錯誤計數器（0x0300 CRC error counters，可定位是哪一段線材劣化——SOEM 沒有現成 CLI）。

## 6. 深度工作分解（WP-I）

> 與 SOEM 版 WP-L 平行；相同項直接引用，不重列。每項含 DoD。

### WP-I0 — 環境建置

| # | 工作項 | 驗收（DoD） |
| --- | --- | --- |
| 0.1 | 主機/BIOS/NIC 採購整備（同 WP-L0.1，**NIC 指定 i210** 保證 `ec_igb` 原生驅動） | `hwlatdetect` 2 h ≈ 0 |
| 0.2 | Ubuntu 24.04 + `pro enable realtime-kernel`（6.8-rt） | `uname` 含 PREEMPT_RT |
| 0.3 | `rt_setup.sh` 套用（沿用 SOEM 版 §3.2） | `cyclictest` 12 h max < 50 µs |
| 0.4 | **IgH stable-1.6 對 6.8-rt 編譯**（GitLab 最新 + DKMS 打包） | `ec_master.ko`/`ec_igb.ko` 載入成功；此為本方案首個技術關卡 |
| 0.5 | `/etc/ethercat.conf` + systemd + udev 權限 | 開機後 `ethercat master` 顯示 link up |
| 0.6 | 關節/ESI/轉接線/電源/STO（同 WP-L0.6-0.7） | `ethercat slaves` 看到 1 顆 PHU |

### WP-I1 — 單軸打通（善用 CLI，先零程式碼驗證）

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 1.1 | CLI 勘察：`ethercat slaves/pdos/cstruct/sdos`，讀 `0x1018`/`0x1008`/`0x2025`/`0x26A2` | 實機 OD 快照存檔（vs 手冊差異表） |
| 1.2 | CLI 手動 CiA402：`ethercat download` 依序 0x6060=8、0x6040=6/7/15（enforce 上電>5 s、鬆閘 500 ms） | 無程式碼下軸使能成功 |
| 1.3 | `rt_master` 應用骨架：request master→PDO 精簡映射→startup SDO→activate→OP | `ethercat slaves` 全 OP、WKC 正確 |
| 1.4 | CSP 點動（使能前 `0x607A`←`0x6064`）正弦 ±5° | 平滑跟隨、無 E404/E405 |
| 1.5 | 故障注入（拔線 E403 / STO E301 / fault reset） | 排錯表逐項覆核；`ethercat reg_read` CRC 計數器基線存檔 |

### WP-I2 — DC 同步與抖動驗收（拍板點）

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 2.1 | `ecrt_slave_config_dc`（SYNC0=1 ms + shift）+ application_time/sync 週期呼叫 | 從站進 DC-Synchron（LED/`0x2670`） |
| 2.2 | 抖動量測：cycle 直方圖 + 示波器 SYNC0 + `ecrt_master_state` | max jitter < 100 µs @1 kHz；SYNC0 對齊 < 1 µs |
| 2.3 | 壓力共存（同機編譯/IO 負載） | RT 指標不劣化 |
| 2.4 | **與 SOEM 版（22.04 分支 ML1）抖動數據對比** | 對比報告 → 兩方案定案依據 |

### WP-I3 — 程式碼整合

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 3.1 | `ec_master.h` 門面的 IgH 後端 `ec_master_igh.c`（§4.2 對映表落地） | 門面 API 不改動即通過 |
| 3.2 | `dual_arm.c` 三後端（canopen / ecat-soem / **ecat-igh**）build flag | 三種 build 皆過 |
| 3.3 | `cia402.c` SDO 三函式接 `ecrt_master_sdo_*` | 單元測試 |
| 3.4 | `control_rate.h` 1 kHz + fake 後端 ctest（沿用 WP-L3.4/3.5） | HOST build 全綠 |
| 3.5 | `robot_config` 單位校正（用 I1.1 實測值） | 換算表更新 + 文件 |

### WP-I4 / I5 — 單臂 7 軸 / 雙臂 14 軸

佈線、供電（樹形 + PD50）、逐軸錯開鬆閘、掉軸演練、24 h soak——**內容與驗收完全同 WP-L4/L5**，另加：

| # | IgH 特有項 | 驗收 |
| --- | --- | --- |
| 4.x | 14 軸 `ec_sync_info` 產生器（腳本從 `cstruct` 輸出批量生成） | 一鍵重生成 |
| 5.x | 線材品質巡檢：soak 前後 `ethercat reg_read` CRC 計數器比對 | 計數器零增長 |

### WP-I6 — 上位機 / 生態整合

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 6.1 | ws_server/3D 視覺化接 telemetry_ring（同 WP-L6.1） | 3D 跟隨 14 軸 |
| 6.2 | host_if 命令信箱（同 WP-L6.2） | 命令延遲 < 2 ms |
| 6.3 | FoE 批次升級腳本（`states BOOT` → `foe_write` ×14 → 驗證 `0x2084` 版本） | 一鍵升級 runbook |
| 6.4 | （選配）ROS 2 Jazzy + `ethercat_driver_ros2` 評估：本專案自研控制層 vs ros2_control 生態 | 評估報告 |

### WP-I7 — 運維韌性

同 WP-L7（開機自檢、故障落盤），另加 IgH 特有：

| # | 工作項 | 驗收 |
| --- | --- | --- |
| 7.1 | **DKMS 內核升級回歸**：6.8-rt 更新 → 模組自動重編 → RT/DC 驗收重跑 | runbook + 鎖版策略（apt hold） |
| 7.2 | `ethercat.service` 故障恢復：主站重啟不重啟從站電源的恢復序列 | 熱恢復 < 10 s 回 OP |

### 里程碑

1. **MI1 = I0+I1+I2**：單軸 1 kHz DC 同步 + 與 SOEM 版對比報告（**兩方案定案點**）
2. **MI2 = I3**：三後端整合、免硬體 CI
3. **MI3 = I4**：單臂 7 軸
4. **MI4 = I5**：雙臂 14 軸 + 24 h soak
5. **MI5 = I6+I7**：全棧 + 運維化

## 7. 風險與對策

| 風險 | 等級 | 對策 |
| --- | --- | --- |
| **IgH 對 6.8-rt 編譯失敗/不穩**（官方 release 落後內核） | 高 | WP-I0.4 放最前面驗證；退路①用 GitLab master 分支、②退 generic driver、③退 22.04（5.15-rt，IgH 支援成熟）、④退 SOEM 方案 A |
| 選用 NIC 無原生驅動（如 igc） | 中 | 採購鎖定 i210（`ec_igb`）；generic 模式僅過渡 |
| DKMS 重編失敗導致升級後主站消失 | 中 | apt hold 鎖內核版本；WP-I7.1 升級 runbook；ethercat.service 啟動失敗告警 |
| kernel module GPL 與產品化 | 低 | 應用只連 LGPL 使用者庫；module 隨機器出貨屬 GPL 正常合規範圍（諮詢法務確認） |
| 其餘（BIOS SMI、鬆閘湧浪、供電壓降、PC 單點故障靠 STO 兜底） | — | 同 SOEM 版 §7，全部沿用 |

## 8. 驗證方式

同 SOEM 版 §8 總表（cyclictest/hwlatdetect、WKC、示波器 SYNC0、跟隨誤差 vs 基線、安全注入、免硬體 ctest），另加：

- `ethercat reg_read` ESC CRC 錯誤計數器：soak 前後零增長（線材/EMC 健康指標）
- DKMS 升級演練後全套 RT + DC 驗收重跑
- MI1 產出「IgH(24.04) vs SOEM(22.04)」抖動對比報告——兩條 PC 方案的最終定案依據

## 9. 關聯文件

- 協定面（沿用）：`ethercat-coe-master-plan.md`
- RT 調校與 SOEM 方案 A：`linux-rt-ethercat-master-plan.md`
- 選型比較：`canopen-vs-ethercat.md`；總體計畫：`dual-arm-control-plan.md`
- 變更紀錄：`../changes/2026-07-04-linux-igh-ethercat-master-plan.md`
