/** @file test_hostif.c — WP7 上位機協定編碼/解碼往返 */
#include "test_framework.h"
#include "host_if.h"
#include <string.h>

/* 覆寫弱連結 host_on_command 以捕捉解析結果 */
static uint8_t  cap_type;
static uint8_t  cap_len;
static uint8_t  cap_payload[64];
static int      cap_count;

void host_on_command(uint8_t type, const uint8_t *payload, uint8_t len)
{
    cap_type = type; cap_len = len; cap_count++;
    memcpy(cap_payload, payload, len);
}

void test_hostif(void)
{
    /* float 讀寫往返 */
    uint8_t fb[4];
    host_wr_f32(fb, 3.14159f);
    CHECK_NEAR(host_rd_f32(fb), 3.14159f, 1e-6f);

    /* 編碼 CMD_JOINT_MOVE(joint=4, rad=1.25) → 解析還原 */
    uint8_t payload[5];
    payload[0] = 4;
    host_wr_f32(payload + 1, 1.25f);
    uint8_t frame[16];
    size_t n = host_encode(CMD_JOINT_MOVE, payload, 5, frame, sizeof(frame));
    CHECK(n == 9);                       /* SYNC+TYPE+LEN+5+CK */
    if (n == 0) return;                  /* 編碼失敗則止（並讓編譯器知 n>0） */
    CHECK(frame[0] == HOST_SYNC);
    CHECK(frame[1] == CMD_JOINT_MOVE);
    CHECK(frame[2] == 5);

    cap_count = 0;
    for (size_t i = 0; i < n; i++) host_feed_byte(frame[i]);
    CHECK(cap_count == 1);
    CHECK(cap_type == CMD_JOINT_MOVE);
    CHECK(cap_len == 5);
    CHECK(cap_payload[0] == 4);
    CHECK_NEAR(host_rd_f32(cap_payload + 1), 1.25f, 1e-6f);

    /* 壞校驗碼應被丟棄 */
    cap_count = 0;
    frame[n - 1] ^= 0xFF;                 /* 破壞 cksum */
    for (size_t i = 0; i < n; i++) host_feed_byte(frame[i]);
    CHECK(cap_count == 0);

    /* 雜訊 + 正確 frame：應只觸發一次 */
    cap_count = 0;
    host_feed_byte(0x00); host_feed_byte(0x12);
    frame[n - 1] ^= 0xFF;                 /* 還原 cksum */
    for (size_t i = 0; i < n; i++) host_feed_byte(frame[i]);
    CHECK(cap_count == 1);
}
