"""
can_slave.py — C1：把 PhuMotor 當成「真實 CAN bus 上的 CiA402 從站」

用途
----
F746 跑你的 CANopen 主站韌體，PC 透過 USB-CAN（CANable，candleLight 韌體）接上
同一條 bus，本程式在 PC 上模擬一顆（或多顆）EYOU PHU 從站，回應主站的 SDO/NMT/PDO。
這樣沒有馬達也能驗證 F746 主站 + CANopen 交握。

拓樸（需要 transceiver，CANable 不能直接接 F746 的 TTL TX/RX）：

    F746 PD1/PD0 ─►[transceiver]─CANH/CANL─[CANable]─USB─► PC（本程式）
          你的韌體      120Ω 端         120Ω(撥碼)

設計
----
SDO/NMT/PDO 的處理拆成純函式 `CiA402Slave.handle_frame(arb_id, data) -> [(arb_id, data)...]`，
與傳輸層脫鉤：
  - 真機：python-can 收到 frame → handle_frame() → python-can 送回應。
  - 離線：`--selftest` 直接餵入 F746 bring-up 會送的 SDO 序列，斷言回應正確（不需 python-can、不需硬體）。

用法
----
  # 離線自測（不需任何硬體 / 套件）
  python can_slave.py --selftest

  # 真機（先 pip install python-can gs_usb，並用 Zadig 把 CANable 換成 WinUSB）
  python can_slave.py --interface gs_usb --channel 0 --bitrate 1000000 --nodes 1:PHU20

  # 若 CANable 改用 slcan 韌體（顯示為 COM 埠）
  python can_slave.py --interface slcan --channel COM5 --bitrate 1000000 --nodes 1:PHU20
"""
import argparse
import sys
import time

from phu_motor import PhuMotor

# ---- CANopen COB-ID 基底（與 firmware/canopen 一致）----
NMT       = 0x000
SDO_RX    = 0x600   # master → slave
SDO_TX    = 0x580   # slave → master
RPDO1     = 0x200   # master → slave
TPDO1     = 0x180   # slave → master
HEARTBEAT = 0x700   # slave → master

# NMT 命令
NMT_START   = 0x01
NMT_STOP    = 0x02
NMT_PRE_OP  = 0x80
NMT_RESET   = 0x81
NMT_RESET_C = 0x82

# NMT 節點狀態（heartbeat / 0x700 第一位元組）
NS_BOOTUP      = 0x00
NS_STOPPED     = 0x04
NS_OPERATIONAL = 0x05
NS_PRE_OP      = 0x7F


def _u32(v):
    return v & 0xFFFFFFFF


class CiA402Slave:
    """單顆從站：包一顆 PhuMotor，處理 SDO/NMT/PDO 並產生回應 frame。"""

    def __init__(self, node_id, model, name="", verbose=False):
        self.motor = PhuMotor(node_id, model, name)
        self.node_id = node_id
        self.verbose = verbose
        self.nmt_state = NS_PRE_OP

    # ---- 對外：餵一個 frame，回傳要送出的回應 frame 串列 ----
    def handle_frame(self, arb_id, data):
        data = list(data)
        if arb_id == NMT:
            return self._on_nmt(data)
        if arb_id == SDO_RX + self.node_id:
            r = self._on_sdo(data)
            return [r] if r else []
        if arb_id == RPDO1 + self.node_id:
            r = self._on_rpdo1(data)
            return [r] if r else []
        return []   # 不是給這個 node 的，忽略

    # ---- NMT（0x000）：[cmd][node]，node==0 為廣播 ----
    def _on_nmt(self, data):
        if len(data) < 2:
            return []
        cmd, node = data[0], data[1]
        if node not in (0, self.node_id):
            return []
        if cmd == NMT_START:
            self.nmt_state = NS_OPERATIONAL
        elif cmd == NMT_STOP:
            self.nmt_state = NS_STOPPED
        elif cmd == NMT_PRE_OP:
            self.nmt_state = NS_PRE_OP
        elif cmd in (NMT_RESET, NMT_RESET_C):
            self.nmt_state = NS_PRE_OP
            self.motor.apply_controlword(0x0080)   # fault reset → switch-on-disabled
        if self.verbose:
            print("    [NMT ] cmd=0x%02X node=%d -> state=0x%02X" %
                  (cmd, node, self.nmt_state))
        return []   # NMT 無回應

    # ---- SDO（0x600+node）expedited 讀/寫 ----
    def _on_sdo(self, data):
        if len(data) < 4:
            return None
        cs = data[0]
        index = data[1] | (data[2] << 8)
        sub = data[3]
        ccs = cs & 0xE0

        if ccs == 0x40:                     # upload（讀）
            v = _u32(self.motor.read_od(index, sub))
            resp = [0x43, index & 0xFF, index >> 8, sub,
                    v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF]
            if self.verbose:
                print("    [SDO ] rd  0x%04X:%02X = 0x%08X" % (index, sub, v))
            return (SDO_TX + self.node_id, resp)

        if ccs == 0x20:                     # download（寫，expedited）
            value = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
            ok = self.motor.write_od(index, sub, value)
            if ok:
                resp = [0x60, index & 0xFF, index >> 8, sub, 0, 0, 0, 0]
            else:                           # 寫唯讀物件 → abort 0x06010002
                resp = [0x80, index & 0xFF, index >> 8, sub, 0x02, 0x00, 0x01, 0x06]
            if self.verbose:
                print("    [SDO ] wr  0x%04X:%02X = 0x%08X -> %s"
                      % (index, sub, value, "OK" if ok else "ABORT(RO)"))
            return (SDO_TX + self.node_id, resp)

        # 不支援的 SDO → abort
        resp = [0x80, index & 0xFF, index >> 8, sub, 0x01, 0x00, 0x01, 0x06]
        return (SDO_TX + self.node_id, resp)

    # ---- RPDO1（0x200+node）：[CW lo][CW hi][target32] → step → TPDO1 ----
    def _on_rpdo1(self, data):
        if len(data) < 2:
            return None
        cw = data[0] | (data[1] << 8)
        self.motor.apply_controlword(cw)
        if len(data) >= 6:
            tc = data[2] | (data[3] << 8) | (data[4] << 16) | (data[5] << 24)
            self.motor.target_counts = self.motor._i32(tc)
        self.motor.step(0.001)
        return self._tpdo1()

    def _tpdo1(self):
        sw = self.motor.statusword
        ap = _u32(self.motor.read_od(0x6064))
        data = [sw & 0xFF, sw >> 8,
                ap & 0xFF, (ap >> 8) & 0xFF, (ap >> 16) & 0xFF, (ap >> 24) & 0xFF]
        return (TPDO1 + self.node_id, data)

    def heartbeat(self):
        return (HEARTBEAT + self.node_id, [self.nmt_state])


# ============================ 真機（python-can）============================
def run_canable(slaves, interface, channel, bitrate, hb_ms, verbose):
    try:
        import can
    except ImportError:
        print("找不到 python-can。請先安裝：\n"
              "    pip install python-can gs_usb\n"
              "（CANable candleLight 韌體在 Windows 需用 Zadig 把該裝置換成 WinUSB 驅動）",
              file=sys.stderr)
        sys.exit(2)

    by_node = {s.node_id: s for s in slaves}
    kwargs = dict(interface=interface, bitrate=bitrate)
    if channel is not None:
        kwargs["channel"] = channel
    bus = can.Bus(**kwargs)
    print("已開啟 CAN：%s ch=%s @ %d bps，模擬從站 node=%s"
          % (interface, channel, bitrate, sorted(by_node)))
    print("等待 F746 主站的 SDO/NMT/PDO …（Ctrl-C 結束）")

    # 開機通知（boot-up heartbeat 0x700+node = 0x00）
    for s in slaves:
        bus.send(can.Message(arbitration_id=HEARTBEAT + s.node_id,
                             data=[NS_BOOTUP], is_extended_id=False))

    next_hb = time.time() + (hb_ms / 1000.0 if hb_ms else 1e9)
    last_step = time.time()
    try:
        while True:
            msg = bus.recv(timeout=0.002)
            # 物理步進：讓 CSP/PV/PT 目標真的驅動 q/qd（否則位置永遠不動，
            # 主站的 moved 檢查與回授看門狗都測不出東西）
            now = time.time()
            dt = now - last_step
            if dt >= 0.001:
                dt = min(dt, 0.05)          # 防暫停後的大步跳變
                for s in slaves:
                    s.motor.step(dt)
                last_step = now
            if msg is not None and not msg.is_extended_id:
                for s in slaves:
                    for arb, data in s.handle_frame(msg.arbitration_id, msg.data):
                        bus.send(can.Message(arbitration_id=arb, data=bytes(data),
                                             is_extended_id=False))
            if hb_ms and time.time() >= next_hb:
                for s in slaves:
                    arb, data = s.heartbeat()
                    bus.send(can.Message(arbitration_id=arb, data=bytes(data),
                                         is_extended_id=False))
                next_hb = time.time() + hb_ms / 1000.0
    except KeyboardInterrupt:
        print("\n結束。")
    finally:
        bus.shutdown()


# ============================ 離線自測（無需套件/硬體）============================
def selftest():
    """重現 F746 bring-up 的 SDO 序列，斷言從站回應正確。"""
    s = CiA402Slave(1, "PHU20", "L_J1", verbose=True)

    def rd(index, sub=0):
        req = [0x40, index & 0xFF, index >> 8, sub, 0, 0, 0, 0]
        out = s.handle_frame(SDO_RX + 1, req)
        assert out, "讀 0x%04X 無回應" % index
        arb, d = out[0]
        assert arb == SDO_TX + 1 and d[0] == 0x43, "讀 0x%04X 回應異常: %r" % (index, d)
        return d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)

    def wr(index, value, sub=0):
        req = [0x23, index & 0xFF, index >> 8, sub,
               value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, (value >> 24) & 0xFF]
        out = s.handle_frame(SDO_RX + 1, req)
        assert out and out[0][1][0] == 0x60, "寫 0x%04X 未 ACK: %r" % (index, out)

    print("=== C1 從站離線自測（模擬 F746 bring-up 的 SDO 序列）===\n")

    # 1) NMT reset_comm → pre-op
    s.handle_frame(NMT, [NMT_RESET_C, 1])
    s.handle_frame(NMT, [NMT_PRE_OP, 1])

    # 2) bring-up 的 SDO 讀
    dt = rd(0x1000)
    assert dt == 0x00020192, "device type 不符: 0x%08X" % dt
    sw = rd(0x6041) & 0xFFFF
    baud = rd(0x26A1)
    node = rd(0x26A0)
    pos0 = rd(0x6064)
    print("\n  deviceType=0x%08X  statusword=0x%04X  baud=%d  node=%d  pos_before=%d"
          % (dt, sw, baud, node, pos0))
    assert baud == 1000000 and node == 1, "baud/node 不符"

    # 3) NMT start，設 PV 模式 + 目標速度
    s.handle_frame(NMT, [NMT_START, 1])
    wr(0x6060, 3)            # MODE_PV
    wr(0x6083, 100000)       # accel
    wr(0x6084, 100000)       # decel
    wr(0x60FF, 2000)         # target velocity

    # 4) CiA402 使能：fault reset → 0x06 → 0x07 → 0x0F（每步讀狀態字）
    for cw in (0x0080, 0x0006, 0x0007, 0x000F):
        wr(0x6040, cw)
    sw = rd(0x6041) & 0xFFFF
    print("  使能後 statusword=0x%04X（0x0027=operation-enabled）" % sw)
    assert sw == 0x0027, "未進入 operation-enabled"

    # 5) 轉動：跑 1000 個 1kHz step，位置應改變
    for _ in range(1000):
        s.motor.step(0.001)
    pos1 = s.motor.read_od(0x6064)
    print("  轉動後 pos_after=%d  moved=%s" % (pos1, pos1 != pos0))
    assert pos1 != pos0, "PV 模式位置未改變"

    # 6) 停止 + disable voltage
    wr(0x60FF, 0)
    wr(0x6040, 0x0000)

    # 7) PDO 路徑也驗一下：RPDO1 → TPDO1
    s.handle_frame(NMT, [NMT_START, 1])
    for cw in (0x0006, 0x0007, 0x000F):
        wr(0x6040, cw)
    s.motor.mode = 8        # CSP
    rpdo = [0x0F, 0x00] + list((50000).to_bytes(4, "little"))
    out = s.handle_frame(RPDO1 + 1, rpdo)
    assert out and out[0][0] == TPDO1 + 1, "RPDO1 未回 TPDO1"
    print("  RPDO1→TPDO1 OK，TPDO1 data=%s" % (" ".join("%02X" % b for b in out[0][1])))

    print("\n=== 全部斷言通過 [OK]  從站邏輯正確，硬體到貨即可上真 bus ===")


# ============================ 8 模式全測（離線）============================
def modetest():
    """驅動從站跑遍 8 種 CiA402 模式 + 急停 + 故障，斷言每個都正確。"""
    from phu_motor import CPR
    s = CiA402Slave(1, "PHU20", "L_J1")
    m = s.motor

    def enable(mode):
        m.write_od(0x6060, 0, mode)
        for cw in (0x06, 0x07, 0x0F):
            m.apply_controlword(cw)
        assert m.enabled, "mode %d 未進 operation-enabled" % mode

    def run(n=1500, dt=0.001):
        for _ in range(n):
            m.step(dt)

    print("=== 8 模式全測（單顆 PHU20）===\n")

    # PP(1)：位置追隨
    m.q = m.qd = 0.0; enable(1)
    m.write_od(0x607A, 0, int(0.5 * CPR) & 0xFFFFFFFF); run()
    assert abs(m.q - 0.5) < 0.05, "PP 未到位: q=%.3f" % m.q
    print("  PP(1)  q→0.5  實際 %.3f  ✔".replace("✔", "OK") % m.q)

    # PV(3)：速度追隨
    m.q = m.qd = 0.0; enable(3)
    m.write_od(0x60FF, 0, int(1.0 * CPR) & 0xFFFFFFFF); run()
    assert abs(m.qd - 1.0) < 0.1, "PV 速度未到: qd=%.3f" % m.qd
    print("  PV(3)  qd→1.0 實際 %.3f  OK" % m.qd)

    # PT(4)：力矩追隨（經 torque slope）
    m.q = m.qd = 0.0; enable(4)
    m.write_od(0x6087, 0, 500000)            # slope ‰/s
    m.write_od(0x6071, 0, 200)               # 200‰ 額定
    run(800)
    exp = 200 / 1000.0 * m.rated_torque
    assert abs(m.torque - exp) < exp * 0.2, "PT 力矩偏差: %.2f vs %.2f" % (m.torque, exp)
    print("  PT(4)  τ→%.1fNm 實際 %.2f  OK" % (exp, m.torque))

    # HM(6)：回零
    m.q = 0.3; m.qd = 0.0; enable(6)
    m.apply_controlword(0x1F)                # bit4 啟動回零
    run(3000)
    assert m.homed and (m.statusword & 0x1000), "HM 未完成回零"
    print("  HM(6)  homed=%s sw=0x%04X  OK" % (m.homed, m.statusword))

    # CSP(8)
    m.q = m.qd = 0.0; enable(8)
    m.write_od(0x607A, 0, int(0.4 * CPR) & 0xFFFFFFFF); run()
    assert abs(m.q - 0.4) < 0.05, "CSP 未到位"
    print("  CSP(8) q→0.4  實際 %.3f  OK" % m.q)

    # CSV(9)
    m.q = m.qd = 0.0; enable(9)
    m.write_od(0x60FF, 0, int(0.8 * CPR) & 0xFFFFFFFF); run()
    assert abs(m.qd - 0.8) < 0.1, "CSV 速度未到"
    print("  CSV(9) qd→0.8 實際 %.3f  OK" % m.qd)

    # CST(10) / CSF(13)：直接力矩
    for mode in (10, 13):
        m.q = m.qd = 0.0; enable(mode)
        m.write_od(0x6087, 0, 0)             # slope=0 → 立即
        m.write_od(0x6071, 0, 150)
        run(300)
        exp = 150 / 1000.0 * m.rated_torque
        assert abs(m.torque - exp) < exp * 0.25, "%d 力矩偏差" % mode
        print("  %s(%d) τ→%.1fNm 實際 %.2f  OK" % ("CST" if mode == 10 else "CSF", mode, exp, m.torque))

    # 急停：op-enabled → quick-stop-active 並減速
    m.q = m.qd = 0.0; enable(3); m.write_od(0x60FF, 0, int(2.0 * CPR) & 0xFFFFFFFF)
    run(200); m.apply_controlword(0x02)
    assert m.state == 5, "急停未進 quick-stop-active"
    run(1500); assert abs(m.qd) < 0.05, "急停後未停穩"
    print("  QuickStop  state=quick-stop-active, qd=%.3f  OK" % m.qd)

    # 故障注入 + 清除
    m.inject_fault()
    assert m.state == 7 and (m.statusword & 0x0008), "未進 fault"
    m.apply_controlword(0x80); m.apply_controlword(0x06)
    assert m.state != 7, "fault reset 失敗"
    print("  Fault inject→clear  sw=0x%04X  OK" % m.statusword)

    print("\n=== 8 模式 + 急停 + 故障 全部通過 [OK] ===")


# ================================ main ================================
def parse_nodes(spec):
    slaves = []
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        nid, _, model = item.partition(":")
        slaves.append((int(nid), model or "PHU20"))
    return slaves


def main():
    ap = argparse.ArgumentParser(description="C1：CANable 上的 CiA402 假從站")
    ap.add_argument("--selftest", action="store_true", help="離線自測（不需 python-can / 硬體）")
    ap.add_argument("--modetest", action="store_true", help="8 模式 + 急停 + 故障全測（離線）")
    ap.add_argument("--interface", default="gs_usb", help="python-can interface（gs_usb / slcan …）")
    ap.add_argument("--channel", default=None, help="通道（gs_usb 多為 0；slcan 為 COM 埠）")
    ap.add_argument("--bitrate", type=int, default=1000000, help="位元率（預設 1Mbps）")
    ap.add_argument("--nodes", default="1:PHU20", help="從站清單，如 '1:PHU20,2:PHU20,3:PHU17'")
    ap.add_argument("--heartbeat", type=int, default=0, help="heartbeat 週期 ms（0=關閉）")
    ap.add_argument("--verbose", action="store_true", help="印出每筆 SDO/NMT")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return
    if args.modetest:
        modetest()
        return

    slaves = [CiA402Slave(nid, model, verbose=args.verbose)
              for nid, model in parse_nodes(args.nodes)]
    run_canable(slaves, args.interface, args.channel, args.bitrate,
                args.heartbeat, args.verbose)


if __name__ == "__main__":
    main()
