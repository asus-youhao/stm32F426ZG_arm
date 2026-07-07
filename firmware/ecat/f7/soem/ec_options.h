/**
 * @file  ec_options.h（F746 bare-metal port）
 * @brief SOEM v2 建置選項——取代 cmake 生成版（WP-SE3）
 *
 * 尺寸對 F746ZG（320KB SRAM）縮編：雙臂 14 軸 + 裕度。
 * 授權注意：SOEM v2 為 GPLv3/商用雙授權（產品化前法務確認）。
 */
#ifndef _ec_options_
#define _ec_options_

#ifdef __cplusplus
extern "C" {
#endif

/* Max sizes */
#define EC_BUFSIZE       (EC_MAXECATFRAME)
#define EC_MAXBUF        (8)              /* 週期 LRW + 背景 SDO 夠用（PC 版 16） */
#define EC_MAXEEPBITMAP  (128)
#define EC_MAXEEPBUF     (EC_MAXEEPBITMAP << 5)
#define EC_LOGGROUPOFFSET (16)
#define EC_MAXELIST      (32)
#define EC_MAXNAME       (40)
#define EC_MAXSLAVE      (18)             /* 14 軸 + 裕度（PC 版 200） */
#define EC_MAXGROUP      (2)
#define EC_MAXIOSEGMENTS (16)
#define EC_MAXMBX        (512)
#define EC_MBXPOOLSIZE   (16)
#define EC_MAXEEPDO      (0x200)
#define EC_MAXSM         (8)
#define EC_MAXFMMU       (4)
#define EC_MAXLEN_ADAPTERNAME (16)
#define EC_MAX_MAPT      (1)
#define EC_MAXODLIST     (64)             /* 板端不掃 OD 清單（PC 版 1024） */
#define EC_MAXOELIST     (32)
#define EC_SOE_MAXNAME   (60)
#define EC_SOE_MAXMAPPING (16)

/* Timeouts (us) — 同 cmake 預設 */
#define EC_TIMEOUTRET    (2000)
#define EC_TIMEOUTRET3   (EC_TIMEOUTRET * 3)
#define EC_TIMEOUTSAFE   (20000)
#define EC_TIMEOUTEEP    (20000)
#define EC_TIMEOUTTXM    (20000)
#define EC_TIMEOUTRXM    (700000)
#define EC_TIMEOUTSTATE  (2000000)
#define EC_DEFAULTRETRIES (3)

/* MAC（EtherCAT 不在乎定址,僅冗餘路徑識別用） */
#define EC_PRIMARY_MAC_ARRAY   {0x0101, 0x0101, 0x0101}
#define EC_SECONDARY_MAC_ARRAY {0x0404, 0x0404, 0x0404}

#ifdef __cplusplus
}
#endif

#endif
