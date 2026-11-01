/* main.h App 公共声明 */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

#ifdef APP_B_BUILD
#define APP_START_ADDR          0x08040000UL
#else
#define APP_START_ADDR          0x08010000UL
#endif
#define DBG_BAUDRATE            115200U

extern UART_HandleTypeDef huart1;
extern CAN_HandleTypeDef  hcan1;
extern IWDG_HandleTypeDef hiwdg;

void Error_Handler(void);
void SystemClock_Config(void);
uint8_t app_confirm_boot(void);

#ifdef __cplusplus
}
#endif

#endif

