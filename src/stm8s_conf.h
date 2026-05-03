/**
  ******************************************************************************
  * @file    stm8s_conf.h
  * @brief   SPL peripheral enable configuration for STM8S103
  ******************************************************************************
  */

#ifndef __STM8S_CONF_H
#define __STM8S_CONF_H

/* Disable SPL runtime parameter assertions (required for embedded build) */
#define assert_param(expr) ((void)0)

/* Enable only the peripherals we need */
#define _GPIO_
#include "stm8s_gpio.h"

#define _ADC1_
#include "stm8s_adc1.h"

#define _UART1_
#include "stm8s_uart1.h"

#define _CLK_
#include "stm8s_clk.h"

#define _TIM4_
#include "stm8s_tim4.h"

#define _AWU_
#include "stm8s_awu.h"

#define _EXTI_
#include "stm8s_exti.h"

/* Uncomment as needed:
#define _ADC1_
#define _ADC2_
#define _AWU_
#define _BEEP_
#define _CLK_
#define _EXTI_
#define _FLASH_
#define _I2C_
#define _ITC_
#define _IWDG_
#define _RST_
#define _SPI_
#define _TIM1_
#define _TIM2_
#define _TIM4_
#define _UART1_
#define _WWDG_
*/

#endif /* __STM8S_CONF_H */
