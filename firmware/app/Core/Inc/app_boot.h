/* app_boot.h App 与 Bootloader 的交接 BKP 标志 */
#ifndef __APP_BOOT_H
#define __APP_BOOT_H

#include <stdint.h>

void app_request_upgrade(void);
void app_clear_upgrade(void);
uint8_t app_upgrade_requested(void);

#endif
