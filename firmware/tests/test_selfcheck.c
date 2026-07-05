/**
 * @file test_selfcheck.c — 開機自檢驗收（項目 6;WP-L7.1）
 *
 * 解析核心（純函式）用注入字串測定;實環境檢查（讀 /proc、/sys）
 * 結果依機器而異,由 pc_master E2E 覆蓋（本開發機必要項必不過 →
 * --rt-strict 必須拒絕啟動,見變更文件驗證節）。
 */
#include "test_framework.h"
#include "rt_selfcheck.h"
#include <string.h>

void test_selfcheck(void)
{
    /* ---- sc_cmdline_has：參數邊界與帶值判斷 ---- */
    {
        const char *cl = "BOOT_IMAGE=/vmlinuz quiet splash isolcpus=2,3 "
                         "nohz_full=2,3 rcu_nocbs=2,3 irqaffinity=0-1";
        CHECK(sc_cmdline_has(cl, "isolcpus="));
        CHECK(sc_cmdline_has(cl, "nohz_full="));
        CHECK(sc_cmdline_has(cl, "rcu_nocbs="));
        CHECK(sc_cmdline_has(cl, "quiet"));            /* 旗標鍵 */
        CHECK(!sc_cmdline_has(cl, "isolcpu="));        /* 不同鍵 */
        CHECK(!sc_cmdline_has(cl, "olcpus="));         /* 非參數邊界 */
        CHECK(!sc_cmdline_has("isolcpus= quiet", "isolcpus="));  /* = 後無值 */
        CHECK(!sc_cmdline_has("", "isolcpus="));
        CHECK(sc_cmdline_has("isolcpus=5", "isolcpus="));        /* 行首/行尾 */
    }

    /* ---- sc_version_is_rt ---- */
    {
        CHECK(sc_version_is_rt("#1 SMP PREEMPT_RT Ubuntu 6.8.1-1010-realtime"));
        CHECK(sc_version_is_rt("#12 SMP PREEMPT RT Tue Jan 1"));
        CHECK(!sc_version_is_rt("#124-Ubuntu SMP PREEMPT_DYNAMIC"));
        CHECK(!sc_version_is_rt("#1 SMP Debian 6.1.0"));
    }

    /* ---- 真實環境跑一輪：只驗結構完整,不驗結果（依機器而異）---- */
    {
        rt_report_t r;
        int nfail = rt_selfcheck_run(&r, NULL);        /* NULL=跳過 NIC */
        CHECK(r.n >= 6 && r.n <= RT_CHECK_MAX);
        CHECK(nfail == r.required_fails);
        int counted = 0;
        for (int i = 0; i < r.n; i++) {
            CHECK(r.c[i].name != NULL && r.c[i].detail[0] != '\0');
            if (r.c[i].required && !r.c[i].pass) counted++;
        }
        CHECK(counted == r.required_fails);
        /* ifnames 給不存在的介面 → bus 介面必要項必 FAIL */
        const char *bad[] = { "no_such_if0", NULL };
        int nf2 = rt_selfcheck_run(&r, bad);
        CHECK(nf2 >= 1);
        int found = 0;
        for (int i = 0; i < r.n; i++)
            if (!r.c[i].pass && r.c[i].required &&
                strstr(r.c[i].detail, "no_such_if0")) found = 1;
        CHECK(found);
    }
}
