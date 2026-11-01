/* boot.h 启动决策、跳转、看门狗、BKP 交接 */
#ifndef __BOOT_H
#define __BOOT_H

#include <stdint.h>

#define BOOT_STAY_IN_BL         1U
#define BOOT_STAY_IN_BL_KEY     2U
#define BOOT_STAY_IN_BL_BLANK   3U
#define BOOT_STAY_IN_BL_UPGRADE 4U

void    boot_init(void);
void    boot_feed_watchdog(void);
uint8_t boot_decide_and_jump(void);
uint8_t boot_jump_to_app(uint32_t app_addr, uint32_t app_size);
uint8_t boot_app_header_valid(uint32_t app_addr, uint32_t app_size);

void     boot_request_upgrade(void);
uint8_t  boot_upgrade_requested(void);
void     boot_clear_upgrade_request(void);

#endif
