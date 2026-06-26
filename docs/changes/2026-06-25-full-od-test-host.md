# 完整物件字典假馬達 + 測試上位機（CANopen 讀寫）

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

依使用者需求「從 PDF 取得馬達所有 CANopen 讀/寫指令,並做一個能發 CMD 讀回假馬達數據的測試上位機」:

- `firmware/sim_py/phu_od.py`：**由通訊手冊 v1.06 自動抽取完整物件字典**（399 條:index/sub/access/type/default/name）。
- `firmware/sim_py/phu_motor.py`：改為 **OD-driven**——`read_od/write_od` 走整份 OD;RO 物件寫入回 **SDO abort（0x06010002）**;0x6040/6060/607A/6071/60FF 等觸發副作用;狀態字/實際位置/扭矩/電流即時計算。
- `firmware/sim_py/can_bus.py`：`sdo_write` 依從站回應回 True/abort。
- `firmware/sim_py/host_console.py`：**測試用上位機**,可 SDO read/write 任意 OD、高階 enable/move/mode、dump、frame log;支援 `--demo`/互動/管線。
- `firmware/sim_py/test_od.py`：OD 讀寫單元測試（PASS）。

## 動機 / 背景

先前假馬達只實作 OD 子集,且沒有可發送任意 CANopen 指令的上位機。本次補齊「全 OD + 可讀寫測試上位機」。

## 驗證結果

- `python3 host_console.py --demo`:讀身分/組態、寫參數讀回一致、寫 RO 物件 ABORT、使能後移動讀回 力 4.91 N·m / 電流 1.23 A、frame log 正常。
- `python3 test_od.py`:**PASS**（讀身分、RW 寫回、RO abort、使能後力/電流非 0）。
- 既有 `sim_main.py` 與 C 單元測試（2590 檢查）不受影響。

## 影響範圍

- 修改/新增 `firmware/sim_py/`：phu_od.py、phu_motor.py、can_bus.py、host_console.py、test_od.py、README。
- 不影響 C 韌體與 C 模擬器。

## 限制

- OD 的 RO/RW 存取為「手冊標註 + 名稱啟發式」推定,個別條目可能需依原廠最終校正。
- 物理量（扭矩/電流/慣量）仍為佔位模型。

## 關聯

- `sim-fake-hardware.md`、`python-fake-hardware.md`、`eyou-phu-motor-analysis.md`、`wp6-safety-wp7-host.md`
