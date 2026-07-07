# 2026-07-07 SE1：SIL-B 1 kHz 基線報告（pc_master --bus ethercat, sim 後端）

## 變更摘要

WP-SE1 驗收：`pc_master --bus ethercat --rate 1000 --trace` 60 秒基線量測 + trace 報表。
純量測與文件，無程式改動。

## 量測環境

- WSL2 Ubuntu 22.04（**非 RT 核心、無 SCHED_FIFO**，RLIMIT_RTPRIO=0）——開機自檢如實回報 3 項必要項不過。
- sim 後端（`ec_master_sim` + phu_sim 14 軸 + `ec_dc_pll`），60 秒 @1 kHz。

## 結果（tools/trace_report.py）

- ticks=60402，實際 1000.0 Hz；miss=21.98%（WSL 排程所致）、overrun=1、drops=0。

| 相位 | p50 | p90 | p99 | max (µs) |
| --- | --- | --- | --- | --- |
| late（tick 延遲） | 75 | 117 | **184** | 3078 |
| read（BUS_RX） | 1 | 3 | **5** | 1005 |
| compute | 2 | 4 | **6** | 257 |
| write（BUS_TX） | 0 | 1 | **1** | 114 |
| housekeep | 0 | 1 | **2** | 270 |

## 結論（進 §8 預算表）

1. **工作負載本身極輕**：14 軸 exchange + L1-L4 控制 + housekeeping 四相位 p99 合計 ≈ **14 µs**
   ——對 F746（216 MHz，約 PC 單核 1/10~1/20 效能）粗估 p99 仍在 **150~300 µs** 量級，
   1 ms 週期預算充足；板端實測是 SE4 驗收。
2. **late p99=184 µs FAIL（§3.5 門檻 50 µs）完全是 WSL 非 RT 環境**：同一程式在
   Ubuntu 24.04 PREEMPT_RT 真機為 **p99=27 µs PASS**（見 2026-07-06 RT 驗證報告）。
   WSL 只做功能性 SIL，RT 數字以 RT 主機與板端為準。
3. **escalation 順帶演練**：miss 視窗超限 → harness 依 §5.2 升級 SAFE_STOP、
   sys=ESTOP，行為與設計一致（掉軸/WKC 短少注入已在 test_ecat/test_h5 單元測試覆蓋）。

## 驗證方式

```bash
# WSL：stdin 需撐住（EOF 會讓主控台迴圈提前退出）
tail -f /dev/null | ./pc_master --bus ethercat --rate 1000 --trace ~/silb.csv --seconds 60
python3 tools/trace_report.py ~/silb.csv
```

## 關聯

- Branch：`feature/stm32-ethercat-master`
- 規劃：`docs/design/stm32-ethercat-master-plan.md` §5（SE1）、§8（CPU 餘裕風險）
- RT 對照：`docs/changes/2026-07-06-u24-rt-verification.md`（1 kHz p99=27 µs PASS）
