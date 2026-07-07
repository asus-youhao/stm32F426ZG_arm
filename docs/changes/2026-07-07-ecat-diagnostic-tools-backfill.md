# 補齊 SAFEOP 診斷工具進 repo（wd_read / phu_state ×2）

- 日期：2026-07-07
- 分支：`feature/linux-rt-ethercat-master`
- 類型：tooling（補入未提交的診斷程式）

## 變更摘要

盤點 gx701 後，把 SAFEOP 卡點診斷過程寫的、但先前未提交的三支
SOEM 診斷程式補進 `tools/ecat_bringup/`：

| 檔案 | 用途 |
| --- | --- |
| `wd_read.c` | 讀 ESC 看門狗暫存器 + 高速輪詢 AL 狀態抓彈跳——**測到「進 SAFEOP 後 0.5 ms 自貶 PREOP、AL 0x0022」的決定性證據就是這支** |
| `phu_state.c` | 逐級 PREOP→SAFEOP→OP，每級讀 AL code；`lsa` 試 logical start addr |
| `phu_state2.c` | 讀 SM sync 模式（0x1C32:01=1）+ PREOP 先啟 SYNC0 |

## 動機 / 背景

使用者要求盤點「gx701 上測 EtherCAT、但沒上 repo 的東西」。結果：

- 已在 repo 的 4 支工具（sdo_probe/phu_jog/igh_probe/igh_jog）皆為最新版。
- 上述 3 支診斷程式**從未提交**——它們是 SAFEOP 三輪診斷（635bc3e/
  d4ade8d/753ab10）背後的實際工具，`wd_read.c` 更是抓到 AL 0x0022
  彈跳證據的那支。補齊以確保診斷可完整重現。

## 未收錄項（一併記錄，避免日後再問）

- `phu_safeop.c`：per-slave SAFEOP 嘗試，但 `ecx_FPRD` 傳參編譯錯；
  功能被 `wd_read.c` 涵蓋 → **捨棄**。
- `/tmp/fmmuA.bin`、`fmmuB.bin`：FMMU 暫存器實驗的 64 B payload，
  丟棄型，診斷文件內有 `printf "\x..."` 指令可重現 → 不收。
- **`h3_1khz.csv`、`can_jitter.csv`（無法救回）**：H3 抖動與 CANopen
  SIL 的逐 tick 原始 CSV，isolcpus 重開機時隨 `/tmp` 清除。摘要
  （H3 late p99=27 µs 等）已存於 `2026-07-06-u24-rt-verification.md`，
  但原始資料已失。**教訓**：真機量測資料應即時拉回 repo 或 scratchpad，
  勿留 `/tmp`（重開機即失）。

## 影響範圍

- 新增：`tools/ecat_bringup/{wd_read,phu_state,phu_state2}.c`
- 修改：`tools/ecat_bringup/README.md`（工具表補三列 + 捨棄說明）
- 純診斷工具，不影響韌體/主站程式。

## 驗證方式

- 與本地 scratchpad 最新版逐支 `diff` 確認為最終版本。
- `wd_read.c` 於 gx701 實測可編譯執行（產出彈跳計時輸出，見診斷文件）。

## 關聯

- 診斷結論：`2026-07-07-phu17-safeop-diagnosis.md`（三輪 → 0x2100=2）
- 工具總覽：`tools/ecat_bringup/README.md`
