/**
 * @file    canopen.h
 * @brief   輕量 CANopen 主站 — 共用型別、COB-ID、回傳碼
 *
 * 目標平台：STM32F746ZG（雙路 bxCAN）
 * 控制對象：EYOU PHU 關節（CiA 301 / CiA 402）
 */
#ifndef CANOPEN_H
#define CANOPEN_H

#include <stdint.h>
#include <stdbool.h>

/* ---- CAN channel（雙手不同 channel）---- */
typedef enum {
    CO_BUS_LEFT  = 0,   /* bxCAN1 → 左臂 */
    CO_BUS_RIGHT = 1,   /* bxCAN2 → 右臂 */
    CO_BUS_COUNT = 2
} co_bus_t;

/* ---- 回傳碼 ---- */
typedef enum {
    CO_OK = 0,
    CO_ERR_PARAM,
    CO_ERR_TX,          /* 送出失敗（mailbox 滿） */
    CO_ERR_TIMEOUT,     /* 等待回應逾時 */
    CO_ERR_ABORT,       /* SDO abort */
    CO_ERR_STATE
} co_status_t;

/* ---- 標準 CANopen COB-ID 基底（function code + node-id）---- */
#define CO_COBID_NMT        0x000u            /* NMT 控制 */
#define CO_COBID_SYNC       0x080u            /* SYNC */
#define CO_COBID_EMCY_BASE  0x080u            /* 0x080 + nodeId */
#define CO_COBID_TPDO1_BASE 0x180u            /* slave→master */
#define CO_COBID_RPDO1_BASE 0x200u            /* master→slave */
#define CO_COBID_TPDO2_BASE 0x280u
#define CO_COBID_RPDO2_BASE 0x300u
#define CO_COBID_SDO_TX_BASE 0x580u           /* slave→master（回應） */
#define CO_COBID_SDO_RX_BASE 0x600u           /* master→slave（請求） */
#define CO_COBID_HEARTBEAT_BASE 0x700u        /* slave→master */

/* ---- NMT 命令 ---- */
typedef enum {
    CO_NMT_START      = 0x01,
    CO_NMT_STOP       = 0x02,
    CO_NMT_PRE_OP     = 0x80,
    CO_NMT_RESET_NODE = 0x81,
    CO_NMT_RESET_COMM = 0x82
} co_nmt_cmd_t;

/* ---- NMT/Heartbeat 節點狀態 ---- */
typedef enum {
    CO_NODE_BOOTUP    = 0x00,
    CO_NODE_STOPPED   = 0x04,
    CO_NODE_OPERATIONAL = 0x05,
    CO_NODE_PRE_OP    = 0x7F,
    CO_NODE_UNKNOWN   = 0xFF
} co_node_state_t;

/* ---- 通用 CAN frame ---- */
typedef struct {
    uint16_t id;        /* 11-bit standard id */
    uint8_t  dlc;       /* 0..8 */
    uint8_t  data[8];
} co_frame_t;

#endif /* CANOPEN_H */
