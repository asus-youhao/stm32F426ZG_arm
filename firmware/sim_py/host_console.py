"""
host_console.py — 測試用「上位機」：對假馬達發送 CANopen 讀/寫 CMD,讀回數據

支援整份物件字典（phu_od.OD）的 SDO 讀寫,並有高階命令（enable/move/mode）。
可互動、可跑腳本、可 --demo。

用法：
  python3 host_console.py --demo          # 跑示範序列
  python3 host_console.py                  # 互動模式（輸入 help）
  echo "read 0x6041" | python3 host_console.py
"""
import sys
from can_bus import CanBus
from phu_motor import PhuMotor, CPR
from phu_od import OD, OD_BY_INDEX, od_name, od_access


class Host:
    def __init__(self, model="PHU17", node=1, log=False):
        self.bus = CanBus("CAN1", 0.002)
        self.motor = PhuMotor(node, model, "J_test")
        self.bus.add_node(self.motor)
        self.node = node
        self.bus.log = log

    # ---- 低階 CANopen CMD ----
    def read(self, index, sub=0):
        v = self.bus.sdo_read(self.node, index, sub)
        print("  READ  0x%04X:%02X (%-28s) = %d  (0x%X)" % (
            index, sub, od_name(index, sub), self._s(v), v & 0xFFFFFFFF))
        return v

    def write(self, index, value, sub=0):
        ok = self.bus.sdo_write(self.node, index, sub, value & 0xFFFFFFFF)
        print("  WRITE 0x%04X:%02X (%-28s) = %d  → %s" % (
            index, sub, od_name(index, sub), self._s(value),
            "OK" if ok else "ABORT (唯讀 0x06010002)"))
        return ok

    # ---- PDO（即時控制 + 步進物理）----
    def pdo(self, cw, target):
        sw, ap = self.bus.pdo_csp(self.node, cw, target)
        return sw, ap

    # ---- 高階 ----
    def enable(self):
        for cw in (0x80, 0x06, 0x07, 0x0F):
            self.pdo(cw, int(self.motor.q * CPR))
        print("  ENABLE → statusword=0x%04X (%s)" % (
            self.motor.statusword, "OP_ENABLED" if self.motor.enabled else "?"))

    def move(self, target_counts, steps=300):
        for _ in range(steps):
            self.pdo(0x0F, target_counts)
        print("  MOVE  target=%d → actual=%d  力=%.2f N·m  電流=%.2f A" % (
            target_counts, int(self.motor.q * CPR), self.motor.torque, self.motor.current))

    def dump(self, prefix=None):
        """讀回整份（或某段）OD 的目前值"""
        n = 0
        for (i, s, acc, dt, dflt, nm) in OD:
            if prefix is not None and (i >> 8) != prefix:
                continue
            v = self.motor.read_od(i, s)
            print("  0x%04X:%02X %-3s %-9s = %-12d %s" % (i, s, acc, dt, self._s(v), nm))
            n += 1
        print("  （共 %d 條）" % n)

    @staticmethod
    def _s(v):
        v &= 0xFFFFFFFF
        return v - 0x100000000 if v & 0x80000000 else v


def run_demo(h):
    print("=== 測試上位機：對假馬達發送 CANopen 讀/寫 ===\n")
    print("[1] 讀身分/組態（SDO read）")
    h.read(0x1000)          # device type
    h.read(0x26A0)          # node id
    h.read(0x26A1)          # baudrate
    h.read(0x6076)          # rated torque mNm
    h.read(0x6075)          # rated current mA

    print("\n[2] 寫參數（SDO write）+ 讀回驗證")
    h.write(0x6060, 8)      # mode CSP
    h.read(0x6061)          # mode display
    h.write(0x6065, 5000)   # following error window（RW 參數）
    h.read(0x6065)

    print("\n[3] 嘗試寫唯讀物件（應 ABORT）")
    h.write(0x6041, 0x1234) # statusword RO
    h.write(0x6064, 999)    # actual position RO

    print("\n[4] 使能 + 移動,讀回即時數據（力/電流/位置）")
    h.enable()
    h.move(150000)
    print("\n  即時讀回：")
    h.read(0x6064)          # actual position
    h.read(0x6077)          # actual torque ‰
    h.read(0x6078)          # actual current ‰
    h.read(0x606C)          # actual velocity

    print("\n[5] 開 frame log 觀察一次 SDO 讀的 CAN 幀")
    h.bus.log = True
    h.read(0x6041)
    h.bus.log = False

    print("\n[6] OD 製造商區段(0x26xx)抽樣讀回")
    cnt = 0
    for (i, s, acc, dt, dflt, nm) in OD:
        if (i >> 8) == 0x26:
            h.read(i, s); cnt += 1
        if cnt >= 4:
            break

    print("\nOD 總條目數：%d（完整清單見 phu_od.py / dump 指令）" % len(OD))


def interactive(h):
    print("互動模式。指令：read <idx> [sub] / write <idx> <val> [sub] / "
          "enable / move <counts> / mode <n> / dump [prefix] / quit")
    for line in sys.stdin:
        t = line.split()
        if not t: continue
        c = t[0].lower()
        try:
            if c in ("quit", "exit"): break
            elif c == "read":  h.read(int(t[1], 0), int(t[2], 0) if len(t) > 2 else 0)
            elif c == "write": h.write(int(t[1], 0), int(t[2], 0), int(t[3], 0) if len(t) > 3 else 0)
            elif c == "enable": h.enable()
            elif c == "move":  h.move(int(t[1], 0))
            elif c == "mode":  h.write(0x6060, int(t[1], 0), 0)
            elif c == "dump":  h.dump(int(t[1], 0) if len(t) > 1 else None)
            else: print("  ?")
        except (IndexError, ValueError) as e:
            print("  用法錯誤:", e)


if __name__ == "__main__":
    h = Host(model="PHU17", node=1)
    if "--demo" in sys.argv:
        run_demo(h)
    else:
        interactive(h)
