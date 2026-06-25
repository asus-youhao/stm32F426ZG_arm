/**
 * @file    safety.h
 * @brief   WP6 安全與系統狀態機（急停 / 看門狗 / 故障處理）
 *
 * 監看：驅動故障字、通訊新鮮度、急停輸入。
 * 輸出：系統狀態 + 是否允許運動;不安全時要求安全停止。
 */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>
#include <stdbool.h>

#define SAFETY_JOINTS 14

typedef enum {
    SYS_INIT = 0,
    SYS_IDLE,
    SYS_ENABLED,
    SYS_RUNNING,
    SYS_FAULT,
    SYS_ESTOP
} sys_state_t;

typedef struct {
    uint32_t comms_timeout_ms;  /* 回授逾時 → 視為通訊失聯 */
    bool     require_all_enabled_for_run; /* RUNNING 需全軸 operation enabled */
} safety_cfg_t;

void safety_init(const safety_cfg_t *cfg);

/** @brief 急停輸入（硬體按鈕/上位機）。 */
void safety_set_estop(bool active);

/**
 * @brief 回報某軸回授（每次收到 TPDO 後呼叫）。
 * @param statusword CiA402 狀態字
 * @param now_ms     目前時間
 */
void safety_report_joint(int joint, uint16_t statusword, uint32_t now_ms);

/**
 * @brief 週期更新（1 kHz）。彙整狀態、判斷故障/逾時。
 * @return 是否允許運動輸出。
 */
bool safety_update(uint32_t now_ms);

sys_state_t safety_state(void);
const char *safety_state_str(void);

/** @brief 不安全時應施加的 CiA402 控制字（quick stop / disable）。 */
uint16_t safety_safe_controlword(void);

#endif /* SAFETY_H */
