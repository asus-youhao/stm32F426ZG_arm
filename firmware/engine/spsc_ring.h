/**
 * @file    spsc_ring.h
 * @brief   單生產者/單消費者 lock-free ring（WP-H0）——RT ↔ 非 RT 域交界
 *
 * 用途（設計文件 §2/§5.3）：cmd ring（harness→RT）、telemetry ring、
 * log ring（RT→harness）。固定元素大小、容量 2 的冪、零 malloc、
 * C11 atomics（F746 單核下亦正確；PC 上跨執行緒正確）。
 * 僅允許「一個」執行緒 push、「一個」執行緒 pop。
 */
#ifndef SPSC_RING_H
#define SPSC_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t         *buf;   /**< 呼叫端提供：elem_size × capacity bytes */
    uint32_t         elem;  /**< 元素大小（bytes） */
    uint32_t         cap;   /**< 容量（元素數，必須為 2 的冪） */
    uint32_t         mask;  /**< cap-1 */
    _Atomic uint32_t head;  /**< 生產者累計寫入數（free-running） */
    _Atomic uint32_t tail;  /**< 消費者累計讀出數（free-running） */
} spsc_t;

/** @brief 初始化；capacity 非 2 的冪或為 0 回 -1。mem 由呼叫端靜態配置。 */
int spsc_init(spsc_t *q, void *mem, uint32_t elem_size, uint32_t capacity);

/** @brief 生產者 push 一個元素（複製進 ring）；滿了回 false（丟新留舊由呼叫端計 drop）。 */
bool spsc_push(spsc_t *q, const void *elem);

/** @brief 消費者 pop 一個元素（複製出來）；空回 false。 */
bool spsc_pop(spsc_t *q, void *elem);

/** @brief 目前元素數（估計值；單邊呼叫時精確）。 */
uint32_t spsc_count(const spsc_t *q);

#endif /* SPSC_RING_H */
