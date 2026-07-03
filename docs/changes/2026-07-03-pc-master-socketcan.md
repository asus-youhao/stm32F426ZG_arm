# PC 端 CANopen 主站（SocketCAN）——不接板子測同一套韌體

> 分支：`feature/pc-canopen-master`

## 變更摘要

新增 `firmware/pc/`：把板端 L0–L4 C 韌體堆疊原封編成 Linux 執行檔 `pc_master`，
底層 bxCAN 抽換成 **SocketCAN**（`vcan0`=左臂、`vcan1`=右臂，對應 F746 CAN1/CAN2），
對端由 `sim_py/can_slave.py` 模擬 14 顆 EYOU PHU CiA402 從站（依 EYOU CANopen 手冊）。
自此**不需要 Nucleo 板**即可跑完整主站邏輯：bring-up、NMT/SDO/PDO、CiA402 使能、
500Hz 全棧控制迴圈、安全看門狗。

新增檔案（其餘韌體源碼與板端共用，零修改）：

- `firmware/pc/co_bxcan_socketcan.c/.h`：實作 `co_bxcan.h` 四函式;非阻塞收發，
  kernel TX queue 滿（ENOBUFS/EAGAIN）→ `CO_ERR_TX`，對齊 bxCAN mailbox 滿的
  丟幀語意（`dual_arm_tx_drops()` 統計照常有效）。介面名可用
  `co_socketcan_set_ifname()` 改綁;空字串=停用該臂（優雅降級為單臂）。
- `firmware/pc/hal_linux.c`：真實牆鐘 `HAL_GetTick/HAL_Delay`
  （CLOCK_MONOTONIC）。sim 版假時鐘每呼叫 +1ms，對接真外部行程會誤判逾時。
- `firmware/pc/pc_master_main.c`：流程對齊 `board/main.c`
  （選配 `--bringup N` 單軸自檢 → `app_main_init` → 500Hz tick），
  TIM6 ISR 換成 `clock_nanosleep(TIMER_ABSTIME)` 並量測遲到抖動;
  另有 stdin 互動命令（`j <idx> <rad>`、`e <0|1>`、`p`、`q`）與 `--seconds`（CI 用）。
- `firmware/pc/Makefile`、`setup_vcan.sh`（sudo 建 vcan）、`run_slaves.sh`
  （雙 bus 各 7 顆從站）、`README.md`（含 **無 sudo 的 `unshare -rn`** 跑法）。

修改：

- `firmware/sim_py/can_slave.py`：`run_canable()` 迴圈補上 `motor.step(dt)`
  物理步進（原本真機模式從不推進物理，CSP/PV 目標下去位置永遠不動，
  bring-up 的 moved 檢查與回授看門狗都測不出東西）;`recv` timeout 0.05→0.002s
  讓步進與 PDO 回應更即時;dt 上限 0.05s 防暫停後跳變。

## 動機 / 背景

板端驗證每次都要接 Nucleo + transceiver + CANable，門檻高、無法自動化。
`firmware/sim/`（行程內假 bus）雖可測邏輯，但主站與從站同行程、同步注入，
測不到「跨行程、真延遲、真序列化」的 bus 行為。SocketCAN vcan 介於兩者之間：
frame 真的經過 kernel CAN 層，主站/從站是獨立行程，卻不需任何硬體。

## 影響範圍

- 純新增 `firmware/pc/`;板端韌體、`sim/`、`sim_py` 其他工具不受影響。
- `can_slave.py` 真機模式行為變更：馬達物理會隨時間推進（這是修 bug，
  原行為下從站位置永遠凍結）;`--selftest`/`--modetest` 離線路徑不變。

## 驗證方式（實際執行）

1. `firmware/pc && make`：gcc 12 無警告編譯通過。
2. `can_slave.py --selftest`、`--modetest`：全部斷言通過（step 補丁無回歸）。
3. **端到端**（`unshare -rn` user namespace 內建 vcan0/vcan1，無 sudo）：
   - 兩行程假從站（各 7 node）+ `./pc_master --bringup 1 --seconds 9`
   - bring-up：deviceType/baud/nodeId/位置 SDO 全過，PV 轉動 **pos 0→3186、moved=1**
   - 全棧：**present 14/14**、安全狀態機達 `RUNNING`
   - stdin `j 0 0.3`：J0 追到 25033 counts 收斂（pos=tgt），`p` 印出 FK 末端位姿
   - 2830 ticks **0 丟幀**;非 RT 核心 tick 最大遲到 ~2.5ms

## 待補 / 風險

- 非 RT 排程抖動數 ms;需要更緊時序可 `chrt -f` 或 PREEMPT_RT。
- 此路徑不驗證 bxCAN 硬體時序（位元時序、濾波器、ISR），燒板前仍應跑板端 bring-up。
- vcan 無位元率概念，測不出 1Mbps 頻寬飽和;頻寬論證仍以 `control_rate.h` 分析為準。

## 關聯

- 分支：`feature/pc-canopen-master`（自 `develop` 切出）
- 前置：`2026-06-26-f746-makefile-bringup-build.md`（板端 bring-up）、
  `2026-07-01-c1-canable-python-slave.md`（`can_slave.py`）
- 對照：`docs/design/sim-fake-hardware.md`（行程內假 bus，與本篇互補）
