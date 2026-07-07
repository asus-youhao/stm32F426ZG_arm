# 2026-07-07 · STM32 學習樹教學站（learn/）

## 變更摘要

新增獨立教學分支 `feature/stm32-learn-tree`，在 `learn/` 建立一套靜態 HTML 學習手冊
（與 `esp32_search` 專案的 learn 站同款版型）：

- `index.html` 學習樹總覽（SVG 樹狀圖 + repo 檔案對照表）
- `l1-basic.html` / `l2-mid.html` / `l3-adv.html` — 初階/中階/高階各 6 節
- `project-dual-arm.html` — 最終專案 A：STM32F746 雙臂 CANopen 主站（WP2→WP7 里程碑）
- `project-ecat-master.html` — 最終專案 B：Linux RT EtherCAT 1kHz 主站（M1→M6 里程碑 + 實戰紀錄）
- `career-skills.html` — 職涯技能地圖（6 主幹技能樹 + 初/中/高階 ladder + 國內外職缺對照）
- `assets/style.css` 共用樣式、`learn/docs/00~07-*.md` 各頁說明

## 動機 / 背景

使用者要求：以本 repo（CANopen、EtherCAT、EYOU PHU、CiA402）為範例，做一份
「初階/中階/高階 + 最終 example 里程碑 + 職缺分級」的學習樹教學，形式比照
`../esp32_search` 的 learn HTML 站。

## 影響範圍

- 純新增：`learn/`（教學站）與本文件。**不動任何 firmware 程式碼與既有文件。**
- 獨立分支 `feature/stm32-learn-tree`（自 `develop` 切出），不影響其他功能分支。

## 驗證方式

- 瀏覽器開 `learn/index.html`，檢查各頁互連、SVG 圖渲染、pager 前後頁。
- 依全域 HTML 規則檢查：頁面不含 box-drawing 字元（圖一律 SVG）。
- 教學中引用的 repo 路徑（`firmware/canopen/*`、`firmware/app/*`、`docs/design/wp*.md`）
  均核對存在於 `develop`。

## 關聯

- Branch：`feature/stm32-learn-tree`（來源 `develop`）
- 參考版型：`../esp32_search` 分支 `feature/esp32-learn-examples` 的 `learn/`
- 教材素材：`firmware/`、`docs/design/`、EtherCAT 系列分支的實戰紀錄
