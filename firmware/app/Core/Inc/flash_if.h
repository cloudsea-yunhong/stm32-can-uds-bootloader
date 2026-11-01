/* flash_if.h 片内 Flash 擦除、字编程、硬件 CRC32 */
#ifndef __FLASH_IF_H
#define __FLASH_IF_H

#include <stdint.h>

uint8_t  flash_if_erase(uint32_t addr, uint32_t size);
uint8_t  flash_if_write(uint32_t addr, const uint8_t *data, uint32_t len);
uint32_t flash_if_crc32(uint32_t addr, uint32_t len);
uint8_t  flash_if_is_erased(uint32_t addr, uint32_t len);
uint8_t  flash_if_sector_of(uint32_t addr);

#endif
