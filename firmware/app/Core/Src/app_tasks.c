/* app_tasks.c - App 侧 FreeRTOS 任务 */
#include "FreeRTOS.h"
#include "task.h"

#include "main.h"
#include "app_tasks.h"
#include "app_uds.h"
#include "bsp_uart.h"
#include "project_config.h"

#define APP_TASK_STACK_UDS   512U
#define APP_TASK_STACK_APP   256U
#define APP_TASK_STACK_WDG   128U

#define APP_TASK_PRIO_UDS    4U
#define APP_TASK_PRIO_APP    3U
#define APP_TASK_PRIO_WDG    2U

static volatile uint32_t s_uds_beat;
static volatile uint32_t s_app_beat;

static void app_uds_task(void *arg)
{
    (void)arg;
    for (;;) {
        app_uds_poll();
        s_uds_beat++;
        /* 防止连续收帧时高优先级任务饿死应用/看门狗任务 */
        vTaskDelay(1U);
    }
}

static void app_main_task(void *arg)
{
    uint32_t start = HAL_GetTick();
    uint32_t last_led = 0U;
    uint32_t last_confirm = 0U;
    uint8_t confirmed = 0U;

    (void)arg;

    for (;;) {
#ifndef APP_SKIP_CONFIRM
        if ((confirmed == 0U) &&
            ((HAL_GetTick() - start) > BOOT_CONFIRM_DELAY_MS) &&
            ((HAL_GetTick() - last_confirm) > 2000U)) {
            last_confirm = HAL_GetTick();
            if (app_confirm_boot() == 0U) {
                confirmed = 1U;
                bsp_uart_printf("[应用] 启动确认成功\r\n");
            }
        }
#endif
        if ((HAL_GetTick() - last_led) >= 500U) {
            last_led = HAL_GetTick();
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);
        }

        s_app_beat++;
        vTaskDelay(pdMS_TO_TICKS(20U));
    }
}

static void app_wdg_task(void *arg)
{
    uint32_t last_uds = 0U;
    uint32_t last_app = 0U;

    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500U));

        /* 两个业务任务都必须有推进，才认为系统还活着 */
        if ((s_uds_beat != last_uds) && (s_app_beat != last_app)) {
            HAL_IWDG_Refresh(&hiwdg);
        }
        last_uds = s_uds_beat;
        last_app = s_app_beat;
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void app_tasks_start(void)
{
    BaseType_t ret;

    ret = xTaskCreate(app_uds_task, "uds", APP_TASK_STACK_UDS, NULL,
                      APP_TASK_PRIO_UDS, NULL);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(app_main_task, "app", APP_TASK_STACK_APP, NULL,
                      APP_TASK_PRIO_APP, NULL);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(app_wdg_task, "wdg", APP_TASK_STACK_WDG, NULL,
                      APP_TASK_PRIO_WDG, NULL);
    configASSERT(ret == pdPASS);

    vTaskStartScheduler();

    /* 正常不会到这里：堆不够/调度器启动失败 */
    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);
    }
}
