# 建立單元測試

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

新增 `firmware/tests/` 單元測試(純 C 輕量框架),涵蓋:
- `test_trajectory`:梯形/五次多項式軌跡。
- `test_kinematics`:FK 一致性 + **Jacobian 有限差分對拍**。
- `test_ik`:**FK→IK 往返收斂**。
- `test_cia402`:狀態字解析 + 使能序列。
- `test_hostif`:WP7 命令/遙測協定編解碼往返。
- `test_canopen`:SDO/PDO/CiA402 **端到端**(主站 ↔ 模擬 PHU 從站)。
- `test_framework.h`、`test_main.c`、`Makefile`;並接入 CMake `enable_testing()`/`ctest`。
- 新增 `docs/design/unit-tests.md`。
- 修正 `host_if.c` 縮排警告、`firmware/.gitignore` 補測試產物。

## 動機 / 背景

深度分析指出「完全沒有測試」。補上 trajectory/kinematics/IK/協定/CANopen 的自動化測試。

## 驗證結果

- `cd firmware/tests && make run` 與 `cmake --build build && ctest` 皆通過。
- **2590 檢查全數通過,0 失敗**;兩個編譯警告已清除。

## 影響範圍

- 新增 `firmware/tests/`;修改 `host/host_if.c`(縮排)、`firmware/CMakeLists.txt`(ctest)、`.gitignore`。

## 待補

- C↔Python 運動學交叉對拍;task_space/dual_arm_ctrl 行為測試;CI 自動跑 ctest。

## 關聯

- `unit-tests.md`、深度分析(對話)、`wp3/wp4` 設計文件
