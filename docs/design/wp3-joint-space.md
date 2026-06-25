# WP3 — Joint-space 控制器（L2, 1 kHz）

- 模組：`firmware/control/trajectory.[ch]`、`joint_space.[ch]`、`linalg.[ch]`
- 狀態：程式碼完成（未硬體驗證）

## 1. 角色

在 CSP 模式下,關節驅動器內部負責位置/速度/轉矩三閉環;**MCU 的 joint-space 層負責「產生每個 1 kHz 週期的位置設定點」** 並做單位換算與限位。

```
上層(task-space/命令) → js_move_to()/js_set_setpoint()
        │
   joint_space (1kHz)  ── 軌跡插值 → 限位 → rad→counts
        │
   dual_arm (L1) ── PDO CSP 下發 counts
```

## 2. 功能

- **軌跡插值**（`trajectory.c`）：
  - 梯形速度剖面（依 `vmax/amax` 自動算時程,含三角形退化）。
  - 五次多項式（指定時程 T,起終速度/加速度為 0,平滑）。
- **多軸管理**（`joint_space.c`,14 軸）：
  - `js_move_to()` 點到點、`js_move_to_quintic()`、`js_set_setpoint()`（外部直接串接）。
  - `rad ↔ counts` 換算（`counts_per_rad`、`offset_rad` 每軸可設）。
  - 關節限位 `q_min/q_max` 夾制（即使外部直接設定也保護）。
  - `js_tick_1khz()` 推進全部軌跡,輸出每軸 counts 目標。
- **小型線代**（`linalg.c`,供 WP4 用）：matmul/transpose/matvec/inverse（Gauss-Jordan）。

## 3. 與 1 kHz 迴圈接法

```c
int32_t cnt[JS_TOTAL_JOINTS];
js_tick_1khz(cnt);                       // 產生 counts 目標
for (int j=0;j<14;j++) dual_arm_set_target(j, cnt[j]);
dual_arm_tick();                    // PDO 下發 + 收回授
// 回授回灌：
for (int j=0;j<14;j++) js_update_feedback(j, g_jstate[j].pos_actual);
```

## 4. 單位換算說明

- `counts_per_rad`：依驅動器 user-unit / 編碼器解析度設定。
  - EYOU 輸出端 19-bit → 每轉 524288 counts → `counts_per_rad ≈ 524288/(2π) ≈ 83443`（實際以驅動 OD 設定為準）。
- `offset_rad`：機械零點與邏輯零點偏移（搭配 HM 回零）。

## 5. 待辦 / 注意

- 目前為「位置設定點產生器」;若要在 MCU 端再加外環校正（PD/前饋）可於 `js_tick_1khz` 擴充（CSP 下通常不需要）。
- 限速/限加速僅在點到點軌跡規劃時生效;外部直接設定點模式請自行確保平滑。
- 測試：可離線單元測試 `traj_step()` 輸出曲線（位置連續、終點到達、限速）。

## 6. 關聯

- 上游：`wp4-task-space.md`（task-space 產生關節目標）
- 下游：`firmware/app/dual_arm.*`（L1 CSP 下發）
- 規劃：`dual-arm-control-plan.md`（WP3）
