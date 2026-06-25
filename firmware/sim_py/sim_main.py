"""
sim_main.py — Python 假硬體：雙臂 CANopen 資料流 + 每顆馬達扭矩/電流

情境：
  1) 建立 14 顆 PHU 馬達（左/右各 7,依 robot.JOINT_MAP）。
  2) CANopen 交握：NMT → SDO 設模式 CSP → SDO 使能（印 frame）。
  3) 1kHz：joint-space 將各軸從 0 緩升到初始姿態（抗重力）→ 觀察每顆馬達的力(N·m)與電流(A)。
  4) 對左肘 +0.5 rad,觀察扭矩/電流暫態。
  5) 用 SDO 讀回 0x6077(扭矩)/0x6078(電流) 示範「讀」資料流。
"""
import math
from phu_motor import PhuMotor, CPR
from can_bus import CanBus
import robot

DT = 0.001  # 1 kHz


def build():
    buses = {"L": CanBus("CAN1-左臂", DT), "R": CanBus("CAN2-右臂", DT)}
    motors = []   # 依 JOINT_MAP 順序（0..13）
    for name, arm, bus, node, model in robot.JOINT_MAP:
        m = PhuMotor(node, model, name)
        buses[bus].add_node(m)
        motors.append((m, buses[bus]))
    return buses, motors


def canopen_init(buses, motors, log=False):
    for b in buses.values():
        b.log = log
        b.nmt(0x82)          # reset comm
        b.nmt(0x80)          # pre-op
    for m, b in motors:
        b.sdo_write(m.node_id, 0x6060, 0, 8, 1)   # mode = CSP
    for b in buses.values():
        b.nmt(0x01)          # start
    # 使能：fault reset → 0x06 → 0x07 → 0x0F
    for m, b in motors:
        for cw in (0x80, 0x06, 0x07, 0x0F):
            b.sdo_write(m.node_id, 0x6040, 0, cw, 2)
    for b in buses.values():
        b.log = False


def print_table(tick, motors, idxs):
    print("  t=%4dms | %-16s %-6s | tgt(rad) act(rad) |  力 N·m  電流 A | sat" % (
        tick, "joint", "model"))
    print("  ---------+-------------------------+-------------------+----------------+----")
    for i in idxs:
        m, _ = motors[i]
        tgt = m.target_counts / CPR
        print("           | %-16s %-6s | %7.3f  %7.3f | %7.2f %7.2f | %s" % (
            m.name, m.model, tgt, m.q, m.torque, m.current, "Y" if m.saturated else "-"))
    print()


def main():
    print("=== EYOU PHU 雙臂 假硬體（Python）— CANopen 資料流 + 馬達力/電流 ===\n")

    buses, motors = build()

    print("[A] joint → PHU 馬達對應（URDF-ready,見 robot.JOINT_MAP / urdf_loader.py）")
    for name, arm, bus, node, model in robot.JOINT_MAP[:7]:
        print("    %-18s bus=%s node=%d  %s" % (name, bus, node, model))
    print("    （右臂同構,bus=R node=1..7）\n")

    print("[B] CANopen 交握（NMT / SDO 設模式 / 使能）— 顯示左臂前段 frame")
    # 只開左臂 log 看交握片段
    buses["L"].log = True
    buses["L"].nmt(0x82); buses["L"].nmt(0x80)
    buses["L"].sdo_write(1, 0x6060, 0, 8, 1)
    buses["L"].sdo_write(1, 0x6040, 0, 0x06, 2)
    buses["L"].log = False
    # 其餘節點靜默初始化
    canopen_init(buses, motors, log=False)
    print("    ... (其餘節點同樣流程)\n")

    # joint-space 目標與緩升（每軸 vmax）
    goal = list(robot.Q_INIT) * 2           # 14 軸
    cmd = [0.0] * 14
    vmax = 1.0                              # rad/s

    print("[C] 1kHz：各軸緩升到初始姿態（抗重力）→ 觀察每顆馬達 力/電流")
    show = [0, 1, 3, 4]   # L_J1(PHU20) L_J2(PHU20) L_J4(PHU17肘) L_J5(PHU14腕)
    for t in range(1, 1201):
        for i, (m, b) in enumerate(motors):
            # joint-space 緩升
            d = goal[i] - cmd[i]
            step = vmax * DT
            if d >  step: d = step
            if d < -step: d = -step
            cmd[i] += d
            b.pdo_csp(m.node_id, 0x0F, int(round(cmd[i] * CPR)))
        if t % 400 == 0:
            print_table(t, motors, show)

    print("[D] 對左肘 (L_J4) 再 +0.5 rad → 觀察扭矩/電流暫態")
    goal[3] += 0.5
    for t in range(1201, 1801):
        for i, (m, b) in enumerate(motors):
            d = goal[i] - cmd[i]; step = vmax * DT
            if d >  step: d = step
            if d < -step: d = -step
            cmd[i] += d
            b.pdo_csp(m.node_id, 0x0F, int(round(cmd[i] * CPR)))
        if t % 200 == 0:
            print_table(t, motors, show)

    print("[E] 用 SDO 讀回 0x6077(扭矩‰) / 0x6078(電流‰) — 示範『讀』資料流")
    buses["L"].log = True
    for i in show:
        m, b = motors[i]
        tq = b.sdo_read(m.node_id, 0x6077)   # ‰ rated torque
        cu = b.sdo_read(m.node_id, 0x6078)   # ‰ rated current
        print("    %-16s 扭矩=%.2f N·m (%d‰)  電流=%.2f A (%d‰)" % (
            m.name, m.torque, tq, m.current, cu))
    buses["L"].log = False

    print("\n[F] CAN 流量：CAN1 TX=%d RX=%d ; CAN2 TX=%d RX=%d" % (
        buses["L"].tx, buses["L"].rx, buses["R"].tx, buses["R"].rx))
    print("\n=== 模擬結束 ===")


if __name__ == "__main__":
    main()
