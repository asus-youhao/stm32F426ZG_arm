#!/usr/bin/env python3
"""專案B M3 · PREOP 下的 CoE SDO 組態（對照 project-ecat-master.html M3）。

只碰手冊寫明的標準 402 物件 — 0x2100 這類廠商物件別碰(本 repo 實戰教訓)。
需求: pip install pysoem; sudo。
"""
import struct
import sys

try:
    import pysoem
except ImportError:
    sys.exit("pip install pysoem")

iface = sys.argv[1] if len(sys.argv) > 1 else "enp2s0"
m = pysoem.Master()
m.open(iface)
assert m.config_init() > 0, "no slaves"

for i, s in enumerate(m.slaves, 1):
    dev_type = struct.unpack("<I", s.sdo_read(0x1000, 0))[0]
    print(f"slave {i}: 0x1000={dev_type:#010x}")

    s.sdo_write(0x6060, 0, bytes([8]))              # CSP
    mode = s.sdo_read(0x6061, 0)[0]                  # modes display 讀回驗證
    print(f"  0x6060=8(CSP) -> 0x6061 read-back = {mode}")

    s.sdo_write(0x60C2, 1, bytes([1]))               # 插補週期 1
    s.sdo_write(0x60C2, 2, struct.pack("<b", -3))    # 10^-3 s → 1 ms,要=DC 週期
    print("  0x60C2 = 1ms (interpolation time, 需與 DC SYNC0 週期一致)")

m.close()
print("PREOP 組態完成 — 下一步 m4: SAFEOP→OP + DC")
