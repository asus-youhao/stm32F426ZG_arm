# PHU17 SAFEOP 診斷：協定面全通，USB NIC 時序過不了同步驗證（P0）

- 日期：2026-07-07
- 分支：`feature/linux-rt-ethercat-master`
- 機器：asus-gx701 + USB r8152 NIC + 1× PHU17（24.75V 母線、STO 未接）
- 類型：診斷報告 + bring-up 工具入庫（`tools/ecat_bringup/`）

## 症狀與排查路徑

SOEM（自寫 + 官方 `ec_sample`）與 IgH（app 模式）都無法把 PHU17 帶進
SAFEOP/OP；但 IgH CLI 裸請求（`ethercat states SAFEOP`）**能成功且穩定**。

系統性排除（全部實測）：
- ✗ 非程式問題：SOEM 官方範例同樣失敗
- ✗ 非 PDO 指派/映射問題：0x1C12/0x1C13 正常;debug trace 顯示逐筆
  SDO 重映射**全部成功**（回應 0x60）
- ✗ 非 SM 值問題：master 寫的 SM2/SM3 與 SII 預設**逐字節相同**
  （0x1100/33/0x64、0x1400/29/0x20）
- ✗ 非 DC 之有無：nodc / dc(0x300) / dc(0x330) 全試
- ✗ 非同步參數：0x1C32:02=1ms / 4ms、0x60C2=4ms、free-run
  （0x1C32:01=0）全試
- ✗ 非 ESM 卡死：INIT 後 CLI SAFEOP 可復原

## 關鍵證據（debug trace + AL 暫存器輪詢）

1. **從站其實有進 SAFEOP**：`Now in SAFEOP. Finished configuration.`
   → **約 5 ms 後自發掉回 PREOP**（`SAFEOP -> PREOP`），master FSM
   重配置→再進→再掉，無限循環——「卡 PREOP」是彈跳的假象。
2. 彈跳瞬間 AL Status Code 暫存器（0x0134）短暫出現 **0x0022**（原始
   讀值,待對照 ETG 規格/原廠定義）。
3. CLI 請求路徑（不掛 app、無週期資料）重試幾輪後**偶爾站穩** SAFEOP
   ——成功與否看時序運氣。
4. USB NIC 品質證據：IgH `2000 datagrams UNMATCHED!`（1 kHz 下回幀
   遲到）、DC 收斂失敗（`Slave did not sync after 5000 ms`）、
   domain WC 曾 3/3 一秒後掉 0/3。app 模式含週期資料流時 90 s 重試
   從未站穩。

## 結論

**協定面（枚舉/mailbox/SDO/PDO 組態）在兩套 master 上都正確**；
故障點是**從站進 SAFEOP 後的同步/通訊品質驗證**——USB r8152 的
毫秒級批次延遲使 1–4 ms 的規律 process data 節拍無法保證，從站
5 ms 內自貶 PREOP。規劃文件 §2.2「避免 Realtek/任何 USB 網卡」
在此從「抖動品質建議」升級為「功能性阻斷」的實證。

## 建議行動（按優先）

1. **換 NIC**：Intel i210/i225（gx701 是筆電 → Thunderbolt 3 轉
   i210/i225 轉接盒,或改用有 PCIe 的桌機/工控機）。工具全部就緒,
   NIC 到位即重跑 `igh_jog`/`phu_jog`。
2. **EYOU 詢問單補三題**（併入 C0.6 那份一起寄）：
   ① 索取 ESI（EYOU_ServoModule.xml）與 TwinCAT 參考組態
   ② AL Status Code 0x0022 的廠商定義
   ③ 進 SAFEOP/OP 的同步前置條件（SM-sync 容許抖動窗、是否強制 DC）
3. （可選）有 Windows+Intel NIC 的機器可先用 TwinCAT 對照驗證
   （通訊手冊 §6 官方流程）,分離「從站韌體」與「master 組態」變因。

## 已確認資產（不受此問題影響）

- PHU17 特性化數據全部拿到（前份文件 e5be5fb）
- IgH 1.6.9 on 6.8-rt 掛載運作正常（master/FSM/CLI/debug 全功能）
- SOEM 2.x 建置與 mailbox 通訊正常
- 四支 bring-up 工具入庫 `tools/ecat_bringup/`（含 README）

## 第二輪（同日）：isolcpus 全套上機後的修正結論

使用者要求試「隔離核能否救 USB NIC」。已對 gx701 施作並**永久生效**：
`rt_setup.sh --grub` + HT siblings（isolcpus/nohz_full/rcu_nocbs=
6,7,14,15、irqaffinity=0-5,8-13、intel_pstate=disable 等）+ 重開機 +
xhci IRQ→核6/irq thread FIFO 85 + r8152 offloads off，測試程序
taskset 核7 FIFO 80。

結果與新證據：
1. RT 調校**有效改善主站側**：`datagrams UNMATCHED` 完全消失；
   SOEM 從「永遠 PREOP」進步到「map 後短暫 state=0x04(SAFEOP)」。
2. **但彈跳仍在**：高速輪詢 AL 暫存器精確計時——進 SAFEOP 後
   **0.5 ms** 自貶 PREOP，AL code **0x0022（Slave requires PREOP）**
   ＝從站應用層主動要求回 PREOP。0.5 ms 遠小於任何資料週期 →
   **靜態組態檢查不過，非時序問題**。
3. ESC 看門狗讀回標準值（0x0400=2498、0x0420=1000 → 100 ms）→
   排除看門狗機制。
4. 修正結論：**根因不是（或不只是）USB NIC 時序**，而是從站韌體在
   SAFEOP 入口的應用層前置檢查。目前最強嫌疑：master 寫入的
   **FMMU（特別是 logical address 從 0x00000000 起始——TwinCAT
   慣例從不用 0，SOEM/IgH 預設都用 0）**；CLI 裸請求（不寫 FMMU）
   可站穩 SAFEOP 支持此說,但 CLI 非同步 FSM 使受控對照實驗採樣
   不穩定,未能終判。
5. NIC 時序問題（UNMATCHED/DC 不收斂）真實存在但已被 RT 調校壓制;
   Intel NIC 仍是 ML1 量測基準的必要條件。

**下一步實驗（NIC 無關,可先做）**：
- SOEM `grouplist[0].logstartaddr=0x10000` 重測（初版 SOEM 2.x 下
  segfault,需查正確設定點）;或 IgH 對照 TwinCAT 抓包。
- **EYOU 詢問單三題升級為關鍵路徑**：ESI、AL 0x0022 觸發條件、
  TwinCAT 參考組態（FMMU/DC/SM 逐字節）。

## 第三輪（同日）：真正根因——0x2100 = 2（控制權在 CANopen）

使用者問「轉 CANopen 有沒有機會」,順手用 CoE 讀切換參數,結果：

| 物件 | 讀回值 | 意義 |
| --- | --- | --- |
| **0x2100 控制權** | **2 = CANopen** | 非文件預設的 1=EtherCAT！ |
| 0x26A0 node-id | 1 | CANopen 佈建態 |
| 0x26A1 CAN 波特率 | 1,000,000 | 1 Mbps |

**全部症狀就位**：控制權不在 EtherCAT → 從站應用層在 SAFEOP 入口
檢查（0.5 ms、AL 0x0022 自貶）;mailbox/SDO 不受控制權管制（枚舉/
讀寫全正常）;CLI 裸 SAFEOP 不建 process data＝不主張控制權＝放行;
SOEM/IgH 寫 FMMU＋process data＝主張控制＝被拒。FMMU LogAddr 與
USB 時序皆為配角（後者仍影響 ML1 量測品質）。

**更正**：前份文件（e5be5fb）「能在 EtherCAT 枚舉 → 0x2100 出廠即 1」
的推論**錯誤**——CoE mailbox 與控制權無關。

**解法（二選一,皆已就緒）**：
- 走 EtherCAT：CoE 寫 `0x2100=1` + `0x2130=1` 存檔 + 重上電 →
  預期 SAFEOP/OP 直通,igh_jog 可直接重跑。
- 走 CANopen：**馬達現在就是 CANopen 模式（node 1@1Mbps）**,
  接 CANable(slcan→socketcan) + 終端電阻即可用既有
  `pc_master --left <can-if> --right none` 全棧直跑（方案 C）。

## 關聯

- 前置：`2026-07-07-phu17-real-enumeration.md`、
  `linux-rt-ethercat-master-plan.md` §2.2（NIC 選型）WP-L1
- 阻塞：WP-L1.4/1.5（使能/點動）、WP-L2、WP-I1——等 EYOU 回覆
  （ESI/0x0022）或 FMMU LogAddr 實驗終判;ML1 量測另需 Intel NIC
- 附帶收穫：gx701 已完成 WP-L0.4 RT 調校（isolcpus 永久生效）,
  cyclictest 基線可重測預期更佳

## 第四輪（同日）：EtherCAT 寫入被控制權鎖死 → 確定改走 CANopen

重上電後嘗試經 EtherCAT CoE 寫 `0x2100=1`（切 EtherCAT 模式）失敗：
`SDO abort 0x06010000 Unsupported access`。進一步試寫良性可寫物件
（0x6081 profile velocity、0x6060 mode）**同樣被拒**——**EtherCAT 下
所有 SDO 寫入都被擋，讀取正常**。

**結論（閉環）**：馬達控制權在 CANopen（`0x2100=2`）時，EtherCAT 介面
是**唯讀被動端**，無法寫任何設定物件（含 0x2100 自己）。這同時解釋
SAFEOP 彈跳（EtherCAT 拿不到控制權）與寫入被拒。**切 0x2100 只能用
當前有控制權的介面 = CANopen（接 CANable）或 UART（EYouServoStudio）**,
不能用 EtherCAT 自己切 → Route A 在切模式前是死路。

**定案：改走 Route B（CANopen）**——馬達現態即 CANopen（node1@1Mbps）,
接 CANable 用既有 `pc_master`（方案 C 全棧,已驗證）直接驅動;若日後要
EtherCAT,屆時在 CANopen 下寫 0x2100=1+存檔+重上電即可。
