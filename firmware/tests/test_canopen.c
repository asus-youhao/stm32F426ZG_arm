/** @file test_canopen.c — SDO/PDO/CiA402 端到端（主站 ↔ 模擬 PHU 從站） */
#include "test_framework.h"
#include "canopen.h"
#include "co_bxcan.h"
#include "co_sdo.h"
#include "co_pdo.h"
#include "cia402.h"

/* 把 sim 注入的 TPDO 從 rx 佇列取出並更新 co_pdo 快取 */
static void pump(co_bus_t bus)
{
    co_frame_t f;
    while (co_bxcan_recv(bus, &f)) co_pdo_process_frame(bus, &f);
}

void test_canopen(void)
{
    const co_bus_t BUS = CO_BUS_LEFT;
    const uint8_t NODE = 1;
    CHECK(co_bxcan_init(BUS) == CO_OK);

    /* --- SDO 讀：device type / baudrate / node-id --- */
    uint32_t v = 0;
    CHECK(co_sdo_read(BUS, NODE, 0x1000, 0, &v, 100) == CO_OK);
    CHECK(v == 0x00020192u);                        /* CiA402 servo */
    CHECK(co_sdo_read(BUS, NODE, 0x26A1, 0, &v, 100) == CO_OK);
    CHECK(v == 1000000u);                           /* 1 Mbps */
    CHECK(co_sdo_read(BUS, NODE, 0x26A0, 0, &v, 100) == CO_OK);
    CHECK(v == NODE);

    /* --- SDO 寫模式 CSP=8,讀回 0x6061 確認 --- */
    CHECK(co_sdo_write(BUS, NODE, 0x6060, 0, 8, 1, 100) == CO_OK);
    CHECK(co_sdo_read(BUS, NODE, 0x6061, 0, &v, 100) == CO_OK);
    CHECK(v == 8);

    /* --- PDO + CiA402 使能序列 --- */
    uint16_t sw; int32_t pa;
    co_pdo_send_csp(BUS, NODE, 0x06, 0); pump(BUS);
    CHECK(co_pdo_get_feedback(BUS, NODE, &sw, &pa));
    CHECK(cia402_decode(sw) == DS_READY_TO_SWITCH_ON);

    co_pdo_send_csp(BUS, NODE, 0x07, 0); pump(BUS);
    co_pdo_get_feedback(BUS, NODE, &sw, &pa);
    CHECK(cia402_decode(sw) == DS_SWITCHED_ON);

    co_pdo_send_csp(BUS, NODE, 0x0F, 0); pump(BUS);
    co_pdo_get_feedback(BUS, NODE, &sw, &pa);
    CHECK(cia402_decode(sw) == DS_OPERATION_ENABLED);

    /* --- 下目標位置,actual 應朝目標移動 --- */
    int32_t target = 200000;   /* counts */
    int32_t last = 0;
    for (int i = 0; i < 200; i++) {
        co_pdo_send_csp(BUS, NODE, 0x0F, target);
        pump(BUS);
    }
    co_pdo_get_feedback(BUS, NODE, &sw, &pa);
    CHECK(pa > last);                       /* 有移動 */
    CHECK(pa > target / 2);                 /* 已明顯朝目標靠近 */

    /* --- 回授序號會遞增（看門狗用） --- */
    uint32_t s0 = co_pdo_feedback_seq(BUS, NODE);
    co_pdo_send_csp(BUS, NODE, 0x0F, target); pump(BUS);
    CHECK(co_pdo_feedback_seq(BUS, NODE) == s0 + 1);

    /* --- SDO 讀回扭矩/電流物件存在（模擬從站回應） --- */
    CHECK(co_sdo_read(BUS, NODE, 0x6077, 0, &v, 100) == CO_OK);  /* 實際扭矩 */
    CHECK(co_sdo_read(BUS, NODE, 0x6078, 0, &v, 100) == CO_OK);  /* 實際電流 */
}
