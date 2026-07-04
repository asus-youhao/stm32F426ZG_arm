# Harness / Agent / Loop Engine 架構強化深度規劃

- 文件層級：架構 / 實作規劃
- 狀態：草案（draft）
- 分支：`feature/harness-loop-engine`（自 `feature/linux-canopen-master` 切出，沿用方案 C 文件脈絡）
- 適用平台：**PC（Ubuntu 24.04 Pro + PREEMPT_RT）為主控**，STM32F746ZG 為同源韌體移植目標
- 適用協定：**CANopen（方案 C，500 Hz）與 EtherCAT（方案 A/B，1 kHz）共用同一套架構**
- 沿用文件：
  - `linux-canopen-master-plan.md` —— 方案 C 真機路徑（PCAN 硬體、SYNC、佈建 SOP、G1–G6 差距）
  - `linux-igh-ethercat-master-plan.md` / `linux-rt-ethercat-master-plan.md` —— EtherCAT 主站與 RT 調校方法論
  - `wp6-safety-wp7-host.md` —— 安全狀態機與上位機協定（本文重構其「掛載方式」，不改其語意）

---

## 0. 名詞定義：harness / agent / loop engine 在控制系統的對映

這三個詞借自 agent 框架（如 Claude Code 的 harness–agent 關係、ros2_control 的
controller_manager–controller 關係），映射到即時運動控制如下：

| 概念 | 在本專案的意義 | 類比 |
| --- | --- | --- |
| **Loop Engine** | RT 週期執行器：唯一的時間源、相位排程（RX→算→TX）、每模組 WCET 預算、超時（overrun）政策、抖動觀測 | ros2_control 的 realtime executor、LinuxCNC 的 motion task |
| **Agent** | 掛進 loop engine 的**自治週期單元**：統一介面（configure/activate/read/compute/write/fault）、自帶狀態機與看門狗、故障自我隔離 | ros2_control 的 controller + hardware_interface、一顆 CiA402 軸就是一個 agent |
| **Harness** | **非 RT 監督層**：載入設定、編排所有 agent 的生命週期、監控健康、對外命令/遙測、降級與重啟策略 | controller_manager、systemd 之於 service |

一句話：**harness 管「誰在跑、出事怎麼辦」；loop engine 管「什麼時候跑、跑多久」；
agent 管「跑什麼」。** 三者解耦之後，同一個 AxisAgent 才能不改一行地跑在
CANopen@500 Hz 或 EtherCAT@1 kHz 上，也才能同一套程式碼在 PC 與 F746 編譯。

## 1. 現況盤點：目前的 loop engine 是什麼、痛在哪

現況的「loop engine」= `firmware/pc/pc_master_main.c:180` 的 while 迴圈
（`clock_nanosleep(TIMER_ABSTIME)` 絕對時間 + 遲到統計），每 tick 呼叫
`app_main_tick()`（`firmware/app/app_main.c:50`）這條**單體管線**：
收回授 → 安全 → L4→L3→L2 → L1 PDO 下發。STM32 版則由 TIM6 ISR 呼叫同一函式。

時間源的選擇（TIMER_ABSTIME 絕對時間）是對的，但架構上有七個具體痛點：

| # | 痛點 | 位置 | 後果 |
| --- | --- | --- | --- |
| P1 | **單體管線**：safety / motion / bus 在 `app_main_tick()` 裡寫死呼叫順序 | `app_main.c:50-83` | 換 EtherCAT 要動刀整條管線；無法單獨測試/替換任一段 |
| P2 | **無相位分離**：RX 何時收、TX 何時發不受控，跟著程式碼順序漂 | 同上 | 軸間鎖存 skew 不可控（方案 C 的 G3）；EtherCAT DC 需要固定的 receive→send 相位才有意義 |
| P3 | **無 per-module 預算**：只量整圈 `late_max_us` | `pc_master_main.c:187-190` | 超時了不知道是 IK 慢還是 CAN 寫入塞住，無從優化 |
| P4 | **overrun 政策 naive**：`next += dt` 無條件累加 | `pc_master_main.c:181-183` | 一次大遲到（如 SDO 阻塞）後 ABSTIME 已過期會立即返回 → **連環補跑 burst**，對匯流排瞬間灌幀 |
| P5 | **故障處理全域化**：一軸失聯 → `safety_update` → 全域 safe stop | `app_main.c:69-70` | 沒有 per-axis 降級（該軸 hold、其餘 13 軸續跑）的選項 |
| P6 | **RT 迴圈內做非 RT 的事**：`printf` 狀態、`select()` 輪詢 stdin 都在 tick 路徑上 | `pc_master_main.c:195-200` | stdout 阻塞（terminal 慢、管線滿）直接吃掉控制週期 |
| P7 | **頻率是編譯期常數** `CONTROL_HZ=500` | `control_rate.h:13` | CANopen 500 Hz / EtherCAT 1 kHz 要維護兩套編譯組態 |

這些痛點正是使用者問「如何強化 loop engine」的答案框架：**P1–P2 用 agent 相位化解、
P3–P4 用預算與 overrun 政策解、P5 用 agent 自治故障解、P6 用 harness/RT 分域解、
P7 用執行期設定解。**

## 2. 目標架構總覽

```c
/* PC（Ubuntu 24.04 Pro + PREEMPT_RT）行程內的兩個域 */

RT 域（隔離核心、SCHED_FIFO 80、mlockall）
  loop_engine（1 kHz 或 500 Hz base tick，執行期設定）
    ├ phase BUS_RX      : BusAgent.rx_begin()      收幀/收 frame
    ├ phase LATCH       : BusAgent.latch()          鎖存回授到 jstate
    ├ phase COMPUTE     : SafetyAgent → MotionAgent → AxisAgent×14
    ├ phase BUS_TX      : BusAgent.commit_tx()      SYNC/domain queue+send
    └ phase HOUSEKEEP   : TelemetryAgent、HealthAgent、SDO 背景步進

非 RT 域（一般核心、SCHED_OTHER）
  harness
    ├ 設定載入（bus 型別、軸表、頻率、PDO 映射）
    ├ 生命週期編排（BOOT→CONFIGURE→BUS_UP→ACTIVATE→RUN⇄DEGRADED→SAFE_STOP）
    ├ 監督（agent heartbeat、deadline-miss 升級、重啟策略）
    └ 對外服務（ws_server 遙測、CLI 命令、log drain、SDO 佇列）

兩域交界：lock-free SPSC ring ×3（cmd、telemetry、log），零鎖、零 malloc
```

> 上圖為程式碼註解式示意；正式對外簡報請依 HTML 產出規範改畫 SVG。

關鍵設計決策：

1. **RT 域內零系統呼叫**（除 `clock_nanosleep` 與 bus 讀寫）：無 printf、無 malloc、
   無 mutex；跨域一律走 SPSC ring。
2. **engine 核心 platform-free**：`loop_engine.c` 不 include 任何 POSIX/HAL 標頭，
   時間與臨界區走 port 層（`port_now_us()`、`port_critical_enter/exit()`）——
   這是讓同一份 engine 在 F746（TIM6 ISR 驅動）與 PC（clock_nanosleep 驅動）
   共用的前提，延續本 repo「同套韌體兩平台」的既有做法。
3. **bus 後端是 agent，不是 engine 的一部分**：CANopen（SocketCAN/PCAN）與
   EtherCAT（IgH ecrt / SOEM）都實作同一個 `bus_if_t`，engine 只認相位。

## 3. Loop Engine 強化設計（核心）

### 3.1 時間源與 overrun 政策（解 P4）

維持 `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` 絕對時間排程，但把
「遲到」分級處理：

| 情況 | 判定 | 動作 |
| --- | --- | --- |
| 正常 | `late < late_warn`（預設 10% 週期） | 記 histogram |
| 輕度遲到 | `late_warn ≤ late < dt` | 記 deadline-miss 計數；本 tick 照跑 |
| **超週期（overrun）** | `late ≥ dt` | **政策 SKIP**：跳過錯過的 tick，`next` 重新錨定為 `now + dt`（**不補跑**，避免對匯流排 burst 灌幀）；miss 計數 += 錯過數 |
| 連續 overrun | 連續 N 次（預設 3） | 通知 harness → 升級（DEGRADED 或 SAFE_STOP，見 §5.2） |

> 為何 SKIP 不 CATCHUP：運動控制下發的是「此刻的設定點」，補發過期設定點對
> 從站是錯誤資訊；CANopen 側連環補發更會瞬間超出 90% 匯流排負載預算。
> 這直接修掉現況 P4 的 burst 行為。

### 3.2 相位化週期（解 P1、P2）

每個 base tick 固定走五個相位，agent 把自己的工作掛到對應相位：

| 相位 | 內容 | CANopen 對映 | EtherCAT（IgH）對映 |
| --- | --- | --- | --- |
| `BUS_RX` | 把匯流排收到的原始資料搬進來 | 抽乾 SocketCAN rx queue | `ecrt_master_receive()` + `ecrt_domain_process()` |
| `LATCH` | 鎖存成一致的回授快照 `jstate[14]` | TPDO 解析（上一 SYNC 週期的回授） | domain 資料 → jstate |
| `COMPUTE` | 純計算：safety → motion(L4→L2) → 14 軸 CiA402 步進 | 相同（bus 無關） | 相同（bus 無關） |
| `BUS_TX` | 提交輸出 | **先發 SYNC**（0x80），再發 14 幀 RPDO | jstate targets → domain、`ecrt_domain_queue()` + `ecrt_master_send()` |
| `HOUSEKEEP` | 遙測入 ring、health 統計、SDO 背景通道步進一小步 | SDO client 狀態機步進 | CoE mailbox 步進 |

相位化帶來兩個直接收益：

- **軸間 skew 受控**（方案 C G3）：SYNC 永遠在 `BUS_TX` 相位開頭發出，從站
  在 SYNC 鎖存 → 14 軸取樣同一時刻；EtherCAT 側則對齊 DC sync0。
- **compute 是 bus-agnostic 的**：COMPUTE 相位裡的 agent 完全看不到匯流排，
  只讀寫 `jstate` / `target`，這就是 CANopen↔EtherCAT 可切換的機制。

### 3.3 每 agent WCET 預算與量測（解 P3）

engine 在呼叫每個 agent 的每個相位 callback 前後取 `port_now_us()`：

- 每 agent × 每相位維護 duration histogram（固定桶：≤10/25/50/100/250/500/1000 µs/更多）
  與 max；存在 RT 域的靜態陣列，HOUSEKEEP 相位打包進 telemetry ring。
- agent 註冊時宣告 `budget_us`；單次超預算記 warning 計數，連續超預算 N 次
  → engine 呼叫該 agent 的 `on_fault(FAULT_BUDGET)`，由 agent 自行降級
  （例：MotionAgent 的 IK 迭代數減半），而不是拖垮整圈。
- 整圈量測維持現有 `late_max_us`，另加每相位小計 —— 之後看報表就知道
  「超時是 IK 吃的還是 PCAN 寫入吃的」。

### 3.4 多速率排程（解 P7 的一半）

base tick 頻率為執行期參數（`--rate 500|1000`），agent 註冊時帶
`divisor`（每幾個 base tick 跑一次）與 `phase_offset`（錯峰）：

| Agent | divisor（@1 kHz base） | 錯峰用意 |
| --- | --- | --- |
| Bus/Axis/Motion/Safety | 1（每 tick） | —— |
| TelemetryAgent | 10（100 Hz） | 與 HealthAgent 錯開 |
| HealthAgent（busload、error counter、EMCY 彙整） | 100（10 Hz） | phase_offset=5 |
| CommandAgent（消化 cmd ring） | 20（50 Hz，對齊現有 stdin 輪詢率） | phase_offset=3 |

CANopen 模式 base 降為 500 Hz 時 divisor 語意不變，慢速 agent 自動跟著減半，
不需要重新調參。

### 3.5 RT 執行緒與系統調校（沿用方案 A/B 方法論）

RT 調校**原封沿用** `linux-rt-ethercat-master-plan.md` §3（隔離核心
`isolcpus`+`nohz_full`+`rcu_nocbs`、IRQ 綁核、`cyclictest` 驗收），本文只補
loop engine 行程自身的要求：

- RT 執行緒：`SCHED_FIFO` 優先權 80、`pthread_setaffinity` 綁隔離核；
  CAN/NIC IRQ 綁到**相鄰核**（不與 RT 執行緒同核，避免 IRQ 搶佔 tick）。
- `mlockall(MCL_CURRENT|MCL_FUTURE)` + 開機時 stack/heap prefault（觸頁）。
- 開啟 `/dev/cpu_dma_latency=0`（擋 C-state 深睡，PREEMPT_RT 下 idle 喚醒
  延遲是主要抖動源之一）。
- **驗收門檻**：cyclictest（同核同優先權）p99 < 20 µs、max < 50 µs；
  loop engine 自量 late p99 < 5% 週期。未達標先修系統，不動程式。

### 3.6 觀測性（trace）

- RT 域內建 **trace ring**（每 tick 一筆：t_wake、late、五相位 duration、
  miss 旗標），共享記憶體，非 RT 側工具離線 dump 成 CSV/histogram —— 這是
  日常儀表，成本一筆 <64 B 寫入。
- ftrace / LTTng / `osnoise` tracer 只在「抖動異常調查期」開，不常駐。
- 遲到事件（≥ late_warn）額外記完整上下文（哪個相位、哪個 agent 超預算），
  事後可回答「那 300 µs 去哪了」。

## 4. Agent 模型與本專案的 agent 目錄

### 4.1 統一介面（C，零 malloc、靜態表）

```c
typedef enum { AG_FAULT_BUDGET, AG_FAULT_BUS, AG_FAULT_AXIS_LOST,
               AG_FAULT_INTERNAL } agent_fault_t;

typedef struct agent {
    const char *name;
    uint32_t    divisor;      /* 每幾個 base tick 跑一次（1=每 tick） */
    uint32_t    phase_offset; /* 錯峰偏移（0..divisor-1） */
    uint32_t    budget_us;    /* COMPUTE 相位 WCET 預算 */
    void       *ctx;

    /* harness 生命週期（非 RT 呼叫） */
    int  (*on_configure)(void *ctx, const eng_cfg_t *cfg);
    int  (*on_activate)(void *ctx);
    void (*on_deactivate)(void *ctx);

    /* loop engine 相位（RT 呼叫，禁系統呼叫/阻塞） */
    void (*cycle_read)(void *ctx);     /* LATCH 之後、COMPUTE 之前 */
    void (*cycle_compute)(void *ctx);  /* COMPUTE */
    void (*cycle_write)(void *ctx);    /* BUS_TX 之前 */
    void (*housekeep)(void *ctx);      /* HOUSEKEEP */

    /* 故障通知（RT 呼叫；agent 自行降級，不得阻塞） */
    void (*on_fault)(void *ctx, agent_fault_t f);
} agent_t;
```

不需要的 callback 填 NULL；engine 以靜態陣列持有（`agent_t *agents[AGENT_MAX]`），
註冊順序即 COMPUTE 相位內的執行順序（safety 永遠第一、axis 下發永遠最後）。

### 4.2 本專案的 agent 拆分（現有程式碼 → agent 的遷移對照）

| Agent | 數量 | 來源程式碼 | 職責 | 自治故障行為 |
| --- | --- | --- | --- | --- |
| **BusAgent** | 每 bus 1（左/右臂各一，或 EtherCAT 單一） | `co_bxcan_socketcan.c` / 未來 `bus_ecat_igh.c` | rx_begin/latch/commit_tx/SYNC；EMCY 解析（補方案 C G5） | bus-off → 重啟 controller、回報 harness |
| **AxisAgent** | 14 | `cia402.c` + `dual_arm.c` 的 per-joint 部分 | 單軸 CiA402 狀態機、使能交握、fb_fresh 看門狗、單位換算 | 失聯/fault → **該軸 hold + 標記 DEGRADED**，不再直接觸發全域 stop（解 P5） |
| **MotionAgent** | 1 | `joint_space.c`/`task_space.c`/`dual_arm_ctrl.c`（L2–L4） | 回授回灌 → 軌跡/IK → 14 軸 counts 目標 | 超預算 → IK 迭代降半；某軸 DEGRADED → 該軸目標鎖 hold 位置 |
| **SafetyAgent** | 1 | `safety.c`（WP6） | 彙整 axis 健康 → 系統級狀態機（RUNNING/HOLD/ESTOP）；升級決策的 RT 端 | —— 它就是故障的最終仲裁者 |
| **TelemetryAgent** | 1 | 取代 `print_status()` | jstate/pose/引擎統計 → telemetry ring（100 Hz） | ring 滿 → 丟舊留新，計 drop |
| **HealthAgent** | 1 | 新增（補方案 C G6） | busload、error counter、EMCY 計數、miss 統計（10 Hz） | —— |
| **CommandAgent** | 1 | 取代 `poll_stdin`/`handle_cmd` 在迴圈內的部分 | 消化 cmd ring（j/e/p/模式切換），呼叫 Motion/Safety API | 非法命令丟棄記數 |

遷移對照（`app_main_tick()` 四步 → 相位）：

- 第 1 步「收回授+回灌」→ BusAgent.latch + AxisAgent.cycle_read + MotionAgent.cycle_read
- 第 2 步「安全看門狗」→ AxisAgent 內部（per-axis）+ SafetyAgent.cycle_compute（系統級）
- 第 3 步「L4→L2 產生目標」→ MotionAgent.cycle_compute
- 第 4 步「CSP 下發 + PDO」→ AxisAgent.cycle_write + BusAgent.commit_tx

**重構不改任何控制語意**：WP6 安全逾時 50 ms、`require_all_enabled_for_run`、
safe stop 覆寫 controlword 等行為原樣保留，只換掛載方式；H1 階段以 SIL 迴歸
（vcan×2 + 14 假從站）逐幀比對重構前後的 bus 行為作驗收。

## 5. Harness 設計（非 RT 監督層）

### 5.1 生命週期

```c
BOOT → CONFIGURE → BUS_UP → ACTIVATE → RUN ⇄ DEGRADED → SAFE_STOP → SHUTDOWN
/*      │            │         │          │         │
 *      │            │         │          │         └ 任一升級條件觸發
 *      │            │         │          └ 部分軸失效仍續跑（可設定禁止）
 *      │            │         └ 全部 agent on_activate OK → engine 起跑
 *      │            └ NMT reset→PreOp→PDO 映射→Start（CANopen）
 *      │              或 slave scan→PreOp→SafeOp→OP（EtherCAT）
 *      └ 載入設定：bus 型別、介面名、軸表、頻率、預算、升級策略  */
```

- CONFIGURE 讀單一設定檔（建議先用簡單 key=value/INI，不引依賴）：
  `bus=canopen|ethercat`、`rate=500|1000`、`left_if=can0`、軸表（node-id、
  型號、齒比、單位換算——把 `robot_config.c` 的常數逐步外移）。
- BUS_UP 就是現有 `app_main_init()`/`dual_arm_init()` 的內容，move 到 harness
  執行緒做（它有 SDO 往返、可阻塞，本來就不該在 RT 域）。
- **同一 binary、旗標切協定**：`pc_master --bus canopen --rate 500` /
  `--bus ethercat --rate 1000`（解 P7 另一半）。

### 5.2 監督與升級策略（escalation）

harness 每 100 ms 檢查 engine 心跳（engine 每 tick 遞增的 seq，共享記憶體）
與各 agent 的故障計數，依策略表升級：

| 事件 | 第一層反應（RT 域內、agent 自治） | 升級條件 | 第二層（harness/SafetyAgent） |
| --- | --- | --- | --- |
| 單軸失聯 > 50 ms | AxisAgent → 該軸 hold、標 DEGRADED | 同臂 ≥ 2 軸失聯，或該軸為肩關節（J1/J2） | 該臂 safe stop |
| bus error passive / bus-off | BusAgent 重啟 CAN controller | 3 s 內未恢復 | 全系統 SAFE_STOP |
| 連續 deadline miss ≥ 3 | engine SKIP 政策 | 1 s 內 miss 率 > 5% | 降頻運行（1 kHz→500 Hz）或 SAFE_STOP（設定檔決定） |
| agent 連續超預算 | agent on_fault 自降級 | 降級後仍超 | 停用該 agent（僅限非關鍵：telemetry/health） |
| engine 心跳停止（RT 執行緒卡死） | —— | 200 ms 無心跳 | harness 直接對 bus 發 NMT stop / 關 EtherCAT OP，**獨立於 RT 執行緒的最後防線** |

> 注意分工：**毫秒級反應必須在 RT 域內由 agent/SafetyAgent 完成**（harness 的
> 100 ms 巡檢來不及）；harness 只做秒級的策略決策與「RT 執行緒本身死掉」的兜底。

### 5.3 非 RT 服務

- **SDO/CoE 背景通道**：非 RT 側佇列請求（讀參數、改 OD、韌體更新），
  RT 域 HOUSEKEEP 相位每 tick 只步進一小步（一幀/一 mailbox 片段），
  結果回填佇列 —— 取代現在「bring-up 時整段阻塞 SDO」的作法（RUN 中也能安全讀參數）。
- **遙測輸出**：telemetry ring → 既有 `ws_server`（3D 檢視器/幀流面板不用改，
  只是資料來源從旁聽 bus 變成主站自報，更完整）。
- **命令輸入**：stdin/WS 命令 → cmd ring → CommandAgent（50 Hz 消化），
  printf 全部移出 RT 域（解 P6）。
- **log drain**：RT 域只寫 log ring（等級+代碼+參數，無格式化字串），
  harness 端格式化落檔。

## 6. CANopen / EtherCAT 統一 bus 抽象

```c
typedef struct bus_if {
    int  (*bus_up)(void *ctx, const eng_cfg_t *cfg);  /* 非 RT：NMT/OP 交握 */
    void (*rx_begin)(void *ctx);                       /* RT: BUS_RX  */
    void (*latch)(void *ctx, jstate_t *js, int n);     /* RT: LATCH   */
    void (*commit_tx)(void *ctx, const jcmd_t *jc, int n); /* RT: BUS_TX（含 SYNC/DC） */
    void (*housekeep)(void *ctx);                      /* RT: mailbox/SDO 步進 */
    int  (*bg_xfer)(void *ctx, sdo_req_t *req);        /* 非 RT 佇列入口 */
} bus_if_t;
```

| 面向 | `bus_canopen_socketcan`（方案 C） | `bus_ecat_igh`（方案 B）/`bus_ecat_soem`（方案 A） |
| --- | --- | --- |
| base rate | 500 Hz（Classic CAN 物理上限） | 1 kHz（可到 2 kHz） |
| 同步機制 | SYNC（0x80）在 BUS_TX 開頭發、從站 SYNC 鎖存 | DC sync0，`ecrt_master_application_time()` 對時 |
| 回授延遲模型 | 本 tick 收到的是上一 SYNC 週期的 TPDO | receive/process 拿到上一週期 domain |
| 診斷 | EMCY、heartbeat、error counter（HealthAgent 消化） | AL status、working counter、CoE emergency |
| 背景通道 | SDO client 狀態機分片步進 | CoE mailbox（IgH 提供非同步 API） |

AxisAgent / MotionAgent / SafetyAgent **完全 bus-agnostic**：只讀寫
`jstate[]`（回授快照）與 `jcmd[]`（controlword+target），CiA402 物件語意在
CANopen 與 CoE 完全相同 —— 這正是方案 C「協定對照組」價值的架構落地。

## 7. STM32F746 端對映（同源韌體）

engine 核心 platform-free（§2 決策 2），F746 移植只做 port 層：

| Port 項 | PC（PREEMPT_RT） | F746（bare-metal） |
| --- | --- | --- |
| tick 來源 | `clock_nanosleep` while 迴圈（RT 執行緒） | TIM6 ISR 設 flag，主迴圈跑 `engine_tick()`（ISR 內只做 LATCH 時戳，避免 ISR 裡跑 IK） |
| `port_now_us()` | `clock_gettime(MONOTONIC)` | DWT->CYCCNT / TIM 讀數 |
| 臨界區 | 無鎖（單 RT 執行緒 + SPSC） | `__disable_irq/​__enable_irq` 包 SPSC 指標 |
| harness | 獨立執行緒 | 背景 super-loop（UART 命令、health 印出）——縮小版，生命週期同一套狀態機 |
| bus 後端 | SocketCAN / ecrt | 既有 `co_bxcan.c`（bxCAN） |

分工定位維持既有結論：**主路徑是 PC 當主站**（方案 A/B/C），F746 保留為
(a) 低成本備援主站、(b) 未來 IO/安全副控。agent 化的價值在於：SIL（PC+vcan）
驗過的 agent 邏輯，燒進 F746 就是同一份 .c 檔。

## 8. 測試 harness（SIL / HIL）

| 層 | 環境 | 驗什麼 |
| --- | --- | --- |
| 單元測試 | host（延續 `firmware/tests`） | engine：overrun SKIP 政策、rate divider/phase offset、預算超標降級路徑；agent 生命週期狀態機；SPSC ring 邊界 |
| **SIL** | vcan×2 + `sim_py` 14 假從站（既有資產） | 重構迴歸（H1：逐幀比對 bus 行為）；**故障注入 agent**：指定軸丟 TPDO、延遲、發 EMCY、模擬 bus-off → 驗 §5.2 升級策略表每一格 |
| 抖動驗收 | 真機 PC、PREEMPT_RT | cyclictest 基線 → engine trace histogram（每相位 p99/max）→ §3.5 門檻 |
| **HIL** | PCAN + 真關節（WP-C）；EtherCAT NIC + 關節（WP-I/L） | SYNC 鎖存 skew 實測、EMCY 真實故障碼、雙臂 14 軸滿載 busload |

## 9. 工作分解（WP-H）

| WP | 內容 | 驗收標準 |
| --- | --- | --- |
| **H0** engine 核心 | `firmware/engine/loop_engine.[ch]`、`agent.h`、`spsc_ring.[ch]`、port 層（platform-free，零 malloc） | host 單元測試全綠：SKIP 政策、divisor/offset、預算量測、ring |
| **H1** agent 化重構 | `app_main_tick()` 拆成 §4.2 七類 agent，行為不變 | SIL vcan 迴歸：重構前後 candump 逐幀 diff 一致；使能時序/safe stop 語意不變 |
| **H2** harness | 生命週期狀態機、設定檔、cmd/telemetry/log ring、printf/stdin 全面移出 RT 路徑 | RUN 中 stdout 被塞住（`pv -L 1` 掐管線）tick 不受影響；`--bus/--rate` 執行期切換 |
| **H3** RT 化 + 觀測 | SCHED_FIFO/綁核/mlockall/prefault、trace ring + dump 工具 | 24.04 Pro PREEMPT_RT 上：late p99 < 5% 週期、cyclictest 達 §3.5 門檻；產出每相位 histogram 報告 |
| **H4** CANopen 後端收斂 | `bus_if_t` 抽象、SYNC 相位化、EMCY（G5）、HealthAgent busload（G6）、SDO 背景通道 | SIL 故障注入全表通過；與 WP-C 真機計畫銜接（PCAN 上重跑 H3 報告） |
| **H5** EtherCAT 後端 | `bus_ecat_igh.c` 實作同一 `bus_if_t`，DC 對齊 BUS_TX 相位 | 同一 binary `--bus ethercat --rate 1000` 起跑；AxisAgent/MotionAgent 零修改 |
| **H6** F746 port | port 層 + TIM6 tick 源 + 縮小版 harness | 既有 bring-up 測試在 agent 化韌體上重跑通過 |

依賴：H0→H1→H2→(H3 ∥ H4)→H5；H6 在 H1 後即可並行。H1–H4 全程可在
SIL 完成，**不阻塞方案 C 真機時程**（WP-C0 佈建/硬體採購可並行）。

## 10. 風險與對策

| 風險 | 對策 |
| --- | --- |
| 重構引入行為回歸（安全語意最怕） | H1 以 candump 逐幀 diff 為硬驗收；safety 相關單元測試先補齊再動刀 |
| 過度工程（14 軸雙臂真的需要 agent 框架嗎） | 邊界收緊：不做動態載入、不做執行緒池、agent 表編譯期靜態；框架程式碼目標 < 1 kLOC |
| per-axis 降級（P5 的解）反而引入新危險模式（單臂 6 軸跑 1 軸 hold 的動力學） | 預設策略保守：肩關節（J1/J2）失聯即整臂 stop；per-axis 續跑僅腕關節且可設定關閉 |
| trace/遙測本身吃 RT 預算 | trace 一筆 < 64 B、無格式化；HOUSEKEEP 相位有自己的預算，超了先砍遙測頻率 |
| F746 port 與 PC 版漂移 | engine 核心單一 .c、CI（host 單元測試）同時以 arm-gcc 編譯把關 |

## 11. 與現有規劃的關係

- 本文是**橫切架構層**：方案 A/B/C 是「跑在哪、走什麼線」，本文是「程式怎麼組織」。
  H4 落地方案 C 的 G3/G5/G6，H5 落地方案 A/B 的主站程式框架。
- `control_rate.h` 的 500 Hz 物理分析仍然成立，只是常數改為設定檔預設值。
- WP6 安全語意不變；WP7 上位機（ws_server）介面不變，資料來源升級為主站自報遙測。
