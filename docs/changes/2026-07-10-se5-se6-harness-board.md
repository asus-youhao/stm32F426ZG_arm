# 2026-07-10 SE5/SE6：板端 harness 正式路徑合體——engine+agents+門面 SOEM 對假從站 PASS

## 變更摘要

把「同套韌體多後端」正式棧搬上 F746 板端並真機驗收（WP-SE5 + SE6/HIL-1）：

1. **併分支**：`feature/h-sdo-bg-escalation`（DC 可動配方：`ec_config.h`＋真
   SOEM/IgH 門面後端＋bus_ecat mode-via-PDO 修正）併入
   `feature/stm32-ethercat-master`（f7 port＋探測韌體）。零衝突、host 5102 檢查 PASS。
2. **`board/ecat_app_main.c`（新）**：板端正式路徑韌體 `make ecat-app` ——
   `bus_ecat(vtable) → ec_master_soem(門面) → f7 port(nicdrv/osal)` +
   loop engine 四相位 agent（bus_rx/safety/motion/bus_tx）+ L1–L4 全棧
   （`app_main_init_hz`）＝ `pc_master --bus ethercat` 的板端等價物。
3. **`ecat/f7/eng_port_f7.c`（新）**：engine 時間源 `port_now_us()` 以 DWT
   CYCCNT 累加成 64-bit（迴繞安全）。
4. **`ec_master_soem.c` 補 `ec_master_dc_error_us()`**：門面契約缺口（h-sdo
   版漏實作,bus_ecat DC PLL 路徑連結不過）;由 `ctx.DCtime % SYNC0` 導出。
5. **`ecat_slave.py --factory-pdo`（新選項）**：模擬 EYOU 真機出廠 PDO 佈局
   （33B/29B,`ec_config.h` 偏移）。「不重映射」配方後端依賴此佈局——精簡
   6B 預設會把 mode@2/tgt@3 寫錯位。selftest 26 檢查不變。
6. **Makefile `ecat-app` target**：`-DEC_TIMEOUTRET=1500`（µs,≪ 4ms 週期）;
   55KB text / 68KB BSS。
7. 板端佔位：`g_bus_canopen` 空 vtable（app_main 預設指標用,進場即切 ecat）。

## 驗證結果（板端 VCP 實錄,PC 假從站 `--factory-pdo --axes 2`）

```
[ ok ] BUS_UP：2 軸 present,OP 完成（掃鏈→PREOP→不重映射→SAFEOP→OP）
engine 啟動 @250Hz（bus_rx/safety/motion/bus_tx）
SE6 harness@250Hz：tick=3001 skip=0 miss=2 overrun=0 late_max=1263µs
  bus_rx：max R=1709µs（AF_PACKET 假從站 RTT 決定,非板端瓶頸）
  safety：max C=6µs   motion：max C=115µs   bus_tx：max W=3µs（超算全 0）
  WKC=6/6 sync=1 安全=RUNNING
  軸0/軸1：sw=0x1427 en=1 fresh=1
CSP：Δpos=834 counts（期望 834=js_rad_to_counts(0,0.01)）跟隨差=0 → PASS
```

- **SE6/HIL-1（harness 正式路徑版）PASS**：使能三步、safety RUNNING 門檻、
  L4→L3→L2→L1 目標鏈、CSP 跟隨全走 agent 相位,非探測韌體直呼 SOEM。
- **CPU 餘裕拍板級數據**：motion（14 軸 L2–L4 含 IK）max **115µs @M7**——
  規劃 §8「1kHz 不夠就拆層」的風險大幅下修;1kHz 預算充足。
- 250 Hz 是遷就 AF_PACKET 假從站 RTT（~1ms）;板端週期源 SE4 已另證 1kHz
  p99=0µs。真 1kHz 閉環驗證屬 HIL-1′/HIL-2（真 ESC）。
- 單位注意：預設軸表 524288 counts/圈（無減速比）;真機 PHU 52953088/圈
  屬 `robot_config` 軸表組態,HIL-2 前要換。

## 影響範圍

- 新增：`board/ecat_app_main.c`、`ecat/f7/eng_port_f7.c`、本文件
- 修改：`ecat/ec_master_soem.c`（補門面函式）、`sim_py/ecat_slave.py`
  （--factory-pdo）、`firmware/Makefile`（ecat-app）、
  `docs/design/stm32-ethercat-master-plan.md`（§5 狀態）
- 板端既有 target（bringup/ecat-probe）與 PC/tests 建置零影響（host 5102 PASS）
- 遺留：`pc_master_soem` PC 端連結需 gx701（SOEM lib）覆核 dc_error_us

## 重跑指令（G16）

```bash
# PC 假從站（出廠佈局,免 sudo）
python3-rawnet firmware/sim_py/ecat_slave.py --iface enx00e04c6809b6 \
  --axes 2 --wd-ms 0 --factory-pdo
# 板端
cd firmware && make ecat-app CUBE_FW_F7=$HOME/toolchains/STM32Cube_FW_F7
arm-none-eabi-objcopy -O binary build-ecat-app/ecat_app_f746.elf build-ecat-app/ecat_app_f746.bin
st-flash --reset write build-ecat-app/ecat_app_f746.bin 0x08000000
# /dev/ttyACM0 115200 觀察;t=12s 出 SE6 報告
```

## 關聯

- Branch：`feature/stm32-ethercat-master`（h-sdo-bg-escalation 已併入）
- 里程碑：**MSE3 補完（harness 正式路徑版 HIL-1）**;SE5 板端側完成
- 前置：`2026-07-10-g16-hil-env-f746-link.md`、`2026-07-08-ecat-dc-recipe-code.md`
- 下一步：HIL-1′（LAN9252 真 ESC,DC PLL 真收斂）→ SE-P0（0x2100）→ SE7 真機
