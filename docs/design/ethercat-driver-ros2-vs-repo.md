# ethercat_driver_ros2 vs 本 repo — EtherCAT 部分對比

- 文件層級：分析 / 對照
- 對象：ICube-Robotics `ethercat_driver_ros2`（gx701 `gallop_ws/src/`，IgH-based
  ros2_control 驅動）對比本 repo 的 EtherCAT 堆疊（`firmware/ecat/` + 規劃）
- 範圍：**只談 EtherCAT**，不含 ROS2 上層控制器本身

## 一句話定位

| | 定位 |
| --- | --- |
| **ethercat_driver_ros2** | 成熟、通用、**只跑 Linux + ROS2** 的 EtherCAT 硬體驅動；底層**只綁 IgH**；用 **YAML 宣告式**設定任意從站 |
| **本 repo** | 為 EYOU 雙臂量身打造、**可攜到 F746 微控制器**、**CANopen 與 EtherCAT 共用同一套控制/資料面**；底層是 `ec_master.h` 門面（sim / SOEM / IgH 三後端） |

## 逐軸差異（EtherCAT 面）

| 面向 | ethercat_driver_ros2 | 本 repo |
| --- | --- | --- |
| 主站後端 | **IgH only**（`ecrt_*` 直呼、kernel module） | `ec_master.h` 門面 → **SOEM（主）/ IgH / sim** 可換 |
| 可攜性 | 綁死 Linux kernel + ROS2，**上不了 F746** | 純 C，同一套碼 PC + F746 板端（nicdrv/osal 移植層） |
| 整合模型 | ros2_control `SystemInterface` 外掛，由 controller_manager 迴圈驅動 | 自有 harness/loop-engine（相位 tick、overrun SKIP、WCET 預算、DC PI 鎖相），**無 OS/ROS 依賴** |
| 設定方式 | **宣告式 YAML/URDF**：vendor/product、`assign_activate`(DC)、啟動 SDO 清單、rpdo/tpdo 逐 channel 綁 OD↔介面 + `factor` 單位換算——改映射免重編 | C structs（ec_config）+ key=value 設定檔；改映射要動碼 |
| CiA402 | 獨立外掛 `ethercat_generic_cia402_drive`（`auto_state_transitions`），**只服務 EtherCAT** | 共用 `cia402.c`，**CANopen 與 EtherCAT 同一份狀態機** |
| 多從站/多廠牌 | pluginlib 通用（Maxon/Beckhoff…），社群驗證 | 專為 EYOU PHU，泛用性低 |
| 免硬體測試 | 無（要真 IgH + 硬體） | **sim 後端 + 單元測試**，逐幀 byte diff，免硬體 CI |
| 協定範圍 | **EtherCAT 唯一** | CANopen + EtherCAT 雙協定共堆疊 |

## 三個重點結論

1. **設計哲學相反**：ROS2 驅動是「把 EtherCAT 塞進 ROS2 生態、用設定檔通吃各種從站」；
   本 repo 是「一套可攜控制核心、通吃兩種匯流排、還能下到 MCU」。前者贏在廣度，
   後者贏在可攜性 + 雙協定。

2. **值得借鏡：宣告式 PDO/SDO 設定**。它的 yaml（如 EPOS4 範例）把 `assign_activate=0x0300`、
   啟動 `0x60C2` SDO、每個 PDO entry 綁介面 + `factor` 換算寫得很乾淨。日後把 IgH 真後端
   接進 `ec_master.h` 時，它的 CiA402 外掛狀態機與 DC 設定序列是現成參考（與
   `eyou-motor-master` 的 pysoem 主站互相印證）。

3. **對「現在這顆 PHU17」它幫不上忙**：`ethercat_driver_ros2` 也是 IgH，一樣要**主張
   EtherCAT 控制權**，會撞上同一道 `0x2100=2` 牆（控制權在 CANopen 時 EtherCAT 唯讀、
   拿不到控制權）。不是繞過當前卡點的捷徑；切 `0x2100` 只能靠 CANopen/UART。

## 關聯

- 診斷鏈：`../changes/2026-07-07-phu17-safeop-diagnosis.md`
- 參考主站：`../changes/2026-07-07-eyou-reference-master-found.md`、記憶 `eyou-reference-master`
- 通訊路徑（controller↔馬達走什麼）：`ros2-control-comm-path.md`
- 本 repo 規劃：`ethercat-coe-master-plan.md`、`linux-rt-ethercat-master-plan.md`
