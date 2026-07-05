/**
 * @file    sdo_bg.h
 * @brief   SDO/CoE 背景通道（WP-H4 遞延項；設計文件 §5.3）
 *
 * RUN 中安全讀寫物件字典：非 RT 端佇列請求 → RT 域 HOUSEKEEP 相位
 * 每步只推進一小步（送一幀 / 檢查回應 / 計逾時），結果回填回應佇列。
 * 取代「RUN 中阻塞 SDO」——阻塞版 co_sdo 僅限 bring-up/BUS_UP 使用。
 *
 * 併發規則：同一時間最多一筆 in-flight（CANopen SDO 協定本身也是）；
 * 兩端各為 SPSC（非 RT 單執行緒 push/poll、RT 單執行緒 step）。
 */
#ifndef SDO_BG_H
#define SDO_BG_H

#include "canopen.h"
#include <stdbool.h>

typedef struct {
    uint32_t tag;       /* 呼叫端自訂,回應原樣帶回（配對用） */
    uint8_t  bus;       /* co_bus_t（EtherCAT 後端忽略,node=axis） */
    uint8_t  node;      /* CANopen node-id / EtherCAT axis(0-based) */
    uint8_t  sub;
    uint8_t  is_write;  /* 0=讀 1=寫 */
    uint8_t  size;      /* 寫入位元組數 1..4（讀忽略） */
    uint16_t index;
    uint32_t value;     /* 寫入值 */
} sdo_bg_req_t;

enum { SDO_BG_OK = 0, SDO_BG_TIMEOUT = -1, SDO_BG_ABORT = -2 };

typedef struct {
    uint32_t tag;
    int8_t   status;    /* SDO_BG_* */
    uint8_t  size;      /* 讀回大小（1/2/4;未知=4） */
    uint32_t value;     /* 讀回值;abort 時 = abort code */
} sdo_bg_rsp_t;

/** @brief 初始化/重置。timeout_steps=等待回應的 step 數上限（0→50）。 */
void sdo_bg_init(uint32_t timeout_steps);

/* ---- 非 RT 端 ---- */
bool sdo_bg_request(const sdo_bg_req_t *r);   /* 佇列滿回 false */
bool sdo_bg_poll(sdo_bg_rsp_t *out);          /* 無回應回 false */

/* ---- RT 端（由 bus 後端的 sdo_bg_step 呼叫）---- */
void sdo_bg_step_canopen(void);

/** @brief pump 分派：是「目前 in-flight 請求」的 SDO 回應則消化並回 true。 */
bool sdo_bg_on_frame(co_bus_t bus, const co_frame_t *f);

/* ---- 其他後端自組 step 用（bus_ecat.c 的 CoE 版在該檔內實作,
 *      避免本模組帶入 ec_master 連結依賴）---- */
bool sdo_bg_take_req(sdo_bg_req_t *out);                 /* RT：取一筆請求 */
void sdo_bg_respond_ext(uint32_t tag, int8_t status,
                        uint8_t size, uint32_t value);   /* RT：回填回應 */

#endif /* SDO_BG_H */
