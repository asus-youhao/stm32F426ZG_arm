# WP3 — Joint-space 控制器（L2）

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增 L2 joint-space 控制層:
- `firmware/control/linalg.[ch]`：小型浮點線代（matmul/transpose/matvec/inverse）。
- `firmware/control/trajectory.[ch]`：單關節軌跡插值（梯形 + 五次多項式）。
- `firmware/control/joint_space.[ch]`：14 軸管理、rad↔counts 換算、限位、1 kHz 設定點輸出。
- `docs/design/wp3-joint-space.md`：設計說明。

## 動機 / 背景

依雙臂控制規劃 WP3,在不依賴硬體下先完成 joint-space 控制器（1 kHz 位置設定點產生器）。

## 設計重點

- CSP 模式下 MCU 負責「每週期位置設定點」,內環由驅動器處理。
- 軌跡:梯形（含三角形退化）+ 五次多項式（平滑、邊界速度/加速度為 0）。
- rad↔counts 換算與關節限位夾制（含外部直接設定保護）。
- linalg 為 WP4 運動學/IK 預備。

## 影響範圍

- 新增 `firmware/control/` 與上述檔案、一份 design 文件。
- 不影響既有 L0/L1。

## 驗證方式

- 程式邏輯/數學檢查（軌跡連續、終點到達、限速、換算）。
- 可離線單元測試軌跡輸出曲線。

## 關聯

- `dual-arm-control-plan.md`（WP3）、`wp3-joint-space.md`
