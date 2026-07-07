#!/usr/bin/env python3
"""專案B M4 · SAFEOP→OP + DC 同步（對照 project-ecat-master.html M4）。

卡 SAFEOP 時：讀 AL status code 講證據（本 repo 實戰: wd_read/phu_state 工具）。
需求: pip install pysoem; sudo。
"""
import sys
import time

try:
    import pysoem
except ImportError:
    sys.exit("pip install pysoem")

iface = sys.argv[1] if len(sys.argv) > 1 else "enp2s0"
m = pysoem.Master()
m.open(iface)
assert m.config_init() > 0, "no slaves"

m.config_map()                                   # PDO -> IOmap
for s in m.slaves:
    s.dc_sync(act=True, sync0_cycle_time=1_000_000)   # SYNC0 = 1ms

m.state_check(pysoem.SAFEOP_STATE, timeout=2_000_000)
print("all SAFEOP, requesting OP...")

m.state = pysoem.OP_STATE
m.send_processdata(); m.receive_processdata(2000)     # OP 前先送一輪
m.write_state()

for _ in range(40):                              # 最多等 2 秒
    m.state_check(pysoem.OP_STATE, timeout=50_000)
    if m.state_check(pysoem.OP_STATE, 50_000) == pysoem.OP_STATE:
        break
    m.send_processdata(); m.receive_processdata(2000)
    time.sleep(0.001)

if m.state_check(pysoem.OP_STATE, 50_000) == pysoem.OP_STATE:
    print("OP ✅ — working counter 應 = 期望值,開始 1kHz 交換(見 m5)")
else:
    print("卡住了 — AL status 逐站排查:")
    m.read_state()
    for i, s in enumerate(m.slaves, 1):
        print(f"  slave {i}: state=0x{s.state:02x} al_status=0x{s.al_status:04x}")
        # 0x001B=SM watchdog、0x001D/1E=無效 SM/輸出組態... 對照 ETG.1000
m.close()
