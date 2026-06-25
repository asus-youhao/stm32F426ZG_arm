/**
 * @file    host_if.c
 * @brief   WP7 上位機介面實作
 */
#include "host_if.h"
#include <string.h>

float host_rd_f32(const uint8_t *p)
{ float v; memcpy(&v, p, 4); return v; }
void host_wr_f32(uint8_t *p, float v)
{ memcpy(p, &v, 4); }

size_t host_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                   uint8_t *out, size_t cap)
{
    if (cap < (size_t)(len + 4)) return 0;
    out[0] = HOST_SYNC;
    out[1] = type;
    out[2] = len;
    uint8_t ck = type ^ len;
    for (uint8_t i = 0; i < len; i++) { out[3+i] = payload[i]; ck ^= payload[i]; }
    out[3+len] = ck;
    return (size_t)(len + 4);
}

/* ---- 接收狀態機 ---- */
static uint8_t s_buf[260];
static int     s_state = 0;   /* 0=sync,1=type,2=len,3=payload,4=cksum */
static uint8_t s_type, s_len, s_idx, s_ck;

void host_feed_byte(uint8_t b)
{
    switch (s_state) {
        case 0: if (b == HOST_SYNC) s_state = 1; break;
        case 1: s_type = b; s_ck = b; s_state = 2; break;
        case 2: s_len = b; s_ck ^= b; s_idx = 0;
                s_state = (s_len == 0) ? 4 : 3; break;
        case 3: s_buf[s_idx++] = b; s_ck ^= b;
                if (s_idx >= s_len) s_state = 4; break;
        case 4:
            if (b == s_ck) host_on_command(s_type, s_buf, s_len);
            s_state = 0; break;
    }
}

/* 預設弱實作 */
__attribute__((weak)) void host_send_bytes(const uint8_t *buf, size_t len)
{ (void)buf; (void)len; }
__attribute__((weak)) void host_on_command(uint8_t type, const uint8_t *payload, uint8_t len)
{ (void)type; (void)payload; (void)len; }
