
## 第五輪（同日）：切 0x2100=1 後 **EtherCAT 全鏈打通**（P0 解決）

使用者用 CANopen/UART 把 `0x2100` 改回 **1（EtherCAT）**。gx701 實測：

1. **SAFEOP 穩定不再彈跳**（連續 5 次全 SAFEOP，`+` 無錯誤旗標）——
   對照 0x2100=2 時 0.5 ms 自貶 AL 0x0022，**控制權問題徹底解決**。
2. **SDO 寫入恢復**：非 PDO 映射物件 `0x60C2:01` 寫入成功並讀回；
   `0x6060/0x6081` 仍拒是因它們在 PDO 映射內（PDO-mapped 本就不能 SDO
   寫，屬正常）——修正第四輪「所有寫入被鎖」的過度推論。
3. **完整 CiA402 bring-up 成功到 OperationEnabled**（`igh_jog` 100 Hz）：
   `OP! wc=3 al=0x8` → CW 0x06→0x07→0x0F → **OperationEnabled** → CSP
   目標串流、drive 跟隨（sw `0x1337` bit12=1）。**但實際位置未動
   （foll≈0）= STO 兩路未接（`0x60FD=0`）、`0x253B=0` 故使能不報故障
   但功率級無扭矩**。差 STO 24V（＋建議 48V 母線）即可實際轉動。

### 兩個關鍵工程發現（收錄 igh_jog no-remap 100Hz 版）

- **此 drive 不可重寫 PDO 映射**：呼叫 `ecrt_slave_config_pdos`（重映射
  0x1600/0x1A00）會使 OP 到不了（`wc_state=0`）；**改用預設映射（不呼叫
  config_pdos，僅 `ecrt_domain_reg_pdo_entry_list` 綁預設）即成功**。
  這正呼應 `eyou-motor-master` pysoem 主站的 `--no-remap-pdo` 選項——
  出廠映射已固化，重寫反而被拒。IgH 端同樣成立。
- **USB r8152 NIC 速率天花板**：process data **100 Hz（10 ms）可穩定達 OP、
  WC 收齊**；**≥250 Hz~1 kHz 則 `datagram UNMATCHED`、WC=0**（回幀太慢）。
  低速 bring-up/驗證可用此 NIC；**1 kHz 生產仍需 Intel NIC（NUC I219-V /
  i225）**——與最初 §2.2 判斷一致，但此為「速率上限」非「功能阻斷」。

**P0 全鏈閉環**：`0x2100=1`（控制權）+ 不重映射 PDO（drive 相容）+
NIC 速率匹配（≤100 Hz on r8152）→ 到 OperationEnabled、CSP 可控。
剩餘僅 STO 24V（實際出力）與 Intel NIC（1 kHz）。

## 第六輪（2026-07-08）：更正——真阻塞是「缺 DC」不是 STO，馬達實際轉動 ✅

第五輪「軸不動＝STO 無扭矩」的結論**錯誤**。真因在使用者 ROS2 專案的
`COMMISSIONING_RUNBOOK.md`（2026-06-05 發現，一開始漏看）：

> **EYOU CSP（mode 8）嚴格依賴 SYNC0（DC）**。free-run（`assign_activate=0x0`）
> 下 drive 進 OperationEnabled（sw `0x1337`）、`0x607A` target ramp，但內部
> position demand（0x6062）凍住、`0x6064` actual 不動、**無 fault**——收到目標
> 卻不執行，因為沒有同步時鐘。開 DC（`assign_activate=0x300`）後 demand 立刻
> 跟上、**馬達實際轉動**。

我先前所有測試都是 free-run/SM-sync（早先 `0x2100=2` 時 DC 上不去,遂關掉),
症狀（`0x1337`、target 動 actual 不動、foll≈0、無故障）與 runbook 描述**一字
不差**,卻被我誤判為 STO 硬體閘。

**開 DC 重測（gx701 RT kernel、`0x2100=1`、no-remap、SYNC0=10ms 對齊 100Hz）：
馬達實際以 100 rpm 轉 10 秒 = 16.6 馬達圈（≈59° 輸出）**,命令 16.67 圈/實際
16.58 圈、穩定追隨誤差 ~18k counts（等速滯後,正常）。**且是在 STO 未接
（`0x60FD=0`）下轉的 → `0x253B=0` 確實旁路了 STO 扭矩閘（手冊為真,前述「STO
硬體閘無法軟體繞過」的推論作廢）。**

### ✅ 單軸 EtherCAT 可動的完整配方（gx701 實證）

| 項 | 值 | 說明 |
| --- | --- | --- |
| `0x2100` | 1 | 控制權在 EtherCAT（原為 2=CANopen,由使用者切回） |
| PDO 映射 | **預設,不重寫** | 呼叫 `ecrt_slave_config_pdos` 會 wc=0 上不了 OP |
| **DC** | **`assign_activate=0x300`,SYNC0=控制週期** | **CSP 動作的必要條件**;每 cycle `sync_reference_clock`+`sync_slave_clocks` |
| `0x253B` | 0 | STO 調試模式,免接 STO 即出力 |
| 週期 | 100 Hz（r8152 上限） | ≥250Hz UNMATCHED;1kHz 需 Intel NIC |
| 內核 | PREEMPT_RT | 非 RT 撐不住 DC（runbook 記載） |

工具：`tools/ecat_bringup/igh_spin.c`（`dc` 參數啟 DC;此版實測轉動）。
