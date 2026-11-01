/* isotp.c ISO-TP SF/FF/CF/FC、SN 回绕、流控 */
#include "isotp.h"
#include "project_config.h"
#include "bsp_can.h"
#include "main.h"
#include <string.h>

#define N_CR_MS                 1000U
#define N_BS_MS                 1000U

void isotp_init(void)
{
    /* 没有持久状态 先留空 */
}

static uint8_t send_frame(const uint8_t *data, uint8_t len)
{
    uint8_t buf[8];

    memset(buf, 0, sizeof(buf));
    memcpy(buf, data, len);
    return bsp_can_send(UDS_RESP_ID, buf, 8U);
}

uint8_t isotp_send(const uint8_t *data, uint16_t len)
{
    uint8_t  frame[8];
    uint16_t offset;
    uint8_t  sn;
    uint8_t  fc_bs = 0U;
    uint8_t  fc_stmin = 0U;
    uint8_t  ret;

    if ((data == 0) || (len == 0U)) {
        return ISOTP_ERR_FORMAT;
    }

    if (len <= 7U) {
        frame[0] = (uint8_t)len;
        memcpy(&frame[1], data, len);
        return send_frame(frame, (uint8_t)(len + 1U));
    }

    frame[0] = (uint8_t)(0x10U | ((len >> 8) & 0x0FU));
    frame[1] = (uint8_t)(len & 0xFFU);
    memcpy(&frame[2], data, 6U);
    ret = send_frame(frame, 8U);
    if (ret != 0U) {
        return ret;
    }

    {
        can_frame_t f;
        uint32_t start = HAL_GetTick();
        uint8_t got = 0U;

        while ((HAL_GetTick() - start) < N_BS_MS) {
            if (bsp_can_recv(&f, 10U) != 0U) {
                continue;
            }
            if ((f.dlc < 3U) || ((f.data[0] & 0xF0U) != 0x30U)) {
                continue;
            }
            if ((f.data[0] & 0x0FU) == 0U) {
                fc_bs    = f.data[1];
                fc_stmin = f.data[2];
                got = 1U;
                break;
            }
            if ((f.data[0] & 0x0FU) == 2U) {
                return ISOTP_ERR_OVERFLOW;
            }
        }
        if (got == 0U) {
            return ISOTP_ERR_TIMEOUT;
        }
    }

    offset = 6U;
    sn = 1U;
    {
        uint8_t in_block = 0U;
        while (offset < len) {
            uint16_t chunk = (uint16_t)(len - offset);
            if (chunk > 7U) {
                chunk = 7U;
            }
            frame[0] = (uint8_t)(0x20U | (sn & 0x0FU));
            memcpy(&frame[1], data + offset, chunk);
            ret = send_frame(frame, (uint8_t)(chunk + 1U));
            if (ret != 0U) {
                return ret;
            }
            offset = (uint16_t)(offset + chunk);
            sn = (uint8_t)((sn + 1U) & 0x0FU);
            in_block++;

            if (offset < len) {
                if (fc_stmin > 0U) {
                    HAL_Delay(fc_stmin);
                }
                if ((fc_bs != 0U) && (in_block >= fc_bs)) {
                    can_frame_t f;
                    uint32_t start = HAL_GetTick();
                    uint8_t got = 0U;
                    while ((HAL_GetTick() - start) < N_BS_MS) {
                        if (bsp_can_recv(&f, 10U) != 0U) {
                            continue;
                        }
                        if ((f.dlc >= 3U) && ((f.data[0] & 0xF0U) == 0x30U)
                            && ((f.data[0] & 0x0FU) == 0U)) {
                            fc_bs = f.data[1];
                            fc_stmin = f.data[2];
                            got = 1U;
                            break;
                        }
                    }
                    if (got == 0U) {
                        return ISOTP_ERR_TIMEOUT;
                    }
                    in_block = 0U;
                }
            }
        }
    }
    return ISOTP_OK;
}

static uint8_t send_fc(uint8_t fs, uint8_t bs, uint8_t stmin)
{
    uint8_t frame[3];
    frame[0] = (uint8_t)(0x30U | (fs & 0x0FU));
    frame[1] = bs;
    frame[2] = stmin;
    return send_frame(frame, 3U);
}

uint8_t isotp_recv(uint8_t *buf, uint16_t max_len, uint16_t *out_len,
                   uint32_t timeout_ms)
{
    can_frame_t f;
    uint16_t total;
    uint16_t offset;
    uint8_t  sn;

    if (bsp_can_recv(&f, timeout_ms) != 0U) {
        return ISOTP_ERR_TIMEOUT;
    }
    if (f.dlc < 1U) {
        return ISOTP_ERR_FORMAT;
    }

    if ((f.data[0] & 0xF0U) == 0x00U) {
        uint8_t len = (uint8_t)(f.data[0] & 0x0FU);
        if ((len == 0U) || (len > 7U) || (len > max_len)) {
            return ISOTP_ERR_FORMAT;
        }
        memcpy(buf, &f.data[1], len);
        *out_len = len;
        return ISOTP_OK;
    }

    if ((f.data[0] & 0xF0U) != 0x10U) {
        return ISOTP_ERR_FORMAT;
    }

    total = (uint16_t)(((uint16_t)(f.data[0] & 0x0FU) << 8) | f.data[1]);
    if ((total <= 7U) || (total > max_len)) {
        return ISOTP_ERR_OVERFLOW;
    }
    memcpy(buf, &f.data[2], 6U);
    offset = 6U;
    sn = 1U;

    (void)send_fc(0U, 0U, 0U);      /* BS=0：一次收完，不再要求流控 */

    while (offset < total) {
        if (bsp_can_recv(&f, N_CR_MS) != 0U) {
            return ISOTP_ERR_TIMEOUT;
        }
        if ((f.data[0] & 0xF0U) != 0x20U) {
            return ISOTP_ERR_FORMAT;
        }
        if ((f.data[0] & 0x0FU) != sn) {
            return ISOTP_ERR_SEQUENCE;
        }
        {
            uint16_t room = (uint16_t)(total - offset);
            uint16_t n = (room > 7U) ? 7U : room;
            memcpy(&buf[offset], &f.data[1], n);
            offset = (uint16_t)(offset + n);
        }
        sn = (uint8_t)((sn + 1U) & 0x0FU);
    }

    *out_len = total;
    return ISOTP_OK;
}
