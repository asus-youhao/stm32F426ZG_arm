# 05 · 專案 A：雙臂 CANopen 主站（project-dual-arm.html）

repo 主線（`firmware/`）的完整 walkthrough 頁。

- 架構 SVG：上位機 → F746（TIM6 500Hz + L1~L4 + safety）→ CAN1 左臂 / CAN2 右臂各 7 軸。
- 硬體清單/接線：收發器、120Ω、STO、腳位（引 `firmware/README.md`）。
- 里程碑 M1~M6 = repo 實際的 WP2→WP7（單軸 bring-up → joint-space → task-space →
  雙臂整合 → 安全 → 上位機），每個附 checklist 與對應 `docs/design/wp*.md`。
- 程式碼導覽：建議閱讀順序 + 免硬體跑起來的指令。
- 完成定義（DoD）：含「repo 現狀已達成」與「真機待辦」的區分。
