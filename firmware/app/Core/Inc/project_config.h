/* project_config.h 分区表、协议参数、CAN ID 都在这 */
#ifndef __PROJECT_CONFIG_H
#define __PROJECT_CONFIG_H

#define BL_START_ADDR           0x08000000UL
#define BL_SIZE                 (32UL * 1024UL)

#define PARAM_A_ADDR            0x08008000UL      /* Sector 2 */
#define PARAM_B_ADDR            0x0800C000UL      /* Sector 3 */
#define PARAM_SECTOR_SIZE       (16UL * 1024UL)

#define APP_A_ADDR              0x08010000UL
#define APP_A_SIZE              (192UL * 1024UL)

#define APP_B_ADDR              0x08040000UL
#define APP_B_SIZE              (192UL * 1024UL)
#define APP_B_PHYS_SIZE         (256UL * 1024UL)

#define PARAM_MAGIC             0x5A5AA5A5UL
#define SLOT_INVALID            0UL
#define SLOT_PENDING            1UL
#define SLOT_CONFIRMED          2UL
#define BOOT_CONFIRM_DELAY_MS   5000UL
#define MAX_BOOT_COUNT          3UL

#define UDS_REQ_ID              0x7E0UL
#define UDS_RESP_ID             0x7E8UL
#define UDS_P2_MS               50UL
#define UDS_P2_STAR_MS          5000UL
#define UDS_S3_MS               5000UL
#define ISOTP_MAX_LEN           4095UL
#define ISOTP_BS                8U
#define ISOTP_STMIN_MS          0U
#define UDS_BLOCK_LENGTH        642UL             /* 0x36：640 数据 + SID + BSC */

#define IWDG_PRESCALER          IWDG_PRESCALER_64
#define IWDG_RELOAD_VALUE       2000U

#define DID_ACTIVE_AREA         0xFF00U
#define DID_LAST_BLOCK          0xFF01U
#define DID_UID_FOLD            0xFF02U

#define RID_ERASE               0xFF00U
#define RID_CHECK_CRC           0xFF01U
#define RID_SWITCH_AREA         0xFF02U

#endif
