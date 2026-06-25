# WP6 安全 + WP7 上位機介面

## WP6 — 安全與系統狀態機（`firmware/safety/safety.[ch]`）

### 系統狀態
`INIT → IDLE → ENABLED → RUNNING`,異常時 `FAULT` / `ESTOP`。

### 監看項目
- **驅動故障**：CiA402 狀態字 fault 位（0x0008）。
- **通訊看門狗**：每軸回授新鮮度,逾時 `comms_timeout_ms`（預設 50ms）→ 視為失聯。
- **急停**：`safety_set_estop()`（硬體按鈕 / 上位機命令）。

### 反應
- 不安全 → `safety_update()` 回 false;`safety_safe_controlword()` 給安全控制字：
  - ESTOP → disable voltage（0x0000,自由停）
  - FAULT → quick stop（0x0002,受控停）
- `dual_arm_set_safe_stop(on, cw)`：L1 下發時強制安全控制字、目標維持實際位置。

### 整合（app_main_tick）
```
safety_report_joint(每軸 statusword) → safety_update() → dual_arm_set_safe_stop(!allow)
```
對外：`app_set_estop()`、`app_sys_state()`。

## WP7 — 上位機介面（`firmware/host/host_if.[ch]`）

### 協定框架
`SYNC(0xAA) TYPE LEN PAYLOAD CKSUM(XOR)`,傳輸無關（弱連結 `host_send_bytes`）。

### 命令（上位機→MCU）
| Type | 名稱 | payload |
| ---- | ---- | ------- |
| 0x01 | CMD_ESTOP | u8 active |
| 0x02 | CMD_SET_MODE | u8 mode |
| 0x03 | CMD_JOINT_MOVE | u8 joint, f32 rad |
| 0x04 | CMD_SET_POSE | u8 arm, f32 x,y,z |

### 遙測（MCU→上位機）
| Type | 名稱 | payload |
| ---- | ---- | ------- |
| 0x81 | TLM_STATE | u8 sys_state, u8 mode |
| 0x82 | TLM_JOINTS | 14 × { f32 pos_rad, u16 sw } |
| 0x83 | TLM_EE | f32 L[3], f32 R[3] |
| 0x84 | TLM_MOTOR | 14 × { f32 torque_Nm, f32 current_A } |

### 用法
- 接收：UART/USB ISR 逐位元組 `host_feed_byte()` → 完成 frame 觸發 `host_on_command()`（應用實作,呼叫 app_* API）。
- 發送：週期 `host_encode()` 打包遙測 → `host_send_bytes()`。

> TLM_MOTOR（扭矩/電流）對應假硬體（Python 版）會回報的 0x6077/0x6078,供上位機監看每顆馬達出力與電流。

## 關聯
- `dual-arm-control-plan.md`（WP6/WP7）、`sim-fake-hardware.md`
