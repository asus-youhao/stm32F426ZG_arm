# 假硬體模擬器（PC 上跑全棧 + 模擬 PHU 從站）

- 目錄：`firmware/sim/`
- 用途：無硬體時,於 PC 編譯執行整個 L1–L4 韌體,接上**模擬的 EYOU PHU CANopen 從站**,觀察資料流。

## 1. 組成

| 檔案 | 角色 |
| ---- | ---- |
| `stm32f7xx_hal.h` | HAL 替身（只提供 HAL_GetTick/HAL_Delay） |
| `hal_shim.c` | 主機時鐘 |
| `phu_sim.[ch]` | **模擬 PHU 從站**：SDO 伺服、NMT、PDO、CiA402 狀態機 + 位置動力學 |
| `co_bxcan_sim.c` | **虛擬 CAN bus**：取代 bxCAN 硬體層,路由主站↔從站,frame 記錄/統計 |
| `sim_main.c` | 情境腳本 + 資料流列印 |
| `Makefile` | 主機編譯 |

被測的真實韌體（未改）：`canopen/`（除 co_bxcan.c）、`control/`、`app/`。

## 2. 編譯與執行

```bash
cd firmware/sim
make
./phu_sim_demo
```

## 3. 模擬從站行為（依手冊 v1.06）

- SDO 讀：0x1000 device type、0x6041 SW、0x6064 actual pos、0x26A0 node、0x26A1=1Mbps…
- SDO 寫：0x6040 CW（驅動 CiA402 狀態機）、0x6060 mode、0x607A、0x60FF、PDO 映射(0x1600/0x1A00 ACK)。
- NMT：start/preop/stop/reset → 切換節點狀態。
- RPDO1（0x200+node）：收 [CW][TargetPos] → 跑動力學 → 回 TPDO1（0x180+node）[SW][ActualPos]。
- CiA402：fault reset→0x06→0x07→0x0F;statusword 對應 switch-on-disabled/ready/switched-on/operation-enabled。
- 動力學：CSP/PP 朝目標位置以 max_step/tick 收斂;CSV/PV 以速度積分。

## 4. 情境腳本（sim_main）

1. `app_main_init()`：印出 CANopen 交握（NMT/SDO/PDO 映射）frame。
2. 1 kHz 啟動：尚未下目標 → 安全保持（PDO 持續交換）。
3. 左臂笛卡爾目標 X+0.05 → 觀察 IK→關節→counts→CAN→回授 收斂。
4. 切 BIMANUAL → 右臂跟隨左臂相對位姿。
5. CAN 流量統計。

## 5. 實測輸出（節錄）

```
[3] 左臂目標 X+0.05 → L_EE (0.000,0.200,0.760) → (0.050,0.200,0.758)
[4] BIMANUAL：左臂動，右臂連動（R_EE 0 → 0.039）
[5] 左臂 bus TX≈10566 / RX≈10563（雙 channel 對稱）
```

驗證了：CANopen 交握、PDO 週期交換、task-space IK、雙臂協同、雙 channel 流量。

## 6. 限制

- 動力學為簡化收斂模型,非真實電機/減速器動態。
- 未模擬匯流排時序/丟包/錯誤幀;HAL 時鐘為計數器。
- 旋轉/力控未納入（位置層為主）。

## 7. 關聯

- `dual-arm-control-plan.md`、`wp3/wp4/wp5` 設計文件。
