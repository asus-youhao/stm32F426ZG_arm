# EYOU 原廠確認清單：CAN FD / CANopen 細節（WP-C0.6 + C2.2 前置）

- 文件層級：對外詢問清單（working doc）
- 狀態：**待寄出** → 回覆後把答案回填本文件並歸檔
- 分支：`feature/linux-canopen-master`
- 背景：`linux-canopen-master-plan.md` §3.1（FD 升級路）、§5.1（SYNC 支援）

## 為什麼要問

1. **頻寬天花板**：Classic CAN 1 Mbps 下雙臂各 500 Hz 已用到 ~95% 匯流排
   （§5.2 精算）。若原廠韌體開放 **CAN FD data phase**（如仲裁 1M/資料 5M），
   8B PDO 序列化時間縮到 ~1/4，500 Hz 變輕載、**1 kHz 進入可行區**——
   方案 C 的定位將從「備援」升級為與 EtherCAT 平行的正式候選。
2. **軸間同步**：SYNC 鎖存（transmission type=1）是把 14 軸取樣 skew 從
   ~1.5 ms 壓到 <100 µs 的關鍵（G3），但手冊未明載支援程度。

## 問題清單（寄給 EYOU 技術窗口）

### A. CAN FD data phase（決定頻寬升級路）

| # | 問題 | 為什麼重要 |
| --- | --- | --- |
| A1 | PHU 系列（PHU-14H/17H/20H）的 CAN FD 埠，目前韌體（v1.23）是否支援 **FD data phase**（BRS）？或僅硬體具備、韌體固定 Classic 2.0B？ | 決定 §3.1 升級路是否存在 |
| A2 | 若支援：data phase 波特率上限？（2M / 5M / 8M）建議取樣點？ | 頻寬預算重算依據 |
| A3 | 協定形式：**CiA 1301（CANopen FD）**？還是「Classic CANopen 幀格式 + FD 幀承載」的廠商自訂？ | 決定主站協定棧要改多少 |
| A4 | 開啟方式：OD 物件（哪個 index）？EYouServoStudio 設定？韌體升級？ | 佈建 SOP 要不要加步驟 |
| A5 | 若目前不支援：是否在 roadmap？預計版本/時間？ | 決定等或不等 |

### B. CANopen 同步（G3，WP-C2.2 前置）

| # | 問題 | 為什麼重要 |
| --- | --- | --- |
| B1 | RPDO/TPDO **transmission type=1（synchronous cyclic）**是否支援？（寫 0x1400/0x1800:02=1） | SYNC 鎖存模式的前提 |
| B2 | 支援的話：SYNC（0x080）到 TPDO 回傳的延遲/抖動規格？ | 軸間 skew 預算 |
| B3 | `0x1005/0x1006`（communication cycle period）是否需要寫？寫 2000 µs（500 Hz）可接受？ | 從站側 SYNC 逾時監控 |
| B4 | 不支援 type=1 的話：async（255）模式下收到 RPDO 到伺服環套用的延遲？ | 退路的 skew 如實記錄 |

### C. 其他（佈建/校正順帶確認）

| # | 問題 | 為什麼重要 |
| --- | --- | --- |
| C1 | `0x2025`（編碼器解析度）出廠值？19-bit=524288 counts/rev 是否正確？ | robot_config 換算校正（WP-C0.5） |
| C2 | `0x2100`（控制權）在 EtherCAT 模式下，CAN 口是否仍回應 SDO？（佈建腳本假設可以，不行則走 UART） | provision_joint.sh 流程分支 |
| C3 | `0x2130=1` 與 `0x1010:01="save"` 兩種存檔的差異？保存耗時上限？ | SOP「嚴禁斷電」窗口大小 |
| C4 | EMCY 的廠商特定欄位（byte 3..7）格式？E1xx/E2xx/E3xx/E4xx 之外還有哪些碼？ | co_emcy 解析完整性（G5） |
| C5 | 運行中（Operational、500 Hz PDO 滿載）發 SDO 讀診斷物件是否安全？有無建議節流？ | RUN 中診斷政策（§5.2 目前保守禁用） |

## 回覆後的動作對照

| 回覆 | 動作 |
| --- | --- |
| A1=支援 | 開 FD 評估支線：`ip link ... dbitrate` + `co_frame_t` 擴 64B + PDO 重排（§3.1 已列） |
| A1=不支援 | 維持 Classic 預算；1 kHz 一律走 EtherCAT（方案 A/B） |
| B1=支援 | WP-C2.2 照計畫（`--sync` 模式已實作於 harness 分支，真機直接驗） |
| B1=不支援 | 退 async + 固定發送順序，skew 如實記錄進對比報告（WP-C5.1） |
| C2=不行 | provision_joint.sh 首步改「必走 UART/EYouServoStudio」，腳本提示已預留 |
