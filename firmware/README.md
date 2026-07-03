# Firmware — STM32F746ZG 雙臂 CANopen 通訊

本目錄為 **STM32F746ZG** 韌體,負責透過 **雙路 bxCAN(CAN1 / CAN2)** 以 **CANopen（CiA 301/402）** 控制 EYOU PHU 雙臂關節。

## 架構：雙手不同 CAN channel

STM32F746 內建 **2 路獨立 bxCAN**,因此左/右臂各佔一條獨立匯流排:

```
            STM32F746ZG
   ┌───────────────────────────┐
   │  bxCAN1 ──► 左臂 7 軸       │  Node-ID 1..7  @1Mbps
   │  bxCAN2 ──► 右臂 7 軸       │  Node-ID 1..7  @1Mbps
   └───────────────────────────┘
```

- 兩條 bus 頻寬獨立、故障隔離。
- 各 bus 上節點 ID 1..7（J1 肩…J7 腕）。
- 預設 1 Mbps（對應關節 OD 0x26A1 預設值）。

> ⚠️ **控制頻率 = 500 Hz（非 1 kHz）**。Classic CAN @1Mbps 每軸每週期 1 RPDO+1 TPDO，
> 單臂 7 軸 = 14 frame/週期；500 Hz → 7000 frame/s（接近 Classic CAN 上限 ~7000–8000）。
> **1 kHz（14000/s）會超載丟幀**，要 1 kHz 須改走 EtherCAT。詳見 `app/control_rate.h`、
> `docs/design/can-bus-architecture.md`、`canopen-vs-ethercat.md`。
> PDO 下發丟幀已有偵測：`dual_arm_tx_drops()`。

## 建置

- **HOST 模擬（PC 驗證控制邏輯）**：`cmake -S . -B build && cmake --build build && ./build/phu_sim_demo`
  （或 `cd sim && make`）
- **PC 主站（SocketCAN，免板子跨行程測試）**：`cd pc && make && ./pc_master`，
  對打 `sim_py/can_slave.py` 假從站（vcan0=左臂、vcan1=右臂），見 `pc/README.md`。
- **TARGET 韌體（可燒錄 .bin）**：需 arm-none-eabi + CubeMX HAL，見 `target/README.md`。

## 目錄

```
firmware/
├── README.md
├── canopen/              # 輕量 CANopen 主站
│   ├── canopen.h         # 共用型別、COB-ID、回傳碼
│   ├── co_bxcan.h/.c     # STM32 bxCAN 硬體層（CAN1/CAN2 @1Mbps）
│   ├── co_sdo.h/.c       # SDO client（讀寫物件字典）
│   ├── co_nmt.h/.c       # NMT 控制 + Heartbeat 監看
│   ├── co_pdo.h/.c       # PDO 收發（cyclic CSP）
│   └── cia402.h/.c       # CiA 402 狀態機 + 模式/目標
└── app/
    ├── dual_arm.h/.c     # 雙臂設定（CAN1=左、CAN2=右、各 7 軸）
    └── app_main.c        # 初始化 + 1kHz 控制迴圈骨架
```

## 整合到 STM32CubeMX 專案

1. 用 CubeIDE/CubeMX 建立 STM32F746ZG 專案,啟用 **CAN1、CAN2**（bxCAN）。
2. 腳位範例（依實際板子調整）:
   - CAN1: `PD0=CAN1_RX`, `PD1=CAN1_TX`（或 PA11/PA12、PB8/PB9）
   - CAN2: `PB12=CAN2_RX`, `PB13=CAN2_TX`（或 PB5/PB6）
3. APB1 時脈設定下,1 Mbps 位元時序見 `co_bxcan.c`（以 APB1=45 MHz 為例:Prescaler=5, BS1=6TQ, BS2=2TQ, SJW=1）。請依你的時脈樹重算。
4. 把 `firmware/canopen` 與 `firmware/app` 加入 Include path 與 source。
5. 在 `HAL_CAN_RxFifo0MsgPendingCallback` 轉呼叫 `co_bxcan_on_rx()`。
6. 主程式呼叫 `app_main_init()` 與 1 kHz `app_main_tick()`。

## 編譯 / 燒錄（Nucleo-F746ZG bring-up）

`board/` + `Makefile` 已備妥一個可直接編譯/燒錄的 **WP2 單軸 bring-up** 韌體
（HAL 取自本機 `STM32Cube_FW_F7_V1.17.4`；換機器用 `make CUBE_FW_F7=<路徑>`）。

```bash
cd firmware
make            # 產出 build/bringup_f746.elf/.hex/.bin
make probe      # 確認 Nucleo ST-Link/目標連線（免燒）
make flash      # 用 STM32CubeProgrammer CLI 燒錄並 reset
```

接線（務必一致）：CAN1_RX=**PD0**、CAN1_TX=**PD1**（AF9）→ CAN transceiver → PHU 關節；
兩端各 120 Ω 終端、24–48V 共地；log = USART3(VCP) **PD8/PD9 @115200**。
測試參數在 `board/main.c`（`BRINGUP_NODE/SPIN_VEL/SPIN_MS`；`SPIN_VEL=0` 則只讀不轉）。
細節見 [docs 變更紀錄](../docs/changes/2026-06-26-f746-makefile-bringup-build.md) 與
[WP2 bring-up](../docs/design/wp2-single-axis-bringup.md)。

## 備註

- 本套為**輕量手寫主站**,適合 bring-up 與理解協定。
- 若要量產等級 / 完整 CiA 301,建議改用開源 **CANopenNode** 移植到 bxCAN;本層介面刻意貼近其概念以便日後切換。
