# 韌體整合說明 — STM32CubeMX（F746ZG）

- 對象：把 `firmware/canopen` + `firmware/app` 接到 CubeMX 產生的專案
- 範例接線檔：`firmware/integration_example.c`

## 1. CubeMX 設定

### 1.1 時脈
- 設定 HSE + PLL,SYSCLK 216 MHz。
- 記下 **APB1 周邊時脈**（bxCAN 在 APB1）與 **APB1 Timer 時脈**（給 TIM6）。
- bxCAN 位元時序需依 APB1 頻率重算（見 §3）。

### 1.2 啟用 CAN1 / CAN2（bxCAN）
| 周邊 | 腳位範例（依板子調整） |
| ---- | ---------------------- |
| CAN1 | `PD0=CAN1_RX`, `PD1=CAN1_TX`（或 PA11/PA12、PB8/PB9） |
| CAN2 | `PB12=CAN2_RX`, `PB13=CAN2_TX`（或 PB5/PB6） |

- 兩者皆啟用,Mode = Normal。
- NVIC：勾選 **CAN1 RX0 interrupt**、**CAN2 RX0 interrupt**。
- 位元時序可先隨意（會被 `co_bxcan_init()` 覆寫）。

### 1.3 TIM6（1 kHz 控制 tick）
- 啟用 TIM6,Internal Clock。
- `PSC` / `ARR` 使 update event = 1000 Hz：
  `f = TimerClk / (PSC+1) / (ARR+1)`,例 TimerClk=90 MHz → PSC=89, ARR=999 → 1 kHz。
- NVIC：勾選 **TIM6 global interrupt**。

### 1.4 （選用）UART / SWO
- 供 `bringup_log()` / debug 輸出。

## 2. 程式接線（複製 `integration_example.c` 片段）

1. **CAN RX 回呼** → 分流到 channel：
   ```c
   void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h){
       CAN_RxHeaderTypeDef rh; co_frame_t f;
       HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &rh, f.data);
       f.id=(uint16_t)rh.StdId; f.dlc=(uint8_t)rh.DLC;
       co_bxcan_on_rx(h->Instance==CAN1?CO_BUS_LEFT:CO_BUS_RIGHT, &f);
   }
   ```
2. **1 kHz tick**：
   ```c
   void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
       if (htim->Instance==TIM6) app_main_tick();
   }
   ```
3. **main()** USER CODE：
   ```c
   app_main_init();                 // 含 dual_arm_init() / co_bxcan_init()
   HAL_TIM_Base_Start_IT(&htim6);   // 啟動 1 kHz
   ```

> 注意：`co_bxcan_init()` 會自行呼叫 `HAL_CAN_Init/Start/ActivateNotification` 並**覆寫位元時序**。若 CubeMX 的 `MX_CANx_Init()` 也呼叫 `HAL_CAN_Init`,擇一即可（建議讓 `co_bxcan_init` 主導時序）。

## 3. 1 Mbps 位元時序計算

`bit time = (1 + BS1 + BS2) × Tq`,`Tq = (Prescaler) / APB1Clk`,採樣點 ≈ `(1+BS1)/(1+BS1+BS2)`。

| APB1 Clk | Prescaler | BS1 | BS2 | 位元率 | 採樣點 |
| -------- | --------- | --- | --- | ------ | ------ |
| 45 MHz   | 5         | 6TQ | 2TQ | 1.0 Mbps | 77.8% |
| 54 MHz   | 6         | 6TQ | 2TQ | 1.0 Mbps | 77.8% |
| 42 MHz   | 6         | 4TQ | 2TQ | 1.0 Mbps | 71.4% |

→ 依你的實際 APB1 頻率挑一組,填進 `firmware/canopen/co_bxcan.c` 的 `co_bxcan_init()`。

## 4. 檔案清單（加入專案 source / include path）

```
firmware/canopen/*.c, *.h
firmware/app/*.c, *.h
firmware/test/bringup.c, bringup.h   （bring-up 階段）
```

## 5. 關聯

- bring-up 流程：`wp2-single-axis-bringup.md`
- 控制規劃：`dual-arm-control-plan.md`（WP1 L0 / WP2 L1）
