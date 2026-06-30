# C1：CANable + Python 假從站（F746 主站對打）

## 變更摘要

新增 `firmware/sim_py/can_slave.py`：把既有的 `PhuMotor` 包成一顆「真實 CAN bus 上的
CiA402 從站」，透過 USB-CAN（CANable，candleLight 韌體）+ python-can 接到與 F746 同一條
匯流排，回應主站的 SDO / NMT / PDO。沒有實體馬達也能驗證 **F746 主站韌體 + CANopen 交握**。

同時為 `firmware/sim_py/phu_motor.py` 的物理模型補上 **PV（Profile Velocity，mode 3）**
分支，使 bring-up 用的速度模式會真的讓位置變化。

## 動機 / 背景

- 硬體 bring-up 直接燒進 F746 後，`can_init=5`（`CO_ERR_STATE`）：板上沒接 CAN
  transceiver、也沒有第二個節點回 ACK，`CAN_MODE_NORMAL` 起不來——屬預期。
- 使用者目前**只有燒錄器、沒有馬達、PC 也沒有 CAN 介面**。經評估（見對話）選擇 **C1**：
  F746 當主站、PC 透過 CANable 當假從站。
- 既有 `sim_py/can_bus.py` 是純記憶體虛擬 bus（主站方法直接呼叫從站物件），**沒有真實
  I/O**，無法接到實體 bus，故另寫 `can_slave.py` 補上傳輸層。

## 影響範圍

| 檔案 | 變更 |
| ---- | ---- |
| `firmware/sim_py/can_slave.py` | **新增**。`CiA402Slave.handle_frame()` 純函式處理 SDO(讀/寫,expedited)、NMT、RPDO1→TPDO1；`run_canable()` 走 python-can；`--selftest` 離線自測 |
| `firmware/sim_py/phu_motor.py` | `step()` 新增 `mode == 3`（PV）分支：`tau_cmd = Kd·(target_vel/CPR − qd)`。只在 PV 模式生效，不影響既有 CSP/CST 情境 |

### 硬體拓樸（重要）

CANable 端是差動 CANH/CANL，F746 的 `PD1/PD0` 是 TTL，**不能直接對接**，F746 端仍需一顆
transceiver：

```
F746 PD1(TX)/PD0(RX) ─►[transceiver]─CANH/CANL─[CANable]─USB─► PC（can_slave.py）
       你的韌體          120Ω 端          120Ω(撥碼)
```

- BOM：CANable ×1 + transceiver 模組 ×1（SN65HVD230 / TJA1050 / MCP2551）。
- 終端：CANable 內建 120Ω（撥碼開），另一端在 transceiver 端補 120Ω。
- 位元率 1 Mbps（對應韌體 `co_bxcan.c` 與關節 OD 0x26A1）。

### 從站對應的 OD（F746 bring-up 會碰到的）

讀：`0x1000` device type、`0x6041` statusword、`0x26A1` baud、`0x26A0` node、`0x6064` pos。
寫：`0x6060` mode、`0x6083/0x6084` accel/decel、`0x60FF` target vel、`0x6040` controlword。
CiA402：fault reset → 0x06 → 0x07 → 0x0F 對應 switch-on-disabled → ready → switched-on
→ operation-enabled（0x0027）。

## 驗證方式

### 已完成（離線，無硬體 / 無 python-can）

```bash
cd firmware/sim_py
python can_slave.py --selftest
```

重現 F746 bring-up 的完整 SDO 序列並斷言：

- `0x1000` deviceType = `0x00020192`、`0x26A1` baud = 1000000、`0x26A0` node = 1
- 使能序列後 `0x6041` statusword = `0x0027`（operation-enabled）
- PV 模式跑 1000 個 1 kHz step 後 `0x6064` 位置改變（`moved=True`，實測 pos_after=934）
- RPDO1 → TPDO1 路徑正確回應

結果：**全部斷言通過**。

### 待硬體到貨（真機）

1. `pip install python-can gs_usb`；Windows 用 Zadig 把 CANable 換成 WinUSB 驅動。
2. F746 端接 transceiver，與 CANable 串成一條 bus，兩端 120Ω。
3. PC：`python can_slave.py --interface gs_usb --channel 0 --bitrate 1000000 --nodes 1:PHU20 --verbose`
4. 燒錄 + 重置 F746，觀察 USART3 VCP log 的 bring-up 結果應變為 `result=0`、deviceType/baud/node
   讀到正確值（不再 `can_init=5`）。

## 關聯

- 對話評估：(C) USB-CAN → C1 分支。
- [假硬體模擬器 (C)](./2026-06-25-sim-fake-hardware.md)、[Python 假硬體](./2026-06-25-python-fake-hardware.md)
- [F746 Makefile bring-up 專案](./2026-06-26-f746-makefile-bringup-build.md)（`can_init=5` 由來）
- 參考：`doc_EYOU/CANable用户手册v1.0`、`doc_EYOU/CANable产品规格书v1.0`
- 後續：若改走 C2（原廠 EYouCanOpenTool 當主站、F746 當從站）需另寫 bxCAN 從站韌體。
