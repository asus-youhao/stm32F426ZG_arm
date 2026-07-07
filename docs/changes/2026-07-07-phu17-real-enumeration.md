# PHU17 真機 EtherCAT 枚舉 + SDO 特性化（WP-L0.6 / L1.2）

- 日期：2026-07-07
- 分支：`feature/linux-rt-ethercat-master`
- 機器：asus-gx701（6.8.1-1052-realtime，SOEM 2.x）
- 硬體：1× **PHU17** 經 USB NIC（r8152，`enx9cebe85f541c`）直連，48V/STO 見末節
- 類型：實機驗證報告（無程式改動）

## 摘要：第一次讀到真馬達，規劃數字全部對上

`slaveinfo` 成功枚舉 1 顆從站、WKC=3、State=PREOP、**Has DC=1**。
自製唯讀 SDO 探測工具（`/tmp/sdo_probe.c`，SOEM 2.x context API）讀回
全部關鍵物件。**規劃裡的待實證項全部證實。**

## 身分（0x1018 / 0x1008）

| 物件 | 值 |
| --- | --- |
| 0x1008 Device name | **"EYOU_ServoModule"**（= 規劃提到的 ESI 檔名） |
| 0x100A SW version | 5.13 |
| 0x1018 Vendor ID | **0x1097** |
| 0x1018 Product（SII/CoE）| SII=0x00010002、CoE 0x1018:02=1（兩者不同，記錄待釐清） |

## 單位換算（規劃最大未知 → 全部實證）

| 物件 | 讀回值 | 意義 |
| --- | --- | --- |
| **0x2025** | **524288 = 2¹⁹** | **19-bit 編碼器確認**；與 `robot_config.c` 的 `CPR_19BIT=524288/2π` 完全一致 |
| **0x26A2 / 0x26A3** | **101 / 1** | 減速比 **101:1**（規劃推測值正確） |
| 0x6092 feed constant | 524288 / 1 | 位置單位 524288 counts/rev |
| 0x6076 rated torque | 600 | 馬達側 0.6 Nm（×101 ≈ 60 Nm 輸出，合 PHU17 規格） |
| 0x6080 max speed | 5000 | 馬達側 5000 rpm（/101 ≈ 49.5 rpm 輸出） |

## CSP 就緒度（幾乎零組態）

| 物件 | 值 | 意義 |
| --- | --- | --- |
| 0x6060 / 0x6061 | 8 / 8 | **出廠即 CSP 模式** |
| 0x60C2 插補週期 | 1 × 10⁻³ | **1 ms，與 1 kHz 目標一致（不會報 E404）** |
| 0x6502 支援模式 | 0x3AD | PP/PV/TQ/HM/**CSP/CSV/CST**（力控 CST 也有） |
| 0x6041 statusword | 0x331 → 遮罩 0x21 | **Ready to switch on**（健康，待使能） |
| 0x603F / 0x60F4 | 0 / 0 | 無故障、無跟隨誤差 |

## 出廠 PDO 映射（slaveinfo -map 讀回，決定後端寫法）

**RxPDO（SM2，33 B，10 entries）**：0x6040 CW、**0x6060 模式**、0x607A 目標位置、
0x6081 profile vel、0x60FF 目標速度、0x240D(廠商) 力矩模式限速、0x6071 目標力矩、
0x6083/0x6084 加減速、0x6087 力矩斜率。
**TxPDO（SM3，29 B，10 entries）**：0x6041 SW、0x6061 模式顯示、0x603F 錯誤碼、
0x6064 位置、0x606C 速度、0x6077 力矩、0x6074 力矩需求、0x60F4 跟隨誤差、
**0x6079 母線電壓**、0x60FD 數位輸入。

**關鍵結論**：
1. 出廠映射**已含 CSP 所需全部物件**（CW/模式/目標位置 out；SW/位置/錯誤碼 in）
   → **單軸 bring-up 不必重映射**，直接用 33B/29B 出廠佈局即可。§5.1 的精簡
   是 14 軸頻寬考量，單軸無所謂。
2. `0x6060` 模式在 PDO 內（規劃假設走 SDO）→ 可逐週期切模式，更靈活。
3. 出廠 RxPDO 有 10 entries，**超過規劃 §5.1「每 PDO ≤6（否則 E405）」卻正常
   枚舉** → 該限制疑為 per-mapping-object 或當初手冊誤讀，**標記待釐清**（不擋事）。

## 實機狀況（兩個要注意）

- **母線電壓 0x6079 ≈ 24.75 V**（24752 mV）——**非 48V**。高於 24V 最低門檻
  （煞車可開）但貼地板；空載點動應可，一出力恐觸發欠壓 E131。額定設計點
  是 48V，**正式測動前建議上 48V**。
- **STO 故障模式 0x253B = 0（出廠=停用/僅調試）**、0x253C=0（手動復位）——
  與 §4.2 記載一致；正式運行前改 1（智能響應）。**要讓馬達出力仍須 STO
  兩路 24V 實體接通**（硬體轉矩閘，與此參數無關）。

## 工具

唯讀 SDO 探測 `sdo_probe.c`（SOEM 2.x：`ecx_init`/`ecx_config_init`/
`ecx_SDOread`）暫存於遠端 `/tmp/`；WP-L1 把 SOEM 真後端接進 `ec_master.h`
門面時，這段掃描邏輯併入 bring-up。

## 關聯 / 下一步

- 前置：`ethercat-coe-master-plan.md` §4/§5/§7、`linux-rt-ethercat-master-plan.md`
  WP-L0.6/L1.2；`2026-07-06-u24-rt-verification.md`（SOEM 建置）
- **下一步（需硬體確認後才動）**：WP-L1.4/1.5 CiA402 使能 + CSP 點動——
  屬「會讓馬達轉」的動作，動工前須確認 STO 24V、48V 供電、軸端機械安全。
