/**
 * @file    co_pdo.c
 * @brief   PDO 收發實作
 *
 * 注意：使用前需透過 SDO 設定關節的 PDO 映射與傳輸型別,使其與下列假設一致：
 *   RPDO1 內容 = [Controlword u16][Target Position i32]  (6 bytes)
 *   TPDO1 內容 = [Statusword u16][Position Actual i32]    (6 bytes)
 *   傳輸型別建議：RPDO=255(事件/非同步) 或同步;TPDO 可設為每 n 個 SYNC。
 * 詳細 PDO 映射物件（0x1600/0x1A00 等）請見通訊手冊 §3.4 / §6.4。
 */
#include "co_pdo.h"
#include "co_bxcan.h"

#define MAX_NODE 128
typedef struct {
    uint16_t statusword;
    int32_t  pos_actual;
    bool     valid;
    uint32_t seq;        /* 每收到一個 TPDO 遞增,供新鮮度/看門狗判斷 */
} fb_t;
static fb_t s_fb[CO_BUS_COUNT][MAX_NODE];

co_status_t co_pdo_send_csp(co_bus_t bus, uint8_t node,
                            uint16_t controlword, int32_t target_pos)
{
    if (node == 0 || node > 127) return CO_ERR_PARAM;
    co_frame_t f = {0};
    f.id = (uint16_t)(CO_COBID_RPDO1_BASE + node);
    f.dlc = 6;
    f.data[0] = (uint8_t)(controlword & 0xFF);
    f.data[1] = (uint8_t)(controlword >> 8);
    f.data[2] = (uint8_t)(target_pos & 0xFF);
    f.data[3] = (uint8_t)((target_pos >> 8) & 0xFF);
    f.data[4] = (uint8_t)((target_pos >> 16) & 0xFF);
    f.data[5] = (uint8_t)((target_pos >> 24) & 0xFF);
    return co_bxcan_send(bus, &f);
}

void co_pdo_process_frame(co_bus_t bus, const co_frame_t *f)
{
    if (!f) return;
    if (f->id >= CO_COBID_TPDO1_BASE + 1 &&
        f->id <= CO_COBID_TPDO1_BASE + 127 && f->dlc >= 6) {
        uint8_t node = (uint8_t)(f->id - CO_COBID_TPDO1_BASE);
        fb_t *fb = &s_fb[bus][node];
        fb->statusword = (uint16_t)(f->data[0] | (f->data[1] << 8));
        fb->pos_actual = (int32_t)((uint32_t)f->data[2]
                       | ((uint32_t)f->data[3] << 8)
                       | ((uint32_t)f->data[4] << 16)
                       | ((uint32_t)f->data[5] << 24));
        fb->valid = true;
        fb->seq++;
    }
}

uint32_t co_pdo_feedback_seq(co_bus_t bus, uint8_t node)
{
    if (bus >= CO_BUS_COUNT || node >= MAX_NODE) return 0;
    return s_fb[bus][node].seq;
}

bool co_pdo_get_feedback(co_bus_t bus, uint8_t node,
                         uint16_t *statusword, int32_t *pos_actual)
{
    if (bus >= CO_BUS_COUNT || node >= MAX_NODE) return false;
    fb_t *fb = &s_fb[bus][node];
    if (!fb->valid) return false;
    if (statusword) *statusword = fb->statusword;
    if (pos_actual) *pos_actual = fb->pos_actual;
    return true;
}

void co_pdo_reset(void)
{
    for (int b = 0; b < CO_BUS_COUNT; b++)
        for (int n = 0; n < MAX_NODE; n++)
            s_fb[b][n] = (fb_t){0};
}
