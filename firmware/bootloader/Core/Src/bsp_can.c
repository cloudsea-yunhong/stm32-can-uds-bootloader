/* bsp_can.c CAN1 驱动 */
#include "bsp_can.h"
#include "project_config.h"
#include "bsp_uart.h"

extern CAN_HandleTypeDef hcan1;

#define CAN_RX_RING_SIZE        32U

static can_frame_t s_ring[CAN_RX_RING_SIZE];
static volatile uint8_t s_head;
static volatile uint8_t s_tail;
static volatile uint32_t s_busoff_count;
static volatile uint8_t s_busoff_flag;

void bsp_can_init(void)
{
    CAN_FilterTypeDef filter = {0};

    hcan1.Instance                  = CAN1;
    hcan1.Init.Prescaler            = 6U;
    hcan1.Init.TimeSeg1             = CAN_BS1_11TQ;
    hcan1.Init.TimeSeg2             = CAN_BS2_2TQ;
    hcan1.Init.SyncJumpWidth        = CAN_SJW_1TQ;
    hcan1.Init.TimeTriggeredMode    = DISABLE;
    hcan1.Init.AutoBusOff           = DISABLE;
    hcan1.Init.AutoWakeUp           = DISABLE;
    hcan1.Init.AutoRetransmission   = ENABLE;
    hcan1.Init.ReceiveFifoLocked    = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;
    hcan1.Init.Mode                 = CAN_MODE_NORMAL;
    if (HAL_CAN_Init(&hcan1) != HAL_OK) {
        bsp_uart_printf("[can] init failed\r\n");
        Error_Handler();
    }

    filter.FilterBank           = 0;
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = (uint16_t)((UDS_REQ_ID << 5) & 0xFFFFU);
    filter.FilterIdLow          = 0x0000U;
    filter.FilterMaskIdHigh     = (uint16_t)((0x7FFU << 5) & 0xFFFFU);
    filter.FilterMaskIdLow      = 0x0000U;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation     = ENABLE;
    filter.SlaveStartFilterBank = 14U;
    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING
                                          | CAN_IT_BUSOFF
                                          | CAN_IT_ERROR_PASSIVE) != HAL_OK) {
        Error_Handler();
    }

    s_head = s_tail = 0U;
    s_busoff_count = 0U;
    s_busoff_flag = 0U;
    bsp_uart_printf("[can] 500 kbps, filter 0x%03lX\r\n", (unsigned long)UDS_REQ_ID);
}

uint8_t bsp_can_send(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef tx = {0};
    uint32_t mailbox;
    uint32_t start = HAL_GetTick();

    if (dlc > 8U) {
        return 1U;
    }

    tx.StdId              = id;
    tx.ExtId              = 0U;
    tx.IDE                = CAN_ID_STD;
    tx.RTR                = CAN_RTR_DATA;
    tx.DLC                = dlc;
    tx.TransmitGlobalTime = DISABLE;

    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U) {
        if ((HAL_GetTick() - start) > 50U) {
            return 2U;
        }
    }
    if (HAL_CAN_AddTxMessage(&hcan1, &tx, (uint8_t *)data, &mailbox) != HAL_OK) {
        return 3U;
    }
    return 0U;
}

uint8_t bsp_can_recv(can_frame_t *frame, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (1) {
        if (s_tail != s_head) {
            *frame = s_ring[s_tail];
            s_tail = (uint8_t)((s_tail + 1U) % CAN_RX_RING_SIZE);
            return 0U;
        }
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return 1U;
        }
    }
}

uint32_t bsp_can_busoff_count(void)
{
    return s_busoff_count;
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx;
    uint8_t data[8];
    uint8_t next;

    if (hcan->Instance != CAN1) {
        return;
    }
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx, data) != HAL_OK) {
        return;
    }
    if (rx.IDE != CAN_ID_STD || rx.RTR != CAN_RTR_DATA) {
        return;
    }

    next = (uint8_t)((s_head + 1U) % CAN_RX_RING_SIZE);
    if (next == s_tail) {
        /* 队列满就丢最旧帧 ISR 不能阻塞 */
        s_tail = (uint8_t)((s_tail + 1U) % CAN_RX_RING_SIZE);
    }
    s_ring[s_head].id  = rx.StdId;
    s_ring[s_head].dlc = (uint8_t)rx.DLC;
    for (uint8_t i = 0U; i < 8U; i++) {
        s_ring[s_head].data[i] = data[i];
    }
    s_head = next;
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance != CAN1) {
        return;
    }
    if ((hcan->ErrorCode & HAL_CAN_ERROR_BOF) != 0U) {
        s_busoff_count++;
        s_busoff_flag = 1U;
    }
}

/* bus-off 恢复放主循环 手册推荐重新初始化 这里用 Start/Stop 省事
   别一检测到就狂重启 加 100 ms 间隔 */
void bsp_can_poll_recovery(void)
{
    static uint32_t last_ms = 0U;
    uint32_t now;

    if (s_busoff_flag == 0U) {
        return;
    }
    now = HAL_GetTick();
    if ((now - last_ms) < 100U) {
        return;
    }
    last_ms = now;
    s_busoff_flag = 0U;

    (void)HAL_CAN_Stop(&hcan1);
    (void)HAL_CAN_Start(&hcan1);
    (void)HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING
                                            | CAN_IT_BUSOFF
                                            | CAN_IT_ERROR_PASSIVE);
    bsp_uart_printf("[can] bus-off recovered (%lu)\r\n",
                    (unsigned long)s_busoff_count);
}
