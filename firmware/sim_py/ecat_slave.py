# -*- coding: utf-8 -*-
"""
ecat_slave.py — PC 端 EtherCAT(CoE) 假從站：模擬 EYOU PHU 關節（SIL-C / HIL-1）

對應 CANopen 路線的 can_slave.py：CiA402/馬達物理復用同一份 phu_motor.PhuMotor
（read_od/write_od/apply_controlword/step 一行不改），本檔只新寫 EtherCAT 傳輸層：

  EscSim        — 單顆 ESC 暫存器空間模擬（4KB regs + 8KB 過程/信箱 RAM）
                  · SII EEPROM（參數取自原廠 ESI：Vendor 0x1097 / Product 0x00010002 /
                    SM0 MBoxOut@0x1000×0x80 / SM1 MBoxIn@0x1080×0x80 /
                    SM2 Outputs@0x1100 / SM3 Inputs@0x1400）
                  · ESM AL 狀態機（INIT→PREOP→SAFEOP→OP，含非法跳轉拒絕 + AL status code）
                  · CoE mailbox SDO（expedited 讀/寫 → phu_motor OD；RO 寫回 abort 0x06010002）
                  · PDO 重映射（0x1600/0x1A00 + 0x1C12/0x1C13；>6 entries 回 abort ≒ 真機 E405；
                    非 PREOP 改映射回 abort 0x08000022 ≒ ESM Changed 規則）
                  · FMMU 邏輯位址映射 + LRD/LWR/LRW 過程資料（WKC：讀+1 寫+2）
                  · SM 看門狗（OP 中斷流 → SAFEOP + AL code 0x001B，馬達失能）
  EcatChain     — 多顆從站菊鏈：一個 frame 依序流過所有 ESC（自動遞增定址）
  raw socket 模式 — Linux/WSL AF_PACKET 綁 NIC/veth，供真 SOEM 主站對打
  --selftest    — 離線（免網卡）用 MiniMaster 走完 SOEM 等效流程

已知限制（規劃 §6.3）：軟體從站無 ESC 硬體 on-the-fly 轉發，不模擬真實傳播延遲與
DC 分散時鐘品質；DC 暫存器（0x0900 區）僅佔位可讀寫。時序驗證交 HIL-1'/HIL-2。

用法：
  python ecat_slave.py --selftest                # 離線協定全驗（Windows/Linux 皆可）
  sudo python3 ecat_slave.py --iface veth1       # 真 SOEM 對打（WSL/Linux，需 root）
  sudo python3 ecat_slave.py --iface veth1 --axes 7 --verbose
"""
import argparse
import struct
import sys
import time

import phu_motor

# ============================ 常數 ============================

ETH_P_ECAT = 0x88A4

# EtherCAT datagram 命令
CMD_NOP, CMD_APRD, CMD_APWR, CMD_APRW, CMD_FPRD, CMD_FPWR, CMD_FPRW, \
    CMD_BRD, CMD_BWR, CMD_BRW, CMD_LRD, CMD_LWR, CMD_LRW, CMD_ARMW, CMD_FRMW = range(15)

# AL 狀態
AL_INIT, AL_PREOP, AL_BOOT, AL_SAFEOP, AL_OP = 0x01, 0x02, 0x03, 0x04, 0x08
AL_ERR = 0x10
ALCODE_OK, ALCODE_INVALID_CHANGE, ALCODE_WATCHDOG, ALCODE_BOOT_UNSUP = 0x0000, 0x0011, 0x001B, 0x0013

# 暫存器位址（ETG.1000.4）
REG_TYPE, REG_STADR, REG_ALIAS = 0x0000, 0x0010, 0x0012
REG_DLCTL, REG_DLSTAT = 0x0100, 0x0110
REG_ALCTL, REG_ALSTAT, REG_ALCODE = 0x0120, 0x0130, 0x0134
REG_PDICTL = 0x0140
REG_EEPCFG, REG_EEPCTL, REG_EEPADR, REG_EEPDAT = 0x0500, 0x0502, 0x0504, 0x0508
REG_FMMU0, REG_SM0, REG_DC = 0x0600, 0x0800, 0x0900
NFMMU, NSM = 4, 4

# 物理 RAM（ESI <Sm>）
MBX_OUT_ADR, MBX_OUT_LEN = 0x1000, 0x80    # SM0：主站→從站
MBX_IN_ADR,  MBX_IN_LEN  = 0x1080, 0x80    # SM1：從站→主站
SM2_ADR, SM3_ADR         = 0x1100, 0x1400  # Outputs / Inputs
RAM_BASE, RAM_SIZE       = 0x1000, 0x2000

# SII 身分（ESI <Vendor><Id>/<Type>）
SII_VENDOR, SII_PRODUCT, SII_REV = 0x00001097, 0x00010002, 0x00000001
SII_CONFIGDATA = bytes.fromhex("050E03440A0000000000")   # ESI <Eeprom><ConfigData>

# 出廠 PDO 映射（精簡版＝韌體 ec_out_t/ec_in_t：cw+target / sw+pos，各 6B）
DEFAULT_RXPDO = [0x60400010, 0x607A0020]   # controlword u16 + target position i32
DEFAULT_TXPDO = [0x60410010, 0x60640020]   # statusword u16 + position actual i32
MAX_PDO_ENTRIES = 6                        # 真機限制：超過回 E405（手冊 0xFF33）

SDO_ABORT_RO      = 0x06010002             # 寫唯讀物件
SDO_ABORT_STATE   = 0x08000022             # 目前裝置狀態不允許（非 PREOP 改映射）
SDO_ABORT_UNSUP   = 0x06010000             # 不支援的存取
SDO_ABORT_TOOMANY = 0x06040042             # PDO 長度/數量超限（≒真機 E405）


def _u16(b, o): return b[o] | (b[o + 1] << 8)
def _u32(b, o): return _u16(b, o) | (_u16(b, o + 2) << 16)
def _p16(v): return struct.pack("<H", v & 0xFFFF)
def _p32(v): return struct.pack("<I", v & 0xFFFFFFFF)


def build_sii(position):
    """組 2KB SII EEPROM 影像（word 定址）。無 category（0xFFFF 結尾），
    SOEM 對缺 category 會用 mailbox words 的預設值——實測夠 config_init 用。"""
    sii = bytearray(2048)
    sii[0:10] = SII_CONFIGDATA                       # words 0-4：PDI 組態
    # word 7：ConfigData checksum（SOEM 不驗，填 0）
    struct.pack_into("<I", sii, 8 * 2, SII_VENDOR)   # word 8-9
    struct.pack_into("<I", sii, 10 * 2, SII_PRODUCT) # word 10-11
    struct.pack_into("<I", sii, 12 * 2, SII_REV)     # word 12-13
    struct.pack_into("<I", sii, 14 * 2, 0x1000 + position)  # serial
    # words 20-23：bootstrap mailbox（ESI <BootStrap>）
    struct.pack_into("<HHHH", sii, 20 * 2, MBX_OUT_ADR, MBX_OUT_LEN, MBX_IN_ADR, MBX_IN_LEN)
    # words 24-27：standard mailbox
    struct.pack_into("<HHHH", sii, 24 * 2, MBX_OUT_ADR, MBX_OUT_LEN, MBX_IN_ADR, MBX_IN_LEN)
    struct.pack_into("<H", sii, 28 * 2, 0x0004)      # word 28：mailbox protocol = CoE
    struct.pack_into("<H", sii, 62 * 2, 0x0001)      # word 62：size/version
    # word 64 起：General category（type 10, 16 words）——CoE details 讓 SOEM 走 CoE 讀 PDO 映射
    struct.pack_into("<HH", sii, 64 * 2, 0x000A, 16)
    gen = 66 * 2
    sii[gen + 0x07] = 0x0F                           # CoE：SDO|SDOinfo|PdoAssign|PdoConfig
    # SM category（type 41, 4 SM × 4 words）——SOEM 由此取得 SM2/SM3 實體位址（ESI <Sm>）
    smcat = 66 + 16
    struct.pack_into("<HH", sii, smcat * 2, 0x0029, 16)
    for i, (adr, ln, ctl, typ) in enumerate([
            (MBX_OUT_ADR, MBX_OUT_LEN, 0x26, 1),     # SM0 MbxOut
            (MBX_IN_ADR, MBX_IN_LEN, 0x22, 2),       # SM1 MbxIn
            (SM2_ADR, 33, 0x64, 3),                  # SM2 Outputs（DefaultSize 同 ESI）
            (SM3_ADR, 29, 0x20, 4)]):                # SM3 Inputs
        struct.pack_into("<HHBBBB", sii, (smcat + 2 + i * 4) * 2, adr, ln, ctl, 0, 1, typ)
    struct.pack_into("<H", sii, (smcat + 2 + 16) * 2, 0xFFFF)   # category end
    return sii


# ============================ 單顆 ESC ============================

class EscSim:
    """一顆 ESC + 一顆 PhuMotor。process(cmd, adp, ado, data, wkc) 回 (data, wkc)。"""

    def __init__(self, position, model=None, name="", cycle_s=0.001,
                 wd_ms=100, verbose=False):
        self.pos = position                     # 鏈上位置（自動遞增定址用）
        self.motor = phu_motor.PhuMotor(position + 1, model or "PHU17",
                                        name or ("ECAT_J%d" % (position + 1)))
        self.verbose = verbose
        self.cycle_s = cycle_s
        self.wd_ms = wd_ms
        self.regs = bytearray(0x1000)
        self.ram = bytearray(RAM_SIZE)          # 0x1000..0x2FFF
        self.sii = build_sii(position)
        self.al_state, self.al_code = AL_INIT, ALCODE_OK
        self.mbx_cnt = 1                        # 信箱序號（1..7）
        self.mbx_pending = None                 # 待取回應（放 SM1）
        self.last_pd_t = None                   # 看門狗：上次過程資料時間
        # CoE 物件（EtherCAT 專屬，不進 phu_motor OD）：PDO assign/mapping
        self.rxpdo = list(DEFAULT_RXPDO)
        self.txpdo = list(DEFAULT_TXPDO)
        struct.pack_into("<H", self.regs, REG_TYPE, 0x0904)   # type/rev 佔位
        self.regs[0x0004] = NFMMU                             # ESC info：FMMU 數
        self.regs[0x0005] = NSM                               # SM 數
        self.regs[0x0006] = 0x40                              # RAM size
        self.regs[0x0007] = 0x03                              # port0/1
        struct.pack_into("<H", self.regs, 0x0008, 0x0000)     # features：無 DC（SOEM 跳過 DC 組態）
        struct.pack_into("<H", self.regs, REG_DLSTAT, 0x0001) # PDI operational
        struct.pack_into("<H", self.regs, REG_ALSTAT, AL_INIT)

    def _log(self, fmt, *a):
        if self.verbose:
            print(("  [ESC%d] " % self.pos) + (fmt % a))

    # -------- 暫存器讀寫（含物理 RAM）--------
    def _read(self, adr, ln):
        out = bytearray(ln)
        for i in range(ln):
            a = adr + i
            if a < 0x1000:
                out[i] = self.regs[a]
            elif RAM_BASE <= a < RAM_BASE + RAM_SIZE:
                out[i] = self.ram[a - RAM_BASE]
        # 主站讀走 SM1 信箱最後一 byte → 清 mailbox full
        if self.mbx_pending is not None and adr <= MBX_IN_ADR + MBX_IN_LEN - 1 < adr + ln:
            self.mbx_pending = None
            self.regs[0x0805 + 8 * 1] &= ~0x08
        return bytes(out)

    def _write(self, adr, data):
        for i, v in enumerate(data):
            a = adr + i
            if a < 0x1000:
                self.regs[a] = v
            elif RAM_BASE <= a < RAM_BASE + RAM_SIZE:
                self.ram[a - RAM_BASE] = v
        # 副作用
        if adr <= REG_ALCTL < adr + len(data) or adr <= REG_ALCTL + 1 < adr + len(data):
            self._al_request(_u16(self.regs, REG_ALCTL))
        if adr <= REG_EEPCTL + 1 < adr + len(data):
            self._eeprom_cmd()
        # 主站寫完 SM0 信箱 → 處理 CoE 請求
        if self.al_state != AL_INIT and adr <= MBX_OUT_ADR < adr + len(data):
            self._mailbox_in(self.ram[MBX_OUT_ADR - RAM_BASE:
                                      MBX_OUT_ADR - RAM_BASE + MBX_OUT_LEN])
        # 主站寫過程資料輸出區（FPWR/LWR 直寫也算餵狗）
        out_adr, out_len = self._sm_area(2)
        if out_len and adr < out_adr + out_len and adr + len(data) > out_adr:
            self._apply_outputs()

    # -------- SII EEPROM 模擬 --------
    def _eeprom_cmd(self):
        ctl = _u16(self.regs, REG_EEPCTL)
        if ctl & 0x0100:                                  # read 命令
            wadr = _u32(self.regs, REG_EEPADR)
            off = (wadr * 2) % len(self.sii)
            self.regs[REG_EEPDAT:REG_EEPDAT + 8] = self.sii[off:off + 8].ljust(8, b"\x00")
            struct.pack_into("<H", self.regs, REG_EEPCTL, ctl & ~0x8100 | 0x0040)  # busy 清除、8B 支援

    # -------- AL 狀態機 --------
    _VALID = {AL_INIT: (AL_INIT, AL_PREOP),
              AL_PREOP: (AL_INIT, AL_PREOP, AL_SAFEOP),
              AL_SAFEOP: (AL_INIT, AL_PREOP, AL_SAFEOP, AL_OP),
              AL_OP: (AL_INIT, AL_PREOP, AL_SAFEOP, AL_OP)}

    def _al_request(self, ctl):
        req = ctl & 0x0F
        if ctl & AL_ERR:                                   # error ack
            self.al_code = ALCODE_OK
        if req == AL_BOOT:
            self._al_fail(ALCODE_BOOT_UNSUP); return
        if req not in self._VALID[self.al_state]:
            self._log("AL %02X→%02X 非法跳轉", self.al_state, req)
            self._al_fail(ALCODE_INVALID_CHANGE); return
        if req == AL_SAFEOP and not (self._sm_area(2)[1] and self._sm_area(3)[1]):
            self._al_fail(0x001D); return                  # SM 未配置
        self.al_state, self.al_code = req, ALCODE_OK
        if req == AL_OP:
            self.last_pd_t = time.monotonic()
            self._refresh_inputs()
        self._commit_al()
        self._log("AL → 0x%02X", req)

    def _al_fail(self, code):
        self.al_code = code
        self.al_state |= 0  # 維持現狀
        self._commit_al(err=True)

    def _commit_al(self, err=False):
        struct.pack_into("<H", self.regs, REG_ALSTAT,
                         self.al_state | (AL_ERR if err or self.al_code else 0))
        struct.pack_into("<H", self.regs, REG_ALCODE, self.al_code)

    # -------- SM / FMMU --------
    def _sm_area(self, n):
        o = REG_SM0 + 8 * n
        return _u16(self.regs, o), _u16(self.regs, o + 2)   # (實體位址, 長度)

    def _fmmus(self):
        out = []
        for i in range(NFMMU):
            o = REG_FMMU0 + 16 * i
            if self.regs[o + 12] & 0x01 or True:
                log = _u32(self.regs, o); ln = _u16(self.regs, o + 4)
                phys = _u16(self.regs, o + 8); typ = self.regs[o + 11]
                act = self.regs[o + 12]
                if act & 1 and ln:
                    out.append((log, ln, phys, typ))
        return out

    # -------- CoE mailbox --------
    def _mailbox_in(self, buf):
        mlen = _u16(buf, 0)
        if mlen < 2 or mlen + 6 > len(buf):
            return
        mtype = buf[5] & 0x0F
        if mtype != 0x03:                                   # 只支援 CoE
            return
        coe = _u16(buf, 6)
        service = (coe >> 12) & 0x07
        payload = buf[8:6 + mlen]
        if service == 0x02:                                 # SDO request
            resp = self._sdo(payload)
            # abort（cs=0x80）依 ETG 走 SDOREQ service；正常回應走 SDORES
            self._mailbox_reply(0x03, 0x2000 if resp[0] == 0x80 else 0x3000, resp)

    def _mailbox_reply(self, mtype, coe_hdr, payload):
        self.mbx_cnt = self.mbx_cnt % 7 + 1
        hdr = _p16(2 + len(payload)) + _p16(0) + bytes([0, (self.mbx_cnt << 4) | mtype])
        frame = hdr + _p16(coe_hdr) + payload
        base = MBX_IN_ADR - RAM_BASE
        self.ram[base:base + MBX_IN_LEN] = frame.ljust(MBX_IN_LEN, b"\x00")
        self.mbx_pending = True
        self.regs[0x0805 + 8 * 1] |= 0x08                   # SM1 mailbox full

    def _sdo(self, p):
        """expedited SDO：語意與 can_slave._on_sdo 對齊，外加 EtherCAT 專屬物件。"""
        cs = p[0]; index = _u16(p, 1); sub = p[3]
        ccs = cs & 0xE0
        if ccs == 0x40:                                     # upload
            v = self._od_read(index, sub)
            n = self._od_size(index, sub)
            self._log("SDO rd 0x%04X:%02X = 0x%08X (%dB)", index, sub, v & 0xFFFFFFFF, n)
            return bytes([0x43 | ((4 - n) << 2)]) + p[1:4] + _p32(v)
        if ccs == 0x20:                                     # download expedited
            val = _u32(p, 4)
            abort = self._od_write(index, sub, val)
            self._log("SDO wr 0x%04X:%02X = 0x%08X -> %s", index, sub, val,
                      "OK" if not abort else "ABORT 0x%08X" % abort)
            if abort:
                return bytes([0x80]) + p[1:4] + _p32(abort)
            return bytes([0x60]) + p[1:4] + _p32(0)
        return bytes([0x80]) + p[1:4] + _p32(SDO_ABORT_UNSUP)

    def _od_read(self, index, sub):
        if index == 0x1C00:                                 # SM comm type（SOEM readPDOmap 讀）
            return (4, 1, 2, 3, 4)[sub] if sub <= 4 else 0
        if index == 0x1C12: return 1 if sub == 0 else 0x1600
        if index == 0x1C13: return 1 if sub == 0 else 0x1A00
        if index == 0x1600:
            return len(self.rxpdo) if sub == 0 else (self.rxpdo[sub - 1] if sub <= len(self.rxpdo) else 0)
        if index == 0x1A00:
            return len(self.txpdo) if sub == 0 else (self.txpdo[sub - 1] if sub <= len(self.txpdo) else 0)
        return self.motor.read_od(index, sub)

    _TYPE_SIZE = {"INT8": 1, "UINT8": 1, "INT": 2, "UINT": 2, "INT16": 2, "UINT16": 2,
                  "DINT": 4, "UDINT": 4, "INT32": 4, "UINT32": 4, "STRING": 4}

    def _od_size(self, index, sub):
        """expedited SDO 回應的資料 byte 數（SOEM 會驗 size bits）。"""
        if index == 0x1C00 or ((index in (0x1C12, 0x1C13, 0x1600, 0x1A00)) and sub == 0):
            return 1
        if index in (0x1C12, 0x1C13):
            return 2
        if index in (0x1600, 0x1A00):
            return 4
        import phu_od
        e = phu_od.entry(index, sub)
        return self._TYPE_SIZE.get(e[1], 4) if e else 4

    def _od_write(self, index, sub, val):
        """回 0=成功，否則 SDO abort code。"""
        if index in (0x1600, 0x1A00, 0x1C12, 0x1C13):
            if self.al_state != AL_PREOP:
                return SDO_ABORT_STATE                      # ESM Changed：僅 PREOP 可改
            if index in (0x1600, 0x1A00) and sub == 0 and val > MAX_PDO_ENTRIES:
                return SDO_ABORT_TOOMANY                    # ≒ 真機 E405
            tbl = self.rxpdo if index == 0x1600 else self.txpdo
            if index in (0x1C12, 0x1C13):
                return 0                                    # assign 固定 0x1600/0x1A00，接受
            if sub == 0:
                del tbl[int(val):]
                while len(tbl) < val:
                    tbl.append(0)
            elif sub <= MAX_PDO_ENTRIES:
                while len(tbl) < sub:
                    tbl.append(0)
                tbl[sub - 1] = val
            return 0
        return 0 if self.motor.write_od(index, sub, val) else SDO_ABORT_RO

    # -------- 過程資料 --------
    @staticmethod
    def _pdo_bytes(entries):
        return sum(((e & 0xFF) + 7) // 8 for e in entries if e)

    def _apply_outputs(self):
        """SM2 buffer → 依 rxpdo 映射寫入馬達，並步進一個週期（＝can_slave._on_rpdo1）。"""
        if self.al_state != AL_OP:
            return
        base = self._sm_area(2)[0]
        if not base:
            return
        off = base - RAM_BASE
        for e in self.rxpdo:
            if not e:
                continue
            idx, sub, bits = e >> 16, (e >> 8) & 0xFF, e & 0xFF
            nb = (bits + 7) // 8
            raw = int.from_bytes(self.ram[off:off + nb], "little")
            self.motor.write_od(idx, sub, raw)
            off += nb
        self.motor.step(self.cycle_s)
        self.last_pd_t = time.monotonic()
        self._refresh_inputs()

    def _refresh_inputs(self):
        base = self._sm_area(3)[0] or SM3_ADR
        off = base - RAM_BASE
        for e in self.txpdo:
            if not e:
                continue
            idx, sub, bits = e >> 16, (e >> 8) & 0xFF, e & 0xFF
            nb = (bits + 7) // 8
            v = self.motor.read_od(idx, sub) & ((1 << (nb * 8)) - 1)
            self.ram[off:off + nb] = v.to_bytes(nb, "little")
            off += nb

    def watchdog_poll(self):
        """OP 中過程資料斷流 → SAFEOP + AL code 0x001B + 馬達失能。"""
        if self.al_state == AL_OP and self.wd_ms and self.last_pd_t is not None \
                and (time.monotonic() - self.last_pd_t) * 1000.0 > self.wd_ms:
            self._log("看門狗逾時 %d ms → SAFEOP", self.wd_ms)
            self.motor.write_od(0x6040, 0, 0x0000)          # 失能
            self.al_state, self.al_code = AL_SAFEOP, ALCODE_WATCHDOG
            self._commit_al(err=True)

    # -------- datagram 處理 --------
    def process(self, cmd, adp, ado, data, wkc):
        """回 (adp, data, wkc)。自動遞增定址會改 adp。"""
        ln = len(data)
        station = _u16(self.regs, REG_STADR)
        hit_rd = hit_wr = False

        if cmd in (CMD_APRD, CMD_APWR, CMD_APRW, CMD_ARMW):
            match = (adp == 0)
            adp = (adp + 1) & 0xFFFF
        elif cmd in (CMD_FPRD, CMD_FPWR, CMD_FPRW, CMD_FRMW):
            match = (adp == station and station != 0)
        elif cmd in (CMD_BRD, CMD_BWR, CMD_BRW):
            match = True
        elif cmd in (CMD_LRD, CMD_LWR, CMD_LRW):
            return adp, self._logical(cmd, adp, ado, data, wkc)
        else:
            return adp, (data, wkc)
        if not match:
            return adp, (data, wkc)

        if cmd in (CMD_APRD, CMD_FPRD, CMD_BRD, CMD_ARMW, CMD_FRMW):
            rb = self._read(ado, ln)
            if cmd == CMD_BRD:                              # 廣播讀：OR 疊加
                data = bytes(a | b for a, b in zip(data, rb))
            else:
                data = rb
            hit_rd = True
        if cmd in (CMD_APWR, CMD_FPWR, CMD_BWR):
            self._write(ado, data)
            hit_wr = True
        if cmd in (CMD_APRW, CMD_FPRW, CMD_BRW):
            rb = self._read(ado, ln)
            self._write(ado, data)
            data = rb
            hit_rd = hit_wr = True

        wkc += (1 if hit_rd and not hit_wr else 0) + (1 if hit_wr and not hit_rd else 0) \
             + (3 if hit_rd and hit_wr else 0)
        return adp, (data, wkc)

    def _logical(self, cmd, log_adr_lo, log_adr_hi, data, wkc):
        log_adr = log_adr_lo | (log_adr_hi << 16)
        buf = bytearray(data)
        did_rd = did_wr = False
        for fl, fln, fphys, ftyp in self._fmmus():
            lo = max(log_adr, fl)
            hi = min(log_adr + len(data), fl + fln)
            if lo >= hi:
                continue
            n = hi - lo
            phys = fphys + (lo - fl)
            doff = lo - log_adr
            if ftyp & 0x02 and cmd in (CMD_LWR, CMD_LRW):   # 主站→從站（outputs）
                self._write(phys, bytes(buf[doff:doff + n]))
                did_wr = True
            if ftyp & 0x01 and cmd in (CMD_LRD, CMD_LRW):   # 從站→主站（inputs）
                if self.al_state in (AL_SAFEOP, AL_OP):
                    self._refresh_inputs()
                buf[doff:doff + n] = self._read(phys, n)
                did_rd = True
        if cmd == CMD_LRW:
            wkc += (1 if did_rd else 0) + (2 if did_wr else 0)
        else:
            wkc += 1 if (did_rd or did_wr) else 0
        return bytes(buf), wkc


# ============================ 菊鏈 + frame 層 ============================

class EcatChain:
    """N 顆 ESC 菊鏈。process_frame(bytes) → bytes（處理後回給主站的 frame）。"""

    def __init__(self, n=1, model="PHU17", cycle_s=0.001, wd_ms=100, verbose=False):
        self.slaves = [EscSim(i, model, cycle_s=cycle_s, wd_ms=wd_ms, verbose=verbose)
                       for i in range(n)]

    def poll(self):
        for s in self.slaves:
            s.watchdog_poll()

    def process_frame(self, frame):
        if len(frame) < 16 or ((frame[12] << 8) | frame[13]) != ETH_P_ECAT:  # EtherType 走網路序
            return None
        out = bytearray(frame)
        ehdr = _u16(frame, 14)
        if (ehdr >> 12) & 0x0F != 1:                        # 只吃 type 1（datagrams）
            return None
        off = 16
        end = 16 + (ehdr & 0x07FF)
        while off + 12 <= end:
            cmd = frame[off]
            adp = _u16(frame, off + 2)
            ado = _u16(frame, off + 4)
            lenw = _u16(frame, off + 6)
            dlen = lenw & 0x07FF
            more = bool(lenw & 0x8000)
            data = bytes(frame[off + 10:off + 10 + dlen])
            wkc = _u16(frame, off + 10 + dlen)
            for s in self.slaves:
                adp, (data, wkc) = s.process(cmd, adp, ado, data, wkc)
            struct.pack_into("<H", out, off + 2, adp)
            out[off + 10:off + 10 + dlen] = data
            struct.pack_into("<H", out, off + 10 + dlen, wkc)
            off += 12 + dlen
            if not more:
                break
        return bytes(out)


# ============================ 離線 MiniMaster（selftest 用）============================

class MiniMaster:
    """免網卡：直接以 frame bytes 打 EcatChain，等效 SOEM 的最小流程。"""

    def __init__(self, chain):
        self.chain = chain
        self.idx = 0

    def dg(self, cmd, adp, ado, data):
        """單 datagram frame 往返，回 (data, wkc)。"""
        self.idx = (self.idx + 1) & 0xFF
        d = struct.pack("<BBHHHH", cmd, self.idx, adp, ado, len(data), 0) + data + b"\x00\x00"
        f = b"\xff" * 6 + b"\x02\x00\x00\x00\x00\x01" + struct.pack(">H", ETH_P_ECAT) \
            + _p16((len(d) & 0x7FF) | 0x1000) + d
        r = self.chain.process_frame(f)
        assert r is not None, "frame 被拒收"
        dlen = len(data)
        return r[26:26 + dlen], _u16(r, 26 + dlen)

    # ---- 便利包裝 ----
    def brd16(self, ado):
        d, w = self.dg(CMD_BRD, 0, ado, b"\x00\x00")
        return _u16(d, 0), w

    def fpwr(self, station, ado, data):
        return self.dg(CMD_FPWR, station, ado, data)[1]

    def fprd(self, station, ado, ln):
        return self.dg(CMD_FPRD, station, ado, b"\x00" * ln)[0]

    def apwr(self, pos, ado, data):
        return self.dg(CMD_APWR, (-pos) & 0xFFFF, ado, data)[1]

    def sii_read32(self, station, wadr):
        self.fpwr(station, REG_EEPADR, _p32(wadr))
        self.fpwr(station, REG_EEPCTL, _p16(0x0100))
        return _u32(self.fprd(station, REG_EEPDAT, 4), 0)

    def al_set(self, station, state, timeout=1.0):
        self.fpwr(station, REG_ALCTL, _p16(state | AL_ERR))
        return _u16(self.fprd(station, REG_ALSTAT, 2), 0)

    def sdo_rd(self, station, index, sub):
        req = bytes([0x40]) + _p16(index)[0:2] + bytes([sub]) + b"\x00" * 4
        return self._mbx_sdo(station, req)

    def sdo_wr(self, station, index, sub, val):
        req = bytes([0x23]) + _p16(index)[0:2] + bytes([sub]) + _p32(val)
        return self._mbx_sdo(station, req)

    def _mbx_sdo(self, station, sdo):
        mbx = _p16(2 + len(sdo)) + _p16(0) + bytes([0, 0x13]) + _p16(0x2000) + sdo
        self.fpwr(station, MBX_OUT_ADR, mbx.ljust(MBX_OUT_LEN, b"\x00"))
        st = self.fprd(station, 0x0805 + 8, 1)[0]           # SM1 status
        assert st & 0x08, "SM1 無回應"
        r = self.fprd(station, MBX_IN_ADR, MBX_IN_LEN)
        cs = r[8]
        val = _u32(r, 12)
        return cs, val                                       # (SDO command byte, 資料/abort code)


# ============================ selftest ============================

def selftest():
    ok = [0, 0]

    def chk(cond, msg):
        ok[0] += 1
        if not cond:
            ok[1] += 1
            print("  FAIL: " + msg)
        else:
            print("    ok: " + msg)

    print("== ecat_slave 離線 selftest（等效 SOEM 流程）==")
    chain = EcatChain(2, verbose=False, wd_ms=0)             # 兩顆：驗 WKC 疊加
    m = MiniMaster(chain)
    S1, S2 = 0x1001, 0x1002

    # 1. 掃鏈：BRD WKC = 從站數
    _, wkc = m.brd16(REG_TYPE)
    chk(wkc == 2, "BRD 掃鏈 WKC=2（兩顆從站）")

    # 2. 設定站址（自動遞增：pos0→S1, pos1→S2）
    m.dg(CMD_APWR, 0, REG_STADR, _p16(S1))
    m.dg(CMD_APWR, 0xFFFF, REG_STADR, _p16(S2))
    chk(_u16(m.fprd(S1, REG_STADR, 2), 0) == S1, "FPRD 站址 S1")
    chk(_u16(m.fprd(S2, REG_STADR, 2), 0) == S2, "FPRD 站址 S2")

    # 3. SII：身分與 mailbox 參數（對照原廠 ESI）
    chk(m.sii_read32(S1, 8) == SII_VENDOR, "SII vendor = 0x1097")
    chk(m.sii_read32(S1, 10) == SII_PRODUCT, "SII product = 0x00010002")
    mbx = m.sii_read32(S1, 24)
    chk((mbx & 0xFFFF) == MBX_OUT_ADR and (mbx >> 16) == MBX_OUT_LEN,
        "SII std mailbox out @0x1000×0x80")

    # 4. ESM：非法跳轉拒絕 + 正常爬升 INIT→PREOP
    st = m.al_set(S1, AL_OP)
    chk(st & AL_ERR and st & 0x0F == AL_INIT, "INIT→OP 非法跳轉被拒（AL err）")
    chk(_u16(m.fprd(S1, REG_ALCODE, 2), 0) == ALCODE_INVALID_CHANGE, "AL code=0x0011")
    for s in (S1, S2):                                       # SM0/SM1 mailbox 組態
        m.fpwr(s, REG_SM0, _p16(MBX_OUT_ADR) + _p16(MBX_OUT_LEN) + bytes([0x26, 0, 1, 0]))
        m.fpwr(s, REG_SM0 + 8, _p16(MBX_IN_ADR) + _p16(MBX_IN_LEN) + bytes([0x22, 0, 1, 0]))
        st = m.al_set(s, AL_PREOP)
        chk(st == AL_PREOP, "站 0x%04X INIT→PREOP" % s)

    # 5. CoE SDO：讀裝置型號 / 模式切換 / RO abort（語意 = can_slave selftest）
    cs, v = m.sdo_rd(S1, 0x1000, 0)
    chk(cs == 0x43 and v == 0x00020192, "SDO rd 0x1000 = 0x00020192")
    cs, _ = m.sdo_wr(S1, 0x6060, 0, 8)                       # CSP
    chk(cs == 0x60, "SDO wr 0x6060=8 (CSP)")
    cs, v = m.sdo_rd(S1, 0x6061, 0)
    chk((cs & 0xE3) == 0x43 and v == 8, "SDO rd 0x6061 = 8")
    cs, v = m.sdo_wr(S1, 0x6064, 0, 123)                     # RO
    chk(cs == 0x80 and v == SDO_ABORT_RO, "寫 RO 0x6064 → abort 0x06010002")

    # 6. PDO 重映射：>6 entries → E405 等效 abort；合法重映射成功
    cs, v = m.sdo_wr(S1, 0x1600, 0, 7)
    chk(cs == 0x80 and v == SDO_ABORT_TOOMANY, "0x1600:00=7 → abort（≒真機 E405）")
    for s in (S1, S2):
        m.sdo_wr(s, 0x1600, 0, 2)
        m.sdo_wr(s, 0x1600, 1, 0x60400010)
        m.sdo_wr(s, 0x1600, 2, 0x607A0020)
    cs, v = m.sdo_rd(S1, 0x1600, 0)
    chk((cs & 0xE3) == 0x43 and v == 2, "重映射後 0x1600:00 = 2")

    # 7. SM2/SM3 + FMMU + SAFEOP→OP（邏輯位址：S1 out@0, S2 out@6, in 從 12 起）
    OUT_B, IN_B = 6, 6
    for i, s in enumerate((S1, S2)):
        m.fpwr(s, REG_SM0 + 16, _p16(SM2_ADR) + _p16(OUT_B) + bytes([0x64, 0, 1, 0]))
        m.fpwr(s, REG_SM0 + 24, _p16(SM3_ADR) + _p16(IN_B) + bytes([0x20, 0, 1, 0]))
        fmmu0 = _p32(i * OUT_B) + _p16(OUT_B) + bytes([0, 7]) + _p16(SM2_ADR) + bytes([0, 0x02, 1, 0, 0, 0])
        fmmu1 = _p32(12 + i * IN_B) + _p16(IN_B) + bytes([0, 7]) + _p16(SM3_ADR) + bytes([0, 0x01, 1, 0, 0, 0])
        m.fpwr(s, REG_FMMU0, fmmu0)
        m.fpwr(s, REG_FMMU0 + 16, fmmu1)
        chk(m.al_set(s, AL_SAFEOP) == AL_SAFEOP, "站 0x%04X → SAFEOP" % s)
        chk(m.al_set(s, AL_OP) == AL_OP, "站 0x%04X → OP" % s)

    # 8. 1kHz LRW 週期：CiA402 使能三步 + CSP 目標跟隨；WKC = 2×(1+2) = 6
    lbuf = bytearray(24)

    def cycle(cw, tgt):
        struct.pack_into("<HiHi", lbuf, 0, cw, tgt, cw, tgt)
        d, w = m.dg(CMD_LRW, 0, 0, bytes(lbuf))
        sw1, p1 = struct.unpack_from("<Hi", d, 12)
        sw2, p2 = struct.unpack_from("<Hi", d, 18)
        return w, sw1, p1, sw2, p2

    w, sw1, _, sw2, _ = cycle(0x0006, 0)
    chk(w == 6, "LRW WKC=6（兩顆 out+in）")
    chk(sw1 & 0x6F == 0x21 and sw2 & 0x6F == 0x21, "cw=0x06 → sw=ready(0x21)")
    w, sw1, _, _, _ = cycle(0x0007, 0)
    chk(sw1 & 0x6F == 0x23, "cw=0x07 → sw=switched-on(0x23)")
    w, sw1, _, sw2, _ = cycle(0x000F, 0)
    chk(sw1 & 0x6F == 0x27 and sw2 & 0x6F == 0x27, "cw=0x0F → sw=operation-enabled(0x27)")
    tgt = 5000
    p_last = 0
    for _ in range(400):
        w, _, p_last, _, _ = cycle(0x000F, tgt)
    chk(abs(p_last - tgt) < 200, "CSP 400 週期後位置跟隨 target=5000（實測 %d）" % p_last)

    # 9. 看門狗：斷流 → SAFEOP + AL code 0x001B
    wd = EcatChain(1, wd_ms=20)
    mm = MiniMaster(wd)
    mm.dg(CMD_APWR, 0, REG_STADR, _p16(S1))
    mm.fpwr(S1, REG_SM0, _p16(MBX_OUT_ADR) + _p16(MBX_OUT_LEN) + bytes([0x26, 0, 1, 0]))
    mm.fpwr(S1, REG_SM0 + 8, _p16(MBX_IN_ADR) + _p16(MBX_IN_LEN) + bytes([0x22, 0, 1, 0]))
    mm.al_set(S1, AL_PREOP)
    mm.fpwr(S1, REG_SM0 + 16, _p16(SM2_ADR) + _p16(OUT_B) + bytes([0x64, 0, 1, 0]))
    mm.fpwr(S1, REG_SM0 + 24, _p16(SM3_ADR) + _p16(IN_B) + bytes([0x20, 0, 1, 0]))
    mm.fpwr(S1, REG_FMMU0, _p32(0) + _p16(OUT_B) + bytes([0, 7]) + _p16(SM2_ADR) + bytes([0, 0x02, 1, 0, 0, 0]))
    mm.fpwr(S1, REG_FMMU0 + 16, _p32(OUT_B) + _p16(IN_B) + bytes([0, 7]) + _p16(SM3_ADR) + bytes([0, 0x01, 1, 0, 0, 0]))
    mm.al_set(S1, AL_SAFEOP)
    mm.al_set(S1, AL_OP)
    mm.dg(CMD_LRW, 0, 0, b"\x0f\x00" + b"\x00" * 10)
    time.sleep(0.05)
    wd.poll()
    st = _u16(mm.fprd(S1, REG_ALSTAT, 2), 0)
    code = _u16(mm.fprd(S1, REG_ALCODE, 2), 0)
    chk(st & 0x0F == AL_SAFEOP and code == ALCODE_WATCHDOG,
        "看門狗 20ms 斷流 → SAFEOP + AL code 0x001B")

    print("\n總計 %d 檢查, %d 失敗 → %s" % (ok[0], ok[1], "PASS" if ok[1] == 0 else "FAIL"))
    return 1 if ok[1] else 0


# ============================ raw socket 模式（Linux/WSL）============================

def run_raw(iface, axes, model, cycle_s, wd_ms, verbose):
    import socket
    chain = EcatChain(axes, model, cycle_s=cycle_s, wd_ms=wd_ms, verbose=verbose)
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ECAT))
    s.bind((iface, 0))
    s.settimeout(0.02)
    print("[ecat_slave] %d 軸 (%s) 掛在 %s，等待主站…（Ctrl-C 結束）" % (axes, model, iface))
    n = 0
    try:
        while True:
            chain.poll()
            try:
                f = s.recv(4096)
            except socket.timeout:
                continue
            r = chain.process_frame(f)
            if r:
                s.send(r)
                n += 1
                if verbose and n % 1000 == 0:
                    print("[ecat_slave] 已處理 %d frame" % n)
    except KeyboardInterrupt:
        print("\n[ecat_slave] 結束，共處理 %d frame" % n)


def run_npcap(iface_match, axes, model, cycle_s, wd_ms, verbose):
    """Windows：scapy + npcap 傳輸（HIL-1 用,延遲 ms 級只驗協定不驗時序）。
    npcap 會把自己送出的幀再抓回來 → 用「最近送出集合」防回音重處理。"""
    from collections import deque
    from scapy.all import conf
    import scapy.arch.windows as w
    ifs = w.get_windows_if_list()
    hit = [i for i in ifs if iface_match.lower() in (i.get('description') or '').lower()
           or iface_match == i.get('name')]
    if not hit:
        raise SystemExit("找不到介面（--iface 給描述關鍵字,如 Realtek）")
    name = hit[0]['name']
    s = conf.L2socket(iface=name, filter="ether proto 0x88a4")
    try:                                    # npcap 立即交付（去掉核心緩衝延遲）
        from scapy.libs.winpcapy import pcap_setmintocopy
        pcap_setmintocopy(s.ins.pcap, 0)
    except Exception:
        pass
    chain = EcatChain(axes, model, cycle_s=cycle_s, wd_ms=wd_ms, verbose=verbose)
    sent = deque(maxlen=16)
    print("[ecat_slave] %d 軸 (%s) 掛在 npcap:%s（Ctrl-C 結束）" % (axes, model, name))
    n = 0
    try:
        while True:
            chain.poll()
            p = s.recv(1600)
            if p is None:
                continue
            f = bytes(p)
            if f in sent:                   # 自己的回音,跳過
                continue
            r = chain.process_frame(f)
            if r:
                sent.append(r)
                s.send(r)
                n += 1
                if verbose and n % 200 == 0:
                    print("[ecat_slave] 已處理 %d frame" % n)
    except KeyboardInterrupt:
        print("\n[ecat_slave] 結束，共處理 %d frame" % n)


def main():
    ap = argparse.ArgumentParser(description="EtherCAT(CoE) PHU 假從站（SIL-C/HIL-1）")
    ap.add_argument("--selftest", action="store_true", help="離線協定自我測試")
    ap.add_argument("--iface", help="raw socket 介面（veth1/eth1…，需 root）")
    ap.add_argument("--axes", type=int, default=1, help="從站數（預設 1）")
    ap.add_argument("--model", default="PHU17")
    ap.add_argument("--cycle-ms", type=float, default=1.0, help="馬達步進週期（預設 1ms）")
    ap.add_argument("--wd-ms", type=int, default=100, help="過程資料看門狗（0=關）")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        sys.exit(selftest())
    if not a.iface:
        ap.error("需 --selftest 或 --iface")
    if sys.platform == "win32":
        run_npcap(a.iface, a.axes, a.model, a.cycle_ms / 1000.0, a.wd_ms, a.verbose)
    else:
        run_raw(a.iface, a.axes, a.model, a.cycle_ms / 1000.0, a.wd_ms, a.verbose)


if __name__ == "__main__":
    main()
