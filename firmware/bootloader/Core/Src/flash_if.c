/* flash_if.c 片内 Flash 擦写与 CRC */
#include "flash_if.h"
#include "main.h"
#include "bsp_uart.h"

typedef struct {
    uint32_t start;
    uint32_t size;
} sector_t;

static const sector_t k_sectors[8] = {
    {0x08000000UL, 16UL * 1024UL},   /* Sector 0 */
    {0x08004000UL, 16UL * 1024UL},   /* Sector 1 */
    {0x08008000UL, 16UL * 1024UL},   /* Sector 2 */
    {0x0800C000UL, 16UL * 1024UL},   /* Sector 3 */
    {0x08010000UL, 64UL * 1024UL},   /* Sector 4 */
    {0x08020000UL, 128UL * 1024UL},  /* Sector 5 */
    {0x08040000UL, 128UL * 1024UL},  /* Sector 6 */
    {0x08060000UL, 128UL * 1024UL},  /* Sector 7 */
};

uint8_t flash_if_sector_of(uint32_t addr)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        if (addr >= k_sectors[i].start &&
            addr < k_sectors[i].start + k_sectors[i].size) {
            return i;
        }
    }
    return 0xFFU;
}

/* 一次擦多个扇区会整体阻塞 喂不了狗 所以一个一个来 */
uint8_t flash_if_erase(uint32_t addr, uint32_t size)
{
    FLASH_EraseInitTypeDef er = {0};
    uint32_t sector_error = 0U;
    uint32_t end = addr + size;
    uint8_t  first;
    uint8_t  last;

    if ((size == 0U) || (end <= addr)) {
        return 1U;
    }
    first = flash_if_sector_of(addr);
    last  = flash_if_sector_of(end - 1U);
    if ((first == 0xFFU) || (last == 0xFFU) || (last < first)) {
        return 2U;
    }

    /* 擦除范围别碰 Sector 0/1 那是 Bootloader */
    if (first <= 1U) {
        return 3U;
    }

    HAL_FLASH_Unlock();

    for (uint8_t s = first; s <= last; s++) {
        er.TypeErase    = FLASH_TYPEERASE_SECTORS;
        er.Banks        = FLASH_BANK_1;
        er.Sector       = s;
        er.NbSectors    = 1U;
        er.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        if (HAL_FLASHEx_Erase(&er, &sector_error) != HAL_OK) {
            HAL_FLASH_Lock();
            bsp_uart_printf("[flash] erase sector %u failed\r\n", s);
            return 4U;
        }
        HAL_IWDG_Refresh(&hiwdg);
    }

    HAL_FLASH_Lock();
    return 0U;
}

uint8_t flash_if_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if ((len == 0U) || ((addr & 0x3U) != 0U) || ((len & 0x3U) != 0U)) {
        return 1U;                                  /* 必须 4 字节对齐 */
    }

    HAL_FLASH_Unlock();
    for (i = 0U; i < len; i += 4U) {
        uint32_t word = (uint32_t)data[i]
                      | ((uint32_t)data[i + 1U] << 8)
                      | ((uint32_t)data[i + 2U] << 16)
                      | ((uint32_t)data[i + 3U] << 24);
        /* 只能按 32 位字写 写之前一定先擦 */
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 2U;
        }
        if ((i & 0x3FFU) == 0U) {
            HAL_IWDG_Refresh(&hiwdg);
        }
    }
    HAL_FLASH_Lock();

    /* 写完回读比对 一致才算成功 */
    for (i = 0U; i < len; i++) {
        if (*(volatile uint8_t *)(addr + i) != data[i]) {
            return 3U;
        }
    }
    return 0U;
}

uint32_t flash_if_crc32(uint32_t addr, uint32_t len)
{
    uint32_t i;

    __HAL_RCC_CRC_CLK_ENABLE();
    CRC->CR = CRC_CR_RESET;

    len &= ~0x3UL;
    for (i = 0U; i < len; i += 4U) {
        CRC->DR = *(volatile uint32_t *)(addr + i);
    }
    return CRC->DR;
}

uint8_t flash_if_is_erased(uint32_t addr, uint32_t len)
{
    for (uint32_t i = 0U; i < len; i++) {
        if (*(volatile uint8_t *)(addr + i) != 0xFFU) {
            return 0U;
        }
    }
    return 1U;
}
