/**
 * @file    spsc_ring.c
 * @brief   SPSC lock-free ring 實作（WP-H0）
 *
 * head/tail 為 free-running 計數（不取模存放），滿/空判斷用差值，
 * 天然處理 uint32 回繞；索引才取 mask。記憶序：push 先寫資料再
 * release head；pop 先 acquire head 再讀資料、release tail。
 */
#include "spsc_ring.h"
#include <string.h>

int spsc_init(spsc_t *q, void *mem, uint32_t elem_size, uint32_t capacity)
{
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) return -1;
    q->buf  = (uint8_t *)mem;
    q->elem = elem_size;
    q->cap  = capacity;
    q->mask = capacity - 1;
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
    return 0;
}

bool spsc_push(spsc_t *q, const void *elem)
{
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h - t == q->cap) return false;                    /* 滿 */
    memcpy(q->buf + (uint64_t)(h & q->mask) * q->elem, elem, q->elem);
    atomic_store_explicit(&q->head, h + 1, memory_order_release);
    return true;
}

bool spsc_pop(spsc_t *q, void *elem)
{
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (h == t) return false;                             /* 空 */
    memcpy(elem, q->buf + (uint64_t)(t & q->mask) * q->elem, q->elem);
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);
    return true;
}

uint32_t spsc_count(const spsc_t *q)
{
    uint32_t h = atomic_load_explicit((_Atomic uint32_t *)&q->head,
                                      memory_order_acquire);
    uint32_t t = atomic_load_explicit((_Atomic uint32_t *)&q->tail,
                                      memory_order_acquire);
    return h - t;
}
