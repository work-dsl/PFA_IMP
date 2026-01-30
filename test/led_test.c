/**
  ******************************************************************************
  * @file        : led_test.c
  * @author      : ZJY
  * @version     : V1.0
  * @data        : 2025-10-17
  * @brief       : LED test
  * @attention   : None
  ******************************************************************************
  * @history     :
  *         V1.0 : 1.LED test
  *
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "led_test.h"
#include "gpio.h"
#include "bsp_conf.h"
#include <stdio.h>

/* Private typedef -----------------------------------------------------------*/


/* Private define ------------------------------------------------------------*/


/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/


/* Exported variables  -------------------------------------------------------*/


/* Private function prototypes -----------------------------------------------*/

/* Exported functions --------------------------------------------------------*/
void led_test_init(void)
{
    gpio_set_mode(LED_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(LED_PIN_ID, 1);
}



void led_test_task(void)
{   
    gpio_write(LED_PIN_ID, 0);
    HAL_Delay(100);
    gpio_write(LED_PIN_ID, 1);
    HAL_Delay(100);
}

/* Private functions ---------------------------------------------------------*/

