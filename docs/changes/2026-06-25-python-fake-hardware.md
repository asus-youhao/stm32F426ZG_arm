# Python 假硬體模擬器（馬達讀寫 + 扭矩/電流 + URDF-ready）

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增 `firmware/sim_py/`（純 Python,無第三方相依）,把假硬體改寫為 Python,並依新需求加入:
- 每顆 **PHU 馬達可讀寫**物件字典（`read_od`/`write_od`）。
- 馬達**扭矩(N·m)與電流(A)** 物理模型（內環 PD 對抗重力/阻尼/慣量,Kt=額定扭矩/額定電流）。
- **URDF-ready** 的 joint→PHU 對應（`robot.JOINT_MAP` + `urdf_loader.py`）。
- 虛擬 CAN bus + CANopen 主站（NMT/SDO/PDO,frame 記錄）。
- DH FK/Jacobian/DLS-IK 純 Python。
- 情境腳本:交握 → 抗重力升起 → 肘部運動 → SDO 讀回扭矩/電流。

## 動機 / 背景

使用者要求:假硬體改 Python;未來吃 URDF 做 joint↔PHU 對應;每顆馬達可讀寫並回報「力(扭矩)與電流」。

## 實測結果

- `python3 sim_main.py` 可執行。
- 顯示每顆馬達 力/電流隨重力與運動變化（左肘 +0.5rad 時 PHU17 達 5.23 N·m / 1.31 A）。
- SDO 讀 0x6077(扭矩‰)/0x6078(電流‰) frame 帶真實值（如 0x8E=142‰）。
- 雙 channel CAN 流量對稱（TX≈RX≈12650）。

## 檔案

- `phu_motor.py`、`can_bus.py`、`kinematics.py`、`robot.py`、`urdf_loader.py`、`sim_main.py`、`README.md`。

## 影響範圍

- 新增 `firmware/sim_py/`;不影響 C 韌體與 C 模擬器。

## 限制 / 待強化

- 馬達參數（額定/峰值扭矩、額定電流、慣量）為佔位值,需以原廠規格書/實測校正。
- 動力學為單關節單擺近似,未含完整多體動力學耦合。
- URDF DH 自動推導未實作（目前 DH 為佔位,URDF 僅取關節名與對應）。

## 關聯

- `sim-fake-hardware.md`（C 版）、`dual-arm-control-plan.md`、`eyou-phu-motor-analysis.md`
