/**
 * @file    co_sdo.c
 * @brief   SDO client 實作（expedited）
 *
 * 寫請求 (master→slave, 0x600+node)：
 *   ccs/size byte: 0x2F(1B) 0x2B(2B) 0x27(3B) 0x23(4B)
 *   回應 (0x580+node)：0x60 = 成功；0x80 = abort（後 4 bytes 為 abort code）
 * 讀請求：0x40,回應 0x4F/0x4B/0x47/0x43（含資料）或 0x80 abort。
 */
#include "co_sdo.h"
#include "co_bxcan.h"
#include "stm32f7xx_hal.h"   /* HAL_GetTick */

static uint8_t write_cs_for_size(uint8_t size)
{
    switch (size) {
        case 1: return 0x2F;
        case 2: return 0x2B;
        case 3: return 0x27;
        case 4: return 0x23;
        default: return 0x00;
    }
}

/* 等待對應 node 的 SDO 回應（0x580+node），其餘 frame 暫時丟回佇列外略過。 */
static co_status_t wait_sdo_resp(co_bus_t bus, uint8_t node,
                                 co_frame_t *resp, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    const uint16_t want = (uint16_t)(CO_COBID_SDO_TX_BASE + node);
    while ((HAL_GetTick() - t0) < timeout_ms) {
        co_frame_t f;
        if (co_bxcan_recv(bus, &f)) {
            if (f.id == want) { *resp = f; return CO_OK; }
            /* 非目標回應（PDO/HB 等）於初始化階段忽略 */
        }
    }
    return CO_ERR_TIMEOUT;
}

co_status_t co_sdo_write(co_bus_t bus, uint8_t node,
                         uint16_t index, uint8_t sub,
                         uint32_t value, uint8_t size,
                         uint32_t timeout_ms)
{
    if (node == 0 || node > 127) return CO_ERR_PARAM;
    uint8_t cs = write_cs_for_size(size);
    if (cs == 0) return CO_ERR_PARAM;

    co_frame_t req = {0};
    req.id  = (uint16_t)(CO_COBID_SDO_RX_BASE + node);
    req.dlc = 8;
    req.data[0] = cs;
    req.data[1] = (uint8_t)(index & 0xFF);
    req.data[2] = (uint8_t)(index >> 8);
    req.data[3] = sub;
    req.data[4] = (uint8_t)(value & 0xFF);
    req.data[5] = (uint8_t)((value >> 8) & 0xFF);
    req.data[6] = (uint8_t)((value >> 16) & 0xFF);
    req.data[7] = (uint8_t)((value >> 24) & 0xFF);

    co_status_t st = co_bxcan_send(bus, &req);
    if (st != CO_OK) return st;

    co_frame_t resp;
    st = wait_sdo_resp(bus, node, &resp, timeout_ms);
    if (st != CO_OK) return st;

    if (resp.data[0] == 0x60) return CO_OK;        /* download OK */
    if (resp.data[0] == 0x80) return CO_ERR_ABORT; /* abort */
    return CO_ERR_STATE;
}

co_status_t co_sdo_read(co_bus_t bus, uint8_t node,
                        uint16_t index, uint8_t sub,
                        uint32_t *out_value,
                        uint32_t timeout_ms)
{
    if (node == 0 || node > 127 || !out_value) return CO_ERR_PARAM;

    co_frame_t req = {0};
    req.id  = (uint16_t)(CO_COBID_SDO_RX_BASE + node);
    req.dlc = 8;
    req.data[0] = 0x40;                              /* upload request */
    req.data[1] = (uint8_t)(index & 0xFF);
    req.data[2] = (uint8_t)(index >> 8);
    req.data[3] = sub;

    co_status_t st = co_bxcan_send(bus, &req);
    if (st != CO_OK) return st;

    co_frame_t resp;
    st = wait_sdo_resp(bus, node, &resp, timeout_ms);
    if (st != CO_OK) return st;

    if (resp.data[0] == 0x80) return CO_ERR_ABORT;
    /* expedited upload 回應 0x4F/0x4B/0x47/0x43 */
    if ((resp.data[0] & 0xE0) != 0x40) return CO_ERR_STATE;

    *out_value = (uint32_t)resp.data[4]
               | ((uint32_t)resp.data[5] << 8)
               | ((uint32_t)resp.data[6] << 16)
               | ((uint32_t)resp.data[7] << 24);
    return CO_OK;
}
