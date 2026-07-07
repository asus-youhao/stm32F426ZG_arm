# 02 · L1 初階（l1-basic.html）

6 節：

1. 認識 STM32 / Cortex-M — 命名解讀、F446 vs F746 對照、為何本專案選 F746（雙 bxCAN + M7 算力）。
2. 開發環境 — arm-none-eabi + CubeMX HAL + Makefile、st-flash/OpenOCD；對照 `firmware/Makefile`。
3. GPIO Blink — HAL 骨架、周邊時脈啟用的經典坑。
4. UART printf 除錯 — `_write` retarget、VCP、對照 `firmware/host/host_if.c`。
5. 時脈樹 / SysTick / NVIC — APB1 54MHz 上限（伏筆到 L2 CAN 時序）、中斷優先權配置、ISR 內 HAL_Delay 死當。
6. Timer 週期節拍 — TIM6 2000µs 中斷 = `CONTROL_DT_US`，直接接到 `app_main_tick()`；硬體節拍 vs delay 的抖動論述。

## 取材

- `firmware/app/control_rate.h`、`firmware/app/app_main.c`、`firmware/README.md`。
- L1 結尾的 2ms 節拍是全書「一路長到 500Hz 控制迴圈」的第一塊骨牌。
