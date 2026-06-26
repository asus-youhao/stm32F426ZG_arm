# 單元測試（firmware/tests）

純 C 輕量測試框架（`test_framework.h`,`CHECK`/`CHECK_NEAR`),在主機編譯執行,
涵蓋控制與通訊核心邏輯。

## 執行

```bash
# 方式一：Makefile
cd firmware/tests && make run        # → ./unit_tests

# 方式二：CMake + CTest
cd firmware && cmake -S . -B build && cmake --build build
cd build && ctest --output-on-failure
```

回傳碼 0 = 全數通過(可接 CI)。目前：**2590 檢查全數通過**。

## 測試項目

| 檔案 | 對象 | 內容 |
| ---- | ---- | ---- |
| `test_trajectory.c` | 軌跡插值 | 梯形:不超界/單調/限速/終點;五次:邊界速度~0、中點對稱、終點到達;退化(起=終) |
| `test_kinematics.c` | FK / Jacobian | FK 有限值、旋轉矩陣正交;**幾何 Jacobian 線速度欄 vs 有限差分對拍**(tol 1e-2) |
| `test_ik.c` | DLS-IK | **FK→IK 往返收斂**(位姿誤差 <2mm/<0.01rad);單步誤差不增 |
| `test_cia402.c` | CiA402 | 狀態字解析、使能序列(fault→reset、SOD→0x06→0x07→0x0F) |
| `test_hostif.c` | WP7 協定 | float 往返、frame 編碼/解碼還原、壞校驗碼丟棄、雜訊容忍 |
| `test_canopen.c` | SDO/PDO/CiA402 端到端 | 主站↔模擬 PHU:SDO 讀寫(device type/baud/node/mode)、PDO 使能序列、目標追隨移動、回授序號遞增、扭矩/電流物件可讀 |

## 設計

- 測試連結「真實韌體模組」+ `sim/`(HAL 替身、虛擬 bus、模擬 PHU 從站),
  因此 `test_canopen` 是**端到端**(co_sdo/co_pdo/cia402 ↔ phu_sim)而非樁件。
- Jacobian 對拍用中央差分驗證解析式正確性,可在改動運動學時即時抓回歸。

## 待補

- C↔Python 運動學**交叉對拍**(同 q 比較兩實作 FK,確保不發散)。
- task_space / dual_arm_ctrl(BIMANUAL 相對位姿、安全 hold)行為測試。
- 在 CI(GitHub Actions)自動跑 `ctest`。
