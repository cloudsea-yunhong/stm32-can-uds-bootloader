/* boot.c 启动决策与跳转 */
#include "boot.h"
#include "main.h"
#include "project_config.h"
#include "params.h"
#include "flash_if.h"
#include "bsp_can.h"
#include "bsp_uart.h"

#define BKP_MAGIC               0x55445355UL
#define BKP_REG                 RTC->BKP0R

extern CAN_HandleTypeDef hcan1;
extern IWDG_HandleTypeDef hiwdg;

static void bkp_prepare(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
}

void boot_request_upgrade(void)
{
    bkp_prepare();
    BKP_REG = BKP_MAGIC;
}

uint8_t boot_upgrade_requested(void)
{
    bkp_prepare();
    return (BKP_REG == BKP_MAGIC) ? 1U : 0U;
}

void boot_clear_upgrade_request(void)
{
    bkp_prepare();
    BKP_REG = 0U;
}

void boot_init(void)
{
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER;
    hiwdg.Init.Reload    = IWDG_RELOAD_VALUE;
    (void)HAL_IWDG_Init(&hiwdg);
}

void boot_feed_watchdog(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}

static uint8_t sp_is_valid(uint32_t sp)
{
    if ((sp > 0x20000000UL) && (sp <= 0x20020000UL)) {
        return 1U;
    }
    if ((sp > 0x10000000UL) && (sp <= 0x10010000UL)) {
        return 1U;
    }
    return 0U;
}

uint8_t boot_app_header_valid(uint32_t app_addr, uint32_t app_size)
{
    uint32_t sp = *(volatile uint32_t *)app_addr;
    uint32_t pc = *(volatile uint32_t *)(app_addr + 4U);

    if (!sp_is_valid(sp)) {
        return 0U;
    }
    if (((pc & 0x1U) == 0U) || (pc < app_addr) || (pc >= (app_addr + app_size))) {
        return 0U;
    }
    return 1U;
}

typedef void (*app_entry_t)(void);

uint8_t boot_jump_to_app(uint32_t app_addr, uint32_t app_size)
{
    uint32_t sp;
    uint32_t pc;

    if (!boot_app_header_valid(app_addr, app_size)) {
        return 1U;
    }
    sp = *(volatile uint32_t *)app_addr;
    pc = *(volatile uint32_t *)(app_addr + 4U);

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    for (uint32_t i = 0U; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    HAL_CAN_DeInit(&hcan1);                 /* CAN 停在 bus-off 时 App 收不到帧 */
    __HAL_RCC_CAN1_CLK_DISABLE();
    __HAL_RCC_GPIOB_CLK_DISABLE();

    SCB->CPACR |= (0xFUL << 20);
    __DSB();
    __ISB();

    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_RESET();
    __DSB();
    __ISB();

    SCB->VTOR = app_addr;                   /* 向量表必须指向 App */
    __DSB();
    __ISB();

    __set_MSP(sp);
    ((app_entry_t)pc)();

    return 2U;
}

/* 启动前把整片固件 CRC 一遍 192 KB 用硬件不到 1 ms 比只看头部靠谱
   参数区里 app_crc32 为 0 说明没登记过 这时跳过 */
static uint8_t app_crc_ok(const BootParam *p, uint32_t app_addr, uint32_t app_size)
{
    if ((p->app_crc32 == 0U) || (p->app_size == 0U) || (p->app_size > app_size)) {
        return 1U;
    }
    return (flash_if_crc32(app_addr, p->app_size) == p->app_crc32) ? 1U : 0U;
}

uint8_t boot_decide_and_jump(void)
{
    BootParam p;
    uint32_t addr;
    uint32_t size;
    uint8_t  other;

    params_get(&p);

    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET) {
        bsp_uart_printf("[boot] KEY_BOOT pressed\r\n");
        return BOOT_STAY_IN_BL_KEY;
    }

    if (boot_upgrade_requested()) {
        boot_clear_upgrade_request();
        bsp_uart_printf("[boot] upgrade requested by App\r\n");
        return BOOT_STAY_IN_BL_UPGRADE;
    }

    if (p.magic != PARAM_MAGIC) {
        return BOOT_STAY_IN_BL;
    }

    addr = (p.active_slot == 0U) ? APP_A_ADDR : APP_B_ADDR;
    size = (p.active_slot == 0U) ? APP_A_SIZE : APP_B_SIZE;
    if (!boot_app_header_valid(addr, size)) {
        other = (uint8_t)(1U - p.active_slot);
        addr = (other == 0U) ? APP_A_ADDR : APP_B_ADDR;
        size = (other == 0U) ? APP_A_SIZE : APP_B_SIZE;
        if (!boot_app_header_valid(addr, size)) {
            bsp_uart_printf("[boot] no valid app\r\n");
            return BOOT_STAY_IN_BL_BLANK;
        }
        p.active_slot = other;
        if (params_set(&p) != 0U) {
            return BOOT_STAY_IN_BL;
        }
    }

    if (p.slot_valid[p.active_slot] == SLOT_PENDING) {
        p.boot_count++;
        if (p.boot_count >= MAX_BOOT_COUNT) {
            other = (uint8_t)(1U - p.active_slot);
            uint32_t o_addr = (other == 0U) ? APP_A_ADDR : APP_B_ADDR;
            uint32_t o_size = (other == 0U) ? APP_A_SIZE : APP_B_SIZE;
            if (boot_app_header_valid(o_addr, o_size)) {
                p.active_slot = other;
                p.boot_count  = 0U;
                (void)params_set(&p);
                bsp_uart_printf("[boot] rollback to slot %c\r\n",
                                (other == 0U) ? 'A' : 'B');
                addr = o_addr;
                size = o_size;
            } else {
                bsp_uart_printf("[boot] both slots invalid\r\n");
                return BOOT_STAY_IN_BL_BLANK;
            }
        } else {
            (void)params_set(&p);
            bsp_uart_printf("[boot] boot_count=%lu (pending)\r\n",
                            (unsigned long)p.boot_count);
        }
    }

    if (!app_crc_ok(&p, addr, size)) {
        other = (uint8_t)(1U - p.active_slot);
        uint32_t o_addr = (other == 0U) ? APP_A_ADDR : APP_B_ADDR;
        uint32_t o_size = (other == 0U) ? APP_A_SIZE : APP_B_SIZE;
        bsp_uart_printf("[boot] CRC mismatch on slot %c\r\n",
                        (p.active_slot == 0U) ? 'A' : 'B');
        if (boot_app_header_valid(o_addr, o_size) &&
            app_crc_ok(&p, o_addr, o_size)) {
            bsp_uart_printf("[boot] fallback to slot %c\r\n",
                            (other == 0U) ? 'A' : 'B');
            addr = o_addr;
            size = o_size;
        } else {
            return BOOT_STAY_IN_BL;
        }
    }

    bsp_uart_printf("[boot] jump to 0x%08lX\r\n", (unsigned long)addr);
    (void)boot_jump_to_app(addr, size);

    return BOOT_STAY_IN_BL;
}
