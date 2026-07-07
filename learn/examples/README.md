# learn/examples — 每節 / 每里程碑的對照範例碼

與教學頁一一對應的最小可跑範例。設計原則承襲 repo 哲學：**免硬體優先** —
L1 用 `common/hal_stub.h` 在 PC 模擬、L2/專案A 用 vcan + `common/fake_slave.py`
（純 stdlib 假 CANopen 關節）對打、L3 純邏輯直接編譯；只有 EtherCAT 真機類
（l3/05、專案B m2~m5）需要實體從站。

```bash
make          # 建置全部可在 PC 編譯的範例
make run      # 跑所有「零依賴」範例（不需 vcan/sudo）
make cansim   # 跑需要 vcan 的範例（先 sudo common/vcan_setup.sh 一次）
```

## 對照表

| 範例 | 對照教學 | 跑法 | 需求 |
| --- | --- | --- | --- |
| `l1/00-toolchain` | L1-2 | 讀 README 照打 | apt |
| `l1/01-blink` | L1-3 | `make run` | 無 |
| `l1/02-uart-printf` | L1-4 | `make run` | 無 |
| `l1/03-systick-timeout` | L1-5 | `make run` | 無 |
| `l1/04-tim-tick-500hz` | L1-6 | `make run` | 無 |
| `l2/01-can-raw` | L2-7 | `make run` | vcan |
| `l2/02-nmt-heartbeat` | L2-8 | `make run` + fake_slave | vcan |
| `l2/03-sdo-client` | L2-9 | `make run` + fake_slave | vcan |
| `l2/04-pdo-csp` | L2-10 | `make run` + fake_slave | vcan |
| `l2/05-bus-budget` | L2-11 | `make run` | 無 |
| `l2/06-run-all` | L2-12 | `./run.sh`（一鍵串全部） | vcan |
| `l3/01-cia402-sm` | L3-13 | `make run` | 無 |
| `l3/02-units` | L3-14 | `make run` | 無 |
| `l3/03-ik-2link` | L3-15 | `make run` | 無 |
| `l3/04-watchdog-fbfresh` | L3-16 | `make run` | 無 |
| `l3/05-ecat-skeleton` | L3-17 | `make run`=dry-run；真跑要 SOEM+網卡 | SOEM |
| `l3/06-rt-1khz` | L3-18 | `make run`（sudo 更準） | 無 |
| `project-a/wp2-single-axis` | 專案A M1 | fake_slave + `make run` | vcan |
| `project-a/wp3-joint-space` | 專案A M2 | `make run` | 無 |
| `project-a/wp4-task-space` | 專案A M3 | `make run` | 無 |
| `project-a/wp5-dual-bus` | 專案A M4 | `./run.sh`（14 假從站） | vcan×2 |
| `project-a/wp6-safety` | 專案A M5 | `./run.sh`（自動拔線） | vcan |
| `project-a/wp7-host-if` | 專案A M6 | `make run`（stdin 命令） | 無 |
| `project-b/m1-master-setup` | 專案B M1 | `./check_rt.sh` | rt-tests |
| `project-b/m2-slave-scan` | 專案B M2 | `sudo ./scan.py <iface>` | pysoem+真從站 |
| `project-b/m3-preop-sdo` | 專案B M3 | `sudo ./config_sdo.py <iface>` | pysoem+真從站 |
| `project-b/m4-safeop-op-dc` | 專案B M4 | `sudo ./to_op.py <iface>` | pysoem+真從站 |
| `project-b/m5-csp-1khz` | 專案B M5 | `make run`=dry-run；真跑要 SOEM | SOEM+真從站 |
| `project-b/m6-jitter-report` | 專案B M6 | `make run` → jitter.csv | 無 |

## vcan 快速上手

```bash
sudo common/vcan_setup.sh                      # 一次性
python3 common/fake_slave.py --node 1 &        # 假關節
candump vcan0                                  # 另開終端看流量
```
