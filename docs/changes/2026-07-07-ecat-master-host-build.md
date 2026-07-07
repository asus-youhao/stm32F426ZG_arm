# EtherCAT 主站機建置腳本（SOEM + IgH 可重現）

- 日期：2026-07-07
- 分支：`feature/linux-rt-ethercat-master`
- 類型：tooling + docs

## 變更摘要

`tools/ecat_bringup/setup_master_host.sh`：在 PREEMPT_RT 主站機上一鍵
取得並建置 SOEM 與 IgH，**版本鎖定、冪等、可重跑**。

## 動機 / 背景

SOEM/IgH 是在 gx701（2026-07-06/07）以手動 `git clone` + `cmake` /
`bootstrap`+`configure`+`make modules` 建起來的，過程只存在於對話與該機
`~/robot_test/`——repo 裡只有四支 bring-up 工具的原始碼，**沒有記錄
主站堆疊怎麼來、鎖哪個 commit、什麼 configure 旗標**。機器一重灌即
需考古；換機（如之後的 NUC）也無標準流程。此腳本把它固化。

## 內容（實測鎖定組合）

| 堆疊 | commit | 說明 |
| --- | --- | --- |
| SOEM | `2f73eaa` | 2.x（context API `ecx_*`） |
| IgH | `beb2bf07` | stable-1.6 tip = 1.6.9-8;含 igc_6.8 合併 → 支援 6.8 內核 |

- IgH configure 旗標：`--disable-8139too --enable-generic
  --with-linux-dir=/lib/modules/$(uname -r)/build`——6.8-rt 上原生
  e1000e/igc 驅動只到 6.4,故用 **generic driver**（任何 NIC 可用；
  SOEM 走 raw socket 則無此限）。
- 腳本結尾印出 IgH 模組載入（`main_devices=<MAC>`）、CoE SDO 讀寫、
  SOEM slaveinfo 的常用指令。

## 影響範圍

- 新增：`tools/ecat_bringup/setup_master_host.sh`
- 修改：`tools/ecat_bringup/README.md`（新增「主站機建置」節）
- 純建置工具,不影響韌體/主站程式。

## 驗證方式

- `bash -n` 語法檢查通過。
- 腳本內鎖定的 commit / configure 旗標**與 gx701 實際 checkout 逐項
  核對一致**：SOEM 2f73eaa、IgH beb2bf07（1.6.9-8-gbeb2bf07）、
  旗標 disable-8139too/enable-generic/with-linux-dir。
- 冪等：repo 已存在時走 `fetch`+`checkout`,重跑安全。

## 關聯

- 前置：`2026-07-06-u24-rt-verification.md`（首次手動建置的驗證）
- 配套：`firmware/rt/rt_setup.sh`（RT 調校）、
  `tools/ecat_bringup/`（bring-up/診斷工具）
- 換機適用：之後 Intel NUC（I219-V, e1000e）當主站時同一腳本即可
