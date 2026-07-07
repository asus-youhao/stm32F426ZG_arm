/**
 * @file    ec_master_sim.h
 * @brief   fake EtherCAT 後端的測試鉤子（僅測試/故障注入用,真後端無此檔）
 */
#ifndef EC_MASTER_SIM_H
#define EC_MASTER_SIM_H

#include "phu_sim.h"
#include <stdbool.h>

/** @brief 模擬掉軸（拔線/斷電）：offline 軸不參與交換,WKC 隨之下降。 */
void phu_ecat_set_offline(int axis, bool offline);

/** @brief 直接存取軸的 CiA402 模型（注入故障 statusword 等）。 */
phu_node_t *phu_ecat_node(int axis);

/**
 * @brief DC 漂移模型：從站柵格週期 = dt_us×(1+ppm/1e6)。
 *        dt_us=0 停用（ec_master_dc_error_us 恆 0）。
 */
void phu_ecat_set_dc_drift(uint32_t dt_us, int32_t drift_ppm);

#endif /* EC_MASTER_SIM_H */
