# WP5 — 雙臂整合（L4）+ 全棧串接

- 模組：`firmware/control/dual_arm_ctrl.[ch]`、`firmware/app/robot_config.[ch]`、`firmware/app/app_main.c`
- 狀態：程式碼完成（未硬體驗證）

## 1. 全棧資料流（1 kHz）

```
g_jstate(counts 回授)
   │ counts→rad
   ▼
da_ctrl_sync_feedback ──► task_space(L3) 回灌
   │
da_ctrl_tick_1khz (L4)
   ├─ BIMANUAL：右目標 = 左位姿 × 相對變換
   ├─ 安全：自碰撞/最小末端距離 → 必要時 hold
   ├─ task_space×2 (L3 IK 單步) → js_set_setpoint
   └─ joint_space (L2) → counts
   ▼
dual_arm_set_target + dual_arm_tick_1khz (L1 CSP 下發 PDO)
```

## 2. 協同模式（`da_mode_t`）

| 模式 | 行為 |
| ---- | ---- |
| `INDEPENDENT` | 兩臂各自追隨各自笛卡爾目標 |
| `COORDINATED` | 同 INDEPENDENT,但保證同一 1 kHz 週期同步更新（共用時基） |
| `BIMANUAL` | 右臂目標 = 左臂目前位姿 × 相對變換（雙手夾持同物件 / 維持相對約束） |

## 3. 安全（初版）

- **最小末端距離**：兩臂末端位置距離 < `min_ee_distance` → `safety_hold`,維持目前設定點不前進。
- 為簡化版自碰撞;完整版需加入連桿幾何掃掠 / 工作空間邊界（後續強化）。

## 4. 全棧整合（`app_main.c`）

- `app_main_init()`：依序 `dual_arm_init`(L1) → `js_init`(L2) → `ts_init`×2(L3) → `da_ctrl_init`(L4)。
- `app_main_tick()`：1 kHz 全棧 tick（回授回灌 → L4→L3→L2 → L1 下發）。
- 對外 API：`app_set_left_pose / app_set_right_pose / app_set_mode / app_joint_move`（供上位機命令解析）。
- 參數來自 `robot_config.c`（**佔位 DH/限位,待 WP0.4 實機填入**）。

## 5. 待強化

- DH/限位/`counts_per_rad`：以實機量測取代佔位值。
- 旋轉誤差改四元數（大角度）。
- 冗餘臂零空間最佳化（避限位/避奇異/舒適姿態）。
- 自碰撞：連桿層級幾何檢查。
- 力控/阻抗：搭配 CST/CSF（EtherCAT 路徑）。
- 笛卡爾軌跡規劃（大跳變目標前先規劃）。

## 6. 關聯

- L3：`wp4-task-space.md`；L2：`wp3-joint-space.md`；L1：`firmware/app/dual_arm.*`
- 整合：`firmware-cubemx-integration.md`
- 規劃：`dual-arm-control-plan.md`（WP5 / L4）
