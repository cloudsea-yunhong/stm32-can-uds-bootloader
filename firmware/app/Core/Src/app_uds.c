/* app_uds.c App 侧 UDS 0x10 / 0x11 / 0x22 / 0x27 / 0x3E */
#include "app_uds.h"
#include "project_config.h"
#include "isotp.h"
#include "params.h"
#include "app_boot.h"
#include "bsp_uart.h"
#include "bsp_can.h"
#include "main.h"
#include <string.h>

#define NRC_SERVICE_NOT_SUPPORTED   0x11U
#define NRC_SUBFUNC_NOT_SUPPORTED   0x12U
#define NRC_INCORRECT_LENGTH        0x13U
#define NRC_CONDITIONS_NOT_CORRECT  0x22U
#define NRC_OUT_OF_RANGE            0x31U
#define NRC_SECURITY_DENIED         0x33U
#define NRC_INVALID_KEY             0x35U
#define NRC_EXCEED_ATTEMPTS         0x36U
#define NRC_SEQUENCE_ERROR          0x24U

static uint8_t  s_session;
static uint8_t  s_unlocked;
static uint8_t  s_seed_level;
static uint32_t s_seed;
static uint8_t  s_attempts;
static uint32_t s_s3_tick;

static uint8_t s_rx[ISOTP_MAX_LEN + 8U];
static uint8_t s_tx[ISOTP_MAX_LEN + 8U];

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
    uint8_t body[5];
    uint8_t session;

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

    body[0] = session;
    body[1] = 0x00U;
    body[2] = 0x32U;
    body[3] = 0x01U;
    body[4] = 0xF4U;
    send_positive(0x10U, body, 5U);

    if (session == 0x02U) {
        bsp_uart_printf("[app] programming session, hand over to bootloader\r\n");
        /* 先回响应 再写 BKP 最后复位 反过来主机会收不到响应 */
        app_request_upgrade();
        HAL_Delay(50U);
        NVIC_SystemReset();
    }

    s_session = session;
    s_s3_tick = HAL_GetTick();
    bsp_uart_printf("[app] session 0x%02X\r\n", session);
}

static void svc_reset(const uint8_t *req, uint16_t len)
{
    uint8_t body = 0x01U;

    if (len < 2U) {
        send_negative(0x11U, NRC_INCORRECT_LENGTH);
        return;
    }
    if (req[1] != 0x01U) {
        send_negative(0x11U, NRC_SUBFUNC_NOT_SUPPORTED);
        return;
    }
    send_positive(0x11U, &body, 1U);
    HAL_Delay(50U);
    NVIC_SystemReset();
}

static void svc_read_data(const uint8_t *req, uint16_t len)
{
    BootParam p;
    uint32_t did;
    uint8_t body[8];
    uint16_t n = 2U;

    if (len < 3U) {
        send_negative(0x22U, NRC_INCORRECT_LENGTH);
        return;
    }
    did = ((uint32_t)req[1] << 8) | req[2];
    body[0] = req[1];
    body[1] = req[2];
    params_get(&p);

    if (did == 0xF186U) {
        body[n++] = s_session;
    } else if (did == 0xF195U) {
        body[n++] = (uint8_t)((p.version >> 24) & 0xFFU);
        body[n++] = (uint8_t)((p.version >> 16) & 0xFFU);
        body[n++] = (uint8_t)((p.version >> 8) & 0xFFU);
        body[n++] = (uint8_t)(p.version & 0xFFU);
    } else if (did == DID_ACTIVE_AREA) {
        body[n++] = (uint8_t)p.active_slot;
    } else if (did == DID_UID_FOLD) {
        uint32_t uid = params_uid_hash();
        body[n++] = (uint8_t)((uid >> 24) & 0xFFU);
        body[n++] = (uint8_t)((uid >> 16) & 0xFFU);
        body[n++] = (uint8_t)((uid >> 8) & 0xFFU);
        body[n++] = (uint8_t)(uid & 0xFFU);
    } else {
        send_negative(0x22U, NRC_OUT_OF_RANGE);
        return;
    }
    send_positive(0x22U, body, n);
}

static void svc_security(const uint8_t *req, uint16_t len)
{
    uint32_t key;

    if (len < 2U) {
        send_negative(0x27U, NRC_INCORRECT_LENGTH);
        return;
    }
    if (req[1] == 0x01U) {
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
    if (req[1] == 0x02U) {
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
            s_attempts = 0U;
            s_seed_level = 0U;
            s_tx[0] = 0x67U;
            s_tx[1] = 0x02U;
            (void)isotp_send(s_tx, 2U);
            return;
        }
        s_attempts++;
        send_negative(0x27U, (s_attempts >= 3U) ? NRC_EXCEED_ATTEMPTS : NRC_INVALID_KEY);
        return;
    }
    send_negative(0x27U, NRC_SUBFUNC_NOT_SUPPORTED);
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
    case 0x10U: svc_session(req, len);   break;
    case 0x11U: svc_reset(req, len);     break;
    case 0x22U: svc_read_data(req, len); break;
    case 0x27U: svc_security(req, len);  break;
    case 0x3EU: svc_tester_present();    break;
    default:
        send_negative(req[0], NRC_SERVICE_NOT_SUPPORTED);
        break;
    }
}

void app_uds_init(void)
{
    s_session = 0x01U;
    s_unlocked = 0U;
    s_seed_level = 0U;
    s_attempts = 0U;
    s_s3_tick = HAL_GetTick();
    bsp_uart_printf("[app] uds ready\r\n");
}

void app_uds_poll(void)
{
    uint16_t len = 0U;

    if ((s_session != 0x01U) && ((HAL_GetTick() - s_s3_tick) > UDS_S3_MS)) {
        s_session = 0x01U;
        s_unlocked = 0U;
    }
    if (isotp_recv(s_rx, sizeof(s_rx), &len, 10U) == ISOTP_OK) {
        dispatch(s_rx, len);
    }
    bsp_can_poll_recovery();
}


