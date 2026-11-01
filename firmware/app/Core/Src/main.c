/* main.c App 入口 向量表、心跳、启动确认、诊断与交权 */
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "project_config.h"
#include "bsp_uart.h"
#include "bsp_can.h"
#include "app_uds.h"
#include "app_boot.h"
#include "params.h"
#include <math.h>

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

UART_HandleTypeDef huart1;
CAN_HandleTypeDef  hcan1;
IWDG_HandleTypeDef hiwdg;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void app_iwdg_init(void);

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

int main(void)
{
    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    /* 必须在任何外设初始化前设好 VTOR */
    SCB->VTOR = APP_START_ADDR;
    __DSB();
    __ISB();

    HAL_Init();

    /* USER CODE BEGIN Init */

    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    app_iwdg_init();

    /* USER CODE BEGIN 2 */

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */

    bsp_uart_printf("\r\n=== App V1.2.3 @ 0x%08lX ===\r\n",
                    (unsigned long)APP_START_ADDR);
    bsp_uart_printf("[app] VTOR = 0x%08lX\r\n", (unsigned long)SCB->VTOR);

    bsp_can_init();
    app_uds_init();

    {
        uint32_t start = HAL_GetTick();
        uint8_t  confirmed = 0U;
        uint32_t last_led = 0U;
        uint32_t last_print = 0U;

        while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
            app_uds_poll();
            HAL_IWDG_Refresh(&hiwdg);

            /* 5 s 后确认启动成功 要放在业务真正跑起来之后
               APP_SKIP_CONFIRM 可以跳过确认 用来演示回滚 */
#ifndef APP_SKIP_CONFIRM
            if ((confirmed == 0U) && ((HAL_GetTick() - start) > BOOT_CONFIRM_DELAY_MS)) {
                if (app_confirm_boot() == 0U) {
                    confirmed = 1U;
                    bsp_uart_printf("[app] boot confirmed\r\n");
                }
            }
#endif

            if ((HAL_GetTick() - last_led) >= 500U) {
                last_led = HAL_GetTick();
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_0);
            }

            if ((HAL_GetTick() - last_print) >= 2000U) {
                float v = sinf((float)HAL_GetTick() / 1000.0f);
                last_print = HAL_GetTick();
                bsp_uart_printf("[app] t=%lu s  sin=%.3f\r\n",
                                (unsigned long)((HAL_GetTick() - start) / 1000U),
                                (double)v);
            }
        }
    }
    /* USER CODE END 3 */
}

static void app_iwdg_init(void)
{
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Reload    = 1000U;
    (void)HAL_IWDG_Init(&hiwdg);
}

uint8_t app_confirm_boot(void)
{
    BootParam p;

    if (params_get(&p) != 0U) {
        return 1U;
    }
    if ((p.boot_count == 0U) && (p.slot_valid[p.active_slot] == SLOT_CONFIRMED)) {
        return 0U;
    }
    p.boot_count = 0U;
    p.slot_valid[p.active_slot] = SLOT_CONFIRMED;
    /* 别动 app_size 和 app_crc32 那是 Bootloader 用来校验的 */
    return params_set(&p);
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 8;
    osc.PLL.PLLN       = 336;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = DBG_BAUDRATE;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* USER CODE BEGIN MX_GPIO_Init_1 */

    /* USER CODE END MX_GPIO_Init_1 */

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);
    gpio.Pin   = GPIO_PIN_0;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    /* USER CODE BEGIN MX_GPIO_Init_2 */

    /* USER CODE END MX_GPIO_Init_2 */
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};
    /* USER CODE BEGIN USART1_MspInit 0 */

    /* USER CODE END USART1_MspInit 0 */
    if (huart->Instance != USART1) {
        return;
    }
    /* Peripheral clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* USART1 GPIO Configuration */
    gpio.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
    /* USER CODE BEGIN USART1_MspInit 1 */

    /* USER CODE END USART1_MspInit 1 */
}

void HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
    GPIO_InitTypeDef gpio = {0};
    /* USER CODE BEGIN CAN1_MspInit 0 */

    /* USER CODE END CAN1_MspInit 0 */
    if (hcan->Instance != CAN1) {
        return;
    }
    /* Peripheral clock enable */
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* CAN1 GPIO Configuration */
    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
    /* USER CODE BEGIN CAN1_MspInit 1 */

    /* USER CODE END CAN1_MspInit 1 */
}

void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */

    __disable_irq();
    while (1) {
    }
    /* USER CODE END Error_Handler_Debug */
}


