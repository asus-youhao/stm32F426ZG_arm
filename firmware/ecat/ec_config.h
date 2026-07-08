/**
 * @file    ec_config.h
 * @brief   EYOU PHU EtherCAT 單軸/多軸「可動配方」——單一真相來源
 *
 * 這些常數是 2026-07-08 在 gx701（Ubuntu 24.04 PREEMPT_RT + IgH）對真
 * PHU17 逐項排查、**實測讓馬達轉動**得到的結論。sim / SOEM / IgH 三後端
 * 與 F746 板端一律引用本檔,不得各自寫死,以免再犯同樣的坑。
 *
 * ─── 血淚重點（六輪診斷,docs/changes/2026-07-07-phu17-safeop-diagnosis.md）───
 *
 * 1. 控制權 0x2100 必須 = 1（EtherCAT）。出廠若 = 2（CANopen）,EtherCAT
 *    介面唯讀、SAFEOP 0.5ms 自貶(AL 0x0022)、寫入全拒。切法只能靠 CANopen
 *    或 UART（不能用 EtherCAT 自己切）。
 *
 * 2. **不可重映射 PDO**。此 drive 對 ecrt_slave_config_pdos / SDO 重寫
 *    0x1600/0x1A00 會導致 working counter=0、上不了 OP。**用出廠預設映射**
 *    (等同 eyou-motor-master 的 --no-remap-pdo)。預設佈局見下 EC_RX/TX_*。
 *
 * 3. **CSP（mode 8）嚴格依賴 DC SYNC0**。free-run（assign_activate=0）下
 *    drive 會進 OperationEnabled、target 會 ramp,但內部 position demand
 *    凍住、實際不動、無 fault——看起來像沒扭矩,其實是沒同步時鐘。
 *    **必須 assign_activate=0x0300、SYNC0 週期=控制週期、每 cycle 由主站
 *    sync reference/slave clocks**。開 DC 後馬達立即跟隨轉動。
 *
 * 4. mode（0x6060）在預設 PDO 內 → **只能經 process data 寫,不能 SDO 寫**
 *    (PDO-mapped 物件 SDO 寫回 0x06010000)。每 cycle 於 RxPDO 寫 mode=8。
 *
 * 5. STO：0x253B=0（調試模式）確實旁路硬體扭矩閘,不接 STO 也能出力
 *    （手冊 §5.7；正常運行禁設 0,需接 STO 24V 雙路）。
 *
 * 6. 單位：輸出一圈 = 52953088 counts = 524288(19-bit 編碼器) × 101(減速比)。
 *    位置物件(0x607A/0x6064) 以馬達側 counts 含齒輪計。
 *
 * 7. NIC：USB r8152 只能撐到 ~100 Hz（≥250 Hz datagram UNMATCHED / WKC=0）;
 *    1 kHz 生產需 Intel i210/i225 級 NIC。且 DC 需 PREEMPT_RT 內核。
 */
#ifndef EC_CONFIG_H
#define EC_CONFIG_H

#include "control_rate.h"   /* CONTROL_DT_US */
#include <stdint.h>

/* ── 控制權 ── */
#define EC_OBJ_CTRL_SOURCE   0x2100u   /* =1 EtherCAT / =2 CANopen（不可經 ECAT 改） */
#define EC_CTRL_SOURCE_ECAT  1u

/* ── PDO 策略 ── */
#define EC_REMAP_PDO         0         /* 0=用出廠預設映射（此 drive 必須） */
#define EC_RXPDO_INDEX       0x1600u
#define EC_TXPDO_INDEX       0x1A00u

/* 出廠預設 RxPDO(0x1600) 位元組偏移（slaveinfo -map 讀回,勿更動）：
 *   0:6040 CW(2) 2:6060 mode(1) 3:607A tgt(4) 7:6081(4) B:60FF(4)
 *   F:240D(4) 13:6071(2) 15:6083(4) 19:6084(4) 1D:6087(4)  共 33B */
#define EC_RX_OFF_CW         0
#define EC_RX_OFF_MODE       2
#define EC_RX_OFF_TARGET     3
#define EC_RX_SIZE           33
/* 出廠預設 TxPDO(0x1A00)：
 *   0:6041 SW(2) 2:6061 modedisp(1) 3:603F err(2) 5:6064 pos(4)
 *   9:606C(4) D:6077(2) F:6074(2) 11:60F4 foll(4) 15:6079 vbus(4) 19:60FD(4) 共 29B */
#define EC_TX_OFF_SW         0
#define EC_TX_OFF_ERR        3
#define EC_TX_OFF_POS        5
#define EC_TX_OFF_FOLL       0x11
#define EC_TX_OFF_VBUS       0x15
#define EC_TX_SIZE           29

/* ── DC（CSP 動作的必要條件）── */
#define EC_DC_ASSIGN_ACTIVATE 0x0300u             /* SYNC0 啟用 */
#define EC_DC_SYNC0_NS       (CONTROL_DT_US * 1000u)  /* SYNC0 週期 = 控制週期 */
#define EC_DC_SYNC0_SHIFT_NS (CONTROL_DT_US * 200u)   /* 資料先到再觸發鎖存(+20%) */

/* ── CiA402 ── */
#define EC_MODE_CSP          8         /* 0x6060 = 8（Cyclic Synchronous Position） */
#define EC_CW_SHUTDOWN       0x0006u
#define EC_CW_SWITCH_ON      0x0007u
#define EC_CW_ENABLE_OP      0x000Fu
#define EC_SW_MASK_STATE     0x006Fu
#define EC_SW_READY_TO_SWON  0x0021u
#define EC_SW_SWITCHED_ON    0x0023u
#define EC_SW_OP_ENABLED     0x0027u
#define EC_SW_FAULT_BIT      0x0008u

/* ── STO（調試）── */
#define EC_OBJ_STO_MODE      0x253Bu   /* =0 調試旁路 / 1,3 正常運行(需接 STO) */

/* ── 單位 ── */
#define EC_ENC_BITS_CNT      524288L   /* 19-bit 編碼器/馬達圈（0x2025） */
#define EC_GEAR_NUM          101L      /* 減速比（0x26A2/0x26A3） */
#define EC_CNT_PER_OUT_REV   (EC_ENC_BITS_CNT * EC_GEAR_NUM)  /* 52953088 counts/輸出圈 */

/* ── 身分（0x1018;掃鏈核對）── */
#define EC_VENDOR_ID         0x00001097u
#define EC_PRODUCT_CODE      0x00010002u

#endif /* EC_CONFIG_H */
