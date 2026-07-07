/**
 * L3-14 · counts ↔ rad 單位換算（對照 learn/l3-adv.html 第 14 節）
 *
 * 兩種 EYOU PHU 介面組態的對照 — 差 101 倍，搞錯就是猛轉一圈變 101 圈：
 *   A) 輸出端 19-bit：524288 counts/輸出圈（firmware/app/robot_config.c）
 *   B) 馬達端 19-bit × 減速比 101：52953088 counts/輸出圈（EtherCAT 路線實測）
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>

#define TWO_PI (2.0 * M_PI)

typedef struct { const char *name; double counts_per_rev; } unit_cfg_t;

static int32_t rad_to_counts(const unit_cfg_t *c, double rad)
{ return (int32_t)llround(rad / TWO_PI * c->counts_per_rev); }

static double counts_to_rad(const unit_cfg_t *c, int32_t counts)
{ return (double)counts / c->counts_per_rev * TWO_PI; }

int main(void)
{
    unit_cfg_t cfg[2] = {
        { "A 輸出端 19-bit",        524288.0 },
        { "B 馬達端 19-bit × 101", 52953088.0 },   /* 524288*101 */
    };
    double test_rad[] = { 0.0, M_PI / 2, M_PI, -M_PI / 4 };

    for (int i = 0; i < 2; i++) {
        printf("== 組態 %s（%.0f counts/圈）==\n", cfg[i].name, cfg[i].counts_per_rev);
        for (int j = 0; j < 4; j++) {
            int32_t cnt = rad_to_counts(&cfg[i], test_rad[j]);
            double back = counts_to_rad(&cfg[i], cnt);
            printf("  %+8.4f rad -> %+10d counts -> %+8.4f rad\n",
                   test_rad[j], cnt, back);
            assert(fabs(back - test_rad[j]) < 1e-4);   /* 往返誤差要極小 */
        }
    }
    printf("\n同一個 90 度: A=%d, B=%d counts — 差 101 倍!\n",
           rad_to_counts(&cfg[0], M_PI / 2), rad_to_counts(&cfg[1], M_PI / 2));
    printf("上電第一件事: 手轉一圈讀回授,驗證你用的是哪一種。\n");
    return 0;
}
