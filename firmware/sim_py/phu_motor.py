"""
phu_motor.py — 模擬單顆 EYOU PHU 關節（CANopen 從站 + 物理模型）

特點：
  - CANopen 物件字典「可讀可寫」（read_od / write_od）
  - CiA402 狀態機（controlword → statusword）
  - 物理模型：依命令位置(CSP)算出馬達**輸出扭矩(N·m)**與**電流(A)**
  - 參數依 EYOU PHU 規格書（PHU14/17/20,輸出端，數值為合理佔位，實機以原廠為準）

物件字典關鍵索引（依通訊手冊 v1.06）：
  0x1000 device type    0x6040 controlword   0x6041 statusword
  0x6060 mode           0x6061 mode display  0x6064 actual position(counts)
  0x607A target pos     0x6071 target torque 0x6077 actual torque(‰rated)
  0x6078 actual current(‰rated)             0x6075 rated current(mA)
  0x6076 rated torque(mNm)  0x60FF target vel
  0x26A0 node-id        0x26A1 baudrate(1Mbps)
"""
import math

CPR = 524288.0 / (2.0 * math.pi)   # 19-bit 輸出端 → counts/rad

# 各型號參數（輸出端）：rated/peak 扭矩(N·m)、額定電流(A)、轉動慣量、阻尼、重力負載峰值
MODELS = {
    "PHU14": dict(rated_torque=8.6,  peak_torque=43.0,  rated_current=5.0,  inertia=0.02, damping=0.6, grav=2.0),
    "PHU17": dict(rated_torque=32.0, peak_torque=134.0, rated_current=8.0,  inertia=0.05, damping=1.0, grav=5.0),
    "PHU20": dict(rated_torque=50.0, peak_torque=182.0, rated_current=12.0, inertia=0.10, damping=2.0, grav=10.0),
}

# CiA402 狀態字（與韌體 cia402.c 對應）
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
        self.rated_torque = p["rated_torque"]
        self.peak_torque  = p["peak_torque"]
        self.rated_current = p["rated_current"]
        self.Kt = self.rated_torque / self.rated_current   # N·m per A（關節等效）
        self.I  = p["inertia"]
        self.b  = p["damping"]
        self.grav = p["grav"]

        # 內環 PD（臨界阻尼附近）
        self.Kp = 120.0
        self.Kd = 2.0 * math.sqrt(self.Kp * self.I)

        # 狀態
        self.q = 0.0       # 實際位置 rad
        self.qd = 0.0      # 速度 rad/s
        self.mode = 8      # CSP
        self.controlword = 0
        self.statusword = SW_SWITCH_ON_DISABLED
        self.enabled = False
        self.target_counts = 0
        self.target_torque = 0   # ‰rated（CST/CSF）
        self.target_vel = 0

        # 輸出量（給上位機讀）
        self.torque = 0.0  # N·m
        self.current = 0.0 # A
        self.saturated = False

        self._od = {}      # 其他被寫入的物件

    # ---- CiA402 狀態機 ----
    def apply_controlword(self, cw):
        self.controlword = cw & 0xFFFF
        if self.controlword & 0x0080:           # fault reset
            self.statusword = SW_SWITCH_ON_DISABLED; self.enabled = False; return
        low = self.controlword & 0x000F
        if low == 0x06:   self.statusword = SW_READY;        self.enabled = False
        elif low == 0x07: self.statusword = SW_SWITCHED_ON;  self.enabled = False
        elif low == 0x0F: self.statusword = SW_OP_ENABLED;   self.enabled = True
        elif low == 0x00: self.statusword = SW_SWITCH_ON_DISABLED; self.enabled = False

    # ---- 物件字典讀 ----
    def read_od(self, index, sub=0):
        if index == 0x1000: return 0x00020192
        if index == 0x6041: return self.statusword
        if index == 0x6061: return self.mode & 0xFF
        if index == 0x6064: return int(round(self.q * CPR))
        if index == 0x607A: return self.target_counts
        if index == 0x6071: return self.target_torque
        if index == 0x6077: return int(round(self.torque / self.rated_torque * 1000.0))   # ‰rated
        if index == 0x6078: return int(round(self.current / self.rated_current * 1000.0))  # ‰rated
        if index == 0x6076: return int(self.rated_torque * 1000.0)   # mNm
        if index == 0x6075: return int(self.rated_current * 1000.0)  # mA
        if index == 0x60FF: return self.target_vel
        if index == 0x26A0: return self.node_id
        if index == 0x26A1: return 1000000
        return self._od.get((index, sub), 0)

    # ---- 物件字典寫 ----
    def write_od(self, index, sub, value):
        if index == 0x6040: self.apply_controlword(value)
        elif index == 0x6060: self.mode = value & 0xFF
        elif index == 0x607A: self.target_counts = self._i32(value)
        elif index == 0x6071: self.target_torque = self._i16(value)
        elif index == 0x60FF: self.target_vel = self._i32(value)
        else: self._od[(index, sub)] = value

    # ---- 物理步進（dt 秒）----
    def step(self, dt):
        if not self.enabled:
            self.torque = 0.0; self.current = 0.0; self.saturated = False
            return
        if self.mode in (8, 1):          # CSP / PP：位置追隨
            target_q = self.target_counts / CPR
            err = target_q - self.q
            tau_cmd = self.Kp * err - self.Kd * self.qd     # 內環輸出 N·m
        elif self.mode in (10, 13):      # CST / CSF：直接扭矩命令
            tau_cmd = self.target_torque / 1000.0 * self.rated_torque
        else:
            tau_cmd = 0.0

        # 飽和到峰值扭矩
        self.saturated = abs(tau_cmd) > self.peak_torque
        if tau_cmd >  self.peak_torque: tau_cmd =  self.peak_torque
        if tau_cmd < -self.peak_torque: tau_cmd = -self.peak_torque

        # 負載：重力（單擺近似）+ 黏滯阻尼
        tau_grav = self.grav * math.sin(self.q)
        tau_net = tau_cmd - tau_grav - self.b * self.qd
        qdd = tau_net / self.I
        self.qd += qdd * dt
        self.q  += self.qd * dt

        # 輸出量
        self.torque = tau_cmd
        self.current = tau_cmd / self.Kt

    # ---- 工具 ----
    @staticmethod
    def _i32(v):
        v &= 0xFFFFFFFF
        return v - 0x100000000 if v & 0x80000000 else v
    @staticmethod
    def _i16(v):
        v &= 0xFFFF
        return v - 0x10000 if v & 0x8000 else v
