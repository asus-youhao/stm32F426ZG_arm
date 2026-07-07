/**
 * @file  oshw.c（F746 bare-metal port, WP-SE3 核心）
 * @brief oshw_mac_init/send/recv——Nucleo-F746ZG ETH MAC(RMII) + LAN8742A PHY,
 *        raw frame（EtherType 0x88A4）,不經 LwIP。
 *
 * 快取一致性策略（規劃 §8 / D-cache 地雷）：
 *   DMA 描述符 + 收發緩衝全放 SRAM2（0x2004C000, 16KB）,由 MPU 設為
 *   non-cacheable —— 收發路徑零 cache 維護操作。SOEM 的 txbuf 在一般 RAM,
 *   send 時 memcpy 進 SRAM2 staging 再啟動 DMA（14 軸 LRW ~100B,拷貝成本可忽略）。
 *
 * 硬體注意（影響腳位）：RMII 腳位 = Nucleo-144 佈線
 *   PA1 REF_CLK / PA2 MDIO / PC1 MDC / PA7 CRS_DV / PC4 RXD0 / PC5 RXD1 /
 *   PG11 TX_EN / PG13 TXD0 / PB13 TXD1
 */
#include "oshw.h"
#include "stm32f7xx_hal.h"
#include <string.h>

/* ---- LAN8742A PHY（Nucleo 板載,PHY addr 0;LAN_ 前綴避開 hal_conf 撞名） ---- */
#define LAN_ADDR         0x00U
#define LAN_BCR          0x00U       /* Basic Control */
#define LAN_BSR          0x01U       /* Basic Status */
#define LAN_SSR          0x1FU       /* Special Status（速率/雙工） */
#define LAN_BCR_RESET    0x8000U
#define LAN_BCR_ANEG_EN  0x1000U
#define LAN_BSR_LINK_UP  0x0004U
#define LAN_BSR_ANEG_OK  0x0020U
#define LAN_SSR_SPEEDMSK 0x001CU     /* [4:2] 001=10H 101=10F 010=100H 110=100F */

#define RX_BUF_CNT   ETH_RX_DESC_CNT             /* 4 */
#define RX_BUF_SIZE  1536U

/* ---- SRAM2 non-cacheable 區（linker: .EthNoCache @0x2004C000） ---- */
static ETH_DMADescTypeDef s_tx_desc[ETH_TX_DESC_CNT] __attribute__((section(".EthNoCache"), aligned(32)));
static ETH_DMADescTypeDef s_rx_desc[ETH_RX_DESC_CNT] __attribute__((section(".EthNoCache"), aligned(32)));
static uint8_t s_rx_pool[RX_BUF_CNT][RX_BUF_SIZE] __attribute__((section(".EthNoCache"), aligned(32)));
static uint8_t s_tx_stage[EC_BUFSIZE] __attribute__((section(".EthNoCache"), aligned(32)));

static ETH_HandleTypeDef s_heth;
static ETH_TxPacketConfig s_txcfg;
static uint8_t s_rx_busy[RX_BUF_CNT];            /* HAL 持有中的 rx 緩衝標記 */

/* 收到的框（RxLink callback 填,oshw_mac_recv 取走） */
static uint32_t s_rx_frame_len;
static uint8_t *s_rx_frame_buf;                  /* 歸還 pool 用 */

extern void osal_dwt_init(void);

/* ================= HAL callbacks（新版 ETH HAL 收包配置鏈） ================= */

/** HAL 要一塊 rx 緩衝。 */
void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    for (int i = 0; i < RX_BUF_CNT; i++) {
        if (!s_rx_busy[i]) {
            s_rx_busy[i] = 1;
            *buff = s_rx_pool[i];
            return;
        }
    }
    *buff = NULL;                                /* pool 耗盡：HAL 會丟框 */
}

/** HAL 交付一個完整框（單描述符;EtherCAT 框 ≤1518 不跨描述符）。
 *  HAL_ETH_ReadData 以 *p_start 為交付把手,這裡填緩衝位址、長度記在旁路。 */
void HAL_ETH_RxLinkCallback(void **p_start, void **p_end, uint8_t *buff, uint16_t len)
{
    *p_start = buff;
    *p_end = buff;
    s_rx_frame_buf = buff;
    s_rx_frame_len = len;
}

static void rx_buf_release(uint8_t *buff)
{
    for (int i = 0; i < RX_BUF_CNT; i++) {
        if (s_rx_pool[i] == buff) {
            s_rx_busy[i] = 0;
            return;
        }
    }
}

/* ================= MSP：時脈 + RMII 腳位 ================= */

void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)
{
    GPIO_InitTypeDef g = {0};
    (void)heth;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_ETH_CLK_ENABLE();

    /* SYSCFG：RMII 模式（F7 直接寫 PMC;動時脈/周邊設定,文件須標註） */
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    SYSCFG->PMC |= SYSCFG_PMC_MII_RMII_SEL;

    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF11_ETH;

    g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;          /* PA1 PA2 PA7 */
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_13;                                   /* PB13 */
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;          /* PC1 PC4 PC5 */
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_11 | GPIO_PIN_13;                     /* PG11 PG13 */
    HAL_GPIO_Init(GPIOG, &g);
}

/* ================= MPU：SRAM2 non-cacheable ================= */

static void mpu_eth_region(void)
{
    MPU_Region_InitTypeDef r = {0};

    HAL_MPU_Disable();
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER7;               /* 避開既有 region（若有,佔 0-6） */
    r.BaseAddress = 0x2004C000;
    r.Size = MPU_REGION_SIZE_16KB;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.IsBufferable = MPU_ACCESS_BUFFERABLE;
    r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;    /* 關鍵：DMA 區不進 D-cache */
    r.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.TypeExtField = MPU_TEX_LEVEL1;
    r.SubRegionDisable = 0x00;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* ================= oshw_mac_* ================= */

int oshw_mac_init(const uint8_t *mac_address)
{
    static uint8_t mac[6];
    memcpy(mac, mac_address, 6);

    osal_dwt_init();
    mpu_eth_region();
    memset(s_rx_busy, 0, sizeof(s_rx_busy));

    s_heth.Instance = ETH;
    s_heth.Init.MACAddr = mac;
    s_heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    s_heth.Init.TxDesc = s_tx_desc;
    s_heth.Init.RxDesc = s_rx_desc;
    s_heth.Init.RxBuffLen = RX_BUF_SIZE;
    if (HAL_ETH_Init(&s_heth) != HAL_OK)
        return -1;

    memset(&s_txcfg, 0, sizeof(s_txcfg));
    s_txcfg.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
    s_txcfg.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    /* PHY：reset → 自動協商 → 取速率/雙工 */
    uint32_t v = 0;
    HAL_ETH_WritePHYRegister(&s_heth, LAN_ADDR, LAN_BCR, LAN_BCR_RESET);
    HAL_Delay(50);
    HAL_ETH_WritePHYRegister(&s_heth, LAN_ADDR, LAN_BCR, LAN_BCR_ANEG_EN);

    uint32_t t0 = HAL_GetTick();
    do {
        HAL_ETH_ReadPHYRegister(&s_heth, LAN_ADDR, LAN_BSR, &v);
        if ((v & (LAN_BSR_LINK_UP | LAN_BSR_ANEG_OK)) == (LAN_BSR_LINK_UP | LAN_BSR_ANEG_OK))
            break;
    } while (HAL_GetTick() - t0 < 5000);
    if ((v & LAN_BSR_LINK_UP) == 0)
        return -2;                               /* 網線未接（≒真機 E403） */

    ETH_MACConfigTypeDef mc;
    HAL_ETH_GetMACConfig(&s_heth, &mc);
    HAL_ETH_ReadPHYRegister(&s_heth, LAN_ADDR, LAN_SSR, &v);
    switch ((v & LAN_SSR_SPEEDMSK) >> 2) {
        case 1: mc.Speed = ETH_SPEED_10M;  mc.DuplexMode = ETH_HALFDUPLEX_MODE; break;
        case 5: mc.Speed = ETH_SPEED_10M;  mc.DuplexMode = ETH_FULLDUPLEX_MODE; break;
        case 2: mc.Speed = ETH_SPEED_100M; mc.DuplexMode = ETH_HALFDUPLEX_MODE; break;
        default: mc.Speed = ETH_SPEED_100M; mc.DuplexMode = ETH_FULLDUPLEX_MODE; break;
    }
    HAL_ETH_SetMACConfig(&s_heth, &mc);

    /* 混雜模式：EtherCAT 回框 dst 仍為廣播/主站 MAC,全收下 */
    ETH_MACFilterConfigTypeDef fc;
    HAL_ETH_GetMACFilterConfig(&s_heth, &fc);
    fc.PromiscuousMode = ENABLE;
    HAL_ETH_SetMACFilterConfig(&s_heth, &fc);

    if (HAL_ETH_Start(&s_heth) != HAL_OK)        /* 輪詢模式,不用中斷 */
        return -3;
    return 0;
}

int oshw_mac_send(const void *payload, size_t tot_len)
{
    if (tot_len > sizeof(s_tx_stage))
        return -1;
    memcpy(s_tx_stage, payload, tot_len);        /* 進 non-cacheable staging */

    ETH_BufferTypeDef buf = {0};
    buf.buffer = s_tx_stage;
    buf.len = tot_len;
    buf.next = NULL;
    s_txcfg.Length = tot_len;
    s_txcfg.TxBuffer = &buf;

    if (HAL_ETH_Transmit(&s_heth, &s_txcfg, 20) != HAL_OK)   /* ms,阻塞至 DMA 取走 */
        return -1;
    return (int)tot_len;
}

int oshw_mac_recv(void *buffer, size_t buffer_length)
{
    void *frame = NULL;
    s_rx_frame_buf = NULL;
    if (HAL_ETH_ReadData(&s_heth, &frame) != HAL_OK || frame == NULL)
        return 0;                                /* 無框 */

    uint32_t n = s_rx_frame_len;
    if (n > buffer_length)
        n = buffer_length;
    memcpy(buffer, frame, n);
    if (s_rx_frame_buf)
        rx_buf_release(s_rx_frame_buf);
    return (int)n;
}

int oshw_mac_link_up(void)
{
    uint32_t v = 0;
    HAL_ETH_ReadPHYRegister(&s_heth, LAN_ADDR, LAN_BSR, &v);
    return (v & LAN_BSR_LINK_UP) ? 1 : 0;
}

/* ================= byte order / adapters ================= */

uint16 oshw_htons(uint16 host)
{
    return (uint16)((host << 8) | (host >> 8));  /* Cortex-M little-endian */
}

uint16 oshw_ntohs(uint16 network)
{
    return oshw_htons(network);
}

ec_adaptert *oshw_find_adapters(void)
{
    return NULL;                                 /* 板上唯一介面,無列舉 */
}

void oshw_free_adapters(ec_adaptert *adapter)
{
    (void)adapter;
}
