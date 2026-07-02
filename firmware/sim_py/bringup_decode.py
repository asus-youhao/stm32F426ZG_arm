"""
bringup_decode.py — 單獨解碼 F746 主站「開機發了哪些 cmd / 讀了哪些」

它在 COB-ID 0x601 上當 node-1 從站回應（讓主站能把整個 bring-up 序列跑完），
同時把**主站→從站每一幀**翻成人看得懂的一行：READ/WRITE 哪個物件字典、名稱、值。
自帶 CiA402 物件名稱表，不依賴 phu_od（兩分支版本分歧）。

  python bringup_decode.py                    # 預設 slcan / COM11 / 1Mbps / node 1

用法：先跑本程式 → 按 F746 RESET（或用 ST-Link 重置）→ 它自動抓完印表格。
"""
import argparse
import sys
import time

from can_slave import CiA402Slave, SDO_RX, SDO_TX, RPDO1, NMT

NMT_CMD = {0x01: "START (operational)", 0x02: "STOP", 0x80: "PRE-OPERATIONAL",
           0x81: "RESET-NODE", 0x82: "RESET-COMMUNICATION"}
MODE = {1: "PP", 3: "PV", 4: "PT", 6: "HM", 8: "CSP", 9: "CSV", 10: "CST", 13: "CSF"}
CW = {0x0000: "disable voltage", 0x0002: "quick stop", 0x0006: "shutdown",
      0x0007: "switch on", 0x000F: "enable operation", 0x001F: "enable+start-homing",
      0x0080: "fault reset", 0x010F: "halt"}

# 自帶：bring-up 常見物件名稱（CiA402 + EYOU 特有）
NAMES = {
    0x1000: "Device type", 0x1001: "Error register", 0x1017: "Producer heartbeat",
    0x100B: "Node-ID", 0x1018: "Identity object",
    0x603F: "Error Code", 0x6040: "Control word", 0x6041: "Status word",
    0x6060: "Modes Of Operation", 0x6061: "Modes Of Op Display",
    0x6062: "Position Demand", 0x6063: "Position Actual Internal",
    0x6064: "Position Actual Value", 0x606C: "Velocity Actual Value",
    0x6071: "Target Torque", 0x6072: "Max Torque", 0x6077: "Torque Actual Value",
    0x607A: "Target Position", 0x607C: "Home Offset",
    0x6081: "Profile Velocity", 0x6083: "Profile Acceleration",
    0x6084: "Profile Deceleration", 0x6085: "Quick Stop Decel",
    0x6087: "Torque Slope", 0x6098: "Homing Method", 0x6099: "Homing Speeds",
    0x60FF: "Target Velocity", 0x6502: "Supported Drive Modes",
    0x26A0: "Node-ID (EYOU)", 0x26A1: "CAN Baudrate (EYOU)",
}
SIGNED = {0x6060, 0x6061, 0x6062, 0x6063, 0x6064, 0x606C,
          0x6071, 0x6077, 0x607A, 0x607C}


def od_name(index):
    return NAMES.get(index, "?")


def _signed(index, v):
    if index in SIGNED and v >= 0x80000000:
        return v - 0x100000000
    return v


def _fmt_val(index, v):
    sv = _signed(index, v)
    extra = ""
    if index == 0x6060:
        b = v & 0xFF
        b = b - 0x100 if b >= 0x80 else b
        extra = "  <%s>" % MODE.get(b, "?")
    elif index == 0x6040:
        extra = "  <%s>" % CW.get(v & 0xFFFF, "0x%04X" % (v & 0xFFFF))
    elif index == 0x6041:
        extra = "  <statusword>"
    elif index == 0x26A1:
        extra = "  <%d bps>" % v
    return "%d (0x%X)%s" % (sv, v, extra)


def main():
    ap = argparse.ArgumentParser(description="解碼 F746 主站 bring-up 序列")
    ap.add_argument("--interface", default="slcan")
    ap.add_argument("--channel", default="COM11")
    ap.add_argument("--bitrate", type=int, default=1000000)
    ap.add_argument("--node", type=int, default=1)
    ap.add_argument("--idle", type=float, default=3.0, help="開始後靜默幾秒視為結束")
    ap.add_argument("--model", default="PHU20")
    args = ap.parse_args()

    try:
        import can
    except ImportError:
        sys.exit("需要 python-can：pip install python-can pyserial")

    slave = CiA402Slave(args.node, args.model, "L_J%d" % args.node)
    bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)
    print("bus open %s %s @%d bps, responding as node %d"
          % (args.interface, args.channel, args.bitrate, args.node))
    print(">>> 現在按 F746 RESET（或用 ST-Link 重置）。等待主站封包...\n")

    log = []
    t0 = None
    last = time.time()
    step = 0
    try:
        while True:
            msg = bus.recv(timeout=0.2)
            now = time.time()
            if msg is None:
                if t0 is not None and now - last > args.idle:
                    break
                continue
            if msg.is_extended_id:
                continue
            arb, data = msg.arbitration_id, list(msg.data)
            resp = slave.handle_frame(arb, data)

            line = None
            if arb == NMT and len(data) >= 2:
                if data[1] in (0, args.node):
                    line = ("NMT", "NMT   %s" % NMT_CMD.get(data[0], "0x%02X" % data[0]))
            elif arb == SDO_RX + args.node and len(data) >= 4:
                cs = data[0]
                index = data[1] | (data[2] << 8)
                sub = data[3]
                nm = od_name(index)
                if (cs & 0xE0) == 0x40:                 # 主站「讀」
                    val = None
                    for a, d in resp:
                        if a == SDO_TX + args.node:
                            val = d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)
                    vs = _fmt_val(index, val) if val is not None else "?"
                    line = ("READ", "READ  0x%04X:%02X %-22s = %s" % (index, sub, nm, vs))
                elif (cs & 0xE0) == 0x20:               # 主站「寫」
                    val = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
                    line = ("WRITE", "WRITE 0x%04X:%02X %-22s = %s"
                            % (index, sub, nm, _fmt_val(index, val)))
            elif arb == RPDO1 + args.node:
                cw = data[0] | (data[1] << 8) if len(data) >= 2 else 0
                line = ("PDO", "RPDO1 CW=0x%04X <%s>" % (cw, CW.get(cw, "?")))

            if line:
                for a, d in resp:
                    bus.send(can.Message(arbitration_id=a, data=bytes(d), is_extended_id=False))
                if t0 is None:
                    t0 = now
                step += 1
                dt_ms = (now - t0) * 1000.0
                log.append((dt_ms, line[0], line[1]))
                print("  %2d  [%7.1f ms]  %s" % (step, dt_ms, line[1]))
                last = now
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()

    reads = [l for l in log if l[1] == "READ"]
    writes = [l for l in log if l[1] == "WRITE"]
    nmts = [l for l in log if l[1] == "NMT"]
    pdos = [l for l in log if l[1] == "PDO"]
    print("\n================ F746 master bring-up sequence summary ================")
    print("total=%d  |  NMT=%d  READ=%d  WRITE=%d  PDO=%d"
          % (len(log), len(nmts), len(reads), len(writes), len(pdos)))
    print("\n-- master READ these objects at boot --")
    for _, _, t in reads:
        print("   " + t)
    print("\n-- master WROTE these objects at boot --")
    for _, _, t in writes:
        print("   " + t)
    if not log:
        print("\n(no master frames — check F746 powered, reset done, wiring/COM correct)")


if __name__ == "__main__":
    main()
