/**
 * @file    rt_selfcheck.h
 * @brief   開機自檢（WP-L7.1;項目 6）——RT 環境不符 → 拒絕進 OP
 *
 * BOOT 階段（BUS_UP 之前）逐項檢查 RT 前置條件,輸出自檢清單。
 * 必要項（required）不過時由呼叫端決策：--rt-strict 拒絕啟動
 * （真機 runbook 預設帶 strict）,否則列警告後降級運行（開發機/SIL）。
 * 與 rt_setup.sh --check（方案 A 分支,佈建面）互補：這裡是執行期
 * 最後一道門,佈建腳本沒跑或內核升級後漂移都擋得住。
 *
 * 解析核心與 I/O 分離：sc_* 純函式吃注入字串（單元測試用）,
 * rt_selfcheck_run() 才讀真實 /proc、/sys。
 */
#ifndef RT_SELFCHECK_H
#define RT_SELFCHECK_H

#include <stdbool.h>
#include <stdio.h>

#define RT_CHECK_MAX 10

typedef struct {
    const char *name;      /* 檢查項名 */
    bool        required;  /* 必要項（不過 → strict 模式拒絕進 OP） */
    bool        pass;
    char        detail[96];
} rt_check_t;

typedef struct {
    rt_check_t c[RT_CHECK_MAX];
    int        n;
    int        required_fails;
} rt_report_t;

/**
 * @brief 跑全部檢查。
 * @param ifnames SocketCAN 介面名（NULL 結尾陣列;NULL 或空=跳過 NIC 檢查,
 *                如 EtherCAT sim 後端）。
 * @return required_fails（0 = 可進 OP）。
 */
int rt_selfcheck_run(rt_report_t *r, const char *const *ifnames);

/** @brief 自檢清單輸出（每項 [PASS]/[FAIL]/[warn] + 細節）。 */
void rt_selfcheck_print(const rt_report_t *r, FILE *out);

/* ---- 可測解析核心（純函式,無 I/O）---- */

/** @brief cmdline 是否含 key（以空白分界的參數前綴,如 "isolcpus="）。 */
bool sc_cmdline_has(const char *cmdline, const char *key);

/** @brief 內核版本字串是否表明 PREEMPT_RT。 */
bool sc_version_is_rt(const char *version);

#endif /* RT_SELFCHECK_H */
