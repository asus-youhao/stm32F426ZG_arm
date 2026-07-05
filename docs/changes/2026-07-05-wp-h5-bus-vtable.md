# WP-H5：bus_if_t vtable 雙後端 + `--bus ethercat` + eng_phase_trim_us

- 日期：2026-07-05
- 分支：`feature/ecat-backend-sim`
- 類型：feat + refactor + test

## 變更摘要

1. **`bus_if_t` vtable 定案**（`firmware/engine/bus_if.h`）：
   `init / present_count / pump_rx / set_target / tick / set_safe_stop /
   tx_drops / take_fault / health(bus_idx, window_us)`。資料面共用
   `g_jstate[]`；COMPUTE 相位（safety/motion）完全 bus-agnostic。
2. **兩個後端**：
   - `firmware/app/bus_canopen.c`——包裝既有 dual_arm/co_emcy（純轉接，
     呼叫順序不變）；per-bus 健康儀表自 app_io_agents 移入（協定特有
     邏輯歸後端）。
   - `firmware/app/bus_ecat.c`——走 `ec_master.h` 門面（一拍延遲：
     pump_rx=exchange+鎖存回授、tick=使能步進/安全覆寫→set_output）；
     take_fault = CiA402 fault 邊緣（EMCY 對等物；0x603F 診斷碼屬 SDO
     背景通道，遞延）。底下 sim/SOEM/IgH 由連結期選擇，本檔不變。
3. **app 層切換**：`app_main.c` 增 `app_select_bus()`/`app_bus()`/
   `app_present_count()`，預設 CANopen；`app_main_tick` 與 H1 四 agent、
   IO agents 全改走 vtable。
4. **pc_master `--bus canopen|ethercat`**：同一 binary 切協定；ethercat
   預設 1 kHz、目前連結 fake 後端（= EtherCAT 路徑的 SIL，不需 vcan）。
5. **`eng_phase_trim_us()`**（交叉盤點缺口 1）：DC 跟隨模式（SOEM）的
   鎖相微調 hook，±5% 週期限幅。
6. **測試** `firmware/tests/test_h5.c`：trim 平移/限幅；**H1 的四個 agent
   零修改**跑 bus_ecat @1 kHz——使能 14/14、點到點收斂、掉軸→安全看門狗
   safe stop→回線復歸、health WKC 全對。

## 動機 / 背景

設計文件 §6 的核心宣稱「AxisAgent/MotionAgent 換 bus 零修改」在本次落地
並取得雙重實證：test_h5（同 agents 跑 EtherCAT @1 kHz）+ test_agents
（CANopen 路徑經 vtable 包裝後逐幀 diff 仍逐 byte 一致）。

## 影響範圍

- 新增：`firmware/app/bus_canopen.c`、`bus_ecat.c`、`firmware/tests/test_h5.c`
- 修改：`engine/bus_if.h`（vtable）、`loop_engine.[ch]`（phase trim）、
  `ecat/ec_master.[ch]+_sim.c`（ec_axis_fresh）、`app/app_main.c`、
  `app_agents.c`、`app_io_agents.c`（走 vtable）、`pc/pc_master_main.c`
  （--bus）、`pc/Makefile`、`tests/Makefile`、`firmware/Makefile`
  （bus_canopen/co_emcy 進板端源列）、`pc/README.md`
- 行為：CANopen 路徑零變化（test_agents 把關）；pc_master 新增 ethercat
  模式（sim）。

## 驗證方式

- `firmware/tests`：4906 檢查 0 失敗（含 test_agents 逐幀 diff 迴歸）。
- `firmware/pc`：建置零警告；實跑
  `(echo "j 3 0.4"; sleep 5) | ./pc_master --bus ethercat --seconds 4`
  → 14/14 使能、@1 kHz ticks=3996、SKIP 政策運作（本機非 RT，miss 屬預期，
  RT 驗收歸 WP-H3）。
- 板端 Makefile 僅更新源列（本機無 CubeMX HAL 無法建置——既有狀況）。

## 關聯

- 前置：`e7e0c08`（ec_master 門面+fake 後端）、`8f89c8b`（WP-H4）
- 設計：`harness-agent-loop-engine-plan.md` §3.1（trim）/§6（bus_if_t）/§9 H5
- 剩餘 H5 真硬體部分：`ec_master_soem.c`/`ec_master_igh.c` 真後端
  （WP-L/I 的硬體項）
