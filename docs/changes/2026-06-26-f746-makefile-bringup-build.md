# Nucleo-F746ZG bring-up：可編譯/可燒錄的 Makefile 專案

- 日期：2026-06-26
- 分支：`feature/f746-makefile-bringup`（自 `develop`）
- 目標板：**NUCLEO-F746ZG**，工具鏈 **arm-none-eabi-gcc 13.3 + GNU Make**，燒錄 **STM32CubeProgrammer CLI**

## 變更摘要

把原本只有「待整合原始碼」的 `firmware/` 補成一個**能 `make` 出 `.elf/.hex/.bin`、能直接燒進 Nucleo-F746ZG** 的最小 bring-up 專案：

1. 新增 `firmware/board/`：
   - `main.c`：`HAL_Init` → 216 MHz 時脈 → USART3(VCP) log → 設定 `hcan1/hcan2` handle →
     呼叫 `bringup_single_axis(CO_BUS_LEFT, node=1)` → 印出報告 → LD1 心跳。
     內含 `HAL_CAN_MspInit`（CAN1=PD0/PD1, AF9 + NVIC）、`HAL_UART_MspInit`（USART3=PD8/PD9）、
     `HAL_CAN_RxFifo0MsgPendingCallback`（RX→`co_bxcan_on_rx`）、`bringup_log` 覆寫（vsnprintf→VCP）。
   - `stm32f7xx_it.c`：`SysTick_Handler`、`CAN1_RX0_IRQHandler`、`CAN2_RX0_IRQHandler`。
   - `stm32f7xx_hal_conf.h`（由 HAL 範本複製，**HSE_VALUE 改 8 MHz**）。
   - 由本機 HAL 包複製：`startup_stm32f746xx.s`、`system_stm32f7xx.c`、`STM32F746ZGTx_FLASH.ld`。
2. 新增 `firmware/Makefile`：只編 bring-up 需要的 HAL 模組（hal/cortex/rcc/rcc_ex/gpio/pwr/pwr_ex/can/uart）
   + `canopen/{co_bxcan,co_nmt,co_sdo,cia402}.c` + `test/bringup.c`。`make` / `make flash` / `make probe`。
3. **修正 CAN 位元時序**：`firmware/canopen/co_bxcan.c` 的 `co_bxcan_init()` Prescaler **5 → 6**。

## 動機 / 背景

- 原 `firmware/` 是「待貼進 CubeMX 專案」的原始碼，**無 .ioc、無 build system、無可燒 image**。要實機 bring-up 必須先有可編譯/可燒的工程。
- 本機沒有獨立 STM32CubeMX，但已安裝 arm-none-eabi-gcc、Make、STM32CubeProgrammer 與
  `STM32Cube_FW_F7_V1.17.4` HAL 包，故改採**手寫 Makefile + 直接引用 HAL 包**，與 CubeMX 等價但全可命令列重現。
- 先做 **bring-up only**（WP2 單軸 SDO 驗證），不含 1 kHz 全控制堆疊，編譯面最小、最安全。

## 影響範圍（含硬體行為）

- **新增**：`firmware/board/*`、`firmware/Makefile`、`firmware/build/`（產物，未追蹤）。
- **修改硬體行為**：`co_bxcan.c` Prescaler 5→6。
  - 原值假設 APB1 = 45 MHz；本專案時脈樹為 **SYSCLK 216 MHz → APB1 = 54 MHz**，
    需 Prescaler=6 才是 1 Mbps（54M/6/9）。**若沿用 5 會變 1.2 Mbps、SDO 全逾時。**
- **腳位（務必照接）**：
  | 訊號 | 腳位 | AF | 說明 |
  | ---- | ---- | -- | ---- |
  | CAN1_RX | PD0 | AF9 | 接 CAN transceiver |
  | CAN1_TX | PD1 | AF9 | 接 CAN transceiver |
  | USART3_TX | PD8 | AF7 | ST-Link VCP（log）|
  | USART3_RX | PD9 | AF7 | ST-Link VCP |
  | LD1 | PB0 | — | 心跳燈 |
- 時脈：HSE 8 MHz（`RCC_HSE_BYPASS`，Nucleo MCO）→ PLL M=4/N=216/P=2 → 216 MHz，APB1=/4、APB2=/2，over-drive、Flash 7WS。

## 驗證方式

- **已驗（編譯）**：`make` 通過，產出 `build/bringup_f746.elf/.hex/.bin`。
  size：text 14 248 / data 100 / bss 2 892 bytes（1 MB Flash、320 KB RAM 綽綽有餘）。
  ELF entry = `Reset_Handler`，`.isr_vector@0x08000000`、`.data/.bss@0x20000000`，向量表正確。
  （linker 對未用 newlib syscall stub 的警告為預期、不影響執行。）
- **待驗（實機，硬體到位後）**：
  1. `cd firmware && make`
  2. 插上 Nucleo（USB），`make probe` 確認 ST-Link/目標連線。
  3. CAN1(PD0/PD1) 經 transceiver 接 PHU 關節，兩端各 120 Ω 終端、24–48V 共地。
  4. `make flash` 燒錄；開 VCP（115200-8-N-1）看 `bringup_log` 逐步輸出。
  5. 依 [WP2 驗收表](../design/wp2-single-axis-bringup.md) 逐項對照（0x1000/0x6041/0x26A1/0x26A0/0x6064…）。
  - 安全：第一版 `BRINGUP_SPIN_VEL=2000` 會讓馬達低速轉動；如只想驗通訊先不轉，把它設為 0。

## 關聯

- 設計：[firmware-cubemx-integration.md](../design/firmware-cubemx-integration.md)（§3 位元時序表）、
  [wp2-single-axis-bringup.md](../design/wp2-single-axis-bringup.md)
- 原始碼骨架：[2026-06-25 F746 CANopen 韌體](./2026-06-25-f746-canopen-firmware.md)
