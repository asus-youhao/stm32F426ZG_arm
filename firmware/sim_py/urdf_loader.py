"""
urdf_loader.py — URDF 完整解析 + 正運動學（TF 樹），供 3D 視覺化與馬達擺放使用。

慣例：Z-up、X-forward、Y-left、rad/m。與 model/dual_arm.urdf 一致。

主要 API
--------
    robot = load_urdf("model/dual_arm.urdf")
    robot.joint_names          # revolute/continuous 關節名（依樹狀順序）
    robot.fk({name: q, ...})   # -> {link_name: T4x4, ...}（世界座標，row-major 16 元素）
    robot.joint_origin_world(q_by_joint)  # -> {joint_name: (T4x4, axis_world3)}
    robot.to_dict()            # -> 可 JSON 序列化的樹（前端建場景用）

相容舊介面（保留）
    load_urdf_joints(path)     # 只回傳關節名清單
    assign_motors(names, map)  # 關節名 → (name, arm, bus, node, model)

CLI
    python3 urdf_loader.py [model/dual_arm.urdf]
        驗證 q=0（手臂垂下）時各 link 世界座標；斷言雙臂沿 -Z 垂直。
"""
import math
import xml.etree.ElementTree as ET


# ------------------------------------------------------------------ 4x4 矩陣工具（row-major）
def _ident():
    return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def _matmul(A, B):
    C = [0.0] * 16
    for i in range(4):
        for j in range(4):
            C[i * 4 + j] = sum(A[i * 4 + k] * B[k * 4 + j] for k in range(4))
    return C


def _translate(x, y, z):
    T = _ident()
    T[3], T[7], T[11] = x, y, z
    return T


def _rpy(r, p, y):
    """URDF rpy = Rz(y) * Ry(p) * Rx(r)（固定軸 XYZ）。"""
    cr, sr = math.cos(r), math.sin(r)
    cp, sp = math.cos(p), math.sin(p)
    cy, sy = math.cos(y), math.sin(y)
    return [cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr, 0,
            sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr, 0,
            -sp,     cp * sr,                cp * cr,                0,
            0, 0, 0, 1]


def _axis_angle(axis, theta):
    """繞單位軸 axis 旋轉 theta 的 4x4（Rodrigues）。"""
    x, y, z = axis
    n = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / n, y / n, z / n
    c, s = math.cos(theta), math.sin(theta)
    C = 1 - c
    return [c + x * x * C,     x * y * C - z * s, x * z * C + y * s, 0,
            y * x * C + z * s, c + y * y * C,     y * z * C - x * s, 0,
            z * x * C - y * s, z * y * C + x * s, c + z * z * C,     0,
            0, 0, 0, 1]


def _pos(T):
    return (T[3], T[7], T[11])


# ------------------------------------------------------------------ 資料結構
class Joint:
    def __init__(self, name, jtype, parent, child, xyz, rpy, axis, limit):
        self.name = name
        self.type = jtype            # revolute / continuous / fixed
        self.parent = parent
        self.child = child
        self.xyz = xyz               # origin 平移 (3)
        self.rpy = rpy               # origin 旋轉 (3)
        self.axis = axis             # 轉軸 (3)
        self.limit = limit           # (lower, upper) 或 None

    @property
    def movable(self):
        return self.type in ("revolute", "continuous")


class Link:
    def __init__(self, name):
        self.name = name
        self.visuals = []            # list of dict: {type, params, origin_xyz, origin_rpy, color}


class Robot:
    def __init__(self, name):
        self.name = name
        self.links = {}              # name -> Link
        self.joints = []             # list[Joint]（文件順序）
        self._by_child = {}          # child link -> Joint
        self.root = None

    def finalize(self):
        children = set()
        for j in self.joints:
            self._by_child[j.child] = j
            children.add(j.child)
        roots = [ln for ln in self.links if ln not in children]
        self.root = roots[0] if roots else None

    @property
    def joint_names(self):
        return [j.name for j in self.joints if j.movable]

    def _local_T(self, joint, q_by_joint):
        """joint 的 parent→child 變換：origin(xyz,rpy) * 轉軸旋轉(q)。"""
        T = _matmul(_translate(*joint.xyz), _rpy(*joint.rpy))
        if joint.movable:
            q = float(q_by_joint.get(joint.name, 0.0))
            T = _matmul(T, _axis_angle(joint.axis, q))
        return T

    def fk(self, q_by_joint=None):
        """回傳每個 link 的世界座標 4x4（含 root=單位）。"""
        q_by_joint = q_by_joint or {}
        world = {self.root: _ident()}
        # 依父到子的順序解（joints 已大致有序；用迴圈補齊未解者）
        pending = list(self.joints)
        guard = 0
        while pending and guard < len(self.joints) + 5:
            guard += 1
            still = []
            for j in pending:
                if j.parent in world:
                    world[j.child] = _matmul(world[j.parent], self._local_T(j, q_by_joint))
                else:
                    still.append(j)
            pending = still
        return world

    def joint_origin_world(self, q_by_joint=None):
        """回傳每個可動關節的世界座標原點 4x4 與其世界轉軸方向（畫馬達/紅線用）。"""
        world = self.fk(q_by_joint)
        out = {}
        for j in self.joints:
            if not j.movable:
                continue
            Tp = world.get(j.parent, _ident())
            To = _matmul(Tp, _matmul(_translate(*j.xyz), _rpy(*j.rpy)))
            R = [To[0], To[1], To[2], To[4], To[5], To[6], To[8], To[9], To[10]]
            ax = (R[0] * j.axis[0] + R[1] * j.axis[1] + R[2] * j.axis[2],
                  R[3] * j.axis[0] + R[4] * j.axis[1] + R[5] * j.axis[2],
                  R[6] * j.axis[0] + R[7] * j.axis[1] + R[8] * j.axis[2])
            out[j.name] = (To, ax)
        return out

    def to_dict(self):
        """可 JSON 序列化的樹（前端 three.js 建場景圖用）。"""
        return {
            "name": self.name,
            "root": self.root,
            "links": {n: {"visuals": l.visuals} for n, l in self.links.items()},
            "joints": [{"name": j.name, "type": j.type, "parent": j.parent,
                        "child": j.child, "xyz": j.xyz, "rpy": j.rpy,
                        "axis": j.axis, "limit": j.limit} for j in self.joints],
        }


# ------------------------------------------------------------------ 解析
def _floats(s, n, default):
    if s is None:
        return list(default)
    v = [float(x) for x in s.split()]
    return (v + list(default))[:n]


def _parse_visual(vis):
    origin = vis.find("origin")
    oxyz = _floats(origin.get("xyz") if origin is not None else None, 3, (0, 0, 0))
    orpy = _floats(origin.get("rpy") if origin is not None else None, 3, (0, 0, 0))
    geom = vis.find("geometry")
    d = {"type": None, "params": {}, "origin_xyz": oxyz, "origin_rpy": orpy, "color": None}
    if geom is not None:
        for tag in ("box", "cylinder", "sphere", "mesh"):
            e = geom.find(tag)
            if e is None:
                continue
            d["type"] = tag
            if tag == "box":
                d["params"]["size"] = _floats(e.get("size"), 3, (0.1, 0.1, 0.1))
            elif tag == "cylinder":
                d["params"]["radius"] = float(e.get("radius", 0.03))
                d["params"]["length"] = float(e.get("length", 0.1))
            elif tag == "sphere":
                d["params"]["radius"] = float(e.get("radius", 0.03))
            elif tag == "mesh":
                d["params"]["filename"] = e.get("filename")
                d["params"]["scale"] = _floats(e.get("scale"), 3, (1, 1, 1))
    mat = vis.find("material")
    if mat is not None:
        col = mat.find("color")
        if col is not None:
            d["color"] = _floats(col.get("rgba"), 4, (0.7, 0.7, 0.7, 1))
    return d


def load_urdf(path):
    tree = ET.parse(path)
    root = tree.getroot()
    robot = Robot(root.get("name", "robot"))
    for l in root.findall("link"):
        link = Link(l.get("name"))
        for vis in l.findall("visual"):
            link.visuals.append(_parse_visual(vis))
        robot.links[link.name] = link
    for j in root.findall("joint"):
        origin = j.find("origin")
        axis = j.find("axis")
        limit = j.find("limit")
        robot.joints.append(Joint(
            name=j.get("name"),
            jtype=j.get("type"),
            parent=j.find("parent").get("link"),
            child=j.find("child").get("link"),
            xyz=_floats(origin.get("xyz") if origin is not None else None, 3, (0, 0, 0)),
            rpy=_floats(origin.get("rpy") if origin is not None else None, 3, (0, 0, 0)),
            axis=_floats(axis.get("xyz") if axis is not None else None, 3, (0, 0, 1)),
            limit=((float(limit.get("lower", 0)), float(limit.get("upper", 0)))
                   if limit is not None else None),
        ))
    robot.finalize()
    return robot


# ------------------------------------------------------------------ 相容舊介面
def load_urdf_joints(path):
    """回傳 URDF 中 revolute/continuous 關節名稱（依文件順序）。"""
    return load_urdf(path).joint_names


def assign_motors(joint_names, model_assign):
    """model_assign: dict joint_name -> (arm, bus, node, model)。"""
    out = []
    for name in joint_names:
        if name not in model_assign:
            raise KeyError("URDF 關節 '%s' 未在 model_assign 指定對應馬達" % name)
        arm, bus, node, model = model_assign[name]
        out.append((name, arm, bus, node, model))
    return out


# ------------------------------------------------------------------ CLI 驗證
def _verify(path):
    robot = load_urdf(path)
    print("robot=%s  root=%s  可動關節=%d" % (robot.name, robot.root, len(robot.joint_names)))
    world = robot.fk({})            # q=0 → 手臂垂下
    print("\n=== q=0（手臂垂下）各 link 世界座標 ===")
    for name in ("base_link", "L_upperarm", "L_forearm", "L_hand",
                 "R_upperarm", "R_forearm", "R_hand"):
        if name in world:
            x, y, z = _pos(world[name])
            print("  %-12s  (% .3f, % .3f, % .3f)" % (name, x, y, z))

    # 斷言：手臂沿 -Z 垂直。關節點：肩(link1) → 肘(forearm 起點) → 腕端(hand)
    ok = True
    for side, sy in (("L", 0.20), ("R", -0.20)):
        sh = _pos(world["%s_link1" % side])       # 肩（3 個肩關節同點）
        el = _pos(world["%s_forearm" % side])     # 肘關節原點
        hd = _pos(world["%s_hand" % side])        # 腕/手端
        # x,y 應維持在肩 x=0, y=±0.20
        for p, tag in ((el, "elbow"), (hd, "hand")):
            if abs(p[0]) > 1e-6 or abs(p[1] - sy) > 1e-6:
                print("  [FAIL] %s %s 偏離垂直線: %r" % (side, tag, p)); ok = False
        # z 應嚴格遞減（往下垂）
        if not (sh[2] > el[2] > hd[2]):
            print("  [FAIL] %s 臂 z 非遞減: sh=%.3f el=%.3f hd=%.3f"
                  % (side, sh[2], el[2], hd[2])); ok = False
    print("\n=== 驗證%s：q=0 時雙臂沿 -Z 自然垂下 ===" % ("通過 [OK]" if ok else "失敗 [FAIL]"))
    return ok


if __name__ == "__main__":
    import sys
    p = sys.argv[1] if len(sys.argv) > 1 else "model/dual_arm.urdf"
    ok = _verify(p)
    sys.exit(0 if ok else 1)
