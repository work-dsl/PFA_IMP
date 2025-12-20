/**
  ******************************************************************************
  * @file        : xxxx.c
  * @author      : ZJY
  * @version     : V1.0
  * @data        : 20xx-xx-xx
  * @brief       : 
  * @attention   : None
  ******************************************************************************
  * @history     :
  *         V1.0 : 1.xxx
  *
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "serial_test.h"
#include "serial.h"
#include <stdio.h>
#include <string.h>

#define  LOG_TAG             "serial_test"
#define  LOG_LVL             4
#include "log.h"
/* Private typedef -----------------------------------------------------------*/


/* Private define ------------------------------------------------------------*/


/* Private macro -------------------------------------------------------------*/


/* Private variables ---------------------------------------------------------*/

/* Exported variables  -------------------------------------------------------*/
serial_t *port1 = NULL;
serial_t *port3 = NULL;
serial_t *port5 = NULL;
char w1_buff[] = {"Hello port1!\n"};
uint8_t r1_buff[128];
char w3_buff[] = {"Hello port3!\n"};
uint8_t r3_buff[128];
char w5_buff[] = {"Hello port5!\n"};
uint8_t r5_buff[128];

/* Private function prototypes -----------------------------------------------*/


/* Exported functions --------------------------------------------------------*/
void serial_test_init(void)
{
    int32_t ret;

    port1 = serial_find("uart1");
    if (!port1) {
        LOG_E("serial port1 find failed");
        return;
    }
    
    port3 = serial_find("uart3");
    if (!port3) {
        LOG_E("serial port2 find failed");
        return;
    }

    port5 = serial_find("uart5");
    if (!port5) {
        LOG_E("serial port5 find failed");
        return;
    }
    
    ret = serial_open(port1);
    if (ret != 0) {
        LOG_E("serial port1 open failed: %d\r\n", ret);
        return;
    }
    
    ret = serial_open(port3);
    if (ret != 0) {
        LOG_E("serial port3 open failed: %d\r\n", ret);
        return;
    }
    
    ret = serial_open(port5);
    if (ret != 0) {
        LOG_E("serial port5 open failed: %d\r\n", ret);
        return;
    }
    
    serial_write(port1, w1_buff, strlen(w1_buff));
    serial_write(port3, w3_buff, strlen(w3_buff));
    serial_write(port5, w5_buff, strlen(w5_buff));
}

void serial_test_task(void)
{
    int ret;
    
    ret = serial_read(port1, r1_buff, 128);
    if (ret > 0)
    {
        serial_write(port1, r1_buff, ret);
    }
    
    ret = serial_read(port3, r3_buff, 128);
    if (ret > 0)
    {
        serial_write(port3, r3_buff, ret);
    }
    
    ret = serial_read(port5, r5_buff, 128);
    if (ret > 0)
    {
        serial_write(port5, r5_buff, ret);
    }
}

/* Private functions ---------------------------------------------------------*/


