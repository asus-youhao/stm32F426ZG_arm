# 目標韌體建置（STM32F746ZG）

本目錄說明如何把控制韌體建成可燒錄的 `firmware.elf/.bin`。
我們的控制邏輯（`canopen/ control/ app/ safety/ host/`）是可移植 C，
但**燒錄韌體還需要 CubeMX 產生的 HAL/啟動碼**，由你產生後放入下列位置。

## 1. 用 CubeMX 產生底層

1. 新建 STM32F746ZG 專案，啟用 **CAN1、CAN2、TIM6（500 Hz）**、時脈樹（SYSCLK 216MHz）。
2. Toolchain 選 **CMake**（或產生後只取 Core/Drivers）。
3. 產生後，把以下複製到 `firmware/`：

```
firmware/
├── Core/
│   ├── Inc/         (stm32f7xx_hal_conf.h, main.h, *_it.h ...)
│   ├── Src/         (main.c, stm32f7xx_it.c, system_stm32f7xx.c, *_msp.c ...)
│   └── Startup/startup_stm32f746xx.s
└── Drivers/
    ├── STM32F7xx_HAL_Driver/  (Inc, Src)
    └── CMSIS/                  (Device/ST/STM32F7xx, Include)
```

## 2. 接線（複製範本）

把 `firmware/integration_example.c` 的片段併入 CubeMX 的 `main.c`：
- `HAL_CAN_RxFifo0MsgPendingCallback` → `co_bxcan_on_rx()`
- `HAL_TIM_PeriodElapsedCallback`(TIM6) → `app_main_tick()`
- `main()`：`app_main_init();` 後 `HAL_TIM_Base_Start_IT(&htim6);`

TIM6 設為 **500 Hz**（週期 2 ms，見 `app/control_rate.h`）。
bxCAN 位元時序依實際 APB1 重算（見 `docs/design/firmware-cubemx-integration.md`）。

## 3. 建置

```bash
cd firmware
cmake -S . -B build-tgt -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build build-tgt
# → build-tgt/firmware.elf / .bin / .hex + size 報告
```

## 4. 燒錄

```bash
# ST-Link
st-flash write build-tgt/firmware.bin 0x08000000
# 或 OpenOCD / STM32CubeProgrammer
```

## 注意

- `co_bxcan.c`（真實 bxCAN 硬體層）僅在 TARGET 建置時編入；HOST 模擬用 `sim/co_bxcan_sim.c`。
- 若 CubeMX 也產生自己的 linker script，擇一即可（本專案提供 `linker/STM32F746ZGTx_FLASH.ld`）。
