# EtherCAT 單軸 bring-up / 診斷工具（WP-L1 / WP-I1 / P0）

2026-07-07 對 PHU17 真機 bring-up 過程產出的工具。
背景與診斷結論見 `docs/changes/2026-07-07-phu17-safeop-diagnosis.md`。

## 主站機建置（先跑這個）

`setup_master_host.sh` — 在一台 PREEMPT_RT 機器上一鍵取得並建置
SOEM + IgH（版本鎖定、冪等、可重跑）。取代「手動 git clone + cmake +
configure」的口耳相傳流程。

```bash
./setup_master_host.sh [--dir ~/robot_test] [--nic-mac <EtherCAT NIC MAC>]
```

鎖定版本（2026-07-06/07 實測於 6.8.1-1052-realtime）：
SOEM `2f73eaa`（2.x context API）、IgH `beb2bf07`（1.6.9-8，含 igc_6.8；
6.8 內核用 generic driver）。腳本結尾印出 IgH 載入 / CoE 讀寫 / SOEM
slaveinfo 的常用指令。

| 檔案 | 堆疊 | 用途 |
| --- | --- | --- |
| `sdo_probe.c` | SOEM 2.x | 唯讀 SDO 特性化：identity、0x2025、減速比、0x6502、STO 參數等 |
| `phu_jog.c` | SOEM 2.x | CiA402 使能 + CSP ±2° 點動（含防跳/故障監看/歸位）——卡在 SAFEOP,見診斷 |
| `igh_probe.c` | IgH ecrt | 變因拆解探測：pdos/dc/cycle/AssignActivate/config-SDO 全參數化 |
| `igh_jog.c` | IgH ecrt | 同 phu_jog 的 IgH 版（domain+週期迴圈+使能 FSM+點動） |
| `wd_read.c` | SOEM 2.x | **診斷關鍵**：讀 ESC 看門狗暫存器(0x0400/0x0420) + 高速輪詢 AL 狀態抓 SAFEOP 彈跳時刻——即此工具測到「進 SAFEOP 後 0.5ms 自貶 PREOP、AL 0x0022」 |
| `phu_state.c` | SOEM 2.x | 逐級 PREOP→SAFEOP→OP 診斷（每級讀 AL code）；`lsa` 參數試 logical start addr |
| `phu_state2.c` | SOEM 2.x | 讀 SM sync 模式(0x1C32:01=1 SM-sync/0x1C33:01=0x22) + PREOP 先啟 SYNC0 再請 SAFEOP |

> `wd_read.c`/`phu_state*.c` 是 SAFEOP 卡點三輪診斷的工具，
> 結論見診斷文件（真根因 = `0x2100=2` 控制權在 CANopen）。
> 已捨棄：`phu_safeop.c`（編譯錯，功能被 `wd_read.c` 涵蓋）。

## 編譯（在有對應堆疊的機器上）

```bash
# SOEM（S=SOEM 原始碼樹,已 cmake build）
gcc sdo_probe.c -o sdo_probe -I$S/include -I$S/build/include \
    -I$S/osal -I$S/osal/linux -I$S/oshw/linux -L$S/build -lsoem -lpthread -lrt
# IgH（E=ethercat 原始碼樹,已 make;需 ec_master.ko 已載入）
gcc igh_jog.c -o igh_jog -I$E/include -L$E/lib/.libs -lethercat -lm -lrt
sudo LD_LIBRARY_PATH=$E/lib/.libs chrt -f 80 ./igh_jog
```

## 使用前提

- 馬達 48V 母線（24V 可枚舉/讀 SDO;出力建議 48V）
- 使能/出力需 STO 兩路 24V
- **NIC 品質決定成敗**：USB r8152 過不了從站同步驗證（診斷文件），
  需 Intel i210/i225 等級（筆電走 Thunderbolt 轉接）
