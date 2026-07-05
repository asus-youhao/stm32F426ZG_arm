# SDO/CoE 背景通道（H4 遞延項落地，設計文件 §5.3）

- 日期：2026-07-05
- 分支：`feature/h-sdo-bg-escalation`（stacked 於 `feature/ecat-backend-sim`）
- 類型：feat + test

## 變更摘要

1. **`firmware/app/sdo_bg.[ch]`**：RUN 中安全讀寫 OD 的背景通道——
   非 RT 端 `sdo_bg_request()/poll()`（SPSC 請求/回應佇列，tag 配對）；
   RT 端 HOUSEKEEP 相位每步只推進一小步。同時最多一筆 in-flight
   （CANopen SDO 協定本身的限制）。
   - CANopen：非阻塞 client 狀態機（IDLE→WAIT），step 送請求幀、
     回應由 pump 分派（`sdo_bg_on_frame`，驗 node+index+sub 吻合防撿舊），
     step 計逾時（預設 50 步）；abort（0x80）帶回 abort code。
   - EtherCAT：走 `ec_coe_read/write`（sim 立即完成；真後端換 mailbox
     分片時本模組介面不變）。
2. **掛載**：`bus_if_t` 增 `sdo_bg_step` op（兩後端各自實作）；
   `dual_arm_pump_rx` 分派鏈加入 `sdo_bg_on_frame`；`app_io_agents`
   新增 sdo_bg agent（divisor 2，HOUSEKEEP）。
3. **阻塞版 co_sdo 定位不變**：僅限 bring-up/BUS_UP（非 RT）使用。

## 動機 / 背景

§5.2 頻寬預算明定「運行中 SDO 禁止」指的是阻塞式輪詢；真機調機必須能
RUN 中讀診斷/改參數。背景通道把流量壓到每請求 2 幀、把延遲移出 RT 路徑。

## 影響範圍

- 新增：`firmware/app/sdo_bg.[ch]`、`firmware/tests/test_sdo_bg.c`
- 修改：`engine/bus_if.h`（+sdo_bg_step）、`bus_canopen.c`、`bus_ecat.c`、
  `dual_arm.c`（pump 分派）、`app_io_agents.c`（+agent）、三個 Makefile
- PDO/控制行為零變化（測試硬驗證，見下）。

## 驗證方式

`firmware/tests` 4935 檢查 0 失敗，新增 test_sdo_bg 覆蓋：

- 單元：佇列滿、逾時（無回應 node）、abort 解析（注入 0x80 幀 →
  status/abort code 正確；index 不符的殘留回應不被撿走）。
- **SIL E2E（CANopen）**：RUN 中讀 node3 0x6061=8、寫 0x6083——
  背景讀期間 **RPDO 幀數與基準一幀不差**、額外流量恰為 2 幀 SDO。
- E2E（EtherCAT 後端）：同一非 RT API 讀 CoE 成功。
- pc_master 建置零警告。

## 關聯

- 設計：`harness-agent-loop-engine-plan.md` §5.3；H4 變更文件遞延清單
- 下一步：真機（WP-C1.4 EMCY/診斷）與 IgH mailbox 非同步版（WP-I3）
