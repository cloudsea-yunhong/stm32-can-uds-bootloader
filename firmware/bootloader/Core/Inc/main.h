/* main.h Bootloader 公共声明 */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

#define DBG_UART                huart1
#define DBG_BAUDRATE            115200U

extern UART_HandleTypeDef huart1;
extern CAN_HandleTypeDef  hcan1;
extern IWDG_HandleTypeDef hiwdg;

void Error_Handler(void);
void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#endif
