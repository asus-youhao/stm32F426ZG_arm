# WP5 — 雙臂整合（L4）+ 全棧串接

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

1. 新增 `firmware/control/dual_arm_ctrl.[ch]`：L4 雙臂協同（INDEPENDENT/COORDINATED/BIMANUAL、相對位姿、最小末端距離安全）。
2. 新增 `firmware/app/robot_config.[ch]`：14 軸 js 設定、雙臂 DH、IK 設定、初始姿態（佔位值待 WP0.4）。
3. 改寫 `firmware/app/app_main.c`：整合 L1–L4 全棧 1 kHz 迴圈 + 對外控制 API。
4. 新增 `docs/design/wp5-dual-arm-integration.md`。

## 動機 / 背景

依雙臂控制規劃 WP5,完成 L4 協同與整個 L1–L4 全棧串接（不依賴硬體）。

## 設計重點

- 全棧 1 kHz：回授回灌 → L4 協同 → L3 IK → L2 軌跡 → L1 CSP 下發。
- BIMANUAL：右臂目標由左臂位姿 × 相對變換推得（雙手協同）。
- 安全：兩末端最小距離保護（簡化自碰撞）。
- 對外 API：笛卡爾/關節/模式控制,供上位機命令解析。

## 影響範圍

- 新增 `dual_arm_ctrl`、`robot_config`;改寫 `app_main.c`。
- robot_config 為**佔位參數**（DH/限位/換算）,需實機量測取代。

## 驗證方式

- 離線：模式切換邏輯、BIMANUAL 相對位姿合成、安全 hold 觸發、全棧資料流。
- 實機驗證待 WP0–WP2 通過後分層進行。

## 關聯

- `wp5-dual-arm-integration.md`、`wp4-task-space.md`、`wp3-joint-space.md`、`dual-arm-control-plan.md`
