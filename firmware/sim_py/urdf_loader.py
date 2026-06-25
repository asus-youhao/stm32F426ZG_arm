"""
urdf_loader.py — 由 URDF 取得關節清單,並對應到 PHU 馬達（URDF-ready）

用法（未來）：
  joints = load_urdf_joints("robot.urdf")          # 取得 revolute 關節名（依序）
  jmap   = assign_motors(joints, MODEL_ASSIGN)     # 對應 bus/node/型號
其中 MODEL_ASSIGN 由使用者指定每個 URDF 關節名 → (arm, bus, node, model)。

目前提供：
  - load_urdf_joints：以標準函式庫 xml 解析 <joint type="revolute|continuous">。
  - assign_motors：把 URDF 關節名套上馬達對應,輸出與 robot.JOINT_MAP 相同結構。
"""
import xml.etree.ElementTree as ET


def load_urdf_joints(path):
    """回傳 URDF 中 revolute/continuous 關節名稱（依文件出現順序）。"""
    tree = ET.parse(path)
    root = tree.getroot()
    joints = []
    for j in root.iter("joint"):
        if j.get("type") in ("revolute", "continuous"):
            joints.append(j.get("name"))
    return joints


def assign_motors(joint_names, model_assign):
    """
    model_assign: dict  joint_name -> (arm, bus, node, model)
    回傳 list of (name, arm, bus, node, model),供建立 PhuMotor / CanBus。
    """
    out = []
    for name in joint_names:
        if name not in model_assign:
            raise KeyError("URDF 關節 '%s' 未在 model_assign 指定對應馬達" % name)
        arm, bus, node, model = model_assign[name]
        out.append((name, arm, bus, node, model))
    return out


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        for n in load_urdf_joints(sys.argv[1]):
            print(n)
    else:
        print("usage: python3 urdf_loader.py robot.urdf")
