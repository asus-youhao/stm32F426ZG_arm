/**
 * L3-16 · 看門狗的 fb_fresh 教訓 — buggy vs fixed（對照 learn/l3-adv.html 第 16 節）
 *
 * 重現 firmware/app/app_main.c 註解裡那個真實 bug：
 *   buggy:  每個 tick 無條件刷新每軸時間戳 → 軸失聯永遠偵測不到
 *   fixed:  只有「真的收到新回授」(fb_fresh) 才刷新 → 正確咬人
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define AXES 3
#define TIMEOUT_MS 50

typedef struct { uint32_t last_ms[AXES]; int tripped; } wd_t;

static void run(const char *tag, int use_fb_fresh)
{
    wd_t wd; memset(&wd, 0, sizeof wd);
    wd.tripped = -1;
    printf("== %s ==\n", tag);

    for (uint32_t now = 0; now <= 200; now += 2) {       /* 500Hz tick 模擬 */
        for (int j = 0; j < AXES; j++) {
            /* 軸2 在 t=100ms 斷線(不再有新回授);其他軸正常 */
            int fb_fresh = !(j == 2 && now >= 100);
            if (use_fb_fresh ? fb_fresh : 1)             /* bug 就在這個條件 */
                wd.last_ms[j] = now;
        }
        for (int j = 0; j < AXES; j++)
            if (now - wd.last_ms[j] > TIMEOUT_MS && wd.tripped < 0) {
                wd.tripped = (int)now;
                printf("  t=%3ums: 軸%d 逾時 -> 安全停止 ✅\n", now, j);
            }
    }
    if (wd.tripped < 0)
        printf("  跑完 200ms: 從未偵測到失聯 ❌ (軸2 其實 100ms 就斷了!)\n");
}

int main(void)
{
    run("buggy: 無條件餵狗", 0);
    run("fixed: 只有 fb_fresh 才餵狗", 1);
    printf("\n看門狗第一定律: 餵狗條件必須是「真的有新資料」。\n");
    return 0;
}
