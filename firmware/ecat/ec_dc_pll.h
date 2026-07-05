/**
 * @file    ec_dc_pll.h
 * @brief   DC 鎖相 PI 控制器（WP-L2.2 前置；設計文件 §3.1 phase trim 的使用者）
 *
 * SOEM 路徑主站是 DC「跟隨者」：以本 PI 把 loop engine 的喚醒點鎖到
 * 從站 DC 柵格。每週期：err = ec_master_dc_error_us()（正=主站晚到）
 * → trim = pll_step(err) → eng_phase_trim_us(engine, trim)。
 * 帶小數殘差進位（error-feedback）,平均 trim 可解析到次 µs 漂移
 * （200 ppm @1 kHz = 0.2 µs/tick,整數 trim 直接四捨五入會鎖不住）。
 * IgH 主站是發號者,不需要本模組。
 */
#ifndef EC_DC_PLL_H
#define EC_DC_PLL_H

#include <stdint.h>

typedef struct {
    float   kp, ki;      /* PI 增益（對象是積分器 plant,預設 0.3/0.02） */
    float   integ;       /* 積分項 */
    float   carry;       /* 整數化殘差（error-feedback） */
    int32_t max_trim_us; /* 單步 trim 限幅（engine 另有 ±5% 週期硬限） */
} ec_dc_pll_t;

/** @brief 初始化。kp/ki ≤0 用預設（0.3/0.02）;max_trim 0→50µs。 */
void    ec_dc_pll_init(ec_dc_pll_t *p, float kp, float ki, int32_t max_trim_us);

/** @brief 餵入本週期相位誤差（µs）,回本週期應施加的 deadline trim（µs）。 */
int32_t ec_dc_pll_step(ec_dc_pll_t *p, int32_t err_us);

#endif /* EC_DC_PLL_H */
