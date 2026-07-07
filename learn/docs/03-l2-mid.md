# 03 · L2 中階（l2-mid.html）

6 節：

7. CAN 匯流排基礎 · bxCAN — 差動/仲裁/位元時序（引 `co_bxcan.c` 的 45MHz/5/6TQ/2TQ 實參）、RX 中斷 + 環形佇列分層。
8. CANopen 入門 · NMT/Heartbeat — OD/COB-ID/NMT 三大件表格、`co_nmt.h` API（`node_age_ms` 是安全的地基）。
9. SDO — 組態通道，`co_sdo.h` expedited API；插入 0x2100 廠商物件教訓（連到專案 B 實戰紀錄）。
10. PDO — RPDO1/TPDO1 映射表（0x6040+0x607A / 0x6041+0x6064）、`co_pdo.h` API、feedback_seq 判新舊。
11. 頻寬預算 — 130 bit/frame 計算、500Hz=7000 frame/s ~90% 載荷 vs 1kHz 超載表；雙 bus 架構的兩個理由；EtherCAT 入口。
12. 免硬體驗證 — tests/ 單元測試、sim/ HOST 模擬、pc/+sim_py SocketCAN 對打；vcan 指令實作。

## 取材

全部來自 `firmware/canopen/`、`firmware/app/control_rate.h`、`firmware/sim|tests|pc/`、
`docs/design/can-bus-architecture.md`、`sim-fake-hardware.md`、`unit-tests.md`。
