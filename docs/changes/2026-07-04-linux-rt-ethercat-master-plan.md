# 2026-07-04 Ubuntu 22.04 Pro + PREEMPT_RT EtherCAT 主站深度規劃

## 變更摘要

新增 `docs/design/linux-rt-ethercat-master-plan.md`：主站架構決策變更——**STM32F746 板端主站暫緩**，改以 **Ubuntu 22.04 Pro（realtime-kernel, PREEMPT_RT）x86 PC/工控機** 當 EtherCAT 主站，直接控制雙臂 14 顆 EYOU PHU 關節（CoE, CSP @1 kHz）。內容：

- 硬體選型：4 核 x86（BIOS 需可關 C-states/Turbo/SMT、SMI 乾淨）、EtherCAT 專用 Intel NIC（i210/i225，禁 Realtek/USB 網卡）
- RT 環境：`pro enable realtime-kernel` 安裝流程、grub 隔離參數（isolcpus/nohz_full/rcu_nocbs/irqaffinity）、IRQ 綁定、SCHED_FIFO 優先權配置表（NIC IRQ 85 > cyclic 80）、mlockall/無鎖 RT 路徑守則、cyclictest/hwlatdetect 驗收基線（12 h max < 50 µs）
- 堆疊選型：**SOEM 主路徑**（與 STM32 規劃共用 `ec_master.h` 抽象、未來可回移）、IgH EtherCAT Master 為抖動不達標時的備援，比較表與升級路徑
- 1 kHz 週期引擎：`clock_nanosleep(TIMER_ABSTIME)` + DC 鎖相（PI 把喚醒點對齊 `ec_DCtime` 柵格）、一拍延遲模型、lock-free telemetry ring
- 深度工作分解 **WP-L0～L7**（每項含 DoD 驗收標準）＋ 里程碑 ML1～ML5，拍板點設在 ML1（單軸 1 kHz DC 同步抖動達標）
- 風險對策（BIOS SMI、raw socket 抖動、內核升級回歸、鬆閘湧浪、PC 單點故障靠硬體 STO 兜底）與驗證方式總表

協定面設計（PDO 映射、DC 週期規則、CiA402 序列、E40x 排錯表）**沿用** `ethercat-coe-master-plan.md`，不重複。

## 動機 / 背景

- 使用者決策：先不做 STM32 板端版本，主站落在 Ubuntu 22.04 Pro + PREEMPT_RT。
- PC 主站可集中風險在「RT 調校 + EtherCAT 協定打通」，避免同時處理 MCU 移植；且與既有 `pc_master`（SocketCAN 版）的「同套韌體跑 PC」模式一脈相承，上位機/3D 視覺化同機整合零成本。
- `ec_master.h` 抽象層讓 STM32 路徑保留為未來選項（只換 nicdrv/osal）。

## 影響範圍

- 新增：`docs/design/linux-rt-ethercat-master-plan.md`、本變更文件
- 修改：`docs/README.md`（索引）
- **純文件變更，無程式碼/硬體行為改動**。對既有規劃的效力：`dual-arm-control-plan.md` WP0.2 主站位置拍板為 Linux RT PC；`ethercat-coe-master-plan.md` 的 WP-E4（F746 移植）暫緩、其餘協定設計沿用

## 驗證方式

- 文件審閱：RT 參數與流程對照 Ubuntu Pro 官方 realtime-kernel 文件；協定數值沿用前置文件（已對照 doc_EYOU 原廠手冊）
- 規劃可行性驗收點：ML1（WP-L0～L2——單軸 1 kHz DC 同步、cyclictest 12 h max < 50 µs、WKC 全對）

## 關聯

- 分支：`feature/linux-rt-ethercat-master`（自 `feature/ethercat-coe-master` 切出）
- 前置文件：`docs/design/ethercat-coe-master-plan.md`（commit 7851268）
- 相關：`docs/design/canopen-vs-ethercat.md`、`docs/design/dual-arm-control-plan.md`
