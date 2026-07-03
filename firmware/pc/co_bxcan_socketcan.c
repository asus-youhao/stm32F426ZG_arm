/**
 * @file    co_bxcan_socketcan.c
 * @brief   Linux SocketCAN 後端 — 取代 co_bxcan.c 的 bxCAN 硬體層
 *
 * 讓同一套 L0–L4 C 韌體堆疊直接跑在 PC 上：CO_BUS_LEFT→vcan0、
 * CO_BUS_RIGHT→vcan1（可用 co_socketcan_set_ifname 改綁）。
 * 對端由 sim_py/can_slave.py 模擬 14 顆 EYOU PHU CiA402 從站。
 *
 * 與 bxCAN 版行為對齊：
 *   - send 非阻塞;kernel TX queue 滿（ENOBUFS/EAGAIN）→ CO_ERR_TX,
 *     對應 mailbox 滿的丟幀語意,dual_arm_tx_drops() 統計照常有效。
 *   - recv 非阻塞輪詢（bxCAN 版由 ISR 入佇列,此處直接讀 socket）。
 */
#include "co_bxcan.h"
#include "co_bxcan_socketcan.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char s_ifname[CO_BUS_COUNT][IFNAMSIZ] = { "vcan0", "vcan1" };
static int  s_fd[CO_BUS_COUNT] = { -1, -1 };

void co_socketcan_set_ifname(co_bus_t bus, const char *ifname)
{
    if (bus >= CO_BUS_COUNT) return;
    if (!ifname) ifname = "";
    strncpy(s_ifname[bus], ifname, IFNAMSIZ - 1);
    s_ifname[bus][IFNAMSIZ - 1] = '\0';
}

const char *co_socketcan_ifname(co_bus_t bus)
{
    return (bus < CO_BUS_COUNT) ? s_ifname[bus] : "";
}

co_status_t co_bxcan_init(co_bus_t bus)
{
    if (bus >= CO_BUS_COUNT) return CO_ERR_PARAM;

    if (s_fd[bus] >= 0) { close(s_fd[bus]); s_fd[bus] = -1; }
    if (s_ifname[bus][0] == '\0') return CO_ERR_STATE;   /* 該臂停用 */

    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        fprintf(stderr, "[socketcan] socket() 失敗: %s\n", strerror(errno));
        return CO_ERR_STATE;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, s_ifname[bus], IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "[socketcan] 介面 %s 不存在（先跑 setup_vcan.sh?）: %s\n",
                s_ifname[bus], strerror(errno));
        close(fd);
        return CO_ERR_STATE;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[socketcan] bind(%s) 失敗: %s\n",
                s_ifname[bus], strerror(errno));
        close(fd);
        return CO_ERR_STATE;
    }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    s_fd[bus] = fd;
    return CO_OK;
}

co_status_t co_bxcan_send(co_bus_t bus, const co_frame_t *f)
{
    if (bus >= CO_BUS_COUNT || !f || f->dlc > 8) return CO_ERR_PARAM;
    if (s_fd[bus] < 0) return CO_ERR_STATE;

    struct can_frame cf;
    memset(&cf, 0, sizeof(cf));
    cf.can_id  = f->id & CAN_SFF_MASK;
    cf.can_dlc = f->dlc;
    memcpy(cf.data, f->data, f->dlc);

    ssize_t n = write(s_fd[bus], &cf, sizeof(cf));
    if (n == sizeof(cf)) return CO_OK;
    if (errno == ENOBUFS || errno == EAGAIN) return CO_ERR_TX; /* = mailbox 滿 */
    return CO_ERR_STATE;
}

bool co_bxcan_recv(co_bus_t bus, co_frame_t *out)
{
    if (bus >= CO_BUS_COUNT || !out || s_fd[bus] < 0) return false;

    struct can_frame cf;
    for (;;) {
        ssize_t n = read(s_fd[bus], &cf, sizeof(cf));
        if (n != (ssize_t)sizeof(cf)) return false;          /* 空/錯誤 → 無資料 */
        /* 只收 11-bit 標準資料幀（協定僅用 SFF;跳過 EFF/RTR/error frame） */
        if (cf.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) continue;
        out->id  = (uint16_t)(cf.can_id & CAN_SFF_MASK);
        out->dlc = (cf.can_dlc > 8) ? 8 : cf.can_dlc;
        memcpy(out->data, cf.data, out->dlc);
        return true;
    }
}

/* bxCAN 版由 RX ISR 呼叫;SocketCAN 直接在 recv 讀 socket,無事可做。 */
void co_bxcan_on_rx(co_bus_t bus, const co_frame_t *f) { (void)bus; (void)f; }
