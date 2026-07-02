"""
can_bus.py — 虛擬 CAN bus + 輕量 CANopen 主站（模擬資料流）

每條 bus 掛多顆 PhuMotor 從站。主站方法（sdo/nmt/pdo）會建立 CAN frame、
路由到從站、取得回應,並可記錄 frame 觀察資料流。
"""

# COB-ID 基底
NMT        = 0x000
SDO_RX     = 0x600   # master→slave
SDO_TX     = 0x580   # slave→master
RPDO1      = 0x200
TPDO1      = 0x180
HEARTBEAT  = 0x700


def _kind(cid):
    if cid == NMT: return "NMT"
    if 0x600 <= cid <= 0x67F: return "SDO-req"
    if 0x580 <= cid <= 0x5FF: return "SDO-rsp"
    if 0x200 <= cid <= 0x27F: return "RPDO1"
    if 0x180 <= cid <= 0x1FF: return "TPDO1"
    return "?"


class CanBus:
    def __init__(self, name, dt=0.001):
        self.name = name
        self.dt = dt
        self.nodes = {}          # node_id -> PhuMotor
        self.log = False
        self.tx = 0
        self.rx = 0

    def add_node(self, motor):
        self.nodes[motor.node_id] = motor

    def _logf(self, direction, cid, data):
        if self.log:
            hexd = " ".join("%02X" % b for b in data)
            print("    [%s %-7s] id=0x%03X data=%s" % (direction, _kind(cid), cid, hexd))

    # ---- NMT ----
    def nmt(self, cmd, node=0):
        data = [cmd, node]
        self.tx += 1; self._logf("TX", NMT, data)
        for nid, m in self.nodes.items():
            if node == 0 or node == nid:
                if cmd in (0x81, 0x82):   # reset → 經狀態機回 switch-on-disabled
                    m.apply_controlword(0x0080)   # fault reset（若在 FAULT）
                    m.apply_controlword(0x0000)   # disable voltage
        # NMT 無回應

    # ---- SDO 寫（expedited）；回 True=成功 / False=abort ----
    def sdo_write(self, node, index, sub, value, size=4):
        cs = {1: 0x2F, 2: 0x2B, 3: 0x27, 4: 0x23}[size]
        data = [cs, index & 0xFF, index >> 8, sub,
                value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, (value >> 24) & 0xFF]
        self.tx += 1; self._logf("TX", SDO_RX + node, data)
        ok = self.nodes[node].write_od(index, sub, value)
        if ok:
            resp = [0x60, index & 0xFF, index >> 8, sub, 0, 0, 0, 0]
        else:
            # SDO abort：0x80 + abort code 0x06010002（嘗試寫唯讀物件）
            resp = [0x80, index & 0xFF, index >> 8, sub, 0x02, 0x00, 0x01, 0x06]
        self.rx += 1; self._logf("RX", SDO_TX + node, resp)
        return ok

    # ---- SDO 讀（expedited） ----
    def sdo_read(self, node, index, sub=0):
        req = [0x40, index & 0xFF, index >> 8, sub, 0, 0, 0, 0]
        self.tx += 1; self._logf("TX", SDO_RX + node, req)
        v = self.nodes[node].read_od(index, sub) & 0xFFFFFFFF
        resp = [0x43, index & 0xFF, index >> 8, sub,
                v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF]
        self.rx += 1; self._logf("RX", SDO_TX + node, resp)
        return self.nodes[node].read_od(index, sub)

    # ---- PDO CSP：送 [CW][target pos] → 從站步進 → 回 [SW][actual pos] ----
    def pdo_csp(self, node, controlword, target_counts):
        tc = target_counts & 0xFFFFFFFF
        data = [controlword & 0xFF, controlword >> 8,
                tc & 0xFF, (tc >> 8) & 0xFF, (tc >> 16) & 0xFF, (tc >> 24) & 0xFF]
        self.tx += 1; self._logf("TX", RPDO1 + node, data)

        m = self.nodes[node]
        m.apply_controlword(controlword)
        m.target_counts = m._i32(tc)
        m.step(self.dt)

        sw = m.statusword
        ap = m.read_od(0x6064) & 0xFFFFFFFF
        resp = [sw & 0xFF, sw >> 8, ap & 0xFF, (ap >> 8) & 0xFF, (ap >> 16) & 0xFF, (ap >> 24) & 0xFF]
        self.rx += 1; self._logf("RX", TPDO1 + node, resp)
        return sw, m.read_od(0x6064)
