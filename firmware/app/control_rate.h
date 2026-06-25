/**
 * @file    control_rate.h
 * @brief   控制迴圈頻率設定
 *
 * 選定 500 Hz（非 1 kHz）：Classic CAN @1Mbps 每軸每週期需 1 RPDO+1 TPDO，
 * 單臂 7 軸 = 14 frame/週期。500 Hz → 7000 frame/s，約為 Classic CAN
 * 可用上限（~7000–8000 frame/s）的 ~90%，屬可行邊界；1 kHz（14000/s）會超載。
 * 若要 1 kHz 高頻力控，須改走 EtherCAT（見 docs/design/canopen-vs-ethercat.md）。
 */
#ifndef CONTROL_RATE_H
#define CONTROL_RATE_H

#define CONTROL_HZ   500.0f
#define CONTROL_DT   (1.0f / CONTROL_HZ)   /* 0.002 s */
#define CONTROL_DT_US 2000u                /* TIM 週期(微秒) */

#endif /* CONTROL_RATE_H */
