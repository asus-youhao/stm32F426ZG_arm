/**
 * @file    bringup.c
 * @brief   WP2 單軸 bring-up 實作
 *
 * 流程（皆用 SDO,阻塞式,僅供測試）：
 *   1. co_bxcan_init
 *   2. NMT reset comm → pre-op → start
 *   3. SDO 讀：0x1000 設備型別、0x6041 狀態字、0x26A1 波特率、0x26A0 節點ID、0x6064 實際位置
 *   4. 設 Profile Velocity 模式(3)、設加減速與目標速度
 *   5. CiA402 使能：fault reset →0x06 →0x07 →0x0F（每步讀狀態字確認）
 *   6. 轉動 spin_ms 後停止（目標速度=0、disable voltage）
 *   7. 再讀位置,比較是否位移
 *
 * 依賴：RX 由中斷餵入佇列（見 co_bxcan.c / 整合範例），SDO 才能收到回應。
 */
#include "bringup.h"
#include "co_bxcan.h"
#include "co_nmt.h"
#include "co_sdo.h"
#include "cia402.h"
#include "stm32f7xx_hal.h"
#include <stdarg.h>

#define T  100u   /* SDO timeout ms */

/* 預設弱實作；專案可覆寫成 printf 導向 UART/SWO。 */
__attribute__((weak)) void bringup_log(const char *fmt, ...) { (void)fmt; }

/* 等待驅動到達目標 CiA402 狀態（透過 SDO 輪詢狀態字）。 */
static co_status_t enable_drive(co_bus_t bus, uint8_t node)
{
    for (int i = 0; i < 20; i++) {
        uint16_t sw = 0;
        co_status_t st = cia402_read_statusword_sdo(bus, node, &sw);
        if (st != CO_OK) return st;
        if (cia402_decode(sw) == DS_OPERATION_ENABLED) return CO_OK;
        uint16_t cw = cia402_enable_step(sw);
        st = cia402_set_controlword_sdo(bus, node, cw);
        if (st != CO_OK) return st;
        HAL_Delay(10);
    }
    return CO_ERR_TIMEOUT;
}

co_status_t bringup_single_axis(co_bus_t bus, uint8_t node,
                                int32_t spin_velocity, uint32_t spin_ms,
                                bringup_report_t *rep)
{
    if (!rep) return CO_ERR_PARAM;
    bringup_report_t r = {0};

    /* 1) bxCAN */
    r.can_init = co_bxcan_init(bus);
    bringup_log("[bringup] can_init=%d\r\n", r.can_init);
    if (r.can_init != CO_OK) { *rep = r; return r.can_init; }

    /* 2) NMT reset comm → pre-op */
    co_nmt_send(bus, CO_NMT_RESET_COMM, node);
    HAL_Delay(100);
    co_nmt_send(bus, CO_NMT_PRE_OP, node);
    HAL_Delay(20);

    /* 3) SDO 讀取驗證 */
    r.read_device_type = co_sdo_read(bus, node, 0x1000, 0x00, &r.device_type, T);
    bringup_log("[bringup] 0x1000 deviceType=0x%08lX st=%d\r\n",
                (unsigned long)r.device_type, r.read_device_type);

    { uint32_t v=0; r.read_statusword = co_sdo_read(bus, node, 0x6041, 0x00, &v, T);
      r.statusword = (uint16_t)v; }
    bringup_log("[bringup] 0x6041 statusword=0x%04X st=%d\r\n",
                r.statusword, r.read_statusword);

    r.read_baudrate = co_sdo_read(bus, node, 0x26A1, 0x00, &r.baudrate_bps, T);
    bringup_log("[bringup] 0x26A1 baud=%lu st=%d\r\n",
                (unsigned long)r.baudrate_bps, r.read_baudrate);

    r.read_node_id = co_sdo_read(bus, node, 0x26A0, 0x00, &r.node_id_read, T);
    bringup_log("[bringup] 0x26A0 nodeId=%lu st=%d\r\n",
                (unsigned long)r.node_id_read, r.read_node_id);

    { uint32_t v=0; r.read_pos_before = co_sdo_read(bus, node, 0x6064, 0x00, &v, T);
      r.pos_before = (int32_t)v; }
    bringup_log("[bringup] 0x6064 pos_before=%ld st=%d\r\n",
                (long)r.pos_before, r.read_pos_before);

    if (r.read_device_type != CO_OK && r.read_statusword != CO_OK) {
        bringup_log("[bringup] SDO 全失敗：檢查接線/終端電阻/波特率/節點ID\r\n");
        *rep = r; return CO_ERR_TIMEOUT;
    }

    /* 4) Profile Velocity 模式設定 */
    co_nmt_send(bus, CO_NMT_START, node);
    HAL_Delay(10);
    co_status_t st;
    st = cia402_set_mode(bus, node, MODE_PV);                 /* 0x6060=3 */
    if (st) { r.enable_result = st; *rep=r; return st; }
    co_sdo_write(bus, node, 0x6083, 0x00, 100000, 4, T);     /* profile accel */
    co_sdo_write(bus, node, 0x6084, 0x00, 100000, 4, T);     /* profile decel */
    co_sdo_write(bus, node, 0x60FF, 0x00, (uint32_t)spin_velocity, 4, T); /* target velocity */

    /* 5) 使能 */
    r.enable_result = enable_drive(bus, node);
    bringup_log("[bringup] enable=%d\r\n", r.enable_result);
    if (r.enable_result != CO_OK) { *rep=r; return r.enable_result; }

    /* 6) 轉動 → 停止 */
    bringup_log("[bringup] spinning %lu ms ...\r\n", (unsigned long)spin_ms);
    HAL_Delay(spin_ms);
    co_sdo_write(bus, node, 0x60FF, 0x00, 0, 4, T);          /* 停 */
    HAL_Delay(50);
    cia402_set_controlword_sdo(bus, node, CW_DISABLE_VOLTAGE);
    r.spin_result = CO_OK;

    /* 7) 再讀位置,比較 */
    { uint32_t v=0; co_sdo_read(bus, node, 0x6064, 0x00, &v, T);
      r.pos_after = (int32_t)v; }
    r.moved = (r.pos_after != r.pos_before);
    bringup_log("[bringup] pos_after=%ld moved=%d\r\n", (long)r.pos_after, r.moved);

    *rep = r;
    return (r.moved) ? CO_OK : CO_ERR_STATE;
}
