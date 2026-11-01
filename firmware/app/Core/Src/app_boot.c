/* app_boot.c BKP0R 交接标志 App 复位前写 Bootloader 读到就留在刷写模式 */
#include "app_boot.h"
#include "main.h"

#define BKP_MAGIC   0x55445355UL
#define BKP_REG     RTC->BKP0R

static void bkp_prepare(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
}

void app_request_upgrade(void)
{
    bkp_prepare();
    BKP_REG = BKP_MAGIC;
}

void app_clear_upgrade(void)
{
    bkp_prepare();
    BKP_REG = 0U;
}

uint8_t app_upgrade_requested(void)
{
    bkp_prepare();
    return (BKP_REG == BKP_MAGIC) ? 1U : 0U;
}
