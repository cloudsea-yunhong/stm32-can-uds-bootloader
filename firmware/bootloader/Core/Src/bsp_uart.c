/* bsp_uart.c 调试串口输出 */
#include "bsp_uart.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;

void bsp_uart_init(void)
{
}

void bsp_uart_printf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0) {
        return;
    }
    if (len > (int)sizeof(buf) - 1) {
        len = (int)sizeof(buf) - 1;
    }
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 100U);
}
