# ros2_control 控制指令通訊路徑 — JTC/FPC → 馬達走 DDS 還是 SHM？

- 文件層級：分析 / 觀念釐清
- 問題：`ethercat_driver_ros2` + ros2_control 下，JTC（joint_trajectory_controller）/
  FPC（forward_position_controller）下達給馬達，中間通訊是 DDS？topic？還是共享記憶體？
  500 Hz 會不會有問題？
- 實測背景：gx701 `phu_controller_ros2` 設定為 **JTC @ 500 Hz**（`update_rate: 500`，
  對應 EtherCAT 2000 µs 週期），`ec_master.cpp` 有設 `SCHED_FIFO`

## 關鍵觀念：有兩段通訊，RT 那段不走 DDS

最常見的誤解：controller → 馬達(EtherCAT) 這段**不是** DDS、不是 topic，
是**同一個 process 內的共享記憶體**。

`controller_manager` 是**單一 RT 執行緒**（gx701 的 `ec_master.cpp` 已設 SCHED_FIFO），
每個週期同步跑三步：

1. **`read()`** — ethercat_driver 從 EtherCAT PDO 收回授，寫進 `state_interface`（`double`）
2. **`controller.update()`** — JTC 讀 state、算出設定點，寫進 `command_interface`（`double`）
3. **`write()`** — ethercat_driver 把 `command_interface` 打包成 PDO → `ecrt` 送出

`command_interface`/`state_interface` 就是 ResourceManager 借出的 **`double*` 參考**——
controller 與 hardware 在**同一 process、同一執行緒**，中間**無 DDS、無 topic、無序列化、
無 IPC**。這正是 ros2_control 為即時性這樣設計的。

## DDS 出現在哪 → 決定高頻會不會出問題（JTC vs FPC 天差地別）

- **JTC（gx701 在用的）**：軌跡目標用 `FollowJointTrajectory` action / `~/joint_trajectory`
  topic **一次性**送進來（走 DDS），之後 **JTC 在 500 Hz 迴圈內自己內插**。
  → **每週期的設定點不走 DDS**，DDS 只負責偶爾送一整條軌跡。

- **FPC（forward_position_controller）**：訂閱 `~/commands`（Float64MultiArray）topic，
  **你發的每一個設定點都過 DDS**。若從外部節點以 500 Hz~1 kHz 逐拍串流 → 每一拍吃 DDS
  延遲/抖動/掉包，且非 RT 保證路徑（掉包時 controller 沿用上一個舊值）。

## 直接回答「500 Hz 會有問題嗎」

- **JTC @ 500 Hz（現況）：不會。** RT 迴圈是 in-process SHM + SCHED_FIFO，DDS 只送一次
  軌跡。500 Hz 甚至 1 kHz 在 PREEMPT_RT 上都很輕鬆（EYOU EtherCAT 最小週期到
  500 µs = 2 kHz 都在規格內）。
- **唯一要小心**：改用 **FPC 並從外部逐拍串流** → 那段走 DDS，500 Hz~1 kHz 會踩到
  抖動/掉包。對策：RT 友善 QoS（best-effort、keep-last-1）並接受抖動；或把設定點
  產生器做成 **chained controller** 塞進 RT 迴圈（就不過 DDS）。

## 補充

- **瓶頸不在 middleware，在 update_rate 迴圈本身**：只要 `ros2_control_node` 有 RT
  優先權、mlockall、隔離核（即 gx701 已套的 isolcpus 調校），500 Hz/1 kHz 的
  read→update→write 綽綽有餘。天花板是 EtherCAT 週期（EYOU 500 µs 整數倍）與 CPU
  算力，不是 DDS。
- **與本 repo 同源思想**：本 repo loop-engine 的 **cmd ring（lock-free SPSC）** 等同
  ros2_control 的 in-process command interface——RT 端讀、非 RT 端寫，兩者都刻意
  讓 RT 迴圈不碰 middleware。差別只在本 repo 沒有 DDS 外圍層，命令直接進 ring。

## 一句話總結

**JTC 架構下，控制指令根本不經 DDS，500 Hz 沒問題；只有「FPC + 外部逐拍 topic
串流」才會讓每一拍吃到 DDS。**

## 關聯

- 驅動對比：`ethercat-driver-ros2-vs-repo.md`
- 本 repo RT/loop-engine：`harness-agent-loop-engine-plan.md`、`linux-rt-ethercat-master-plan.md`
