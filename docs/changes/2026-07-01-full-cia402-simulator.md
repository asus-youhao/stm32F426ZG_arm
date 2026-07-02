# 全面忠實 CiA402 從站模擬器（8 模式 + 完整狀態機 + Homing + 故障 + OD）

## 變更摘要

把 PC 端假從站從「夠用」升級成**完整的 EYOU PHU CiA402 裝置模型**，涵蓋通信手冊 v1.06
定義的全部功能，並讓 web 上位機可從瀏覽器切換模式、下控制字、設目標、注入/清除故障。

- **`phu_od.py`（新增）**：EYOU PHU 物件字典資料表，以 `pdftotext -raw` 從通信手冊各模式
  章節抽出（index/sub/名稱/型別/單位/存取/PDO 對齊乾淨），少數標準物件以 CiA402 補並標 `[std]`。
- **`phu_motor.py`（重寫）**：完整 CiA402 狀態機 + 8 模式動力學 + Homing + 故障注入；公開介面
  與舊版相容（`read_od`/`write_od`/`apply_controlword`/`step`）。
- **`can_slave.py`（新增 `--modetest`）**：離線驅動 8 模式 + 急停 + 故障的全測。
- **`web_monitor.py`（升級）**：新增控制台（模式下拉、使能/解除/急停/Halt/回零/注入故障/清故障/
  目標值）與 `POST /cmd`；demo 改用正規 enable 序列，手動命令可接管 demo 自驅。

## 動機 / 背景

使用者要「EYOU 所有 doc 的功能都可模擬、可切 mode」。手冊（Table 4-1 + 物件字典）是權威來源。
舊 `phu_motor` 只有 CSP/PP/CST/CSF 與 4 個狀態字，無法涵蓋全部。本批以手冊為準補齊。

## 影響範圍

| 檔案 | 變更 |
| ---- | ---- |
| `firmware/sim_py/phu_od.py` | 新增 OD 資料表 + 型別/存取/簽號輔助 |
| `firmware/sim_py/phu_motor.py` | 重寫為完整 CiA402 裝置（狀態機/8 模式/Homing/故障/OD 驅動）；位置與速度模式加重力前饋，減速含重力補償 |
| `firmware/sim_py/can_slave.py` | 新增 `modetest()` 與 `--modetest` |
| `firmware/sim_py/web_monitor.py` | 控制台 + `POST /cmd` + manual 接管 |

### 支援的 8 種模式（手冊 Table 4-1，0x6060 可切）

| 模式 | 0x6060 | 模型行為 |
| ---- | ------ | -------- |
| PP 輪廓位置 | 1 | 位置 PD + 重力前饋，到位置窗口置 target-reached |
| PV 輪廓速度 | 3 | 速度環 + 前饋，追隨 0x60FF |
| PT 輪廓力矩 | 4 | 經 torque slope(0x6087) 逼近 0x6071 |
| HM 回零 | 6 | controlword bit4 觸發，收斂到 home offset，置 homing-attained |
| CSP 同步位置 | 8 | 週期位置追隨，切入時自動 0x6064→0x607A |
| CSV 同步速度 | 9 | 週期速度追隨 |
| CST 同步力矩 | 10 | 直接力矩命令 |
| CSF 同步力控 | 13 | EYOU 擴充，力矩命令 |

### 完整 CiA402 狀態機

controlword 0x6040：shutdown(0x06) / switch-on(0x07) / enable(0x0F) / disable-voltage /
**quick-stop(0x02)** / **halt(bit8)** / **fault-reset(0x80)**；狀態：switch-on-disabled →
ready → switched-on → operation-enabled → quick-stop-active / fault。狀態字低位元組維持
canonical（0x0027 等），高位元組動態旗標（target-reached 0x400、setpoint-ack/homing-attained 0x1000）。

## 驗證方式（皆已完成，離線無硬體）

```bash
cd firmware/sim_py
python can_slave.py --selftest    # bring-up SDO 序列：通過
python can_slave.py --modetest    # 8 模式 + 急停 + 故障：全部通過
python web_monitor.py --demo      # 瀏覽器看板 + 控制台
```

- `--modetest` 實測：PP→0.500、PV→1.000、PT→10.0Nm、HM homed(sw=0x1427)、CSP→0.400、
  CSV→0.800、CST/CSF→7.5Nm、QuickStop qd→0.000、Fault inject→clear。
- web 上位機以 Playwright 端到端驗證：從 UI 把 L_J1 切 CST + 使能、對 R_J4 注入故障，
  看板即時反映（CST/operation-enabled、CSP/fault）。

## 關聯

- [C1：CANable + Python 假從站](./2026-07-01-c1-canable-python-slave.md)、[C1 web 看板](./2026-07-01-web-monitor-14axis.md)
- 來源：`doc_EYOU/EYou-PHU&RHU系列关节CANopen与EtherCAT通信手册v1.06`
- 後續：補製造商區 0x2xxx 完整參數、SDO segmented/block 傳輸、故障碼對照手冊章節、PDO 動態映射。
