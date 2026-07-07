# EtherCAT 單軸 bring-up / 診斷工具（WP-L1 / WP-I1 / P0）

2026-07-07 對 PHU17 真機 bring-up 過程產出的四支工具。
背景與診斷結論見 `docs/changes/2026-07-07-phu17-safeop-diagnosis.md`。

| 檔案 | 堆疊 | 用途 |
| --- | --- | --- |
| `sdo_probe.c` | SOEM 2.x | 唯讀 SDO 特性化：identity、0x2025、減速比、0x6502、STO 參數等 |
| `phu_jog.c` | SOEM 2.x | CiA402 使能 + CSP ±2° 點動（含防跳/故障監看/歸位）——卡在 SAFEOP,見診斷 |
| `igh_probe.c` | IgH ecrt | 變因拆解探測：pdos/dc/cycle/AssignActivate/config-SDO 全參數化 |
| `igh_jog.c` | IgH ecrt | 同 phu_jog 的 IgH 版（domain+週期迴圈+使能 FSM+點動） |

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
