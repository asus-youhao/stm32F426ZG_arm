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

## 關聯

- 前置：`2026-07-07-phu17-real-enumeration.md`、
  `linux-rt-ethercat-master-plan.md` §2.2（NIC 選型）WP-L1
- 阻塞：WP-L1.4/1.5（使能/點動）、WP-L2、WP-I1 全數等 NIC
