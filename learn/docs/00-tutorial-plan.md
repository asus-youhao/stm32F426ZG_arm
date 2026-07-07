# STM32 學習手冊 — 教學規劃

分支：`feature/stm32-learn-tree`（獨立教學分支，不動 firmware 程式碼）。

## 目標

以本 repo（STM32F746 雙臂 CANopen/EtherCAT + EYOU PHU + CiA402）為活教材，
做一套與 `esp32_search` 的 learn 站同款的靜態 HTML 學習手冊：

- 學習樹總覽（SVG 樹狀圖，禁 ASCII 線條圖）
- L1 初階 / L2 中階 / L3 高階，各 6 節
- 兩條最終專案路線（= repo 實際走過的兩條路線）
- 職涯技能地圖（技能樹 + 初/中/高階 + 國內外職缺對照）

## 頁面結構

| 頁 | 內容 | 說明檔 |
| --- | --- | --- |
| `index.html` | 學習樹總覽 + repo 對照表 | `01-learning-tree.md` |
| `l1-basic.html` | L1 初階 6 節（MCU/工具鏈/GPIO/UART/時脈中斷/Timer 節拍） | `02-l1-basic.md` |
| `l2-mid.html` | L2 中階 6 節（CAN/bxCAN/CANopen/SDO/PDO/頻寬/模擬測試） | `03-l2-mid.md` |
| `l3-adv.html` | L3 高階 6 節（CiA402/CSP/運動學/安全/EtherCAT/Linux RT） | `04-l3-adv.md` |
| `project-dual-arm.html` | 專案 A：雙臂 CANopen 主站（firmware/ 主線） | `05-project-dual-arm.md` |
| `project-ecat-master.html` | 專案 B：Linux RT EtherCAT 1kHz 主站 | `06-project-ecat-master.md` |
| `career-skills.html` | 職涯技能地圖 | `07-career-skills.md` |

## 原則

1. 每一節都連回 repo 真實檔案（`firmware/...`、`docs/design/...`），不虛構範例。
2. 程式碼區塊只放程式碼；圖一律 SVG（遵守全域 HTML 規則）。
3. repo 踩過的坑（頻寬、fb_fresh 看門狗 bug、0x2100、EtherCAT 控制權）當一級教材。
