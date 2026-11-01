/* params.c 参数区读写 双 slot 取 seq 最大 */
#include "params.h"
#include "project_config.h"
#include "flash_if.h"
#include "main.h"
#include "bsp_uart.h"
#include <string.h>

static BootParam s_current;
static uint8_t   s_current_slot;

static uint32_t crc_of_param(const BootParam *p)
{
    BootParam tmp = *p;
    tmp.struct_crc = 0U;
    return flash_if_crc32((uint32_t)(uintptr_t)&tmp, sizeof(tmp));
}

uint32_t params_uid_hash(void)
{
    const volatile uint32_t *uid = (const volatile uint32_t *)0x1FFF7A10UL;
    return uid[0] ^ uid[1] ^ uid[2];
}

static uint8_t param_is_valid(const BootParam *p)
{
    if (p->magic != PARAM_MAGIC) {
        return 0U;
    }
    return (crc_of_param(p) == p->struct_crc) ? 1U : 0U;
}

static uint8_t slot_addr(uint8_t slot, uint32_t *addr)
{
    if (slot == 0U) {
        *addr = PARAM_A_ADDR;
        return 0U;
    }
    if (slot == 1U) {
        *addr = PARAM_B_ADDR;
        return 0U;
    }
    return 1U;
}

void params_default(BootParam *out)
{
    memset(out, 0, sizeof(*out));
    out->magic       = PARAM_MAGIC;
    out->seq         = 1U;
    out->active_slot = 0U;
    out->app_base    = APP_A_ADDR;
    out->slot_valid[0] = SLOT_CONFIRMED;
    out->slot_valid[1] = SLOT_INVALID;
    out->uid_hash    = params_uid_hash();
    out->version     = 0x01000000UL;
    out->struct_crc  = 0U;
    out->struct_crc  = crc_of_param(out);
}

void params_init(void)
{
    BootParam a, b;
    uint8_t a_ok, b_ok;

    memcpy(&a, (const void *)PARAM_A_ADDR, sizeof(a));
    memcpy(&b, (const void *)PARAM_B_ADDR, sizeof(b));

    a_ok = param_is_valid(&a) && (a.uid_hash == params_uid_hash());
    b_ok = param_is_valid(&b) && (b.uid_hash == params_uid_hash());

    if (a_ok && b_ok) {
        if (a.seq >= b.seq) {
            s_current = a;
            s_current_slot = 0U;
        } else {
            s_current = b;
            s_current_slot = 1U;
        }
    } else if (a_ok) {
        s_current = a;
        s_current_slot = 0U;
    } else if (b_ok) {
        s_current = b;
        s_current_slot = 1U;
    } else {
        params_default(&s_current);
        s_current_slot = 0U;
        (void)params_set(&s_current);
        bsp_uart_printf("[param] no valid slot, init default\r\n");
        return;
    }
    bsp_uart_printf("[param] slot %c seq %lu active %lu\r\n",
                    (s_current_slot == 0U) ? 'A' : 'B',
                    (unsigned long)s_current.seq,
                    (unsigned long)s_current.active_slot);
}

uint8_t params_get(BootParam *out)
{
    *out = s_current;
    return 0U;
}

uint8_t params_set(const BootParam *in)
{
    BootParam p = *in;
    uint8_t target_slot = (uint8_t)(1U - s_current_slot);
    uint32_t addr;
    uint8_t *raw = (uint8_t *)&p;

    if (slot_addr(target_slot, &addr) != 0U) {
        return 1U;
    }

    p.seq        = s_current.seq + 1U;
    p.uid_hash   = params_uid_hash();
    p.struct_crc = 0U;
    p.struct_crc = crc_of_param(&p);

    if (flash_if_erase(addr, PARAM_SECTOR_SIZE) != 0U) {
        return 2U;
    }

    if (flash_if_write(addr + 4U, raw + 4U, 56U) != 0U) {
        return 3U;
    }
    if (flash_if_write(addr, raw, 4U) != 0U) {
        return 4U;
    }
    if (flash_if_write(addr + 60U, raw + 60U, 4U) != 0U) {
        return 5U;
    }

    /* 回读确认 */
    {
        BootParam chk;
        memcpy(&chk, (const void *)addr, sizeof(chk));
        if (!param_is_valid(&chk)) {
            return 6U;
        }
    }

    s_current = p;
    s_current_slot = target_slot;
    return 0U;
}
