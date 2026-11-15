/* bsp_uart.h 调试串口 阻塞发送够用 */
#ifndef __BSP_UART_H
#define __BSP_UART_H

#include <stdarg.h>
#include "main.h"

void bsp_uart_init(void);
void bsp_uart_printf(const char *fmt, ...);

#endif /* __BSP_UART_H */
