/* stm32f4xx_hal_conf.h HAL 配置 只开用到的模块 */
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_hal_conf.h
  * @brief   HAL configuration file.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ########################## Module Selection ############################## */
/**
  * @brief This is the list of modules to be used in the HAL driver
  */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CAN_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED

/* ########################## Oscillator Values adaptation ####################*/
/**
  * @brief Adjust the value of External High Speed oscillator (HSE) used in your application.
  *        This value is used by the RCC HAL module to compute the system frequency
  *        (when HSE is used as system clock source, directly or through the PLL).
  */
#if !defined(HSE_VALUE)
#define HSE_VALUE               8000000U
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT     100U
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE               16000000U
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE               32000U
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE               32768U
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT     5000U
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE    12288000U
#endif

/* ########################## System Configuration ########################### */
/**
  * @brief This is the list of the system configuration parameters
  */
#define VDD_VALUE               3300U
#define TICK_INT_PRIORITY       0x0FU
#define USE_RTOS                0U
#define PREFETCH_ENABLE         1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE       1U

/* ########################## Callback Register ############################## */
/**
  * @brief Uncomment the line below to enable the register callback feature
  */
#define USE_HAL_CAN_REGISTER_CALLBACKS   0U
#define USE_HAL_UART_REGISTER_CALLBACKS  0U

/* ########################## Assert Selection ############################## */
/**
  * @brief Uncomment the line below to enable assert_param
  */
#define assert_param(expr) ((void)0U)

/* ########################## Module Inclusion ############################## */
#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_cortex.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_pwr.h"
#include "stm32f4xx_hal_can.h"
#include "stm32f4xx_hal_uart.h"
#include "stm32f4xx_hal_iwdg.h"

#ifdef __cplusplus
}
#endif

#endif
