/**
 * @file    cia402.c
 * @brief   CiA 402 狀態機實作
 */
#include "cia402.h"
#include "co_sdo.h"

#define SDO_TIMEOUT_MS 50

cia402_state_t cia402_decode(uint16_t sw)
{
    /* 依 CiA 402 statusword 低位元遮罩判斷狀態 */
    if ((sw & 0x004F) == 0x0000) return DS_NOT_READY;
    if ((sw & 0x004F) == 0x0040) return DS_SWITCH_ON_DISABLED;
    if ((sw & 0x006F) == 0x0021) return DS_READY_TO_SWITCH_ON;
    if ((sw & 0x006F) == 0x0023) return DS_SWITCHED_ON;
    if ((sw & 0x006F) == 0x0027) return DS_OPERATION_ENABLED;
    if ((sw & 0x006F) == 0x0007) return DS_QUICK_STOP_ACTIVE;
    if ((sw & 0x004F) == 0x000F) return DS_FAULT_REACTION;
    if ((sw & 0x004F) == 0x0008) return DS_FAULT;
    return DS_UNKNOWN;
}

uint16_t cia402_enable_step(uint16_t sw)
{
    switch (cia402_decode(sw)) {
        case DS_FAULT:
        case DS_FAULT_REACTION:
            return CW_FAULT_RESET;             /* 先清故障 */
        case DS_SWITCH_ON_DISABLED:
            return CW_SHUTDOWN;                /* → Ready to switch on */
        case DS_READY_TO_SWITCH_ON:
            return CW_SWITCH_ON;               /* → Switched on */
        case DS_SWITCHED_ON:
            return CW_ENABLE_OP;               /* → Operation enabled */
        case DS_OPERATION_ENABLED:
            return CW_ENABLE_OP;               /* 維持 */
        default:
            return CW_SHUTDOWN;
    }
}

co_status_t cia402_set_mode(co_bus_t bus, uint8_t node, cia402_mode_t mode)
{
    return co_sdo_write(bus, node, 0x6060, 0x00, (uint32_t)mode, 1, SDO_TIMEOUT_MS);
}

co_status_t cia402_set_controlword_sdo(co_bus_t bus, uint8_t node, uint16_t cw)
{
    return co_sdo_write(bus, node, 0x6040, 0x00, (uint32_t)cw, 2, SDO_TIMEOUT_MS);
}

co_status_t cia402_read_statusword_sdo(co_bus_t bus, uint8_t node, uint16_t *sw)
{
    uint32_t v = 0;
    co_status_t st = co_sdo_read(bus, node, 0x6041, 0x00, &v, SDO_TIMEOUT_MS);
    if (st == CO_OK && sw) *sw = (uint16_t)v;
    return st;
}
