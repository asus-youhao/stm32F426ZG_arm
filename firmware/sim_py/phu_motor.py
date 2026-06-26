"""
phu_motor.py — 模擬 EYOU PHU 關節（CANopen 從站 + 物理模型）

OD-driven：物件字典來自 phu_od.OD（由手冊 v1.06 抽取,共數百條,可讀寫）。
  - read_od：先回「即時計算值」(狀態字/實際位置/扭矩/電流…),否則回 OD 儲存值。
  - write_od：檢查存取權限(RO → 回 False=SDO abort),RW 則更新並觸發副作用。
扭矩/電流物理模型不變（內環 PD 對抗重力/阻尼/慣量）。
"""
import math
from phu_od import OD, OD_BY_INDEX, od_access

CPR = 524288.0 / (2.0 * math.pi)   # 19-bit 輸出端 → counts/rad

MODELS = {
    "PHU14": dict(rated_torque=8.6,  peak_torque=43.0,  rated_current=5.0,  inertia=0.02, damping=0.6, grav=2.0),
    "PHU17": dict(rated_torque=32.0, peak_torque=134.0, rated_current=8.0,  inertia=0.05, damping=1.0, grav=5.0),
    "PHU20": dict(rated_torque=50.0, peak_torque=182.0, rated_current=12.0, inertia=0.10, damping=2.0, grav=10.0),
}

SW_SWITCH_ON_DISABLED = 0x0040
SW_READY              = 0x0021
SW_SWITCHED_ON        = 0x0023
SW_OP_ENABLED         = 0x0027


class PhuMotor:
    def __init__(self, node_id, model, name=""):
        if model not in MODELS:
            raise ValueError("unknown PHU model: %s" % model)
        p = MODELS[model]
        self.node_id = node_id
        self.model = model
        self.name = name or ("node%d" % node_id)
        self.rated_torque = p["rated_torque"]; self.peak_torque = p["peak_torque"]
        self.rated_current = p["rated_current"]; self.Kt = self.rated_torque / self.rated_current
        self.I = p["inertia"]; self.b = p["damping"]; self.grav = p["grav"]
        self.Kp = 120.0; self.Kd = 2.0 * math.sqrt(self.Kp * self.I)

        self.q = 0.0; self.qd = 0.0
        self.mode = 8; self.controlword = 0; self.statusword = SW_SWITCH_ON_DISABLED
        self.enabled = False; self.target_counts = 0; self.target_torque = 0; self.target_vel = 0
        self.torque = 0.0; self.current = 0.0; self.saturated = False

        # OD 儲存（其餘可讀寫物件）：以手冊預設值初始化
        self._od = {}
        for (i, s, acc, dt, dflt, nm) in OD:
            self._od[(i, s)] = dflt
        self._od[(0x26A0, 0)] = node_id      # Node-ID
        self._od[(0x26A1, 0)] = 1000000      # baudrate
        self._od[(0x6075, 0)] = int(self.rated_current * 1000)  # mA
        self._od[(0x6076, 0)] = int(self.rated_torque * 1000)   # mNm

    # ---- CiA402 狀態機 ----
    def apply_controlword(self, cw):
        self.controlword = cw & 0xFFFF
        if self.controlword & 0x0080:
            self.statusword = SW_SWITCH_ON_DISABLED; self.enabled = False; return
        low = self.controlword & 0x000F
        if low == 0x06:   self.statusword = SW_READY;       self.enabled = False
        elif low == 0x07: self.statusword = SW_SWITCHED_ON; self.enabled = False
        elif low == 0x0F: self.statusword = SW_OP_ENABLED;  self.enabled = True
        elif low == 0x00: self.statusword = SW_SWITCH_ON_DISABLED; self.enabled = False

    # ---- 即時計算值（讀取優先回這些）----
    def _live(self, index, sub):
        if index == 0x1000: return 0x00020192
        if index == 0x6040: return self.controlword
        if index == 0x6041: return self.statusword
        if index == 0x6060 or index == 0x6061: return self.mode & 0xFF
        if index == 0x6064 or index == 0x6063: return int(round(self.q * CPR))
        if index == 0x606C: return int(round(self.qd * CPR))         # velocity actual
        if index == 0x6062 or index == 0x607A: return self.target_counts
        if index == 0x6071: return self.target_torque
        if index == 0x60FF: return self.target_vel
        if index == 0x6074 or index == 0x6077:                        # torque actual ‰rated
            return int(round(self.torque / self.rated_torque * 1000.0))
        if index == 0x6078:                                           # current actual ‰rated
            return int(round(self.current / self.rated_current * 1000.0))
        return None

    # ---- 物件字典讀 ----
    def read_od(self, index, sub=0):
        v = self._live(index, sub)
        if v is not None:
            return v
        return self._od.get((index, sub), 0)

    # ---- 物件字典寫；回 True=成功 / False=abort（RO 或不存在且非 RW）----
    def write_od(self, index, sub, value):
        acc = od_access(index, sub)
        live_ro = index in (0x6041, 0x6064, 0x6063, 0x606C, 0x6077, 0x6078, 0x6074, 0x1000)
        if acc == 'RO' or acc == 'CONST' or live_ro:
            return False                      # SDO abort（唯讀）
        # 副作用
        if index == 0x6040: self.apply_controlword(value)
        elif index == 0x6060: self.mode = value & 0xFF
        elif index == 0x607A: self.target_counts = self._i32(value)
        elif index == 0x6071: self.target_torque = self._i16(value)
        elif index == 0x60FF: self.target_vel = self._i32(value)
        elif index == 0x26A0: self.node_id = value & 0x7F
        self._od[(index, sub)] = value
        return True

    # ---- 物理步進 ----
    def step(self, dt):
        if not self.enabled:
            self.torque = 0.0; self.current = 0.0; self.saturated = False; return
        if self.mode in (8, 1):
            target_q = self.target_counts / CPR
            tau_cmd = self.Kp * (target_q - self.q) - self.Kd * self.qd
        elif self.mode in (10, 13):
            tau_cmd = self.target_torque / 1000.0 * self.rated_torque
        else:
            tau_cmd = 0.0
        self.saturated = abs(tau_cmd) > self.peak_torque
        tau_cmd = max(-self.peak_torque, min(self.peak_torque, tau_cmd))
        tau_net = tau_cmd - self.grav * math.sin(self.q) - self.b * self.qd
        self.qd += (tau_net / self.I) * dt
        self.q  += self.qd * dt
        self.torque = tau_cmd
        self.current = tau_cmd / self.Kt

    @staticmethod
    def _i32(v):
        v &= 0xFFFFFFFF
        return v - 0x100000000 if v & 0x80000000 else v
    @staticmethod
    def _i16(v):
        v &= 0xFFFF
        return v - 0x10000 if v & 0x8000 else v
