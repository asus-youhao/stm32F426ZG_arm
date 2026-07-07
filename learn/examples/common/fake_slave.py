#!/usr/bin/env python3
"""fake_slave.py — 迷你 CANopen/CiA402 假關節從站（純 stdlib，走 SocketCAN）。

模擬一顆 EYOU PHU 關節的最小行為，給 L2/專案A 範例當對手：
  - Heartbeat：0x700+id，100ms 一次（NMT 狀態）
  - SDO server：0x600+id 收、0x580+id 回（expedited，含小型物件字典）
  - RPDO1 0x200+id：收 Controlword + Target position
  - TPDO1 0x180+id：100Hz 發 Statusword + Position actual（一階慣性跟隨目標）
  - CiA402 狀態機：照 controlword 0x06→0x07→0x0F 使能交握

用法：
  python3 fake_slave.py [--iface vcan0] [--node 1] [--rate 100]
  多軸： for n in 1 2 3 4 5 6 7; do python3 fake_slave.py --node $n & done
"""
import argparse
import socket
import struct
import time

CAN_FMT = "<IB3x8s"  # can_id, dlc, pad, data


def mkframe(can_id, data):
    return struct.pack(CAN_FMT, can_id, len(data), bytes(data).ljust(8, b"\0"))


class Cia402:
    """最小 CiA402 狀態機：statusword 依 controlword 演進。"""

    SW = {  # state -> statusword 樣板
        "SWITCH_ON_DISABLED": 0x0040,
        "READY_TO_SWITCH_ON": 0x0021,
        "SWITCHED_ON":        0x0023,
        "OPERATION_ENABLED":  0x0027,
        "FAULT":              0x0008,
    }

    def __init__(self):
        self.state = "SWITCH_ON_DISABLED"

    def on_controlword(self, cw):
        if cw & 0x80:                      # fault reset
            self.state = "SWITCH_ON_DISABLED"
            return
        low = cw & 0x0F
        if low == 0x06:
            self.state = "READY_TO_SWITCH_ON"
        elif low == 0x07 and self.state in ("READY_TO_SWITCH_ON", "OPERATION_ENABLED"):
            self.state = "SWITCHED_ON"
        elif low == 0x0F and self.state in ("SWITCHED_ON", "OPERATION_ENABLED"):
            self.state = "OPERATION_ENABLED"
        elif low == 0x00:
            self.state = "SWITCH_ON_DISABLED"

    def statusword(self):
        return self.SW[self.state]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="vcan0")
    ap.add_argument("--node", type=int, default=1)
    ap.add_argument("--rate", type=float, default=100.0, help="TPDO 頻率 Hz")
    a = ap.parse_args()
    nid = a.node

    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.bind((a.iface,))
    s.settimeout(0.002)

    od = {  # (index, sub) -> value，讀不到的回 abort
        (0x1000, 0): 0x00020192,   # device type: 402 profile
        (0x6041, 0): 0x0040,
        (0x6060, 0): 0,
        (0x6064, 0): 0,
        (0x6081, 0): 0,
    }
    sm = Cia402()
    target = 0
    pos = 0.0
    t_hb = t_pdo = 0.0
    print(f"[slave {nid}] up on {a.iface} (heartbeat/SDO/PDO)")

    while True:
        try:
            frame = s.recv(16)
            can_id, dlc, data = struct.unpack(CAN_FMT, frame)
            can_id &= 0x7FF
            d = data[:dlc]
            if can_id == 0x000 and dlc >= 2:            # NMT
                if d[1] in (0, nid):
                    print(f"[slave {nid}] NMT cmd 0x{d[0]:02x}")
            elif can_id == 0x600 + nid and dlc == 8:    # SDO request
                cs, idx, sub = d[0], d[1] | d[2] << 8, d[3]
                if cs & 0xE0 == 0x20:                   # download(write)
                    od[(idx, sub)] = int.from_bytes(d[4:8], "little")
                    s.send(mkframe(0x580 + nid,
                                   struct.pack("<BHB4x", 0x60, idx, sub)))
                else:                                    # upload(read)
                    if (idx, sub) == (0x6041, 0):
                        od[(idx, sub)] = sm.statusword()
                    if (idx, sub) == (0x6064, 0):
                        od[(idx, sub)] = int(pos) & 0xFFFFFFFF
                    if (idx, sub) in od:
                        s.send(mkframe(0x580 + nid, struct.pack(
                            "<BHBI", 0x43, idx, sub, od[(idx, sub)] & 0xFFFFFFFF)))
                    else:                                # abort 0x06020000
                        s.send(mkframe(0x580 + nid, struct.pack(
                            "<BHBI", 0x80, idx, sub, 0x06020000)))
            elif can_id == 0x200 + nid and dlc >= 6:    # RPDO1: cw + target
                cw, tgt = struct.unpack("<hi", d[0:6])
                sm.on_controlword(cw & 0xFFFF)
                target = tgt
        except socket.timeout:
            pass

        now = time.monotonic()
        if now - t_hb >= 0.1:                            # heartbeat: operational
            t_hb = now
            s.send(mkframe(0x700 + nid, b"\x05"))
        if now - t_pdo >= 1.0 / a.rate:                  # TPDO1: sw + pos
            t_pdo = now
            if sm.state == "OPERATION_ENABLED":          # 一階慣性跟隨
                pos += (target - pos) * 0.2
            s.send(mkframe(0x180 + nid,
                           struct.pack("<Hi", sm.statusword(), int(pos))))


if __name__ == "__main__":
    main()
