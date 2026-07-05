/**
 * @file    viz_bridge.c
 * @brief   pc_master ↔ ws_server 視覺化橋實作（UDP JSON）
 */
#include "viz_bridge.h"
#include "joint_space.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int                s_fd = -1;
static struct sockaddr_in s_peer;
static bool               s_have_peer;

int viz_open(int port)
{
    s_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_fd < 0) return -1;
    struct sockaddr_in a = { .sin_family = AF_INET,
                             .sin_port = htons((uint16_t)port) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s_fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(s_fd);
        s_fd = -1;
        return -1;
    }
    s_have_peer = false;
    return s_fd;
}

bool viz_poll_cmd(char *line, size_t cap)
{
    if (s_fd < 0) return false;
    char buf[256];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    ssize_t n = recvfrom(s_fd, buf, sizeof(buf) - 1, MSG_DONTWAIT,
                         (struct sockaddr *)&from, &flen);
    if (n <= 0) return false;
    buf[n] = '\0';

    s_peer = from;                       /* 任何來訊都算訂閱/保活 */
    s_have_peer = true;
    if (strncmp(buf, "hello", 5) == 0) return false;

    snprintf(line, cap, "%s", buf);
    return true;
}

void viz_send_tele(const app_tele_t *t, const char *sys_str)
{
    if (s_fd < 0 || !s_have_peer) return;

    char js[1536];
    int n = snprintf(js, sizeof(js),
                     "{\"src\":\"pc_master\",\"t\":%llu,\"sys\":\"%s\","
                     "\"late\":%lu,\"miss\":%llu,\"drops\":%lu,\"q\":[",
                     (unsigned long long)t->tick, sys_str,
                     (unsigned long)t->late_max_us,
                     (unsigned long long)t->miss, (unsigned long)t->tx_drops);
    for (int j = 0; j < APP_TELE_NJ; j++)
        n += snprintf(js + n, sizeof(js) - (size_t)n, "%s%.5f",
                      j ? "," : "", (double)js_counts_to_rad(j, t->pos[j]));
    n += snprintf(js + n, sizeof(js) - (size_t)n, "],\"qt\":[");
    for (int j = 0; j < APP_TELE_NJ; j++)
        n += snprintf(js + n, sizeof(js) - (size_t)n, "%s%.5f",
                      j ? "," : "", (double)js_counts_to_rad(j, t->tgt[j]));
    n += snprintf(js + n, sizeof(js) - (size_t)n, "],\"sw\":[");
    for (int j = 0; j < APP_TELE_NJ; j++)
        n += snprintf(js + n, sizeof(js) - (size_t)n, "%s%u",
                      j ? "," : "", (unsigned)t->sw[j]);
    n += snprintf(js + n, sizeof(js) - (size_t)n, "]}");
    if (n >= (int)sizeof(js)) return;    /* 截斷防禦（正常 <800B 不會發生） */

    (void)sendto(s_fd, js, (size_t)n, MSG_DONTWAIT,
                 (struct sockaddr *)&s_peer, sizeof(s_peer));
}

void viz_close(void)
{
    if (s_fd >= 0) close(s_fd);
    s_fd = -1;
    s_have_peer = false;
}
