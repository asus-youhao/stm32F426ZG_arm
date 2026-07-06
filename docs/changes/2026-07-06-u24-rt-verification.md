# 24.04 PREEMPT_RT 真機驗證：H3 抖動 PASS / IgH I0.4 退役 / SOEM / vcan SIL

- 日期：2026-07-06
- 分支：`feature/h-sdo-bg-escalation`（純驗證報告，無程式改動）
- 機器：asus-gx701（i7-10875H 16C，Ubuntu 24.04.4 Pro，
  **6.8.1-1052-realtime PREEMPT_RT**，`/sys/kernel/realtime=1`）
- 注意：**尚未設 isolcpus/nohz_full**（要動 grub＋重開機，留待使用者決定）；
  以下成績都是「未隔離核」的基線。

## 結果總表

| 測項 | 結果 |
| --- | --- |
| 單元測試（真機建置） | **5083 檢查 0 失敗** |
| B. CANopen vcan SIL 全流程 | **PASS**：14/14 上線、500 Hz、busload 91%、J3 移動→末端位姿變化、estop on/off 復歸、5819 ticks miss=1 |
| C. WP-H3 抖動驗收（1 kHz） | **PASS**：57,996 ticks，late p50/p99/max = 9/27/1329 µs → p99 27 < 50（5% 週期）；miss 率 0.01% |
| C′. cyclictest 基線 | avg 2 µs、**max 14 µs**（30 s，SCHED_FIFO 90）→ §3.5 門檻（p99<20/max<50）過 |
| D. SOEM | clone+cmake 建置 OK；`slaveinfo` 於 USB NIC `ecx_init succeeded`、No slaves found（無硬體，如預期） |
| E. IgH（**I0.4 風險項**） | **stable-1.6（1.6.9-8-gbeb2bf07，含 igc_6.8 合併）對 6.8.1-realtime：bootstrap/configure/make/modules 全過；`ec_master.ko`+`ec_generic.ko` insmod/rmmod 乾淨** |

## 細節

### H3（trace ring + trace_report.py 即驗收工具）

`sudo ./pc_master --bus ethercat --rate 1000 --seconds 60 --trace h3.csv`
→ 相位耗時 p99：read 20 / compute 25 / write 19 / house 5 µs——1 kHz
週期餘裕充足。58 秒僅 1 筆 >1 ms 離群（skipped=1，啟動暫態）。
與本開發機（非 RT，late p99=248 µs FAIL）對照，同一條命令、同一
工具鏈——儀器面（項目 3）達成設計目的。

### CANopen（方案 C 的 SIL 預演）

真 SocketCAN（vcan）+ python-can 14 假從站；`--bringup 1` 單軸自檢
先過再進 RUN。busload 91% 與規劃文件的 500 Hz 物理估算一致——
真機接 EYOU 後這就是現場儀表數字。

### SOEM / IgH（方案 A / B 環境面）

兩者環境門檻全數排除；差 EtherCAT 從站硬體即可跑 ML1（單軸 1 kHz
DC 同步）拍板對比。機器只有一張乙太 NIC（USB Realtek，目前作
SSH 連線用）——接從站測試時建議：SSH 改走 WiFi（wlo1 現為 DOWN）
把有線 NIC 讓給 EtherCAT，或加一張 PCIe/USB3 NIC。

## 建議後續（需使用者決定）

1. `rt_setup.sh`（A 分支）套 isolcpus/nohz_full/IRQ 綁核 → grub＋重開機，
   之後 `--rt-strict` 應全 PASS、抖動再壓一級。
2. EYOU 從站到貨：SOEM `slaveinfo` 讀 SII → 單軸 CSP（WP-L1/L2）。
3. 遠端工作區留在 `~/robot_test/`（repo/SOEM/ethercat 三者已建好）。

## 關聯

- `4c2a65a`（trace ring，本次的量測儀器）、`e1e78c2`（開機自檢）
- 設計：`harness-agent-loop-engine-plan.md` §3.5/§3.6、
  `linux-rt-ethercat-master-plan.md`（WP-L0 環境/ML1 拍板）、
  `linux-igh-ethercat-master-plan.md`（I0.4）
