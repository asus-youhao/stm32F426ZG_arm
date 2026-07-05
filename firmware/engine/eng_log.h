/**
 * @file    eng_log.h
 * @brief   log ring（WP-H2 計畫項；設計文件 §5.3）——RT 域結構化事件日誌
 *
 * RT 域**只寫**固定大小紀錄（等級+代碼+兩個參數,無格式化字串、無 I/O）;
 * 非 RT 端（harness/pc_master 主執行緒）pop 後格式化列印/落檔。
 * SPSC：RT 單寫、非 RT 單讀。滿了丟新留舊並計 drop（日誌不搶控制預算）。
 */
#ifndef ENG_LOG_H
#define ENG_LOG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { EL_INFO = 0, EL_WARN, EL_ERR } eng_log_level_t;

/* 事件代碼（新增請同步 eng_log_code_str） */
enum {
    ELC_NONE = 0,
    ELC_SAFE_STOP_ON,    /* a=sys_state */
    ELC_SAFE_STOP_OFF,   /* a=sys_state */
    ELC_FAULT_EVT,       /* a=joint, b=code（EMCY/CiA402;b=0 復歸） */
    ELC_OVERRUN_ESC,     /* a=連續 overrun 數 */
    ELC_AXIS_STALE,      /* a=joint（回授失聯,保留給後續使用） */
    ELC_BUDGET_OVER,     /* a=agent idx, b=耗時 µs（§3.6 遲到上下文） */
};

typedef struct {
    uint64_t t_us;       /* port_now_us 時戳 */
    uint16_t code;       /* ELC_* */
    uint8_t  level;      /* EL_* */
    int32_t  a, b;       /* 事件參數 */
} eng_log_rec_t;

/** @brief 初始化/清空（app 啟動或測試 setup 呼叫）。 */
void eng_log_init(void);

/** @brief RT 端寫入一筆；ring 滿回 false 並計 drop。 */
bool eng_log(uint8_t level, uint16_t code, int32_t a, int32_t b);

/** @brief 非 RT 端取出；空回 false。 */
bool eng_log_pop(eng_log_rec_t *out);

uint32_t eng_log_drops(void);

/** @brief 代碼 → 短字串（非 RT 格式化用）。 */
const char *eng_log_code_str(uint16_t code);

#endif /* ENG_LOG_H */
