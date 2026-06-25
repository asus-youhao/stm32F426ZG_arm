# 建立 F746 雙臂 CANopen 通訊韌體骨架

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

於 `firmware/` 新增 STM32F746ZG 的 CANopen 通訊韌體骨架,以 **雙路 bxCAN（CAN1=左臂、CAN2=右臂）** 控制 EYOU PHU 雙臂各 7 軸（共 14 軸）。

新增模組:
- `firmware/canopen/canopen.h`：共用型別、COB-ID、NMT/節點狀態定義。
- `firmware/canopen/co_bxcan.[ch]`：STM32 bxCAN 硬體層（CAN1/CAN2、1 Mbps、RX 環形佇列）。
- `firmware/canopen/co_sdo.[ch]`：SDO client（expedited 讀寫,組態用）。
- `firmware/canopen/co_nmt.[ch]`：NMT 控制 + Heartbeat 監看。
- `firmware/canopen/co_pdo.[ch]`：PDO 收發（CSP：CW+目標位置 / SW+實際位置）。
- `firmware/canopen/cia402.[ch]`：CiA 402 狀態機 + 運行模式（含 0x6060 對應）。
- `firmware/app/dual_arm.[ch]`：雙臂設定（左/右各 7 軸、節點 ID、型號對照 CLAUDE.md）與 1 kHz 控制。
- `firmware/app/app_main.c`：初始化 + 1 kHz tick 骨架。
- `firmware/README.md`：架構、整合到 CubeMX、腳位與位元時序說明。

## 動機 / 背景

使用者要求:以 F746 建立與 EYOU PHU 馬達的通訊韌體,**雙手走不同 CAN channel**,採 **CANopen**。

## 設計重點

- **雙手不同 channel:可行且建議** —— F746 內建 2 路獨立 bxCAN,左臂 CAN1、右臂 CAN2,頻寬獨立、故障隔離。
- 控制模式採 **CSP（循環同步位置）**,初始化以 SDO 設定模式與 PDO 映射,運行期用 PDO 週期交換。
- CiA 402 使能流程：fault reset → shutdown(0x06) → switch on(0x07) → enable op(0x0F)。
- 未使能時目標位置 = 實際位置,避免使能瞬間跳動。

## 影響範圍

- 新增 `firmware/` 目錄與上述檔案。
- 更新 `CLAUDE.md` 目錄結構、`docs/README.md` 索引。
- 韌體為**骨架**:protocol 邏輯完整,但 bxCAN 控制代碼/腳位/位元時序需依 CubeMX 專案與時脈樹調整,尚未在硬體驗證。

## 注意 / 待辦

- 位元時序（Prescaler/BS1/BS2）以 APB1=45 MHz 為例,需依實際時脈重算。
- PDO 映射假設 RPDO1=CW+TgtPos、TPDO1=SW+PosAct,需與關節實際 OD 一致（手冊 §3.4/§6.4）。
- 1 Mbps × 7 軸 1 kHz 頻寬吃緊,高頻力控建議改 EtherCAT（見 canopen-vs-ethercat.md）。
- 量產等級可考慮移植 CANopenNode。

## 驗證方式

- 程式結構與 CANopen 幀格式（SDO/NMT/PDO/CiA402）對照標準與手冊檢查。
- 實機驗證待 CubeMX 專案整合後進行（單軸 bring-up → 單臂 → 雙臂）。

## 關聯

- `dual-arm-control-plan.md`（WP1/WP2）、`canopen-vs-ethercat.md`、`eyou-phu-motor-analysis.md`
