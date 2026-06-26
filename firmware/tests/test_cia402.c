/** @file test_cia402.c — CiA402 狀態字解析 + 使能序列 */
#include "test_framework.h"
#include "cia402.h"

void test_cia402(void)
{
    /* 狀態字解析 */
    CHECK(cia402_decode(0x0040) == DS_SWITCH_ON_DISABLED);
    CHECK(cia402_decode(0x0021) == DS_READY_TO_SWITCH_ON);
    CHECK(cia402_decode(0x0023) == DS_SWITCHED_ON);
    CHECK(cia402_decode(0x0027) == DS_OPERATION_ENABLED);
    CHECK(cia402_decode(0x0007) == DS_QUICK_STOP_ACTIVE);
    CHECK(cia402_decode(0x0008) == DS_FAULT);

    /* 使能序列：依目前狀態給下一步控制字 */
    CHECK(cia402_enable_step(0x0008) == CW_FAULT_RESET);    /* fault → reset */
    CHECK(cia402_enable_step(0x0040) == CW_SHUTDOWN);       /* SOD → 0x06 */
    CHECK(cia402_enable_step(0x0021) == CW_SWITCH_ON);      /* ready → 0x07 */
    CHECK(cia402_enable_step(0x0023) == CW_ENABLE_OP);      /* switched on → 0x0F */
    CHECK(cia402_enable_step(0x0027) == CW_ENABLE_OP);      /* op enabled → 維持 */
}
