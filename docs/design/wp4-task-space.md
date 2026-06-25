# WP4 — Task-space 控制器（L3, 1 kHz）

- 模組：`firmware/control/kinematics.[ch]`、`ik.[ch]`、`task_space.[ch]`
- 狀態：程式碼完成（未硬體驗證）

## 1. 角色

把**笛卡爾目標位姿**轉成**關節目標**,再交給 joint-space（L2）下發。

```
笛卡爾目標 pose → task_space (IK 單步 DLS, 1kHz) → js_set_setpoint() → joint_space → dual_arm
```

## 2. 模組

- **kinematics**：
  - `kin_fk()`：標準 DH 正運動學（7 軸）→ 末端位姿（位置 + 旋轉矩陣）。
  - `kin_jacobian()`：幾何 Jacobian（6×7,線速度 + 角速度）。
  - `kin_pose_error()`：6 維位姿誤差（位置差 + 軸角旋轉差）。
- **ik（DLS）**：
  - `dq = Jᵀ (J Jᵀ + λ²I)⁻¹ e`,阻尼處理奇異點、適合 7-DoF 冗餘臂。
  - `ik_step()`：單步（1 kHz 即時追隨）。
  - `ik_solve()`：迭代收斂（離線/規劃）。
- **task_space**：
  - 單臂封裝（DH + IK 設定 + joint_space 起始索引）。
  - `ts_set_target()` 設目標、`ts_tick_1khz()` 每週期 IK 一步 → 寫 joint_space。

## 3. 1 kHz 串接

```c
ts_sync_q(&arm, q_fb);            // 由回授回灌目前關節角
ts_set_target(&arm, &pose_des);  // 設笛卡爾目標
float err = ts_tick_1khz(&arm);  // IK 單步 → js_set_setpoint
js_tick_1khz(cnt);               // joint_space 產生 counts
// → dual_arm_set_target + dual_arm_tick
```

## 4. 設計選擇與限制

- **CSP 位置層 IK**：每 tick 走一步 DLS,追隨平滑的笛卡爾目標即可收斂;劇烈跳變的目標應先在笛卡爾空間做軌跡規劃（後續可加 Cartesian 軌跡產生器）。
- **旋轉誤差**用旋轉矩陣軸角近似（小角度有效）;大角度建議改用四元數誤差（待強化）。
- **冗餘（7-DoF）**：目前 DLS 主任務求解;零空間最佳化（避關節限位/避奇異/舒適姿態）為後續強化項。
- **DH 參數**：`arm_kin_t.dh[7]` 需依實機機構填入（連桿長度/扭轉/偏移）— 來自 WP0.4。
- **力控**：本層為位置型 task-space;阻抗/力控需搭配 CST/CSF（EtherCAT 路徑）後續整合。

## 5. 驗證（離線）

- 給定 q → `kin_fk` → 設為 target → `ik_solve` 應收斂回近似 q。
- Jacobian 數值微分對照（finite-difference）檢查。
- 奇異點附近 λ 阻尼穩定性。

## 6. 關聯

- 上游：`wp5-dual-arm-integration.md`
- 下游：`wp3-joint-space.md`
- 規劃：`dual-arm-control-plan.md`（WP4）
