/**
  ******************************************************************************
  * @copyright: Copyright To Hangzhou Dinova EP Technology Co.,Ltd
  * @file     : main.c
  * @author   : ZJY
  * @version  : V1.0
  * @date     : 20xx-xx-xx
  * @brief    : xxx
  *                  1.xx
  *                  2.xx
  *
  * @attention: None
  ******************************************************************************
  * @history  :
  *      V1.0 : 1.xxx
  *
  *
  *
  ******************************************************************************
  */
/*------------------------------ include --------------------------------------*/
#include "board.h"
#include "serial_test.h"
#include "led_test.h"

#define  LOG_TAG             "main"
#define  LOG_LVL             4
#include "log.h"

/*------------------------------ Macro definition -----------------------------*/

/*------------------------------ typedef definition ---------------------------*/


/*------------------------------ variables prototypes -------------------------*/

/*------------------------------ function prototypes --------------------------*/

/*------------------------------ application ----------------------------------*/
/**
 * @brief  Main program
 * @param  None
 * @retval None
 */
int main(void)
{
    /* 底层驱动初始化 */
    board_init();

    /* 测试初始化 */
    led_test_init();
    serial_test_init();

    while (1)
    {
        led_test_task();
        serial_test_task();
    }
}

/******************************* End Of File ************************************/


