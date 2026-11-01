/* uds.c UDS 服务端 Bootloader 侧 */
#include "uds.h"
#include "project_config.h"
#include "isotp.h"
#include "flash_if.h"
#include "params.h"
#include "boot.h"
#include "bsp_can.h"
#include "bsp_uart.h"
#include "main.h"
#include <string.h>

#define NRC_SERVICE_NOT_SUPPORTED       0x11U
#define NRC_SUBFUNC_NOT_SUPPORTED       0x12U
#define NRC_INCORRECT_LENGTH            0x13U
#define NRC_CONDITIONS_NOT_CORRECT      0x22U
#define NRC_SEQUENCE_ERROR              0x24U
#define NRC_OUT_OF_RANGE                0x31U
#define NRC_SECURITY_DENIED             0x33U
#define NRC_INVALID_KEY                 0x35U
#define NRC_EXCEED_ATTEMPTS             0x36U
#define NRC_DELAY_NOT_EXPIRED           0x37U
#define NRC_PROGRAMMING_FAILURE         0x72U
#define NRC_RESPONSE_PENDING            0x78U

static const uint8_t k_vin[17] = {
    'L','S','V','A','A','1','2','3','4','5','6','7','8','9','0','A','B'
};

static uint8_t  s_session;
static uint8_t  s_unlocked;
static uint8_t  s_seed_level;
static uint32_t s_seed;
static uint8_t  s_key_attempts;
static uint32_t s_s3_tick;

static uint32_t s_download_addr;
static uint32_t s_download_size;
static uint32_t s_write_offset;
static uint32_t s_last_block;
static uint32_t s_max_block_len;
static uint32_t s_flash_crc;
static uint32_t s_flash_size;

static uint8_t  s_rx[ISOTP_MAX_LEN + 8U];
static uint8_t  s_tx[ISOTP_MAX_LEN + 8U];

static uint32_t uid_fold(void)
{
    return params_uid_hash();
}

static uint32_t teaching_key(uint32_t seed, uint32_t uid)
{
    uint32_t k = seed ^ uid;
    k ^= 0xA5A5A5A5UL;
    k = ((k << 3) | (k >> 29)) & 0xFFFFFFFFUL;
    k = (~k) & 0xFFFFFFFFUL;
    return (k ^ 0x5A5A5A5AUL) & 0xFFFFFFFFUL;
}

static uint32_t rng_read(void)
{
    uint32_t start;

    __HAL_RCC_RNG_CLK_ENABLE();
    RNG->CR |= RNG_CR_RNGEN;
    start = HAL_GetTick();
    while (((RNG->SR & RNG_SR_DRDY) == 0U) && ((HAL_GetTick() - start) < 10U)) {
    }
    return RNG->DR ^ (HAL_GetTick() * 2654435761UL);
}

static void send_positive(uint8_t sid, const uint8_t *data, uint16_t len)
{
    uint16_t n = 0U;
    s_tx[n++] = (uint8_t)(sid + 0x40U);
    if ((data != 0) && (len > 0U)) {
        memcpy(&s_tx[n], data, len);
        n = (uint16_t)(n + len);
    }
    (void)isotp_send(s_tx, n);
}

static void send_negative(uint8_t sid, uint8_t nrc)
{
    s_tx[0] = 0x7FU;
    s_tx[1] = sid;
    s_tx[2] = nrc;
    (void)isotp_send(s_tx, 3U);
}

static void svc_session(const uint8_t *req, uint16_t len)
{
    uint8_t session;
    uint8_t resp[5] = {0x00, 0x00, 0x32, 0x01, 0xF4};

    if (len < 2U) {
        send_negative(0x10U, NRC_INCORRECT_LENGTH);
        return;
    }
    session = req[1];
    if ((session != 0x01U) && (session != 0x02U) && (session != 0x03U)) {
        send_negative(0x10U, NRC_SUBFUNC_NOT_SUPPORTED);
        return;
    }
    if ((session == 0x02U) && (s_unlocked == 0U)) {
        send_negative(0x10U, NRC_SECURITY_DENIED);
        return;
    }
    s_session = session;
    s_s3_tick = HAL_GetTick();
    resp[0] = 0x00;
    resp[0] = 0x00;
    {
        uint8_t body[5];
        body[0] = session;
        body[1] = 0x00U;
        body[2] = 0x32U;
        body[3] = 0x01U;
        body[4] = 0xF4U;
        send_positive(0x10U, body, 5U);
    }
    (void)resp;
    bsp_uart_printf("[uds] session 0x%02X\r\n", session);
}

static void svc_reset(const uint8_t *req, uint16_t len)
{
    uint8_t body[1];

    if (len < 2U) {
        send_negative(0x11U, NRC_INCORRECT_LENGTH);
        return;
    }
    if (req[1] != 0x01U) {
        send_negative(0x11U, NRC_SUBFUNC_NOT_SUPPORTED);
        return;
    }
    body[0] = 0x01U;
    send_positive(0x11U, body, 1U);
    HAL_Delay(50U);
    NVIC_SystemReset();
}

static void svc_read_data(const uint8_t *req, uint16_t len)
{
    BootParam p;
    uint32_t did;
    uint8_t body[24];
    uint16_t n;
    uint32_t value;

    if (len < 3U) {
        send_negative(0x22U, NRC_INCORRECT_LENGTH);
        return;
    }
    did = ((uint32_t)req[1] << 8) | req[2];
    body[0] = req[1];
    body[1] = req[2];
    n = 2U;

    params_get(&p);

    switch (did) {
    case 0xF186U:
        body[n++] = s_session;
        break;
    case 0xF190U:
        memcpy(&body[n], k_vin, sizeof(k_vin));
        n = (uint16_t)(n + sizeof(k_vin));
        break;
    case 0xF195U:
        body[n++] = (uint8_t)((p.version >> 24) & 0xFFU);
        body[n++] = (uint8_t)((p.version >> 16) & 0xFFU);
        body[n++] = (uint8_t)((p.version >> 8) & 0xFFU);
        body[n++] = (uint8_t)(p.version & 0xFFU);
        break;
    case DID_ACTIVE_AREA:
        body[n++] = (uint8_t)p.active_slot;
        break;
    case DID_LAST_BLOCK:
        body[n++] = (uint8_t)((s_last_block >> 8) & 0xFFU);
        body[n++] = (uint8_t)(s_last_block & 0xFFU);
        break;
    case DID_UID_FOLD:
        value = uid_fold();
        body[n++] = (uint8_t)((value >> 24) & 0xFFU);
        body[n++] = (uint8_t)((value >> 16) & 0xFFU);
        body[n++] = (uint8_t)((value >> 8) & 0xFFU);
        body[n++] = (uint8_t)(value & 0xFFU);
        break;
    default:
        send_negative(0x22U, NRC_OUT_OF_RANGE);
        return;
    }
    send_positive(0x22U, body, n);
}

static void svc_security(const uint8_t *req, uint16_t len)
{
    uint8_t sub;
    uint32_t key;

    if (len < 2U) {
        send_negative(0x27U, NRC_INCORRECT_LENGTH);
        return;
    }
    sub = req[1];

    if (sub == 0x01U) {
        s_seed = rng_read();
        s_seed_level = 0x01U;
        s_tx[0] = 0x67U;
        s_tx[1] = 0x01U;
        s_tx[2] = (uint8_t)((s_seed >> 24) & 0xFFU);
        s_tx[3] = (uint8_t)((s_seed >> 16) & 0xFFU);
        s_tx[4] = (uint8_t)((s_seed >> 8) & 0xFFU);
        s_tx[5] = (uint8_t)(s_seed & 0xFFU);
        (void)isotp_send(s_tx, 6U);
        return;
    }

    if (sub == 0x02U) {
        if (s_seed_level != 0x01U) {
            send_negative(0x27U, NRC_SEQUENCE_ERROR);
            return;
        }
        if (len < 6U) {
            send_negative(0x27U, NRC_INCORRECT_LENGTH);
            return;
        }
        key = ((uint32_t)req[2] << 24) | ((uint32_t)req[3] << 16)
            | ((uint32_t)req[4] << 8) | req[5];
        if (key == teaching_key(s_seed, uid_fold())) {
            s_unlocked = 1U;
            s_key_attempts = 0U;
            s_seed_level = 0U;
            s_tx[0] = 0x67U;
            s_tx[1] = 0x02U;
            (void)isotp_send(s_tx, 2U);
            return;
        }
        s_key_attempts++;
        if (s_key_attempts >= 3U) {
            send_negative(0x27U, NRC_EXCEED_ATTEMPTS);
        } else {
            send_negative(0x27U, NRC_INVALID_KEY);
        }
        return;
    }

    send_negative(0x27U, NRC_SUBFUNC_NOT_SUPPORTED);
}

static uint8_t range_is_app(uint32_t addr, uint32_t size)
{
    if ((addr & 0x3U) != 0U) {
        return 0U;
    }
    if ((addr >= APP_A_ADDR) && ((addr + size) <= (APP_A_ADDR + APP_A_SIZE))) {
        return 1U;
    }
    if ((addr >= APP_B_ADDR) && ((addr + size) <= (APP_B_ADDR + APP_B_SIZE))) {
        return 2U;
    }
    return 0U;
}

static void svc_routine(const uint8_t *req, uint16_t len)
{
    uint16_t rid;
    uint32_t addr;
    uint32_t size;
    uint32_t crc;
    uint8_t  body[5];

    if (len < 4U) {
        send_negative(0x31U, NRC_INCORRECT_LENGTH);
        return;
    }
    if (s_unlocked == 0U) {
        send_negative(0x31U, NRC_SECURITY_DENIED);
        return;
    }
    rid = (uint16_t)(((uint16_t)req[2] << 8) | req[3]);

    if (rid == RID_ERASE) {
        if (len < 12U) {
            send_negative(0x31U, NRC_INCORRECT_LENGTH);
            return;
        }
        addr = ((uint32_t)req[4] << 24) | ((uint32_t)req[5] << 16)
             | ((uint32_t)req[6] << 8) | req[7];
        size = ((uint32_t)req[8] << 24) | ((uint32_t)req[9] << 16)
             | ((uint32_t)req[10] << 8) | req[11];
        if (range_is_app(addr, size) == 0U) {
            send_negative(0x31U, NRC_OUT_OF_RANGE);
            return;
        }
        /* 先回 0x78 再擦 256 KB 要 1.5~3 s */
        send_negative(0x31U, NRC_RESPONSE_PENDING);
        if (flash_if_erase(addr, size) != 0U) {
            send_negative(0x31U, NRC_PROGRAMMING_FAILURE);
            return;
        }
        s_download_addr = addr;
        s_download_size = size;
        s_write_offset  = 0U;
        s_last_block    = 0U;
        body[0] = 0x01U; body[1] = 0xFFU; body[2] = 0x00U; body[3] = 0x00U;
        send_positive(0x31U, body, 4U);
        return;
    }

    if (rid == RID_CHECK_CRC) {
        if (len < 16U) {
            send_negative(0x31U, NRC_INCORRECT_LENGTH);
            return;
        }
        addr = ((uint32_t)req[4] << 24) | ((uint32_t)req[5] << 16)
             | ((uint32_t)req[6] << 8) | req[7];
        size = ((uint32_t)req[8] << 24) | ((uint32_t)req[9] << 16)
             | ((uint32_t)req[10] << 8) | req[11];
        crc = ((uint32_t)req[12] << 24) | ((uint32_t)req[13] << 16)
            | ((uint32_t)req[14] << 8) | req[15];
        if (range_is_app(addr, size) == 0U) {
            send_negative(0x31U, NRC_OUT_OF_RANGE);
            return;
        }
        send_negative(0x31U, NRC_RESPONSE_PENDING);
        if (flash_if_crc32(addr, size) != crc) {
            send_negative(0x31U, NRC_PROGRAMMING_FAILURE);
            return;
        }
        s_flash_crc = crc;

        s_flash_size = size;

        body[0] = 0x01U; body[1] = 0xFFU; body[2] = 0x01U; body[3] = 0x00U;
        send_positive(0x31U, body, 4U);
        return;
    }

    if (rid == RID_SWITCH_AREA) {
        BootParam p;
        uint8_t area;

        if (len < 5U) {
            send_negative(0x31U, NRC_INCORRECT_LENGTH);
            return;
        }
        area = (uint8_t)(req[4] & 0x01U);
        params_get(&p);
        p.active_slot = area;
        p.boot_count  = 0U;
        p.slot_valid[area] = SLOT_PENDING;
        p.app_base = (area == 0U) ? APP_A_ADDR : APP_B_ADDR;
        p.app_size = (area == 0U) ? APP_A_SIZE : APP_B_SIZE;
        p.app_crc32 = (s_flash_size != 0U) ? s_flash_crc : flash_if_crc32(p.app_base, p.app_size);
        p.app_size  = (s_flash_size != 0U) ? s_flash_size : p.app_size;
        if (params_set(&p) != 0U) {
            send_negative(0x31U, NRC_PROGRAMMING_FAILURE);
            return;
        }
        body[0] = 0x01U; body[1] = 0xFFU; body[2] = 0x02U; body[3] = 0x00U;
        send_positive(0x31U, body, 4U);
        return;
    }

    send_negative(0x31U, NRC_OUT_OF_RANGE);
}

static void svc_download(const uint8_t *req, uint16_t len)
{
    uint32_t addr;
    uint32_t size;

    if (len < 11U) {
        send_negative(0x34U, NRC_INCORRECT_LENGTH);
        return;
    }
    if (s_unlocked == 0U) {
        send_negative(0x34U, NRC_SECURITY_DENIED);
        return;
    }
    addr = ((uint32_t)req[4] << 24) | ((uint32_t)req[5] << 16)
         | ((uint32_t)req[6] << 8) | req[7];
    size = ((uint32_t)req[8] << 24) | ((uint32_t)req[9] << 16)
         | ((uint32_t)req[10] << 8) | req[11];
    if (range_is_app(addr, size) == 0U) {
        send_negative(0x34U, NRC_OUT_OF_RANGE);
        return;
    }

    s_download_addr = addr;
    s_download_size = size;
    s_write_offset  = 0U;
    s_last_block    = 0U;
    s_max_block_len = UDS_BLOCK_LENGTH;

    s_tx[0] = 0x74U;
    s_tx[1] = 0x20U;
    s_tx[2] = (uint8_t)((s_max_block_len >> 8) & 0xFFU);
    s_tx[3] = (uint8_t)(s_max_block_len & 0xFFU);
    (void)isotp_send(s_tx, 4U);
}

static void svc_transfer(const uint8_t *req, uint16_t len)
{
    uint8_t  bsc;
    uint16_t data_len;
    uint8_t  ret;

    if (len < 2U) {
        send_negative(0x36U, NRC_INCORRECT_LENGTH);
        return;
    }
    if (s_download_addr == 0U) {
        send_negative(0x36U, NRC_SEQUENCE_ERROR);
        return;
    }
    bsc = req[1];
    if (bsc != (uint8_t)((s_last_block + 1U) & 0xFFU)) {
        send_negative(0x36U, 0x73U);
        return;
    }
    data_len = (uint16_t)(len - 2U);
    if ((s_write_offset + data_len) > s_download_size) {
        send_negative(0x36U, NRC_OUT_OF_RANGE);
        return;
    }
    ret = flash_if_write(s_download_addr + s_write_offset, &req[2], data_len);
    if (ret != 0U) {
        send_negative(0x36U, NRC_PROGRAMMING_FAILURE);
        return;
    }
    s_write_offset += data_len;
    s_last_block++;

    s_tx[0] = 0x76U;
    s_tx[1] = bsc;
    (void)isotp_send(s_tx, 2U);
}

static void svc_transfer_exit(void)
{
    s_tx[0] = 0x77U;
    (void)isotp_send(s_tx, 1U);
}

static void svc_tester_present(void)
{
    s_tx[0] = 0x7EU;
    s_tx[1] = 0x00U;
    (void)isotp_send(s_tx, 2U);
    s_s3_tick = HAL_GetTick();
}

static void dispatch(const uint8_t *req, uint16_t len)
{
    if (len == 0U) {
        return;
    }
    s_s3_tick = HAL_GetTick();

    switch (req[0]) {
    case 0x10U: svc_session(req, len);          break;
    case 0x11U: svc_reset(req, len);            break;
    case 0x22U: svc_read_data(req, len);        break;
    case 0x27U: svc_security(req, len);         break;
    case 0x31U: svc_routine(req, len);          break;
    case 0x34U: svc_download(req, len);         break;
    case 0x36U: svc_transfer(req, len);         break;
    case 0x37U: svc_transfer_exit();            break;
    case 0x3EU: svc_tester_present();           break;
    default:
        send_negative(req[0], NRC_SERVICE_NOT_SUPPORTED);
        break;
    }
}

void uds_init(void)
{
    s_session = 0x01U;
    s_unlocked = 0U;
    s_seed_level = 0U;
    s_key_attempts = 0U;
    s_s3_tick = HAL_GetTick();
    s_download_addr = 0U;
    s_download_size = 0U;
    s_write_offset = 0U;
    s_last_block = 0U;
    bsp_uart_printf("[uds] ready (0x%03lX/0x%03lX)\r\n",
                    (unsigned long)UDS_REQ_ID, (unsigned long)UDS_RESP_ID);
}

void uds_poll(void)
{
    uint16_t len = 0U;
    uint8_t  ret;

    if ((s_session != 0x01U) &&
        ((HAL_GetTick() - s_s3_tick) > UDS_S3_MS)) {
        s_session = 0x01U;
        s_unlocked = 0U;
        bsp_uart_printf("[uds] S3 timeout -> default session\r\n");
    }

    ret = isotp_recv(s_rx, sizeof(s_rx), &len, 10U);
    if (ret == ISOTP_OK) {
        dispatch(s_rx, len);
    }
    bsp_can_poll_recovery();
}

