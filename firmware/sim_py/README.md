# Python 假硬體模擬器（sim_py）

純 Python（無第三方相依）模擬 **EYOU PHU 雙臂 CANopen** 系統,每顆馬達**可讀寫**並回報**扭矩(力)與電流**,且 **URDF-ready**。

## 執行

```bash
cd firmware/sim_py
python3 sim_main.py
```

## 檔案

| 檔案 | 角色 |
| ---- | ---- |
| `phu_motor.py` | 單顆 PHU 馬達：CANopen 物件字典讀寫 + CiA402 + **扭矩/電流物理模型** |
| `can_bus.py` | 虛擬 CAN bus + CANopen 主站（NMT/SDO/PDO,frame 記錄） |
| `kinematics.py` | DH FK / Jacobian / DLS-IK（純 Python） |
| `robot.py` | **joint → PHU 馬達對應**（URDF-ready）、雙臂 DH、基座偏移 |
| `urdf_loader.py` | 由 URDF 解析關節 + 對應馬達（`load_urdf_joints` / `assign_motors`） |
| `sim_main.py` | 情境腳本：交握 → 抗重力升起 → 肘部運動 → SDO 讀扭矩/電流 |

## 每顆馬達「可讀寫」

`PhuMotor.read_od(index)` / `write_od(index, sub, value)` 支援關鍵物件:

| 物件 | 意義 |
| ---- | ---- |
| 0x6040 / 0x6041 | 控制字 / 狀態字（CiA402） |
| 0x6060 / 0x6064 / 0x607A | 模式 / 實際位置 / 目標位置 |
| **0x6077** | **實際扭矩**（‰ 額定）→ 換算 N·m |
| **0x6078** | **實際電流**（‰ 額定）→ 換算 A |
| 0x6076 / 0x6075 | 額定扭矩(mNm) / 額定電流(mA) |
| 0x26A0 / 0x26A1 | 節點 ID / 波特率(1Mbps) |

## 扭矩 / 電流模型

CSP 模式下,內環 PD 產生輸出扭矩,對抗**重力負載 + 黏滯阻尼 + 慣量**:

```
tau_cmd = Kp·(q_target − q) − Kd·q̇        （內環,夾制到峰值扭矩）
q̈ = ( tau_cmd − grav·sin(q) − b·q̇ ) / I
電流 = tau_cmd / Kt        （Kt = 額定扭矩 / 額定電流）
```

型號參數（PHU14/17/20 額定/峰值扭矩、額定電流）見 `phu_motor.MODELS`（**佔位值,實機以原廠規格書為準**）。

## URDF 對應（未來）

```python
from urdf_loader import load_urdf_joints, assign_motors
joints = load_urdf_joints("robot.urdf")        # 取 revolute 關節（依序）
jmap = assign_motors(joints, {                  # 指定每關節 → (arm,bus,node,model)
    "left_shoulder_pitch": ("L","L",1,"PHU20"),
    ...
})
# jmap 結構同 robot.JOINT_MAP,可直接建立 PhuMotor / CanBus
```

## 實測輸出（節錄）

```
[D] 左肘 +0.5 rad：L_J4_elbow PHU17  力 5.23 N·m  電流 1.31 A（暫態）
[E] SDO 讀 0x6077/0x6078：L_J4 扭矩=4.55 N·m(142‰) 電流=1.14 A(142‰)
[F] CAN1 TX≈12650 RX≈12645 ; CAN2 對稱
```

## 完整物件字典 + 測試上位機（OD-driven）

- `phu_od.py`：**由手冊 v1.06 自動抽取的完整物件字典**（399 條：index/sub/access/type/default/name）。
- `phu_motor.py` 為 **OD-driven**：`read_od/write_od` 走整份 OD，RO 物件寫入回 SDO abort，
  並對 0x6040/6060/607A/6071/60FF 等觸發副作用；狀態字/實際位置/扭矩/電流為即時計算。
- `host_console.py`：**測試用上位機**，可對假馬達發送 CANopen 讀/寫 CMD 並讀回數據：

```bash
python3 host_console.py --demo     # 示範：讀身分→寫參數→RO abort→使能移動→讀力/電流
python3 host_console.py            # 互動：read 0x6041 / write 0x6060 8 / enable / move 150000 / dump 0x26
echo "read 0x6077" | python3 host_console.py
```

- `test_od.py`：OD 讀寫單元測試（讀身分、RW 寫回一致、RO 寫 abort、使能後力/電流非 0）。
  `python3 test_od.py`（回傳碼 0=通過）。

### 示範輸出（節錄）
```
READ  0x1000 Device type = 131474 (0x20192)
WRITE 0x6060 Modes Of Operation = 8 → OK
WRITE 0x6041 Status word = 4660 → ABORT (唯讀 0x06010002)
MOVE  target=150000 → actual=146583  力=4.91 N·m  電流=1.23 A
READ  0x6077 Torque Actual Value = 154 (‰rated)
```

## 與 C 版關係

行為對齊 `firmware/sim`（C 版全棧）;Python 版**更聚焦在單顆馬達讀寫與力/電流可視化**,且方便日後接 URDF。
