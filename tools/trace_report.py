#!/usr/bin/env python3
"""trace_report.py — pc_master --trace CSV 離線分析（WP-H3 儀器,設計文件 §3.6）

用法：python3 tools/trace_report.py jitter.csv

輸入欄位（pc_master 排水格式）：
    t_us,late_us,read_us,compute_us,write_us,house_us,flags
輸出：tick 數/實際頻率、miss/overrun 統計、各欄 p50/p90/p99/max、
      late 直方圖（桶界與 engine 的 ENG_HIST_BUCKETS 一致）。
驗收門檻（§3.5）：late p99 < 5% 週期——結尾直接判 PASS/FAIL。
純標準函式庫,無 numpy 依賴。
"""
import csv
import sys

HIST_EDGE = [10, 25, 50, 100, 250, 500, 1000]   # µs,與 loop_engine.c 一致
ETR_MISS, ETR_OVERRUN = 1, 2


def pct(sorted_vals, q):
    """最近秩百分位數（nearest-rank）。"""
    if not sorted_vals:
        return 0
    k = max(0, min(len(sorted_vals) - 1, int(q * len(sorted_vals) + 0.5) - 1))
    return sorted_vals[k]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    cols = ["late_us", "read_us", "compute_us", "write_us", "house_us"]
    data = {c: [] for c in cols}
    t_us, miss, overrun = [], 0, 0

    with open(sys.argv[1], newline="") as f:
        for row in csv.DictReader(f):
            t_us.append(int(row["t_us"]))
            fl = int(row["flags"])
            miss += bool(fl & ETR_MISS)
            overrun += bool(fl & ETR_OVERRUN)
            for c in cols:
                data[c].append(int(row[c]))

    n = len(t_us)
    if n < 2:
        print(f"紀錄不足（{n} 筆）,無法分析")
        return 1

    span_s = (t_us[-1] - t_us[0]) / 1e6
    hz = (n - 1) / span_s if span_s > 0 else 0.0
    dt_us = 1e6 / hz if hz > 0 else 0.0
    print(f"ticks={n}  時長={span_s:.2f}s  實際≈{hz:.1f} Hz（週期≈{dt_us:.0f} µs）")
    print(f"miss={miss}（{100.0 * miss / n:.2f}%）  overrun={overrun}")

    print(f"\n{'欄位':<12}{'p50':>8}{'p90':>8}{'p99':>8}{'max':>8}  (µs)")
    for c in cols:
        v = sorted(data[c])
        print(f"{c:<12}{pct(v, 0.50):>8}{pct(v, 0.90):>8}"
              f"{pct(v, 0.99):>8}{v[-1]:>8}")

    print("\nlate 直方圖：")
    hist = [0] * (len(HIST_EDGE) + 1)
    for x in data["late_us"]:
        for i, e in enumerate(HIST_EDGE):
            if x <= e:
                hist[i] += 1
                break
        else:
            hist[-1] += 1
    labels = [f"<={e}" for e in HIST_EDGE] + [f">{HIST_EDGE[-1]}"]
    peak = max(hist) or 1
    for lab, cnt in zip(labels, hist):
        bar = "#" * max(1 if cnt else 0, round(40 * cnt / peak))
        print(f"  {lab:>6} µs  {cnt:>8}  {bar}")

    late_p99 = pct(sorted(data["late_us"]), 0.99)
    lim = 0.05 * dt_us
    ok = late_p99 < lim
    print(f"\n§3.5 門檻：late p99 = {late_p99} µs "
          f"{'<' if ok else '>='} 5% 週期（{lim:.0f} µs）→ {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
