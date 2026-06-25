# 新增 STM32F746ZG 雙臂低階控制需求/架構文件

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增設計文件 [`docs/design/dual-arm-low-level-control.md`](../design/dual-arm-low-level-control.md)，以「需求 / 架構概述」層級記錄在 **STM32F746ZG** 上實作 **dual-arm low-level control** 的目標、系統架構、功能模組與待決議項。

## 動機 / 背景

使用者要記錄此 MCU 的實作規劃，主軸為雙臂機器手臂的低階即時控制。先以需求與架構概述定調範圍，作為後續底層設計與通訊協定文件的基礎。

## 影響範圍

- 新增檔案：`docs/design/dual-arm-low-level-control.md`
- 更新 `docs/README.md` 文件索引。
- 不影響任何韌體程式碼或硬體行為（目前為文件階段）。

## 重要標註

- 本 repo 名稱為 `stm32F426ZG_arm`（F426ZG，Cortex-M4F），但本實作目標經使用者確認為 **STM32F746ZG**（Cortex-M7）。兩者為不同晶片，已於設計文件開頭明確標註，避免腳位 / 時脈 / 周邊設定混用。

## 驗證方式

- 確認文件已建立、內容符合「需求/架構概述」層級。
- 確認索引已更新。

## 關聯

- 延續初始化文件規範（見 `2026-06-25-init-project-docs.md`）。
