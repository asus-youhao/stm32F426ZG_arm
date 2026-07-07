/**
 * L2-11 · 匯流排頻寬預算計算器（對照 learn/l2-mid.html 第 11 節）
 *
 * 純計算、隨處可跑。重現 firmware/app/control_rate.h 選 500Hz 的那筆帳。
 */
#include <stdio.h>

#define FRAME_BITS   130.0   /* Classic CAN 8-byte frame 含 overhead+stuffing 約值 */
#define BITRATE      1e6     /* 1 Mbps */

int main(void)
{
    double fps_max = BITRATE / FRAME_BITS;
    printf("Classic CAN @1Mbps 每秒 frame 上限 ≈ %.0f\n\n", fps_max);
    printf("%8s %8s %12s %10s %s\n", "軸數", "頻率Hz", "frame/s", "載荷%", "判定");

    int axes[] = { 7, 7, 14, 14 };
    int hz[]   = { 500, 1000, 500, 1000 };
    for (int i = 0; i < 4; i++) {
        double fps = axes[i] * 2.0 * hz[i];        /* RPDO+TPDO 每軸每週期 */
        double load = fps / fps_max * 100.0;
        printf("%8d %8d %12.0f %9.0f%% %s\n", axes[i], hz[i], fps, load,
               load < 80 ? "✅ 安全" : load <= 100 ? "⚠️ 邊界(本專案 500Hz 在此)"
                                                   : "❌ 超載必丟幀");
    }
    printf("\n結論：單臂 7 軸 1kHz 超載 → 拆雙 bus 也只救回 500Hz;"
           "真要 1kHz 走 EtherCAT(專案 B)。\n");
    return 0;
}
