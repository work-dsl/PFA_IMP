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
#include "major_logic.h"
#include "stimer.h"
#include "safety.h"
#include "custom_slave.h"
#include "custom_host.h"
#include "contact_imp.h"
#include "cmd_handler.h"

/* 后续可能需要删除的头文件 */
#include "loop_imp.h"
#include "port_ctrl.h"
#include "ad5940.h"

#define  LOG_TAG             "main"
#define  LOG_LVL             4
#include "log.h"

/*------------------------------ Macro definition -----------------------------*/
#define LOOP_IMP_UPLOAD_PERIOD_MS     (20U)  /* 回路阻抗数据上传周期：20ms */

/*------------------------------ typedef definition ---------------------------*/



/*------------------------------ variables prototypes -------------------------*/

static host_handle_t g_host1 = NULL;
static host_handle_t g_host2 = NULL;
static stimer_t g_loop_imp_upload_timer;  /* 回路阻抗数据上传定时器 */

/*------------------------------ function prototypes --------------------------*/

/* 主动上传回调 */
static void upload_callback(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    LOG_I("Upload: cmd=0x%02X, len=%d", cmd, len);
}

/* 回路阻抗数据上传定时器回调 */
static void loop_imp_upload_timer_callback(void *arg)
{
    float imp_real = 0.0f;
    
    (void)arg;
    
    /* 检查上传使能标志 */
    if (cmd_get_loop_imp_upload_enable() == 0U) {
        return;
    }
    
    /* 检查当前工作模式是否为回路阻抗模式 */
    if (port_ctrl_get_mode() != PORT_MODE_LOOP_IMP) {
        return;
    }
    
    /* 获取最新的阻抗数据 */
    float loop_imp_data = loop_imp_get_data();
    
    LOG_D("Final:%.2f", loop_imp_data);
    
    /* 发送主动上传帧（只上传实部，float类型，4字节） */
    (void)slave_send_upload_frame(CMD_UPLOAD_LOOP_IMP_DATA, 
                                 (const uint8_t*)&loop_imp_data, 
                                 sizeof(float));
}

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
    
    /* 系统服务初始化 */
    stimer_init(HAL_GetTick);
   
    /* 协议应用层初始化 */
    slave_proto_init();  
    port_ctrl_init();
    
    /* 初始化主机 */
    g_host1 = host_init("uart3", 0x06, upload_callback);
    if (g_host1 == NULL) {
        LOG_E("Host init failed");
        return -1;
    }
    
    /* 初始化贴靠检测模块 */
    if (contact_imp_init(g_host1) != 0) {
        LOG_E("Contact imp app init failed");
        return -1;
    }
    
    /* 应用主逻辑协调器初始化 */
    major_logic_init();
    
    /* 安全模块初始化 */
    (void)safety_init();

    /* 初始化回路阻抗测量模块 */
    loop_imp_init();
    
    /* 创建回路阻抗数据上传定时器（20ms周期，自动重载） */
    (void)stimer_create(&g_loop_imp_upload_timer,
                        LOOP_IMP_UPLOAD_PERIOD_MS,
                        STIMER_AUTO_RELOAD,
                        loop_imp_upload_timer_callback,
                        NULL);
    (void)stimer_start(&g_loop_imp_upload_timer);
    
    LOG_I("System init suceeces!");
    
    while (1)
    {
        slave_proto_task();     /* 协议处理任务 */
        host_task(g_host1);
        major_logic_task();     /* 主逻辑协调任务 */
        loop_imp_task();
        stimer_service();       /* 软件定时器服务 */
        safety_task();          /* 安全任务 */
    }
}

/******************************* End Of File ************************************/


