/**
 * @file    ec_dc_pll.c
 * @brief   DC 鎖相 PI 實作
 *
 * 模型：e[k+1] = e[k] + Δ + trim[k]（Δ=主從時基差/週期,積分器 plant）。
 * PI：u = kp·e + ki·Σe,trim = −u。kp=0.3/ki=0.02 對 1 kHz 穩定
 * （收斂 ~數十週期,無振盪;test_dc_pll 以 ±500 ppm 驗證）。
 */
#include "ec_dc_pll.h"

void ec_dc_pll_init(ec_dc_pll_t *p, float kp, float ki, int32_t max_trim_us)
{
    p->kp = (kp > 0.0f) ? kp : 0.3f;
    p->ki = (ki > 0.0f) ? ki : 0.02f;
    p->integ = 0.0f;
    p->carry = 0.0f;
    p->max_trim_us = max_trim_us > 0 ? max_trim_us : 50;
}

int32_t ec_dc_pll_step(ec_dc_pll_t *p, int32_t err_us)
{
    p->integ += (float)err_us;
    float u = p->kp * (float)err_us + p->ki * p->integ;

    /* 整數化 + 殘差進位：平均輸出等於 -u,可跟上次 µs 級漂移 */
    float want = -u + p->carry;
    int32_t trim = (int32_t)(want >= 0.0f ? want + 0.5f : want - 0.5f);
    if (trim >  p->max_trim_us) trim =  p->max_trim_us;
    if (trim < -p->max_trim_us) trim = -p->max_trim_us;
    p->carry = want - (float)trim;
    /* 飽和抗積分饱和（clamp 時凍結積分,避免 windup） */
    if (trim == p->max_trim_us || trim == -p->max_trim_us)
        p->integ -= (float)err_us;
    return trim;
}
