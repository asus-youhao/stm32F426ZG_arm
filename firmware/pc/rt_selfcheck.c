/**
 * @file    rt_selfcheck.c
 * @brief   開機自檢實作（WP-L7.1）
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE          /* tests 以 -std=c11 編譯,POSIX 宣告需明示 */
#endif
#include "rt_selfcheck.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>

/* ---- 可測解析核心 ---- */

bool sc_cmdline_has(const char *cmdline, const char *key)
{
    size_t klen = strlen(key);
    const char *p = cmdline;
    while ((p = strstr(p, key)) != NULL) {
        if (p == cmdline || p[-1] == ' ') {          /* 參數邊界 */
            const char *end = p + klen;
            /* "isolcpus=" 要求帶值;"quiet" 這類旗標鍵允許結尾/空白 */
            if (klen && key[klen - 1] == '=')
                return *end != '\0' && *end != ' ';
            return *end == '\0' || *end == ' ' || *end == '=';
        }
        p += klen;
    }
    return false;
}

bool sc_version_is_rt(const char *version)
{
    return strstr(version, "PREEMPT_RT") != NULL ||
           strstr(version, "PREEMPT RT") != NULL;
}

/* ---- 檔案小工具 ---- */

static bool read_first_line(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    bool ok = fgets(buf, (int)cap, f) != NULL;
    fclose(f);
    if (ok) buf[strcspn(buf, "\n")] = '\0';
    return ok;
}

static rt_check_t *add(rt_report_t *r, const char *name, bool required)
{
    rt_check_t *c = &r->c[r->n++];
    c->name = name;
    c->required = required;
    c->pass = false;
    c->detail[0] = '\0';
    return c;
}

/* ---- 各檢查項 ---- */

static void chk_kernel(rt_report_t *r)
{
    rt_check_t *c = add(r, "PREEMPT_RT 內核", true);
    char line[128];
    if (read_first_line("/sys/kernel/realtime", line, sizeof(line)) &&
        line[0] == '1') {
        c->pass = true;
        snprintf(c->detail, sizeof(c->detail), "/sys/kernel/realtime=1");
        return;
    }
    struct utsname u;
    if (uname(&u) == 0) {
        c->pass = sc_version_is_rt(u.version);
        snprintf(c->detail, sizeof(c->detail), "%.90s", u.version);
    }
}

static void chk_cmdline(rt_report_t *r)
{
    char cl[1024] = "";
    (void)read_first_line("/proc/cmdline", cl, sizeof(cl));

    rt_check_t *c = add(r, "isolcpus 隔離核", true);
    c->pass = sc_cmdline_has(cl, "isolcpus=");
    snprintf(c->detail, sizeof(c->detail), c->pass ? "cmdline 有 isolcpus"
                                                   : "cmdline 無 isolcpus");
    c = add(r, "nohz_full/rcu_nocbs", false);
    c->pass = sc_cmdline_has(cl, "nohz_full=") && sc_cmdline_has(cl, "rcu_nocbs=");
    snprintf(c->detail, sizeof(c->detail),
             c->pass ? "tick/RCU 已挪離隔離核" : "建議與 isolcpus 同組設定");
}

static void chk_rtprio(rt_report_t *r)
{
    rt_check_t *c = add(r, "SCHED_FIFO 權限", true);
    struct rlimit rl;
    if (geteuid() == 0) {
        c->pass = true;
        snprintf(c->detail, sizeof(c->detail), "root");
    } else if (getrlimit(RLIMIT_RTPRIO, &rl) == 0) {
        c->pass = rl.rlim_cur >= 80;
        snprintf(c->detail, sizeof(c->detail), "RLIMIT_RTPRIO=%ld（需 ≥80）",
                 (long)rl.rlim_cur);
    }
}

static void chk_mlockall(rt_report_t *r)
{
    rt_check_t *c = add(r, "mlockall 鎖頁", true);
    c->pass = mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
    if (c->pass) munlockall();                    /* 只探測;正式鎖在 main */
    snprintf(c->detail, sizeof(c->detail),
             c->pass ? "可鎖定" : "失敗（RLIMIT_MEMLOCK?）");
}

static void chk_dma_latency(rt_report_t *r)
{
    rt_check_t *c = add(r, "/dev/cpu_dma_latency", false);
    int fd = open("/dev/cpu_dma_latency", O_WRONLY);
    c->pass = fd >= 0;
    if (fd >= 0) close(fd);
    snprintf(c->detail, sizeof(c->detail),
             c->pass ? "可寫（可擋 C-state 深睡）" : "不可寫");
}

static void chk_governor(rt_report_t *r)
{
    rt_check_t *c = add(r, "cpufreq governor", false);
    char g[64] = "";
    if (read_first_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",
                        g, sizeof(g))) {
        c->pass = strcmp(g, "performance") == 0;
        snprintf(c->detail, sizeof(c->detail), "%s（建議 performance）", g);
    } else {
        c->pass = true;                           /* 無 cpufreq（VM 等）不追究 */
        snprintf(c->detail, sizeof(c->detail), "無 cpufreq 介面,跳過");
    }
}

static void chk_ifaces(rt_report_t *r, const char *const *ifnames)
{
    rt_check_t *c = add(r, "bus 介面", true);
    if (!ifnames || !ifnames[0]) {
        c->pass = true;
        snprintf(c->detail, sizeof(c->detail), "sim 後端,跳過");
        return;
    }
    c->pass = true;
    int n = 0;
    for (int i = 0; ifnames[i]; i++) {
        if (!ifnames[i][0]) continue;             /* 空字串=該臂停用 */
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/net/%s", ifnames[i]);
        if (access(path, F_OK) != 0) {
            c->pass = false;
            snprintf(c->detail, sizeof(c->detail), "%s 不存在", ifnames[i]);
            return;
        }
        n++;
    }
    snprintf(c->detail, sizeof(c->detail), "%d 個介面存在", n);
}

/* ---- 對外 ---- */

int rt_selfcheck_run(rt_report_t *r, const char *const *ifnames)
{
    memset(r, 0, sizeof(*r));
    chk_kernel(r);
    chk_cmdline(r);
    chk_rtprio(r);
    chk_mlockall(r);
    chk_dma_latency(r);
    chk_governor(r);
    chk_ifaces(r, ifnames);

    r->required_fails = 0;
    for (int i = 0; i < r->n; i++)
        if (r->c[i].required && !r->c[i].pass) r->required_fails++;
    return r->required_fails;
}

void rt_selfcheck_print(const rt_report_t *r, FILE *out)
{
    fprintf(out, "=== 開機自檢（WP-L7.1）===\n");
    for (int i = 0; i < r->n; i++) {
        const rt_check_t *c = &r->c[i];
        fprintf(out, "  [%s] %-22s %s\n",
                c->pass ? "PASS" : (c->required ? "FAIL" : "warn"),
                c->name, c->detail);
    }
    fprintf(out, "  必要項不過：%d%s\n", r->required_fails,
            r->required_fails ? "（--rt-strict 下拒絕進 OP）" : "");
}
