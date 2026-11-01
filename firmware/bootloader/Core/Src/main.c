/* main.c Bootloader 入口 时钟、串口、CAN、协议栈、启动决策 */
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
#include "params.h"
#include "boot.h"
#include "uds.h"

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

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

int main(void)
{
    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
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

    /* USER CODE BEGIN 2 */

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */

    bsp_uart_printf("\r\n=== CAN Bootloader V1.0 ===\r\n");

    /* 看门狗上电就开 4 s 擦除时逐扇区喂 */
    boot_init();

    /* 参数区双 slot 掉电安全 */
    params_init();

    if (boot_decide_and_jump() == BOOT_STAY_IN_BL) {
        bsp_uart_printf("[boot] stay in bootloader\r\n");

        bsp_can_init();
        uds_init();

        while (1) {
            uds_poll();          /* 收帧 → 解析 → 响应；内部有超时与 0x78 */
            boot_feed_watchdog();
        }
    }

    while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

/* 系统时钟 HSE 8 MHz → PLL → 168 MHz
   APB1 = 42 MHz 给 CAN 用 APB2 = 84 MHz */
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

    gpio.Pin  = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
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
        /* 卡死 等看门狗复位或手动断电 */
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
