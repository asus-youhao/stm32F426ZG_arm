/** @file test_safety.c — WP6 安全狀態機（含 require_all_enabled_for_run 門檻） */
#include "test_framework.h"
#include "../safety/safety.h"

#define SW_OP_EN 0x0027u   /* operation enabled */
#define SW_RDY   0x0021u   /* ready to switch on */
#define SW_FAULT 0x0008u

static void report_all(uint16_t sw, uint32_t t)
{
    for (int j = 0; j < SAFETY_JOINTS; j++) safety_report_joint(j, sw, t);
}

void test_safety(void)
{
    /* ---- require_all_enabled_for_run = true：需全軸才 RUNNING ---- */
    safety_cfg_t cfg = { .comms_timeout_ms = 50, .require_all_enabled_for_run = true };
    safety_init(&cfg);

    /* 啟動初期：無回授 → 無危險（allow=true）但只到 ENABLED,不可 RUNNING */
    CHECK(safety_update(10) == true);
    CHECK(safety_state() == SYS_ENABLED);

    /* 只有 1 軸 enabled → 仍 ENABLED（門檻未達）,且 allow 必須為 true
       （否則 safe-stop 會蓋掉使能交握,其餘軸永遠 enable 不了） */
    safety_report_joint(0, SW_OP_EN, 20);
    CHECK(safety_update(20) == true);
    CHECK(safety_state() == SYS_ENABLED);

    /* 全軸 enabled → RUNNING */
    report_all(SW_OP_EN, 30);
    CHECK(safety_update(30) == true);
    CHECK(safety_state() == SYS_RUNNING);

    /* ---- require_all_enabled_for_run = false：任一軸即 RUNNING ---- */
    safety_init(&cfg);
    cfg.require_all_enabled_for_run = false;
    safety_init(&cfg);

    CHECK(safety_update(10) == true);
    CHECK(safety_state() == SYS_ENABLED);        /* 無軸 enabled → 尚未 RUNNING */

    safety_report_joint(3, SW_OP_EN, 20);        /* 單軸即達門檻 */
    CHECK(safety_update(20) == true);
    CHECK(safety_state() == SYS_RUNNING);

    /* ---- 兩種設定必須產生不同結果（防退化為形同虛設） ---- */
    {
        safety_cfg_t a = { .comms_timeout_ms = 50, .require_all_enabled_for_run = true };
        safety_init(&a);
        safety_report_joint(0, SW_OP_EN, 5);
        safety_update(5);
        sys_state_t st_require = safety_state();

        safety_cfg_t b = { .comms_timeout_ms = 50, .require_all_enabled_for_run = false };
        safety_init(&b);
        safety_report_joint(0, SW_OP_EN, 5);
        safety_update(5);
        sys_state_t st_loose = safety_state();

        CHECK(st_require == SYS_ENABLED);
        CHECK(st_loose == SYS_RUNNING);
        CHECK(st_require != st_loose);
    }

    /* ---- 故障位 → FAULT 且 allow=false ---- */
    safety_init(&cfg);
    report_all(SW_OP_EN, 40);
    safety_report_joint(5, SW_OP_EN | SW_FAULT, 41);
    CHECK(safety_update(41) == false);
    CHECK(safety_state() == SYS_FAULT);

    /* ---- 看門狗：活過的軸失聯 → FAULT；從未回報的軸不算失聯 ---- */
    safety_init(&cfg);
    safety_report_joint(0, SW_RDY, 100);         /* 只有軸 0 活過 */
    CHECK(safety_update(120) == true);           /* 20ms < 50ms → OK */
    CHECK(safety_update(200) == false);          /* 100ms > 50ms → 失聯 */
    CHECK(safety_state() == SYS_FAULT);

    /* ---- 急停最高優先 ---- */
    safety_init(&cfg);
    report_all(SW_OP_EN, 300);
    safety_set_estop(true);
    CHECK(safety_update(300) == false);
    CHECK(safety_state() == SYS_ESTOP);
    CHECK(safety_safe_controlword() == 0x0000);  /* estop → disable voltage */
    safety_set_estop(false);
    CHECK(safety_update(301) == true);           /* 解除後恢復 */
}
