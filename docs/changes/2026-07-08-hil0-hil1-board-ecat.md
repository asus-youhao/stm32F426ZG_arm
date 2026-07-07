# 2026-07-08 HIL-0/HIL-1 通過：F746 板端 EtherCAT 主站 ↔ PC 假 PHU 從站全流程

## 變更摘要

板端真機驗收（規劃 §6.2 HIL-0/HIL-1）＋除錯過程中修掉的三個板端 bug：

1. **HIL-0 PASS**：Nucleo-F746ZG 直連 PC Realtek NIC，npcap/scapy 抓到板子送出的
   0x88A4 掃鏈幀（穩定 44 幀/9s），板端同時能收 PC 廣播（雙向驗證）。
2. **HIL-1 PASS**：板端 SOEM 對 `ecat_slave.py`（npcap 模式、2 軸）完整跑通——
   掃鏈 2 從站（vendor 0x1097/product 0x00010002）→ CoE PDO 重映射 12O+12I →
   SAFEOP → SDO 0x1000 → OP → CiA402 使能三步（sw 0x21→0x23→0x27）→
   **CSP 點動 pos=5000、2000 週期 WKC 漏=0 → PASS**。
3. 板端修正（`firmware/ecat/f7/`）：
   - **DWT 上鎖（關鍵）**：Cortex-M7 的 DWT 需先寫 `LAR(0xE0001FB0)=0xC5ACCE55` 解鎖,
     否則 CYCCNT 不計數 → SOEM 逾時永不到期 → `ecx_srconfirm` 無限迴圈（掃鏈卡死）。
   - `oshw_mac_init` 冪等（`ecx_init` 會二次呼叫,避免重複 PHY reset 5s）。
   - `HAL_ETH_Transmit` 後補 `HAL_ETH_ReleaseTxPacket`（4 次後描述符耗盡）。
   - SYSCFG PMC(RMII sel) 移到 ETH 時脈啟用之前（RM0385 規定）。
   - 新增 `ECAT_PHY_FORCE_10M` 建置選項與 `oshw_phy_read()` 診斷介面。
4. `ecat_slave.py` 新增 **Windows npcap 傳輸**（scapy L2socket + `pcap_setmintocopy(0)`
   立即交付 + 回音防護），Windows 免 WSL 直接當假從站；Linux AF_PACKET 路徑不變。
5. `ec_options.h` 逾時改 `#ifndef` 可覆寫（HIL-1 npcap 延遲 ms 級,
   建置用 `-DEC_TIMEOUTRET=50000 -DEC_TIMEOUTSAFE=200000`）。
6. 探測韌體加診斷：GPIO/AF dump、PHY 暫存器（BSR/SSR）、MMC 計數器、RX 輪詢統計。

## 重要發現（硬體）：100M 鏈路不穩,10M 全雙工穩定

- 100BASE-TX 下 link 每秒彈跳（BSR latching-low 交替）,幀全滅；
  **板端 ANAR 只廣告 10M（FORCE_10M）後 link 穩定、雙向通**。
- 嫌疑：網線品質或 **Nucleo 僅 ST-Link USB 供電**（UM1974 警告;100BASE-TX
  發射功耗顯著高於 10BASE-T,電源塌陷 → PHY 重啟）。
- ⚠ **HIL-2 前必須解決**：真實 EtherCAT 從站（EYOU PHU）**只支援 100BASE-TX
  全雙工**,10M 繞道只對軟體假從站有效。待辦：換短線/好線、外部 5V（E5V jumper）
  或大電流 USB 埠,再驗 100M 穩定性。

## 驗證方式

```
# 板端（10M 診斷組態 + npcap 逾時）
make ecat-probe C_DEFS="-DUSE_HAL_DRIVER -DSTM32F746xx -DECAT_PHY_FORCE_10M \
  -DEC_TIMEOUTRET=50000 -DEC_TIMEOUTSAFE=200000"
make flash-ecat
# PC 端（Windows,npcap 驅動需存在;--iface 給介面描述關鍵字）
python firmware/sim_py/ecat_slave.py --iface Realtek --axes 2 --wd-ms 0
# VCP(COM3,115200) 觀察全流程輸出;離線 selftest 26 檢查照常 PASS
```

假從站累計處理 32,800+ 幀無錯（含 LRW 週期資料）。

## 影響範圍

- `firmware/ecat/f7/osal.c`（DWT LAR）、`oshw.c`（冪等/ReleaseTx/PMC 順序/10M 選項/phy_read）、
  `oshw.h`、`soem/ec_options.h`（逾時 #ifndef）
- `firmware/board/ecat_probe_main.c`（診斷輸出）
- `firmware/sim_py/ecat_slave.py`（npcap 傳輸,平台自動選）
- 無腳位/時脈變更（沿用 SE3 標註）；PHY 廣告能力屬執行期組態

## 關聯

- Branch：`feature/stm32-ethercat-master`
- 里程碑：**MSE2（板端 raw frame 通）+ MSE3（板端對假從站 OP+CSP）同日達成**
- 前置：`2026-07-07-se3-f746-soem-port.md`、`2026-07-07-ecat-slave-sim-sil-c.md`
- 下一步：SE4（TIM 1kHz + DC 鎖相）；HIL-2 前先解 100M 穩定性（線材/供電）
