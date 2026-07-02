# P0：URDF 人形模型 + urdf_loader 完整解析 + FK 驗證

> 分支：`feature/3d-humanoid-visualizer`　對應設計：[3D 人形視覺化](../design/3d-humanoid-visualizer.md) §2–3, §7(P0)

## 變更摘要

建立 3D 視覺化的運動學地基——一份 URDF 人形模型與完整的 URDF 解析/FK 模組：

- 新增 `firmware/sim_py/model/dual_arm.urdf`：雙臂 7-DoF ×2 = 14 軸人形上半身。
  - 慣例 **Z-up / X-forward / Y-left**；3-1-3 擬人臂（肩 J1 pitch/Y、J2 roll/X、J3 yaw/Z；
    肘 J4/Y；腕 J5 roll/Z、J6/Y、J7/X）。
  - **零點 = 手臂垂下**：所有關節 `q=0` 時雙臂沿 −Z 自然垂下（人站立、手臂貼身）。
  - 含 link/joint/origin/axis/`<limit>` 與基本 `<visual>`（軀幹 box、上臂/前臂 cylinder、手 box）。
- 新增 `firmware/sim_py/model/robot_config.json`：可在 UI 調整的疊加設定
  （每軸 bus/node/model、`home_offset`、soft limit、Kp/Kd、mesh 檔名、顯示選項、動作 preset）。
- 重寫 `firmware/sim_py/urdf_loader.py`：
  - `load_urdf(path)` → `Robot`（links/joints 樹）。
  - `Robot.fk(q_by_joint)` → 各 link 世界座標 4×4（TF）；`joint_origin_world()` → 關節原點與世界轉軸（畫馬達/紅線用）；`to_dict()` → 可 JSON 序列化樹（前端建場景用）。
  - 4×4 矩陣工具（translate / rpy / 軸角 Rodrigues / matmul）。
  - **保留舊介面** `load_urdf_joints`、`assign_motors`（無其他程式依賴，仍相容）。
  - CLI 驗證：`python3 urdf_loader.py model/dual_arm.urdf`。

## 動機 / 背景

原 `urdf_loader.py` 只抓關節名、無 FK；`robot.py` 的 DH 為占位符且 `Q_INIT` 為微彎姿，
不符合「手臂垂下＝零點」需求。3D 視覺化需要一棵可遍歷、每個 link 都有世界座標的 TF 樹，
且零位要對齊人體自然下垂，故以 URDF 為單一真相來源重建。

## 影響範圍

- 新增：`firmware/sim_py/model/dual_arm.urdf`、`firmware/sim_py/model/robot_config.json`
- 重寫：`firmware/sim_py/urdf_loader.py`（向後相容，未破壞既有 import）
- 不影響 MCU 韌體、不影響現有 `ws_server.py`/前端（本階段僅新增模型與解析）。
- `robot.py` 的 DH 仍保留（後端動態/既有控制沿用）；URDF 為視覺化與擺放之依據，
  後續 P1 再由後端載入。

## 驗證方式

```bash
cd firmware/sim_py
python3 urdf_loader.py model/dual_arm.urdf
```
輸出（實測）：
- `可動關節=14`，root=`base_link`。
- q=0 時：肩 (0, ±0.20, 0.50) → 肘 (0, ±0.20, 0.20) → 手 (0, ±0.20, −0.08)，
  x/y 不變、z 遞減 → **雙臂沿 −Z 垂下，驗證通過 [OK]**。
- 附加：肘關節 `q=-1.0` 時 `L_hand` → (0.236, 0.20, 0.049)，手掌前擺上抬，符合擬人屈肘。

## 待補 / 風險

- 連桿長度（上臂 0.30 / 前臂 0.28 / 肩高 0.50）為合理估值，實機到貨需校正。
- soft limit 為初值；`home_offset` 對應 CiA402 `0x607C`，實機校零後回填。

## 關聯

- 分支：`feature/3d-humanoid-visualizer`
- 設計文件：[3D 人形視覺化](../design/3d-humanoid-visualizer.md)
- 下一步：P1 後端 `ws_server` 載入本模型並提供 `get_model`/config 端點與遙測 `q`。
