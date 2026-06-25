/**
 * @file    cia402.h
 * @brief   CiA 402 驅動狀態機 + 運行模式
 *
 * 物件：Controlword 0x6040、Statusword 0x6041、Modes of operation 0x6060。
 * 運行模式（0x6060）：PP=1, PV=3, PT=4, HM=6, CSP=8, CSV=9, CST=10, CSF=13。
 */
#ifndef CIA402_H
#define CIA402_H

#include "canopen.h"

/* 運行模式（對應 EYOU PHU 手冊 §4.3 表 4-1） */
typedef enum {
    MODE_PP  = 1,    /* 輪廓位置 */
    MODE_PV  = 3,    /* 輪廓速度 */
    MODE_PT  = 4,    /* 輪廓轉矩 */
    MODE_HM  = 6,    /* 回零 */
    MODE_CSP = 8,    /* 循環同步位置 */
    MODE_CSV = 9,    /* 循環同步速度 */
    MODE_CST = 10,   /* 循環同步轉矩 */
    MODE_CSF = 13    /* 循環同步力控（力控關節專屬） */
} cia402_mode_t;

/* Controlword 常用命令 */
#define CW_SHUTDOWN        0x0006u
#define CW_SWITCH_ON       0x0007u
#define CW_ENABLE_OP       0x000Fu
#define CW_DISABLE_VOLTAGE 0x0000u
#define CW_QUICK_STOP      0x0002u
#define CW_FAULT_RESET     0x0080u

/* 由 Statusword 解析的驅動狀態（CiA 402） */
typedef enum {
    DS_NOT_READY = 0,
    DS_SWITCH_ON_DISABLED,
    DS_READY_TO_SWITCH_ON,
    DS_SWITCHED_ON,
    DS_OPERATION_ENABLED,
    DS_QUICK_STOP_ACTIVE,
    DS_FAULT_REACTION,
    DS_FAULT,
    DS_UNKNOWN
} cia402_state_t;

/** @brief 由 statusword 解析驅動狀態。 */
cia402_state_t cia402_decode(uint16_t statusword);

/**
 * @brief 依目前 statusword 推算「使能」過程下一步應送的 controlword。
 * @return 下一步 controlword;若已 OPERATION_ENABLED 回 CW_ENABLE_OP。
 *         若處於 FAULT 會回 CW_FAULT_RESET。
 */
uint16_t cia402_enable_step(uint16_t statusword);

/* ---- 以 SDO 設定（初始化階段使用）---- */
co_status_t cia402_set_mode(co_bus_t bus, uint8_t node, cia402_mode_t mode);
co_status_t cia402_set_controlword_sdo(co_bus_t bus, uint8_t node, uint16_t cw);
co_status_t cia402_read_statusword_sdo(co_bus_t bus, uint8_t node, uint16_t *sw);

#endif /* CIA402_H */
