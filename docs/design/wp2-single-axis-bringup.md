# WP2 — 單軸 Bring-up 測試流程（SDO 讀寫驗證 + 單軸轉動）

- 目標：在整合進雙臂前,先驗證「一條 bus 上的一個關節」L0/L1 是否正常。
- 測試程式：`firmware/test/bringup.c`（`bringup_single_axis()`）

## 0. 安全前提 ⚠️

- 關節**單獨固定**、輸出端可**安全自由轉動**、周圍淨空。
- 使用**低速**、短時間。
- 備妥**急停 / 斷電**手段。
- 先測**單一節點**,確認無誤再接多軸。

## 1. 硬體接線檢查

- [ ] 關節電源 24–48V 正確、共地。
- [ ] CAN_H / CAN_L 接到對應 channel（左→CAN1、右→CAN2）。
- [ ] **兩端各一個 120Ω 終端電阻**（整條 bus 共 2 個）。
- [ ] 關節**節點 ID** 與測試參數一致（出廠多為 1;可先用 bring-up 讀 0x26A0 確認）。
- [ ] 關節 **CAN 波特率** = 1 Mbps（OD 0x26A1 預設）。

## 2. 執行測試

在 `main()`（或一個臨時測試入口）呼叫：

```c
#include "test/bringup.h"

bringup_report_t rep;
/* 左臂 bus、節點 1、目標速度（counts/s,先給小值）、轉 2 秒 */
co_status_t st = bringup_single_axis(CO_BUS_LEFT, 1, /*spin_velocity=*/2000,
                                     /*spin_ms=*/2000, &rep);
```

（可把 `bringup_log` 覆寫為 printf 導向 UART/SWO 以看到逐步輸出。）

## 3. 驗收標準（逐步）

| 步驟 | 物件 | 期望 | 失敗時檢查 |
| ---- | ---- | ---- | ---------- |
| CAN init | — | `can_init==CO_OK` | 時脈、腳位、CubeMX CAN 設定 |
| SDO 讀身分 | `0x1000` | 回非 0 device type | 接線、終端電阻、波特率、節點ID |
| SDO 讀狀態字 | `0x6041` | 回有效 statusword | 同上;ISR 是否把 RX 餵進佇列 |
| SDO 讀波特率 | `0x26A1` | =1000000 | 關節波特率設定 |
| SDO 讀節點ID | `0x26A0` | =使用的 node | 節點 ID 衝突/錯置 |
| 讀初始位置 | `0x6064` | 取得 pos_before | — |
| 設模式 PV | `0x6060=3` | 寫入成功(0x60) | SDO abort code |
| 使能 | `0x6040` | 進入 OPERATION_ENABLED | 故障字 0x603F、是否有報錯 |
| 轉動 | `0x60FF` | 馬達轉動 | 機械卡死、扭矩/速度限制 |
| 位移確認 | `0x6064` | `moved==true` | 編碼器、實際是否轉 |

`bringup_single_axis()` 回 `CO_OK` 代表全流程通過且觀察到位移。

## 4. 常見問題

- **SDO 全逾時**：90% 是接線/終端電阻/波特率/節點ID;其次是 RX 中斷沒接到 `co_bxcan_on_rx()`。
- **使能失敗**：讀 `0x603F`（Error Code）與 statusword 故障位;先送 `0x80` fault reset。
- **能讀不能轉**：確認已 NMT Start、模式正確、目標速度非 0、未被限位/限扭。
- **位元時序**：採樣點不當會偶發 bit error;依 APB1 重算（見 `firmware-cubemx-integration.md` §3）。

## 5. 通過後

1. 換 PP / CSP 模式做小角度定位測試。
2. 同 bus 接第 2 軸,測 node ID 區分與多節點共存。
3. 一臂 7 軸全上 → 進 `dual_arm_init()` + 1 kHz `dual_arm_tick_1khz()`。
4. 兩臂雙 channel 同時 → 進 WP3 joint-space 控制器。

## 6. 關聯

- 整合：`firmware-cubemx-integration.md`
- 規劃：`dual-arm-control-plan.md`（WP1 L0 / WP2 L1）
