"""
phu_motor.py — 模擬單顆 EYOU PHU 關節（完整 CiA402 從站 + 物理模型）

依 `doc_EYOU` 通信手冊 v1.06 實作「全面忠實」從站：

  - 完整 CiA402 狀態機：controlword(0x6040) → statusword(0x6041)，含 shutdown / switch-on /
    enable / disable-voltage / **quick-stop** / **halt** / **fault reset** 與對應狀態。
  - **8 種運動模式**（手冊 Table 4-1）：PP(1) PV(3) PT(4) HM(6) CSP(8) CSV(9) CST(10) CSF(13)。
  - 模式切換守則：切到位置模式(PP/CSP)前自動把 0x6064 複製到 0x607A（手冊規定，避免跳動）。
  - **Homing 序列**（mode 6）：bit4 觸發 → 收斂到 home offset → 置 homing-attained。
  - **故障注入 / reset**：inject_fault() → FAULT 狀態 + 0x603F；controlword 0x80 清除。
  - 物件字典讀寫依 [[phu_od]]（型別/存取/預設）；即時值（位置/扭矩/電流/狀態字）動態計算。
  - 物理模型：重力 + 黏滯阻尼 + 慣量；輸出扭矩(N·m)與電流(A)。

公開介面與舊版相容（can_slave / web_monitor 不需改）：
  read_od(index, sub=0) / write_od(index, sub, value) / apply_controlword(cw) / step(dt)
  以及屬性 q, qd, mode, statusword, enabled, torque, current, target_counts, target_vel,
  target_torque, peak_torque, rated_current, rated_torque。
"""
import math

import phu_od

CPR = 524288.0 / (2.0 * math.pi)   # 19-bit 輸出端 → counts/rad

# 各型號參數（輸出端）：rated/peak 扭矩(N·m)、額定電流(A)、慣量、阻尼、重力峰值
MODELS = {
    "PHU14": dict(rated_torque=8.6,  peak_torque=43.0,  rated_current=5.0,  inertia=0.02, damping=0.6, grav=2.0),
    "PHU17": dict(rated_torque=32.0, peak_torque=134.0, rated_current=8.0,  inertia=0.05, damping=1.0, grav=5.0),
    "PHU20": dict(rated_torque=50.0, peak_torque=182.0, rated_current=12.0, inertia=0.10, damping=2.0, grav=10.0),
}

# ---- CiA402 狀態（內部）----
NOT_READY, SWITCH_ON_DISABLED, READY, SWITCHED_ON = 0, 1, 2, 3
OPERATION_ENABLED, QUICK_STOP_ACTIVE, FAULT_REACTION, FAULT = 4, 5, 6, 7

# 狀態字低位元組（canonical，與韌體 cia402.c 對應；高位元組為動態旗標）
SW_BASE = {
    NOT_READY: 0x0000, SWITCH_ON_DISABLED: 0x0040, READY: 0x0021, SWITCHED_ON: 0x0023,
    OPERATION_ENABLED: 0x0027, QUICK_STOP_ACTIVE: 0x0007, FAULT_REACTION: 0x000F, FAULT: 0x0008,
}
# 別名（相容舊匯入）
SW_SWITCH_ON_DISABLED = 0x0040
SW_READY = 0x0021
SW_SWITCHED_ON = 0x0023
SW_OP_ENABLED = 0x0027

# 狀態字動態位元
SW_TARGET_REACHED = 0x0400   # bit10：target reached / speed reached
SW_SETPOINT_ACK   = 0x1000   # bit12：set-point ack（PP）/ homing attained（HM）
SW_FOLLOW_ERROR   = 0x2000   # bit13：following error / homing error

# 模式
MODE_PP, MODE_PV, MODE_PT, MODE_HM = 1, 3, 4, 6
MODE_CSP, MODE_CSV, MODE_CST, MODE_CSF = 8, 9, 10, 13
POSITION_MODES = (MODE_PP, MODE_CSP)
VELOCITY_MODES = (MODE_PV, MODE_CSV)
TORQUE_MODES = (MODE_PT, MODE_CST, MODE_CSF)

# 故障碼（0x603F，取手冊類別的代表值）
FAULT_NONE = 0x0000
FAULT_HOMING = 0x7305          # homing failure（示意）
FAULT_FOLLOWING = 0x8611       # following error
FAULT_OVERCURRENT = 0x2310


class PhuMotor:
    def __init__(self, node_id, model, name=""):
        if model not in MODELS:
            raise ValueError("unknown PHU model: %s" % model)
        p = MODELS[model]
        self.node_id = node_id
        self.model = model
        self.name = name or ("node%d" % node_id)
        self.rated_torque = p["rated_torque"]
        self.peak_torque = p["peak_torque"]
        self.rated_current = p["rated_current"]
        self.Kt = self.rated_torque / self.rated_current
        self.I = p["inertia"]
        self.b = p["damping"]
        self.grav = p["grav"]
        self.Kp = 120.0
        self.Kd = 2.0 * math.sqrt(self.Kp * self.I)

        # 運動狀態
        self.q = 0.0
        self.qd = 0.0
        self.mode = MODE_CSP
        self.target_counts = 0
        self.target_vel = 0          # 0x60FF（counts/s）
        self.target_torque = 0       # 0x6071（‰ 額定）
        self.torque = 0.0            # 輸出 N·m
        self.current = 0.0           # A
        self.saturated = False
        self._tau_applied = 0.0      # PT/CST 經 torque slope 後的實際‰命令

        # CiA402 狀態機
        self.state = SWITCH_ON_DISABLED
        self.controlword = 0
        self.statusword = SW_BASE[SWITCH_ON_DISABLED]
        self.error_code = FAULT_NONE
        self._cw_prev = 0
        self.halt = False

        # Homing
        self.homing_active = False
        self.homed = False
        self._home_target_rad = 0.0

        # OD 儲存（可寫物件的當前值，以 phu_od 預設值種子）
        self._od = {}
        for (idx, sub), e in phu_od.OD.items():
            if e[2] in ("RW", "WO", "CONST"):
                self._od[(idx, sub)] = e[5]

    # ================= 相容屬性 =================
    @property
    def enabled(self):
        return self.state == OPERATION_ENABLED

    # ================= CiA402 狀態機 =================
    def apply_controlword(self, cw):
        cw &= 0xFFFF
        self.controlword = cw
        fault_reset_edge = (cw & 0x0080) and not (self._cw_prev & 0x0080)
        self._cw_prev = cw
        self.halt = bool(cw & 0x0100)

        # 故障狀態：只有 fault reset 上升緣能離開
        if self.state in (FAULT, FAULT_REACTION):
            if fault_reset_edge:
                self.error_code = FAULT_NONE
                self._set_state(SWITCH_ON_DISABLED)
            return

        cmd = cw & 0x000F
        ev = bool(cw & 0x0002)   # enable voltage
        qs = bool(cw & 0x0004)   # quick stop（高電位=不急停）

        # disable voltage（EV=0）：任何狀態 → switch-on-disabled
        if not ev:
            self._set_state(SWITCH_ON_DISABLED)
            return
        # quick stop（EV=1, QS=0）：op-enabled → quick-stop-active
        if ev and not qs:
            if self.state == OPERATION_ENABLED:
                self._set_state(QUICK_STOP_ACTIVE)
            return

        low = cw & 0x000F
        if low == 0x06:          # shutdown
            if self.state in (SWITCH_ON_DISABLED, SWITCHED_ON, OPERATION_ENABLED):
                self._set_state(READY)
        elif low == 0x07:        # switch on / disable operation
            if self.state in (READY, OPERATION_ENABLED, QUICK_STOP_ACTIVE):
                self._set_state(SWITCHED_ON)
        elif low == 0x0F:        # enable operation
            if self.state in (SWITCHED_ON, QUICK_STOP_ACTIVE, READY):
                self._enter_operation()

    def _enter_operation(self):
        # 切到位置模式前：把實際位置設為目標，避免使能瞬間跳動（手冊規定）
        if self.mode in POSITION_MODES:
            self.target_counts = int(round(self.q * CPR))
        self._set_state(OPERATION_ENABLED)

    def _set_state(self, st):
        self.state = st
        self.statusword = SW_BASE[st]
        if st != OPERATION_ENABLED:
            self.torque = 0.0
            self.current = 0.0
            self._tau_applied = 0.0
        if st == FAULT:
            self.statusword = SW_BASE[FAULT]

    def inject_fault(self, code=FAULT_FOLLOWING):
        """外部注入故障（如上位機測試 fault/recover）。"""
        self.error_code = code
        self.homing_active = False
        self._set_state(FAULT)

    # ================= 物件字典 =================
    def read_od(self, index, sub=0):
        # 即時計算值
        if index == 0x6041: return self.statusword
        if index == 0x6061: return self.mode & 0xFF
        if index == 0x603F: return self.error_code
        if index == 0x6064: return self._counts()                      # actual position
        if index == 0x6062: return self._counts()                      # position demand（簡化）
        if index == 0x606C: return int(round(self.qd * CPR))           # velocity actual
        if index == 0x60F4: return int(round((self.target_counts - self._counts())))  # following error
        if index == 0x6077: return int(round(self.torque / self.rated_torque * 1000.0))
        if index == 0x6078: return int(round(self.current / self.rated_current * 1000.0))
        if index == 0x6074: return int(round(self._tau_applied))       # torque demand ‰
        if index == 0x6075: return int(self.rated_current * 1000.0)    # mA
        if index == 0x6076: return int(self.rated_torque * 1000.0)     # mNm
        if index == 0x1000: return 0x00020192
        if index == 0x26A0: return self.node_id
        if index == 0x26A1: return 1000000
        # 儲存值（含 OD 預設）
        if (index, sub) in self._od:
            return self._od[(index, sub)]
        e = phu_od.entry(index, sub)
        return e[5] if e else 0

    def write_od(self, index, sub, value):
        """寫物件字典。回傳 True=成功 / False=不可寫（呼叫端應回 SDO abort）。"""
        if index == 0x6040:
            self.apply_controlword(value); return True
        if index == 0x6060:                       # 模式
            self.mode = self._i8(value)
            if self.mode in POSITION_MODES:       # 切位置模式 → 對齊目標
                self.target_counts = self._counts()
            return True
        if index == 0x607A: self.target_counts = self._i32(value); return True
        if index == 0x60FF: self.target_vel = self._i32(value); return True
        if index == 0x6071: self.target_torque = self._i16(value); return True
        e = phu_od.entry(index, sub)
        if e is not None:                         # 已知物件 → 依 OD 存取權
            if e[2] not in ("RW", "WO"):
                return False                      # RO/CONST → SDO abort 0x06010002
            self._od[(index, sub)] = value
            return True
        self._od[(index, sub)] = value            # 未知物件：寬鬆接受（實機保留區）
        return True

    # ================= 物理步進 =================
    def step(self, dt):
        if self.state == QUICK_STOP_ACTIVE:
            self._decelerate(dt); self._update_dynamic_sw(); return
        if self.state != OPERATION_ENABLED:
            self.torque = 0.0; self.current = 0.0; self._update_dynamic_sw(); return
        if self.halt:
            self._decelerate(dt); self._update_dynamic_sw(); return

        m = self.mode
        grav_ff = self.grav * math.sin(self.q)        # 重力前饋（內環補償）
        if m in POSITION_MODES:
            target_q = self.target_counts / CPR
            tau_cmd = self.Kp * (target_q - self.q) - self.Kd * self.qd + grav_ff
        elif m in VELOCITY_MODES:
            target_qd = self.target_vel / CPR
            tau_cmd = self.Kp * (target_qd - self.qd) + grav_ff + self.b * target_qd
        elif m in TORQUE_MODES:
            tau_cmd = self._slew_torque(dt)
        elif m == MODE_HM:
            tau_cmd = self._homing_step(dt)
        else:
            tau_cmd = 0.0

        self.saturated = abs(tau_cmd) > self.peak_torque
        tau_cmd = max(-self.peak_torque, min(self.peak_torque, tau_cmd))

        tau_grav = self.grav * math.sin(self.q)
        qdd = (tau_cmd - tau_grav - self.b * self.qd) / self.I
        self.qd += qdd * dt
        self.q += self.qd * dt

        self.torque = tau_cmd
        self.current = tau_cmd / self.Kt
        if m not in TORQUE_MODES:
            self._tau_applied = tau_cmd / self.rated_torque * 1000.0
        self._update_dynamic_sw()

    # ---- 各模式輔助 ----
    def _slew_torque(self, dt):
        """PT/CST/CSF：以 torque slope(0x6087, ‰/s) 逼近 target_torque(‰)。"""
        slope = self._od.get((0x6087, 0), 0)
        tgt = float(self.target_torque)
        if slope > 0:
            step = slope * dt
            if self._tau_applied < tgt:
                self._tau_applied = min(tgt, self._tau_applied + step)
            else:
                self._tau_applied = max(tgt, self._tau_applied - step)
        else:
            self._tau_applied = tgt
        return self._tau_applied / 1000.0 * self.rated_torque

    def _homing_step(self, dt):
        """mode 6：controlword bit4 觸發回零，收斂到 home offset 後置 attained。"""
        if (self.controlword & 0x0010) and not self.homing_active and not self.homed:
            self.homing_active = True
            self.homed = False
            off = self._od.get((0x6265, 0), self._od.get((0x2265, 0), 0))
            self._home_target_rad = off / CPR
        if self.homing_active:
            tau = self.Kp * (self._home_target_rad - self.q) - self.Kd * self.qd
            if abs(self._home_target_rad - self.q) < 1e-3 and abs(self.qd) < 1e-2:
                self.homing_active = False
                self.homed = True
            return tau
        return self.Kp * (self._home_target_rad - self.q) - self.Kd * self.qd if self.homed else 0.0

    def _decelerate(self, dt):
        """quick stop / halt：受控減速到 0 並保持位置（含重力前饋）。"""
        tau_grav = self.grav * math.sin(self.q)
        tau = tau_grav - self.Kd * self.qd          # 重力補償 + 阻尼煞停
        tau = max(-self.peak_torque, min(self.peak_torque, tau))
        qdd = (tau - tau_grav - self.b * self.qd) / self.I
        self.qd += qdd * dt
        self.q += self.qd * dt
        self.torque = tau
        self.current = tau / self.Kt

    def _update_dynamic_sw(self):
        """更新狀態字動態位元（target reached / setpoint-ack / homing attained）。"""
        if self.state not in (OPERATION_ENABLED, QUICK_STOP_ACTIVE):
            return
        sw = SW_BASE[self.state]
        m = self.mode
        reached = False
        if m in POSITION_MODES or m == MODE_PP:
            reached = abs(self.target_counts - self._counts()) < 100
            if reached: sw |= SW_SETPOINT_ACK
        elif m in VELOCITY_MODES:
            reached = abs(self.target_vel / CPR - self.qd) < 0.02
        elif m == MODE_HM:
            if self.homed: sw |= SW_SETPOINT_ACK | SW_TARGET_REACHED
        if reached:
            sw |= SW_TARGET_REACHED
        self.statusword = sw

    # ---- 工具 ----
    def _counts(self):
        return int(round(self.q * CPR))

    @staticmethod
    def _i32(v):
        v &= 0xFFFFFFFF
        return v - 0x100000000 if v & 0x80000000 else v

    @staticmethod
    def _i16(v):
        v &= 0xFFFF
        return v - 0x10000 if v & 0x8000 else v

    @staticmethod
    def _i8(v):
        v &= 0xFF
        return v - 0x100 if v & 0x80 else v
