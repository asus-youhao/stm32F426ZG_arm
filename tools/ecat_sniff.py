#!/usr/bin/env python3
"""ecat_sniff.py — EtherCAT(0x88A4) 抓包 + 協定解碼 → JSON（Wireshark 式 UI 的資料源）

用法（需 CAP_NET_RAW,G16 用 python3-rawnet）：
  python3-rawnet tools/ecat_sniff.py --iface enx00e04c6809b6 --seconds 18 \
      --out /tmp/ecat_capture.json [--max 2500]

方向判定：AF_PACKET pkttype==PACKET_OUTGOING(4) → 本機假從站回幀;
其餘（BROADCAST/HOST）→ 板端主站發幀。
解碼層級：EtherCAT frame → datagram(cmd/addr/wkc) → 暫存器名 / SII /
CoE mailbox(SDO) / FMMU 過程資料（factory 33B/29B ×2 軸,CiA402 cw/sw）。
取樣：非週期幀全留;LRW 週期幀只留「內容有變化」者,超過 --max 均勻抽稀。
"""
import argparse, json, socket, struct, time

ETH_P_ECAT = 0x88A4
CMDS = ["NOP", "APRD", "APWR", "APRW", "FPRD", "FPWR", "FPRW", "BRD", "BWR",
        "BRW", "LRD", "LWR", "LRW", "ARMW", "FRMW"]
AL_STATES = {1: "INIT", 2: "PREOP", 3: "BOOT", 4: "SAFEOP", 8: "OP"}
REGS = [  # (起,迄, 名稱)
    (0x0000, 0x0010, "ESC 型別/版本"), (0x0010, 0x0012, "站址(configured)"),
    (0x0100, 0x0104, "DL Control"), (0x0110, 0x0112, "DL Status"),
    (0x0120, 0x0122, "AL Control"), (0x0130, 0x0132, "AL Status"),
    (0x0134, 0x0136, "AL Status Code"), (0x0500, 0x0510, "SII/EEPROM"),
    (0x0600, 0x0700, "FMMU"), (0x0800, 0x0900, "SyncManager"),
    (0x0900, 0x0A00, "DC 時鐘"), (0x1000, 0x1080, "Mailbox Out(SM0)"),
    (0x1080, 0x1100, "Mailbox In(SM1)"), (0x1100, 0x1400, "Outputs(SM2)"),
    (0x1400, 0x1700, "Inputs(SM3)"),
]
CIA402_CW = {0x0000: "失能", 0x0006: "Shutdown", 0x0007: "SwitchOn",
             0x000F: "EnableOp", 0x0080: "FaultReset", 0x0002: "QuickStop"}
SDO_SVC = {2: "SDO 請求", 3: "SDO 回應"}
# factory PDO 佈局（ec_config.h）
RX_SZ, TX_SZ, AXES = 33, 29, 2


def reg_name(ado):
    for lo, hi, name in REGS:
        if lo <= ado < hi:
            return name
    return ""


def cia402_sw(sw):
    m = sw & 0x006F
    if m & 0x004F == 0x0000: return "NotReady/SwOnDisabled" if not sw & 0x40 else "SwOnDisabled"
    if m == 0x0021: return "ReadyToSwitchOn"
    if m == 0x0023: return "SwitchedOn"
    if m == 0x0027: return "OperationEnabled"
    if m == 0x0007: return "QuickStopActive"
    if m & 0x004F == 0x000F: return "FaultReaction"
    if m & 0x004F == 0x0008: return "Fault"
    return "?"


def decode_mbx(data, tree):
    if len(data) < 8:
        return
    mlen, _sta, _pri, typ = struct.unpack_from("<HHBB", data, 0)
    mtyp = typ & 0x0F
    if mtyp != 3 or len(data) < 8 + 4:
        tree.append("Mailbox type=%d len=%d" % (mtyp, mlen))
        return
    coe, = struct.unpack_from("<H", data, 6)
    svc = coe >> 12
    tree.append("Mailbox CoE：%s" % SDO_SVC.get(svc, "svc=%d" % svc))
    if len(data) >= 16:
        cs, idx, sub = data[8], struct.unpack_from("<H", data, 9)[0], data[11]
        val, = struct.unpack_from("<I", data, 12)
        ccs = cs & 0xE0
        if ccs == 0x40: op = "讀 0x%04X:%02X" % (idx, sub)
        elif ccs == 0x20: op = "寫 0x%04X:%02X = 0x%08X" % (idx, sub, val)
        elif ccs == 0x60: op = "寫回應 0x%04X:%02X OK" % (idx, sub)
        elif ccs == 0x40 or ccs == 0x43: op = "讀回應 0x%04X:%02X = 0x%08X" % (idx, sub, val)
        elif cs == 0x80: op = "ABORT 0x%04X:%02X code=0x%08X" % (idx, sub, val)
        elif ccs & 0x40: op = "讀回應 0x%04X:%02X = 0x%08X" % (idx, sub, val)
        else: op = "cs=0x%02X 0x%04X:%02X val=0x%08X" % (cs, idx, sub, val)
        tree.append("SDO " + op)


def decode_pd(data, is_reply, tree, sig):
    """LRW 過程資料（out 66B + in 58B,factory 佈局）。sig 收「變化偵測」欄位。"""
    if len(data) < AXES * (RX_SZ + TX_SZ):
        return
    for a in range(AXES):
        o = a * RX_SZ
        cw, = struct.unpack_from("<H", data, o + 0)
        mode = data[o + 2]
        tgt, = struct.unpack_from("<i", data, o + 3)
        tree.append("軸%d 輸出：cw=0x%04X(%s) mode=%d tgt=%d"
                    % (a, cw, CIA402_CW.get(cw, "?"), mode, tgt))
        sig += [cw, tgt]
    base = AXES * RX_SZ
    for a in range(AXES):
        i = base + a * TX_SZ
        sw, = struct.unpack_from("<H", data, i + 0)
        err, = struct.unpack_from("<H", data, i + 3)
        pos, = struct.unpack_from("<i", data, i + 5)
        tree.append("軸%d 輸入：sw=0x%04X(%s) err=0x%04X pos=%d"
                    % (a, sw, cia402_sw(sw), err, pos))
        if is_reply:
            sig += [sw, pos]


def decode_frame(raw, is_reply):
    """回 (info 摘要, tree 解碼行, sig 變化簽章, is_cyclic)。raw 含 14B eth 頭。"""
    tree, sig = [], []
    b = raw[14:]
    if len(b) < 2:
        return "短幀", tree, sig, False
    hdr, = struct.unpack_from("<H", b, 0)
    flen, ftyp = hdr & 0x7FF, hdr >> 12
    tree.append("EtherCAT frame：len=%d type=%d" % (flen, ftyp))
    off, infos, cyclic = 2, [], False
    while off + 12 <= len(b):
        cmd, idx = b[off], b[off + 1]
        lenw, = struct.unpack_from("<H", b, off + 6)
        dlen, more = lenw & 0x7FF, bool(lenw & 0x8000)
        if off + 10 + dlen + 2 > len(b):
            break
        data = b[off + 10:off + 10 + dlen]
        wkc, = struct.unpack_from("<H", b, off + 10 + dlen)
        name = CMDS[cmd] if cmd < len(CMDS) else "cmd%d" % cmd
        if cmd in (10, 11, 12):                         # 邏輯定址
            laddr, = struct.unpack_from("<I", b, off + 2)
            info = "%s L:0x%08X len=%d wkc=%d" % (name, laddr, dlen, wkc)
            tree.append("datagram idx=%d %s" % (idx, info))
            decode_pd(data, is_reply, tree, sig)
            cyclic = True
        else:
            adp, ado = struct.unpack_from("<HH", b, off + 2)
            rn = reg_name(ado)
            info = "%s slave=0x%04X reg=0x%04X%s len=%d wkc=%d" \
                   % (name, adp, ado, " [%s]" % rn if rn else "", dlen, wkc)
            tree.append("datagram idx=%d %s" % (idx, info))
            if ado in (0x0120, 0x0130) and dlen >= 2:
                v, = struct.unpack_from("<H", data, 0)
                tree.append("AL %s = 0x%04X（%s%s）"
                            % ("Control" if ado == 0x0120 else "Status",
                               v, AL_STATES.get(v & 0x0F, "?"),
                               "+ERR" if v & 0x10 else ""))
                sig.append(("al", ado, v))
            if 0x1000 <= ado < 0x1100 and dlen >= 8:
                decode_mbx(data, tree)
        infos.append(info)
        off += 10 + dlen + 2
        if not more:
            break
    return " | ".join(infos), tree, sig, cyclic


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", required=True)
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--out", required=True)
    ap.add_argument("--max", type=int, default=2500)
    a = ap.parse_args()

    # ETH_P_ALL：本機送出的幀（假從站回幀）只派送給 ALL taps,綁 0x88A4 收不到
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0003))
    s.bind((a.iface, 0))
    s.settimeout(0.2)
    t0, t_end, raws = time.monotonic(), time.monotonic() + a.seconds, []
    print("[sniff] %s 抓 %.0fs…" % (a.iface, a.seconds))
    while time.monotonic() < t_end:
        try:
            pkt, addr = s.recvfrom(2048)
        except socket.timeout:
            continue
        if len(pkt) < 16 or pkt[12:14] != b"\x88\xa4":   # 只留 EtherCAT
            continue
        raws.append((time.monotonic() - t0, addr[2], bytes(pkt)))
    s.close()
    print("[sniff] 原始 %d 幀,解碼/抽稀…" % len(raws))

    pkts, last_sig, kept_cyc, drop_cyc = [], {}, 0, 0
    for t, ptype, raw in raws:
        is_reply = (ptype == socket.PACKET_OUTGOING)
        info, tree, sig, cyclic = decode_frame(raw, is_reply)
        key = ("cyc", is_reply)
        if cyclic:
            if last_sig.get(key) == sig:
                drop_cyc += 1
                continue                                # 內容沒變的週期幀不留
            last_sig[key] = sig
            kept_cyc += 1
        pkts.append({"t": round(t, 6), "dir": "reply" if is_reply else "master",
                     "len": len(raw), "info": info, "tree": tree, "cyc": cyclic,
                     "hex": raw.hex()})
    if len(pkts) > a.max:                               # 均勻抽稀（保頭尾）
        step = len(pkts) / a.max
        pkts = [pkts[int(i * step)] for i in range(a.max)]
    meta = {"iface": a.iface, "captured": len(raws), "kept": len(pkts),
            "cyclic_kept": kept_cyc, "cyclic_dropped": drop_cyc,
            "duration_s": a.seconds}
    with open(a.out, "w") as f:
        json.dump({"meta": meta, "packets": pkts}, f, ensure_ascii=False)
    print("[sniff] 留 %d 幀（週期幀變化保留 %d/丟 %d）→ %s"
          % (len(pkts), kept_cyc, drop_cyc, a.out))


if __name__ == "__main__":
    main()
