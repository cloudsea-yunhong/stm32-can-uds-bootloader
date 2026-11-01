/* isotp.h ISO-TP 服务端 收请求发响应 */
#ifndef __ISOTP_H
#define __ISOTP_H

#include <stdint.h>

#define ISOTP_OK                0U
#define ISOTP_ERR_TIMEOUT       1U
#define ISOTP_ERR_SEQUENCE      2U
#define ISOTP_ERR_FORMAT        3U
#define ISOTP_ERR_OVERFLOW      4U

void    isotp_init(void);
uint8_t isotp_recv(uint8_t *buf, uint16_t max_len, uint16_t *out_len,
                   uint32_t timeout_ms);
uint8_t isotp_send(const uint8_t *data, uint16_t len);

#endif
