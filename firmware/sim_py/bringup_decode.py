"""
bringup_decode.py — 單獨解碼 F746 主站「開機發了哪些 cmd / 讀了哪些」

它在 COB-ID 0x601 上當 node-1 從站回應（讓主站能把整個 bring-up 序列跑完），
同時把**主站→從站每一幀**翻成人看得懂的一行：READ/WRITE 哪個物件字典、名稱、值。
純被動觀察主站行為，不改韌體。

  python bringup_decode.py                    # 預設 slcan / COM11 / 1Mbps / node 1
  python bringup_decode.py --channel COM11 --node 1

用法：先跑本程式 → 按 F746 RESET（或另開視窗用 ST-Link 重置）→ 它自動抓完印表格。
"""
import argparse
import glob
import platform
import sys
import time

from phu_motor import PhuMotor
from phu_od import od_name, od_access, OD_BY_INDEX
from can_slave import CiA402Slave, SDO_RX, SDO_TX, RPDO1, NMT

NMT_CMD = {0x01: "START (operational)", 0x02: "STOP", 0x80: "PRE-OPERATIONAL",
           0x81: "RESET-NODE", 0x82: "RESET-COMMUNICATION"}
MODE = {1: "PP", 3: "PV", 4: "PT", 6: "HM", 8: "CSP", 9: "CSV", 10: "CST", 13: "CSF"}
CW = {0x0000: "disable voltage", 0x0002: "quick stop", 0x0006: "shutdown",
      0x0007: "switch on", 0x000F: "enable operation", 0x001F: "enable+start-homing",
      0x0080: "fault reset", 0x010F: "halt"}
SIGNED = {"INT", "DINT", "SINT"}


def _is_wsl():
    """WSL 下 /proc/version 會含 'microsoft'。"""
    if platform.system() != "Linux":
        return False
    try:
        with open("/proc/version") as f:
            return "microsoft" in f.read().lower()
    except OSError:
        return False


def _default_channel():
    """依作業系統挑合理的 slcan 通道。

    - Windows：沿用 COM11。
    - Linux / WSL：自動抓第一個 /dev/ttyACM* 或 /dev/ttyUSB*（CANable 常見），
      抓不到就退回 /dev/ttyACM0，讓錯誤訊息去引導設定。
    """
    if platform.system() == "Windows":
        return "COM11"
    ports = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    return ports[0] if ports else "/dev/ttyACM0"


def _port_help(channel):
    """開埠失敗時，依環境印出對應的排錯步驟。"""
    if platform.system() == "Windows":
        return ("在 Windows：確認裝置管理員裡有這個 COM 埠、沒被其他程式（如另一個 "
                "python-can）佔用，並用 --channel COMx 指定正確埠號。")
    common = ("在 Linux：確認轉接器已插上、你在 dialout 群組（sudo usermod -aG dialout $USER "
              "後重登），並用 --channel /dev/ttyACM0 指定正確裝置。")
    if _is_wsl():
        return (common + "\n在 WSL：USB 不會自動進來，需先在 Windows PowerShell（系統管理員）用 "
                "usbipd 掛入：\n"
                "    usbipd list                    # 找到 CANable 的 BUSID\n"
                "    usbipd bind   --busid <BUSID>   # 首次需綁定\n"
                "    usbipd attach --wsl --busid <BUSID>\n"
                "掛好後在 WSL `ls /dev/ttyACM*` 應能看到裝置。")
    return common


def _dtype(index, sub=0):
    e = OD_BY_INDEX.get((index, sub))
    return e[1] if e else "UDINT"


def _as_signed(v, dtype):
    if dtype == "SINT" and v >= 0x80:
        return v - 0x100
    if dtype == "INT" and v >= 0x8000:
        return v - 0x10000
    if dtype == "DINT" and v >= 0x80000000:
        return v - 0x100000000
    return v


def _fmt_val(index, sub, v):
    dt = _dtype(index, sub)
    sv = _as_signed(v, dt) if dt in SIGNED else v
    extra = ""
    if index == 0x6060:
        extra = "  <%s>" % MODE.get(sv, "?")
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
    ap.add_argument("--channel", default=None,
                    help="slcan 通道；預設 Windows=COM11，Linux/WSL 自動抓 /dev/ttyACM*")
    ap.add_argument("--bitrate", type=int, default=1000000)
    ap.add_argument("--node", type=int, default=1)
    ap.add_argument("--idle", type=float, default=3.0, help="開始後靜默幾秒視為結束")
    ap.add_argument("--model", default="PHU20")
    args = ap.parse_args()

    try:
        import can
    except ImportError:
        sys.exit("需要 python-can：pip install python-can pyserial")

    channel = args.channel if args.channel is not None else _default_channel()

    slave = CiA402Slave(args.node, args.model, "L_J%d" % args.node)
    try:
        bus = can.Bus(interface=args.interface, channel=channel, bitrate=args.bitrate)
    except Exception as e:                      # 開埠失敗 → 給環境對應的排錯提示
        sys.exit("開啟 CAN 埠失敗（interface=%s channel=%s）：%s\n\n%s"
                 % (args.interface, channel, e, _port_help(channel)))
    print("bus open %s %s @%d bps, responding as node %d"
          % (args.interface, channel, args.bitrate, args.node))
    print(">>> 現在按 F746 RESET（或用 ST-Link 重置）。等待主站封包...\n")

    log = []          # (t, kind, text)
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

            # 讓從站回應（使序列能繼續），並取回應值供 READ 顯示
            resp = slave.handle_frame(arb, data)

            line = None
            if arb == NMT and len(data) >= 2:
                if data[1] in (0, args.node):
                    line = ("NMT", "NMT   %s" % NMT_CMD.get(data[0], "0x%02X" % data[0]))
            elif arb == SDO_RX + args.node and len(data) >= 4:
                cs = data[0]
                index = data[1] | (data[2] << 8)
                sub = data[3]
                nm = od_name(index, sub)
                if (cs & 0xE0) == 0x40:                 # upload = 主站「讀」
                    val = None
                    for a, d in resp:
                        if a == SDO_TX + args.node:
                            val = d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)
                    vs = _fmt_val(index, sub, val) if val is not None else "?"
                    line = ("READ", "READ  0x%04X:%02X %-26s = %s" % (index, sub, nm, vs))
                elif (cs & 0xE0) == 0x20:               # download = 主站「寫」
                    val = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
                    line = ("WRITE", "WRITE 0x%04X:%02X %-26s = %s"
                            % (index, sub, nm, _fmt_val(index, sub, val)))
            elif arb == RPDO1 + args.node:
                cw = data[0] | (data[1] << 8) if len(data) >= 2 else 0
                line = ("PDO", "RPDO1 CW=0x%04X <%s>" % (cw, CW.get(cw, "?")))

            if line:
                # 回送從站回應
                for a, d in resp:
                    bus.send(can.Message(arbitration_id=a, data=bytes(d), is_extended_id=False))
                if t0 is None:
                    t0 = now
                step += 1
                dt_ms = (now - t0) * 1000.0
                log.append((dt_ms, line[0], line[1]))
                print("  %2d  [%6.1f ms]  %s" % (step, dt_ms, line[1]))
                last = now
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()

    # ---- 匯總 ----
    reads = [l for l in log if l[1] == "READ"]
    writes = [l for l in log if l[1] == "WRITE"]
    nmts = [l for l in log if l[1] == "NMT"]
    print("\n================ F746 主站 bring-up 序列匯總 ================")
    print("總幀數=%d  |  NMT=%d  READ=%d  WRITE=%d  PDO=%d"
          % (len(log), len(nmts), len(reads), len(writes),
             len([l for l in log if l[1] == "PDO"])))
    print("\n-- 主站開機讀了這些物件 (READ) --")
    for _, _, t in reads:
        print("   " + t)
    print("\n-- 主站開機寫了這些物件 (WRITE) --")
    for _, _, t in writes:
        print("   " + t)
    if not log:
        print("\n(沒收到任何主站封包 — 確認 F746 有通電、已按 RESET、接線/COM 埠正確)")


if __name__ == "__main__":
    main()
