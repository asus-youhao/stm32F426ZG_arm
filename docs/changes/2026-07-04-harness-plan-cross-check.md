# Harness 規劃 × 方案 A/B/C 交叉盤點：補三個缺口

- 日期：2026-07-04
- 分支：`feature/harness-loop-engine`
- 類型：docs（設計文件修訂，無程式碼變更）

## 變更摘要

對 `linux-rt-ethercat-master-plan.md`（A，WP-L）、`linux-igh-ethercat-master-plan.md`
（B，WP-I）、`linux-canopen-master-plan.md`（C，WP-C）逐項交叉盤點後，修訂
`docs/design/harness-agent-loop-engine-plan.md`，補上三個缺口：

1. **§3.1 相位微調 hook**：新增 `eng_phase_trim_us()`——方案 A（SOEM）主站是
   DC「跟隨者」，需要 PI 鎖相微調喚醒點（WP-L2.2）；trim 限幅 ±5% 週期。
   CANopen 與 IgH 模式 trim=0 不受影響。
2. **§5.2 降頻語意**：明定「降頻運行」（方案 C 的 EMCY 風暴退避、miss 率超標
   應變）走 harness 生命週期 deactivate→以新 rate 重新 activate，切換中斷
   目標 < 100 ms；**不是**無縫變速，正常運行不提供動態變速。
3. **§6 `bus_health_t`**：`bus_if_t` 增 `health()` callback 與 bus-agnostic
   健康快照結構，CANopen 診斷（EMCY/error counter/busload）與 EtherCAT 診斷
   （WKC/AL state/link）餵進同一結構，HealthAgent 不認協定。

配套更新 §6 對照表（診斷/相位微調兩列）、§9 WP-H4/H5 工作項、§11 盤點結論。

## 動機 / 背景

使用者要求確認 harness/agent/loop engine 架構能否承接既有各分支規劃。盤點
結論：**無結構性衝突**——三份計畫的軟體組織需求均由 WP-H 承接（部分項目被
升級：build flag→執行期切換、編譯期頻率→`--rate`、telemetry_ring/命令信箱
＝H2 的 ring）；硬體採購、佈建 SOP、RT OS 調校與協定驗證與 harness 正交。
僅上述三處是計畫有要求、harness 文件原本沒寫到的，本次補齊。

## 影響範圍

- 僅文件：`docs/design/harness-agent-loop-engine-plan.md`、本檔、`docs/README.md`。
- 對實作的影響（未來）：`eng_phase_trim_us()` 屬 engine API 增項（列入 WP-H5）；
  `bus_health_t` 列入 WP-H4/H5；降頻語意屬 WP-H2/H4 行為規格。

## 驗證方式

- 文件審閱：對照 WP-L2.2（PI 鎖相）、WP-C §5.2/§8（降頻對策）、WP-L/I 驗證
  總表（WKC/AL 監控）確認三個缺口的出處與補法一致。

## 關聯

- 分支：`feature/harness-loop-engine`
- 前置：`docs/changes/2026-07-04-harness-agent-loop-engine-plan.md`（規劃本體）、
  `docs/changes/2026-07-04-wp-h1-agents-refactor.md`（WP-H1 實作）
- 對照文件：`linux-rt-ethercat-master-plan.md`、`linux-igh-ethercat-master-plan.md`、
  `linux-canopen-master-plan.md`
