
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
