/**
  ******************************************************************************
  * @copyright: Copyright To Hangzhou Dinova EP Technology Co.,Ltd
  * @file     : xx.c
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
#include "safety.h"
#include "board.h"
#include "gpio.h"
#include "stimer.h"
#include "wdg.h"
#include "errno-base.h"

#define  LOG_TAG             "safety"
#define  LOG_LVL             4
#include "log.h"
/*------------------------------ Macro definition -----------------------------*/


/*------------------------------ typedef definition ---------------------------*/

/*------------------------------ variables prototypes -------------------------*/
stimer_t led_timer;

static struct wdg_device* iwdg_dev = NULL;

/*------------------------------ application ----------------------------------*/
void led_timer_callback(void* arg)
{
    stimer_t *timer = (stimer_t*)arg;

    if (gpio_read(LED_PIN_ID) == 1) {
        gpio_write(LED_PIN_ID, 0);
        stimer_change_period(timer, 200);
    } else {
        gpio_write(LED_PIN_ID, 1);
        stimer_change_period(timer, 800);
    }
}

int safety_init(void)
{
    int ret = 0;
    
    /* set led gpio mode */
    gpio_set_mode(LED_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(LED_PIN_ID, 1);

    stimer_create(&led_timer, 800, STIMER_AUTO_RELOAD, led_timer_callback, (void*)&led_timer);
    stimer_start(&led_timer);

    iwdg_dev = wdg_find("stm32_iwdg");
    if (iwdg_dev == NULL) {
        LOG_W("safety_init: wdg_find fail!");
        return -EIO;
    }
    
    ret = wdg_start(iwdg_dev);
    if (ret != 0) {
        LOG_W("safety_init: wdg_start fail!");
        iwdg_dev = NULL;
    }
    
    return ret;
}

void safety_task(void)
{
    static uint32_t feed_fail_count = 0U;

    if (iwdg_dev == NULL) {
        return;
    }

    if (wdg_feed(iwdg_dev) != 0) {
        feed_fail_count++;
        /* 仅首次失败或每第 500 次失败打印，避免刷屏 */
        if ((feed_fail_count == 1U) || ((feed_fail_count % 1000U) == 0U)) {
            LOG_W("safety_task: wdg_feed fail, count=%lu", (unsigned long)feed_fail_count);
        }
    } else {
        feed_fail_count = 0U;  /* 成功则清零，便于下次连续失败时再次打印 */
    }
}

void safety_perform_software_reset(void)
{
    __disable_irq();

    // 确保 Flash 操作完成
    FLASH_WaitForLastOperation(FLASH_TIMEOUT_VALUE);

    IWDG->KR = 0xAAAA;

    // 触发软件复位
    NVIC_SystemReset();

    // 复位指令发出后，CPU 还需要几个时钟周期才能真正复位
    // 加个死循环防止 CPU 继续往下乱跑
    while(1) {}
}


/******************************* End Of File ************************************/
