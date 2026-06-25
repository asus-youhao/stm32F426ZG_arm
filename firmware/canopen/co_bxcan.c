/**
 * @file    co_bxcan.c
 * @brief   STM32F746 bxCAN 硬體層實作（CAN1=左臂, CAN2=右臂, 1 Mbps）
 *
 * 依賴 STM32 HAL（stm32f7xx_hal_can.h）。CAN_HandleTypeDef 由 CubeMX 產生
 * （hcan1 / hcan2）。本檔以弱連結方式取得控制代碼,請依專案調整。
 */
#include "co_bxcan.h"
#include "stm32f7xx_hal.h"

/* CubeMX 產生的控制代碼（於 main.c）。 */
extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

static CAN_HandleTypeDef *handle_of(co_bus_t bus)
{
    switch (bus) {
        case CO_BUS_LEFT:  return &hcan1;
        case CO_BUS_RIGHT: return &hcan2;
        default:           return 0;
    }
}

/* ---- 接收環形佇列（每 channel 一份）---- */
#define RXQ_LEN 32
typedef struct {
    co_frame_t buf[RXQ_LEN];
    volatile uint16_t head, tail;
} rxq_t;
static rxq_t s_rxq[CO_BUS_COUNT];

static bool rxq_push(co_bus_t bus, const co_frame_t *f)
{
    rxq_t *q = &s_rxq[bus];
    uint16_t n = (uint16_t)((q->head + 1u) % RXQ_LEN);
    if (n == q->tail) return false;     /* 滿,丟棄 */
    q->buf[q->head] = *f;
    q->head = n;
    return true;
}
static bool rxq_pop(co_bus_t bus, co_frame_t *out)
{
    rxq_t *q = &s_rxq[bus];
    if (q->tail == q->head) return false;
    *out = q->buf[q->tail];
    q->tail = (uint16_t)((q->tail + 1u) % RXQ_LEN);
    return true;
}

/*
 * 1 Mbps 位元時序。
 *   Tq       = Prescaler / APB1Clk
 *   bit time = (1 + BS1 + BS2) * Tq
 *   採樣點   = (1+BS1)/(1+BS1+BS2) = 7/9 ≈ 77.8%
 *
 * 本專案目標板 Nucleo-F746ZG：SYSCLK 216 MHz → APB1 = 54 MHz。
 *   取 Prescaler=6, BS1=6TQ, BS2=2TQ → 54M/6/9 = 1.0 Mbps
 * （原 45 MHz 範例用 Prescaler=5；換板/換時脈樹請依 APB1 重算。）
 * 參考：docs/design/firmware-cubemx-integration.md §3。
 */
co_status_t co_bxcan_init(co_bus_t bus)
{
    CAN_HandleTypeDef *h = handle_of(bus);
    if (!h) return CO_ERR_PARAM;

    h->Init.Prescaler = 6;   /* APB1 = 54 MHz → 1 Mbps */
    h->Init.Mode = CAN_MODE_NORMAL;
    h->Init.SyncJumpWidth = CAN_SJW_1TQ;
    h->Init.TimeSeg1 = CAN_BS1_6TQ;
    h->Init.TimeSeg2 = CAN_BS2_2TQ;
    h->Init.TimeTriggeredMode = DISABLE;
    h->Init.AutoBusOff = ENABLE;          /* bus-off 自動恢復 */
    h->Init.AutoWakeUp = DISABLE;
    h->Init.AutoRetransmission = ENABLE;
    h->Init.ReceiveFifoLocked = DISABLE;
    h->Init.TransmitFifoPriority = ENABLE;
    if (HAL_CAN_Init(h) != HAL_OK) return CO_ERR_STATE;

    /* 濾波器：接收所有標準 ID 到 FIFO0。
       CAN2 使用 bank 14..27,CAN1 使用 0..13。 */
    CAN_FilterTypeDef filt = {0};
    filt.FilterBank = (bus == CO_BUS_RIGHT) ? 14 : 0;
    filt.SlaveStartFilterBank = 14;
    filt.FilterMode = CAN_FILTERMODE_IDMASK;
    filt.FilterScale = CAN_FILTERSCALE_32BIT;
    filt.FilterIdHigh = 0x0000;
    filt.FilterIdLow = 0x0000;
    filt.FilterMaskIdHigh = 0x0000;       /* mask=0 → 全收 */
    filt.FilterMaskIdLow = 0x0000;
    filt.FilterFIFOAssignment = CAN_RX_FIFO0;
    filt.FilterActivation = ENABLE;
    if (HAL_CAN_ConfigFilter(h, &filt) != HAL_OK) return CO_ERR_STATE;

    if (HAL_CAN_Start(h) != HAL_OK) return CO_ERR_STATE;
    if (HAL_CAN_ActivateNotification(h, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
        return CO_ERR_STATE;

    return CO_OK;
}

co_status_t co_bxcan_send(co_bus_t bus, const co_frame_t *f)
{
    CAN_HandleTypeDef *h = handle_of(bus);
    if (!h || !f) return CO_ERR_PARAM;

    CAN_TxHeaderTypeDef tx = {0};
    tx.StdId = f->id;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = f->dlc;

    uint32_t mbox;
    if (HAL_CAN_GetTxMailboxesFreeLevel(h) == 0) return CO_ERR_TX;
    if (HAL_CAN_AddTxMessage(h, &tx, (uint8_t *)f->data, &mbox) != HAL_OK)
        return CO_ERR_TX;
    return CO_OK;
}

bool co_bxcan_recv(co_bus_t bus, co_frame_t *out)
{
    if (bus >= CO_BUS_COUNT || !out) return false;
    return rxq_pop(bus, out);
}

void co_bxcan_on_rx(co_bus_t bus, const co_frame_t *f)
{
    if (bus < CO_BUS_COUNT && f) (void)rxq_push(bus, f);
}

/*
 * 範例：在 main.c 的 HAL 回呼中分流到正確 channel：
 *
 * void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h){
 *     CAN_RxHeaderTypeDef rh; co_frame_t f;
 *     HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &rh, f.data);
 *     f.id = (uint16_t)rh.StdId; f.dlc = (uint8_t)rh.DLC;
 *     co_bxcan_on_rx(h->Instance==CAN1 ? CO_BUS_LEFT : CO_BUS_RIGHT, &f);
 * }
 */
