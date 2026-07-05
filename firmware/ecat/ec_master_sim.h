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

#endif /* EC_MASTER_SIM_H */
