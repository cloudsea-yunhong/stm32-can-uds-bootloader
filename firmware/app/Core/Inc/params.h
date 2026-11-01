/* params.h 参数区 双 slot 交替写 magic 后置 掉电安全 */
#ifndef __PARAMS_H
#define __PARAMS_H

#include <stdint.h>

#define PARAM_WORDS             16U
#define PARAM_ERASED            0xFFFFFFFFUL

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t active_slot;
    uint32_t boot_count;
    uint32_t app_size;          /* 固件字节数（4 字节对齐） */
    uint32_t app_crc32;         /* 硬件 CRC32 */
    uint32_t app_base;
    uint32_t version;
    uint32_t slot_valid[2];
    uint32_t uid_hash;
    uint32_t reserved[4];
    uint32_t struct_crc;
} BootParam;

void     params_init(void);
uint8_t  params_get(BootParam *out);
uint8_t  params_set(const BootParam *in);
uint32_t params_uid_hash(void);
void     params_default(BootParam *out);

#endif
