#!/usr/bin/env python3
"""專案B M2 · 從站掃描（pysoem 版 — EYOU 官方參考主站同款函式庫）。

需求: pip install pysoem；sudo 跑（raw socket）。
SOEM C 版等價指令: sudo ./slaveinfo enp2s0（SOEM test 內建）
IgH 版: ethercat slaves / ethercat pdos
"""
import sys

try:
    import pysoem
except ImportError:
    sys.exit("pip install pysoem 後再跑（或用 SOEM slaveinfo / IgH ethercat slaves）")

iface = sys.argv[1] if len(sys.argv) > 1 else "enp2s0"
m = pysoem.Master()
m.open(iface)
n = m.config_init()
if n <= 0:
    sys.exit(f"{iface}: 沒找到從站（網卡對嗎? sudo 了嗎? 線接了嗎?）")

print(f"found {n} slaves on {iface}:")
for i, s in enumerate(m.slaves, 1):
    print(f"  {i:2d}: {s.name:24s} vendor=0x{s.man:08x} product=0x{s.id:08x} rev=0x{s.rev:08x}")
    # 對照 EYOU ESI XML 確認 PDO 佈局;把 vendor/product 記進你的組態檔
m.close()
