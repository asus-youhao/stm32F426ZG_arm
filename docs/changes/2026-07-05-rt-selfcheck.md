# 開機自檢（項目 6；WP-L7.1）

- 日期：2026-07-05
- 分支：`feature/h-sdo-bg-escalation`
- 類型：feat + test

## 變更摘要

1. **`firmware/pc/rt_selfcheck.[ch]`**：BOOT 階段（BUS_UP 之前）逐項檢查
   RT 前置條件並輸出自檢清單。必要項：PREEMPT_RT 內核
   （`/sys/kernel/realtime` 或 uname）、`isolcpus`、SCHED_FIFO 權限
   （root 或 RLIMIT_RTPRIO≥80）、mlockall、bus 介面存在
   （SocketCAN；ecat sim 後端跳過）。警告項：`nohz_full/rcu_nocbs`、
   `/dev/cpu_dma_latency` 可寫、cpufreq governor=performance。
2. **pc_master `--rt-strict`**：必要項不過 → 拒絕進 OP（exit 3；L7.1
   驗收語意）。預設（開發機/SIL）列警告後降級運行。
3. 解析核心與 I/O 分離：`sc_cmdline_has`/`sc_version_is_rt` 為純函式
   吃注入字串，單元可測；`rt_selfcheck_run` 才讀 /proc、/sys。

## 動機 / 背景

WP-L7.1：「RT 內核/隔離核/NIC/優先權不符 → 拒絕進 OP」。與
`rt_setup.sh --check`（方案 A 分支，佈建面）互補——自檢是執行期最後
一道門：佈建腳本沒跑、內核 apt 升級後 RT 特性漂移（WP-L7.2 風險）、
或 systemd 服務忘了給 rtprio，都在起機時擋下而不是跑到一半才炸。

## 影響範圍

- 新增：`pc/rt_selfcheck.[ch]`、`tests/test_selfcheck.c`
- 修改：`pc/pc_master_main.c`（BOOT 自檢 + --rt-strict；置於 mlockall
  之前，因探測會 munlockall）、pc/tests 兩個 Makefile
- 控制行為零變化；不帶 --rt-strict 時只多一段自檢輸出。

## 驗證方式

- `firmware/tests` **5048 檢查 0 失敗**（+26）：cmdline 參數邊界
  （`isolcpus=` 帶值、旗標鍵、非邊界子串、`=` 後空值）、PREEMPT_RT
  版本字串判別、報告結構自洽、不存在介面 → bus 必要項 FAIL。
- E2E（本開發機，非 RT）：預設模式印出清單（3 個必要項 FAIL：內核/
  isolcpus/RTPRIO——符合本機實況）後降級運行；`--rt-strict` 拒絕
  啟動 **exit 3**。24.04 PREEMPT_RT 真機（rt_setup.sh 佈建後）應
  全 PASS 進 OP。

## 關聯

- 設計：`linux-rt-ethercat-master-plan.md` WP-L7.1；
  `harness-agent-loop-engine-plan.md` §3.5（環境門檻）
- 配套：`rt_setup.sh`（A 分支 1a2ca5a，佈建面）、trace ring（4c2a65a，
  量測面）——佈建/自檢/量測三件套
