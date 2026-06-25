"""
robot.py — 雙臂機器人定義：joint → PHU 馬達對應（URDF-ready）

JOINT_MAP 列出每個關節對應的:
  name（之後可對齊 URDF joint 名）、arm（L/R）、bus、node_id、PHU 型號
未來以 urdf_loader 解析 URDF 後,即可用同樣結構自動產生此對應。
"""
import math
from kinematics import ArmKin

PI = math.pi

# 每臂 7 軸：J1/J2 肩 PHU20、J3 肩yaw PHU17、J4 肘 PHU17、J5-7 腕 PHU14
# bus: "L"=左臂(CAN1), "R"=右臂(CAN2);node_id 1..7
JOINT_MAP = [
    # name,            arm, bus, node, model
    ("L_J1_shoulder",  "L", "L", 1, "PHU20"),
    ("L_J2_shoulder",  "L", "L", 2, "PHU20"),
    ("L_J3_shoulder_yaw","L","L", 3, "PHU17"),
    ("L_J4_elbow",     "L", "L", 4, "PHU17"),
    ("L_J5_wrist1",    "L", "L", 5, "PHU14"),
    ("L_J6_wrist2",    "L", "L", 6, "PHU14"),
    ("L_J7_wrist3",    "L", "L", 7, "PHU14"),
    ("R_J1_shoulder",  "R", "R", 1, "PHU20"),
    ("R_J2_shoulder",  "R", "R", 2, "PHU20"),
    ("R_J3_shoulder_yaw","R","R", 3, "PHU17"),
    ("R_J4_elbow",     "R", "R", 4, "PHU17"),
    ("R_J5_wrist1",    "R", "R", 5, "PHU14"),
    ("R_J6_wrist2",    "R", "R", 6, "PHU14"),
    ("R_J7_wrist3",    "R", "R", 7, "PHU14"),
]

# 7-DoF DH（佔位,待 URDF/實機）：(a, alpha, d, theta_off)
DH_ARM = [
    (0.0, -PI/2, 0.10, 0.0),
    (0.0,  PI/2, 0.0,  0.0),
    (0.0, -PI/2, 0.30, 0.0),
    (0.0,  PI/2, 0.0,  0.0),
    (0.0, -PI/2, 0.28, 0.0),
    (0.0,  PI/2, 0.0,  0.0),
    (0.0,  0.0,  0.08, 0.0),
]

# 左/右肩基座（世界座標,分開 0.4m）
LEFT_KIN  = ArmKin(DH_ARM, base_p=(0.0,  0.20, 0.0))
RIGHT_KIN = ArmKin(DH_ARM, base_p=(0.0, -0.20, 0.0))

Q_INIT = [0.0, 0.3, 0.0, 0.7, 0.0, 0.5, 0.0]
