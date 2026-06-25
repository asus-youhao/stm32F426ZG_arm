# WP4 — Task-space 控制器（L3）

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增 L3 task-space 控制層:
- `firmware/control/kinematics.[ch]`：DH 正運動學、幾何 Jacobian、位姿誤差。
- `firmware/control/ik.[ch]`：DLS 逆運動學（單步 + 迭代）。
- `firmware/control/task_space.[ch]`：單臂笛卡爾控制（目標位姿 → IK → joint_space 設定點）。
- `docs/design/wp4-task-space.md`：設計說明。

## 動機 / 背景

依雙臂控制規劃 WP4,完成 task-space 控制器（笛卡爾目標 → 關節目標,1 kHz）。

## 設計重點

- IK 採 DLS（阻尼最小平方）處理 7-DoF 冗餘與奇異點。
- `ik_step` 單步供 1 kHz 即時追隨;`ik_solve` 供離線收斂。
- 旋轉誤差用旋轉矩陣軸角近似（小角度）;大角度建議改四元數（待強化）。
- DH 參數待 WP0.4 實機填入;零空間最佳化、力控（CST/CSF）為後續。

## 影響範圍

- 新增 `firmware/control/kinematics|ik|task_space` 與一份 design 文件。
- 依賴既有 `linalg`、`joint_space`。

## 驗證方式

- 離線：FK→IK 往返收斂、Jacobian 有限差分對照、奇異點阻尼穩定性。

## 關聯

- `dual-arm-control-plan.md`（WP4）、`wp4-task-space.md`、`wp3-joint-space.md`
