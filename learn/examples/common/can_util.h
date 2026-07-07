/**
 * @file  can_util.h
 * @brief L2/專案A 範例共用：SocketCAN 開啟/收發 + CANopen 常數。
 *
 * 對照 repo：這層等價於 firmware/canopen/co_bxcan.h 的 4 個函式 —
 * 換掉底層（bxCAN ↔ SocketCAN），上面的 CANopen 邏輯完全不變（L2-12 的那一刀）。
 */
#ifndef CAN_UTIL_H
#define CAN_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/time.h>

/* CANopen COB-ID（同 firmware/canopen/canopen.h 的定義） */
#define COB_NMT        0x000u
#define COB_TPDO1(n)   (0x180u + (n))
#define COB_RPDO1(n)   (0x200u + (n))
#define COB_SDO_TX(n)  (0x580u + (n))   /* server -> client */
#define COB_SDO_RX(n)  (0x600u + (n))   /* client -> server */
#define COB_HB(n)      (0x700u + (n))

static inline int can_open(const char *iface, int timeout_ms)
{
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) { perror("socket"); return -1; }
    struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) { perror(iface); close(s); return -1; }
    struct sockaddr_can addr = { .can_family = AF_CAN, .can_ifindex = ifr.ifr_ifindex };
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); close(s); return -1; }
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return s;
}

static inline int can_send(int s, uint32_t id, const void *data, uint8_t len)
{
    struct can_frame f; memset(&f, 0, sizeof f);
    f.can_id = id; f.can_dlc = len;
    memcpy(f.data, data, len);
    return (int)write(s, &f, sizeof f);
}

/** @return 1 有 frame、0 逾時 */
static inline int can_recv(int s, struct can_frame *f)
{
    return read(s, f, sizeof *f) == (ssize_t)sizeof *f ? 1 : 0;
}

static inline uint32_t now_ms(void)
{
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}

#endif /* CAN_UTIL_H */
