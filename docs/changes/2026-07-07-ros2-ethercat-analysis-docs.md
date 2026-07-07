# ROS2/EtherCAT 兩份分析文件（md + html）

- 日期：2026-07-07
- 分支：`feature/linux-rt-ethercat-master`
- 類型：docs

## 變更摘要

把近期兩個 ROS2/EtherCAT 分析問題各自寫成獨立文件，每題一份 md
（`docs/design/`）＋一份 self-contained html（`docs/`，比照既有
`eyou-phu-protocols.html`）：

1. **ethercat_driver_ros2 vs 本 repo（只談 EtherCAT）**
   - `docs/design/ethercat-driver-ros2-vs-repo.md`
   - `docs/ethercat-driver-ros2-vs-repo.html`
2. **ros2_control 控制指令通訊路徑（JTC/FPC → 馬達走 DDS/topic/SHM？500 Hz 會有問題嗎）**
   - `docs/design/ros2-control-comm-path.md`
   - `docs/ros2-control-comm-path.html`

## 內容重點

- 對比：ethercat_driver_ros2 = IgH-only、ROS2/Linux 限定、YAML 宣告式、
  EtherCAT 唯一；本 repo = ec_master.h 三後端門面、可攜 F746、CANopen+
  EtherCAT 共堆疊。三結論含「它也是 IgH → 一樣撞 0x2100=2 牆」。
- 通訊路徑：controller↔ethercat_driver 是**同 process 共享記憶體
  （double 參考）非 DDS**；DDS 只在 JTC 一次性收軌跡時用。**JTC @ 500 Hz
  無問題**；只有 FPC 外部逐拍串流才每拍走 DDS。gx701 實測 = JTC @ 500 Hz
  + SCHED_FIFO。

## 影響範圍

- 新增 4 檔（2 md + 2 html）；純文件，不影響程式。
- HTML 依全域規則：內嵌 SVG 畫圖（層次堆疊圖、RT 迴圈+SHM/DDS 邊界圖）、
  light/dark 主題感知、無 box-drawing 字元；`<pre>`/`code` 僅用於程式/
  物件索引。playwright 實際渲染確認 SVG 正常、交叉連結正確。

## 驗證方式

- `grep` 確認無 box-drawing 字元。
- playwright 開兩頁截圖：SVG 圖、表格、卡片、判定框、交叉連結皆正確渲染。

## 關聯

- 依據對話分析；資料源自 gx701 `gallop_ws/src/ethercat_driver_ros2`、
  `phu_controller_ros2/config`（JTC @ 500 Hz）
- 關聯：`ethercat-coe-master-plan.md`、`harness-agent-loop-engine-plan.md`、
  `2026-07-07-phu17-safeop-diagnosis.md`
