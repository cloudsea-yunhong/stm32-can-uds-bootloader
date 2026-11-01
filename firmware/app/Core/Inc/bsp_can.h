/* bsp_can.h CAN1 底层 初始化、收发、bus-off 恢复 */
#ifndef __BSP_CAN_H
#define __BSP_CAN_H

#include <stdint.h>
#include "main.h"

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} can_frame_t;

void bsp_can_init(void);
uint8_t bsp_can_send(uint32_t id, const uint8_t *data, uint8_t dlc);
uint8_t bsp_can_recv(can_frame_t *frame, uint32_t timeout_ms);
uint32_t bsp_can_busoff_count(void);
void bsp_can_poll_recovery(void);

#endif /* __BSP_CAN_H */
