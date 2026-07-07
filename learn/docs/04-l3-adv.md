# 04 · L3 高階（l3-adv.html）

6 節：

13. CiA 402 狀態機 — 0x6040/0x6041/0x6060 三物件表、`cia402_enable_step()` 純函式設計論述（驅動器是真相來源）。
14. 運行模式與單位換算 — 8 模式表、為何多軸選 CSP、`CPR_19BIT`（輸出端 524288）vs EtherCAT 路線實測馬達端 ×101=52953088 的對照警示。
15. 運動學/IK/軌跡 — L1~L4 四層表、`app_main_tick()` 三步資料流實碼、7-DoF 冗餘與 following error。
16. 安全機制 — 威脅/對策表、`fb_fresh` 看門狗 bug 原文引用（「餵狗第一定律」）。
17. EtherCAT 入門 — ESM/CoE/DC/ESI 四概念對照 CANopen 表、SOEM 最小流程；CoE 讓 L2/L3 所學原樣沿用的論述。
18. Linux 即時主站 — PREEMPT_RT 四件套、`clock_nanosleep` 絕對時間 1kHz 骨架、cyclictest 先量再信。

## 取材

`firmware/canopen/cia402.h`、`firmware/app/robot_config.c`、`firmware/control/`、
`firmware/safety/`、`firmware/rt/rt_setup.sh`、EtherCAT 系列分支與
`docs/design/canopen-vs-ethercat.md`。
