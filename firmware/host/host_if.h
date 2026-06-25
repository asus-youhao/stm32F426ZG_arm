/**
 * @file    host_if.h
 * @brief   WP7 上位機介面：命令 / 遙測協定（傳輸無關）
 *
 * 框架：SYNC(0xAA) TYPE(1) LEN(1) PAYLOAD(LEN) CKSUM(1=XOR of TYPE..PAYLOAD)
 * 傳輸層以弱連結 host_send_bytes() 介接（UART/USB/Ethernet 皆可）。
 */
#ifndef HOST_IF_H
#define HOST_IF_H

#include <stdint.h>
#include <stddef.h>

#define HOST_SYNC 0xAA

/* 命令（上位機→MCU） */
enum {
    CMD_ESTOP      = 0x01,  /* payload: u8 active */
    CMD_SET_MODE   = 0x02,  /* payload: u8 mode(da_mode_t) */
    CMD_JOINT_MOVE = 0x03,  /* payload: u8 joint, f32 rad */
    CMD_SET_POSE   = 0x04,  /* payload: u8 arm(0=L,1=R), f32 x,y,z */
};

/* 遙測（MCU→上位機） */
enum {
    TLM_STATE  = 0x81,  /* payload: u8 sys_state, u8 mode */
    TLM_JOINTS = 0x82,  /* payload: 14 × { f32 pos_rad, u16 statusword } */
    TLM_EE     = 0x83,  /* payload: f32 L[3], f32 R[3] */
    TLM_MOTOR  = 0x84,  /* payload: 14 × { f32 torque_Nm, f32 current_A } */
};

/* ---- 編碼（建立一個 frame 到 out,回傳長度;失敗回 0） ---- */
size_t host_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                   uint8_t *out, size_t cap);

/* ---- 解析：逐位元組餵入,完成一個 frame 時呼叫 host_on_command() ---- */
void host_feed_byte(uint8_t b);

/* ---- 由應用實作：收到命令時的處理 ---- */
void host_on_command(uint8_t type, const uint8_t *payload, uint8_t len);

/* ---- 由傳輸層實作（弱連結預設空）：送出位元組 ---- */
void host_send_bytes(const uint8_t *buf, size_t len);

/* 小工具：float 讀寫（小端） */
float    host_rd_f32(const uint8_t *p);
void     host_wr_f32(uint8_t *p, float v);

#endif /* HOST_IF_H */
