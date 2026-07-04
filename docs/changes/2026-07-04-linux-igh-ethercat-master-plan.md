# 2026-07-04 Ubuntu 24.04 Pro + PREEMPT_RT + IgH EtherCAT Master 深度規劃

## 變更摘要

新增 `docs/design/linux-igh-ethercat-master-plan.md`：與 SOEM 版（22.04 分支）平行的 **PC 主站方案 B**——Ubuntu 24.04 LTS Pro（realtime-kernel 6.8-rt, PREEMPT_RT）+ **IgH EtherCAT Master（EtherLab, kernel-space）**，控制雙臂 14 顆 EYOU PHU 關節（CoE, CSP @1 kHz）。內容：

- IgH vs SOEM 選型分析：kernel module + 原生網卡驅動（`ec_igb`）的抖動上限、`ethercat` CLI 工具鏈（SDO 調試/`cstruct` PDO 生成/**FoE 韌體升級**/ESC CRC 計數器）、LGPL 使用者庫、ROS 2 Jazzy（`ethercat_driver_ros2`）生態
- 平台：24.04 `pro enable realtime-kernel`（6.8-rt）；**IgH stable-1.6 對 6.8 內核的 DKMS 編譯列為首個技術關卡**（WP-I0.4），含四層退路（GitLab master → generic driver → 退 22.04 → 退 SOEM）
- NIC 原生驅動對應表（鎖定 i210/`ec_igb`；igc/i225 原生驅動未必有）
- 架構：`ec_master.h` 門面 → `ecrt_*` API 完整對映表（request/domain/slave_config/startup SDO/`ecrt_slave_config_dc`）；DC 模型差異——IgH 主站是「發號者」（`ecrt_master_application_time` + `sync_reference_clock`/`sync_slave_clocks`），非 SOEM 的鎖相跟隨
- 深度工作分解 **WP-I0～I7**（每項含 DoD）＋里程碑 MI1～MI5；MI1 產出「IgH(24.04) vs SOEM(22.04)」抖動對比報告作為兩方案定案依據
- IgH 特有運維：DKMS 內核升級回歸 runbook、apt hold 鎖版、`ethercat.service` 熱恢復、CRC 計數器線材巡檢

RT 調校方法論沿用 `linux-rt-ethercat-master-plan.md`，協定面沿用 `ethercat-coe-master-plan.md`，不重複。

## 動機 / 背景

- 使用者要求：獨立分支評估 Ubuntu 24.04 Pro + PREEMPT_RT + IgH 的主站方案。
- STM32 暫緩後主站確定落在 Linux PC，SOEM 的「可回移 MCU」優勢弱化；IgH 的 kernel-space 資料路徑、工具鏈與 ROS 2 生態成為更工業正規的選項，值得與 SOEM 方案平行推進到 ML1/MI1 後以實測抖動數據定案。

## 影響範圍

- 新增：`docs/design/linux-igh-ethercat-master-plan.md`、本變更文件
- 修改：`docs/README.md`（索引）
- **純文件變更，無程式碼/硬體行為改動**。三條規劃分支分工：STM32 板端（暫緩）/ 22.04+SOEM（方案 A）/ 24.04+IgH（方案 B，本分支）

## 驗證方式

- 文件審閱：ecrt_* API 對映與 IgH 部署流程對照 EtherLab 官方文件；協定數值沿用前置文件（已對照 doc_EYOU 原廠手冊）
- 規劃可行性驗收點：WP-I0.4（IgH 對 6.8-rt DKMS 編譯）與 MI1（單軸 1 kHz DC 同步 + 對比報告）

## 關聯

- 分支：`feature/linux-igh-ethercat-master`（自 `feature/linux-rt-ethercat-master` 切出）
- 前置文件：`docs/design/ethercat-coe-master-plan.md`（7851268）、`docs/design/linux-rt-ethercat-master-plan.md`（0bd28f9）
- 相關：`docs/design/canopen-vs-ethercat.md`、`docs/design/dual-arm-control-plan.md`
