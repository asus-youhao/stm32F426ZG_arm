/**
 * @file    ec_master.h
 * @brief   EtherCAT 主站門面（WP-L3.1 / WP-I3.1 / WP-H5）
 *
 * 設計出處：ethercat-coe-master-plan.md §6.2（抽象層）、
 * linux-rt-ethercat-master-plan.md §5.2（一拍延遲模型）。
 * 同一門面三種後端,只換 .c 不動呼叫端：
 *   ec_master_sim.c  — fake 後端接 phu_sim CiA402 模型（免硬體 CI,本檔案組）
 *   ec_master_soem.c — 方案 A（SOEM user-space,WP-L）
 *   ec_master_igh.c  — 方案 B（IgH ecrt,WP-I）
 *
 * 使用序列（對映 ESM）：
 *   ec_master_init(n)             掃鏈 → PREOP;回實際軸數
 *   ec_coe_write/read(...)        PREOP 組態（0x6060=8、PDO 映射、0x60C2）
 *   ec_master_op()                SAFEOP→OP + DC 啟用（SYNC0）
 *   每週期: ec_axis_set_output×N → ec_master_exchange() → ec_axis_get_input×N
 *   （一拍延遲：本週期 set 的輸出於 exchange 送出,讀到的輸入是
 *     從站上次 SYNC/DC 鎖存的回授）
 */
#ifndef EC_MASTER_H
#define EC_MASTER_H

#include "bus_if.h"
#include <stdint.h>

#define EC_AXES_MAX 14

/** @brief RxPDO（主站→從站）：精簡映射（CSP 起步）。 */
typedef struct {
    uint16_t controlword;   /* 0x6040 */
    int32_t  target_pos;    /* 0x607A */
} ec_out_t;

/** @brief TxPDO（從站→主站）。 */
typedef struct {
    uint16_t statusword;    /* 0x6041 */
    int32_t  pos_actual;    /* 0x6064 */
} ec_in_t;

/** @brief 掃鏈 + 全部從站帶到 PREOP。回實際找到的軸數（<0 = 失敗）。 */
int  ec_master_init(int expected_axes);

/** @brief PREOP 組態完成後：SAFEOP→OP + DC（SYNC0）啟用。回 0 成功。 */
int  ec_master_op(void);

/**
 * @brief 週期資料交換（RT 相位 BUS_RX/BUS_TX 的合體,依一拍延遲模型）。
 * @return 本週期 working counter（與 ec_master_expected_wkc() 比對判失軸）。
 */
int  ec_master_exchange(void);

/** @brief 期望 WKC（全部軸健在時 exchange 應回傳的值）。 */
int  ec_master_expected_wkc(void);

void ec_axis_set_output(int axis, const ec_out_t *o);
void ec_axis_get_input(int axis, ec_in_t *i);

/** @brief 週期外 CoE SDO（PREOP 組態/診斷用;RUN 中請走背景通道）。回 0 成功。 */
int  ec_coe_read(int axis, uint16_t idx, uint8_t sub, uint32_t *val);
int  ec_coe_write(int axis, uint16_t idx, uint8_t sub, uint32_t val);

/** @brief 健康快照（bus-agnostic;proto[0]=最近 WKC、[1]=期望 WKC、[2]=AL 概況）。 */
void ec_master_health(bus_health_t *h);

void ec_master_close(void);

#endif /* EC_MASTER_H */
