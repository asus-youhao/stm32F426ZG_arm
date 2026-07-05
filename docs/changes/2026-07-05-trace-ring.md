# trace ring + 離線報表（項目 3；WP-H3 儀器面）

- 日期：2026-07-05
- 分支：`feature/h-sdo-bg-escalation`
- 類型：feat + test + tools

## 變更摘要

1. **`firmware/engine/eng_trace.[ch]`**：trace ring（設計文件 §3.6）——
   engine 於每個 `eng_tick` 尾端寫一筆固定紀錄（24 B：喚醒時戳、遲到量、
   四相位總耗時、miss/overrun 旗標），無格式化、無 I/O。記憶體由呼叫端
   提供（PC 8192 筆、F746 可給小環）；未 init 時寫入為 no-op。
   滿了丟新留舊計 drop——已存的連續段不破洞，從尾端截斷。
2. **`loop_engine.c`**：`run_phase` 回傳相位總耗時供 trace；預算連續
   超標（`budget_over_n`）通知 on_fault 的同時記 `ELC_BUDGET_OVER`
   log（a=agent idx、b=耗時）——§3.6「那 300 µs 去哪了」的上下文。
3. **pc_master `--trace FILE`**：主執行緒 20 ms 週期把 trace ring 排水成
   CSV（buffered stdio，離 RT 路徑）；結束時收尾排空並報 drop 數。
4. **`tools/trace_report.py`**（純標準函式庫）：離線分析——tick 數/
   實際頻率、miss/overrun、各欄 p50/p90/p99/max、late 直方圖（桶界與
   engine 一致），結尾按 §3.5 門檻（late p99 < 5% 週期）判 PASS/FAIL。

## 動機 / 背景

WP-H3 的驗收（PREEMPT_RT 上 late p99 < 5% 週期、每相位 histogram 報告）
需要日常儀表：cyclictest 量的是內核喚醒延遲，不是「控制迴圈實際哪個
相位吃掉時間」。trace ring 是常駐低成本（一筆 24 B）的自量儀器，
ftrace/LTTng 只留給異常調查期。

## 影響範圍

- 新增：`engine/eng_trace.[ch]`、`tests/test_trace.c`、`tools/trace_report.py`
- 修改：`engine/loop_engine.c`（相位耗時匯總 + trace push + BUDGET_OVER
  log）、`engine/eng_log.[ch]`（+`ELC_BUDGET_OVER`）、
  `pc/pc_master_main.c`（--trace 排水）、tests/pc 兩個 Makefile
- 控制行為零變化；未給 `--trace` 時 trace 停用（engine 只多一個
  bool 檢查）。板端不連 `loop_engine.c`（H6 未做），不受影響。

## 驗證方式

- `firmware/tests` **5022 檢查 0 失敗**（+49）。test_trace 覆蓋：未啟用
  no-op、滿→丟新+drop、FIFO/欄位、engine 整合（準時 late=0/相位耗時
  正確、miss/overrun 旗標與 late 值、>65535 飽和、預算連續超標→恰一筆
  BUDGET_OVER 上下文）。
- E2E（本機非 RT）：`pc_master --bus ethercat --seconds 5 --trace jitter.csv`
  → 4994 筆、drops=0；`trace_report.py` 輸出：compute p99=6 µs（控制棧
  便宜）、late p99=248 µs（全是排程抖動）→ §3.5 判 FAIL——符合預期
  （無 SCHED_FIFO 權限的開發機）。同一條命令在 24.04 PREEMPT_RT 機上
  重跑即為 **WP-H3 驗收報告**。

## 關聯

- 設計：`harness-agent-loop-engine-plan.md` §3.5/§3.6、WP-H3
- 前置：`6770e44`（log ring；BUDGET_OVER 走同一條 log 通道）
- 後續：真機 H3 驗收（rt_setup.sh --check + cyclictest + 本儀器）
