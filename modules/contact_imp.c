/**
  ******************************************************************************
  * @file        : contact_imp.c
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2024-12-XX
  * @brief       : 贴靠检测模块实现
  * @details     本文件实现了contact_imp.h中定义的贴靠检测板功能。
  *              职责：
  *              - 封装对贴靠检测板的模块操作
  *              - 控制贴靠阻抗继电器
  *              - 控制贴靠阻抗检测
  *              - 获取贴靠阻抗数据
  * @attention   使用前需要先调用 contact_imp_init() 进行初始化
  ******************************************************************************
  * @history     :
  *         V1.0 : 1.实现贴靠检测板应用层功能
  *                2.封装继电器控制、检测控制、数据获取功能
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "contact_imp.h"
#include "custom_host.h"
#include "proto_custom.h"
#include <string.h>

#define  LOG_TAG             "contact_imp"
#define  LOG_LVL             4
#include "log.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/**
 * @brief 主机实例句柄（与贴靠检测板通信的通道）
 */
static host_handle_t g_contact_imp_host = NULL;

/**
 * @brief 数据回调函数指针
 */
static contact_imp_data_callback_t g_data_callback = NULL;

/* Exported variables  -------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static void contact_imp_cmd_callback(uint8_t cmd, uint8_t ack, 
                                     const uint8_t *data, uint16_t len);

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化贴靠检测板应用层
 * @param host 主机实例句柄（与贴靠检测板通信的通道）
 * @retval 0 成功
 * @retval -1 失败
 */
int contact_imp_init(host_handle_t host)
{
    if (host == NULL) {
        LOG_E("Invalid host handle");
        return -1;
    }
    
    g_contact_imp_host = host;
    g_data_callback = NULL;
    
    LOG_I("Contact impedance app initialized");
    return 0;
}

/**
 * @brief 控制贴靠阻抗继电器
 * @param state 继电器状态（0=关闭，1=打开）
 * @retval 0 成功（命令已加入队列）
 * @retval -1 失败
 */
int contact_imp_ctrl_relay(uint8_t state)
{
    /* 使用静态变量保存数据，确保在异步发送时数据有效 */
    static uint8_t data[1];
    
    if (g_contact_imp_host == NULL) {
        LOG_E("Contact imp app not initialized");
        return -1;
    }
    
    data[0] = state;
    
    return host_send_cmd(g_contact_imp_host,
                        CMD_CTRL_CONTACT_IMP_RELAY,
                        data, 1U,
                        contact_imp_cmd_callback);
}

/**
 * @brief 控制贴靠阻抗检测启动/停止
 * @param state 检测状态（0=停止，1=启动）
 * @retval 0 成功（命令已加入队列）
 * @retval -1 失败
 */
int contact_imp_ctrl_detect(uint8_t state)
{
    /* 使用静态变量保存数据，确保在异步发送时数据有效 */
    static uint8_t data[1];
    
    if (g_contact_imp_host == NULL) {
        LOG_E("Contact imp app not initialized");
        return -1;
    }
    
    data[0] = state;
    
    return host_send_cmd(g_contact_imp_host,
                        CMD_CTRL_CONTACT_IMP_DETECT,
                        data, 1U,
                        contact_imp_cmd_callback);
}

/**
 * @brief 获取贴靠阻抗数据（异步）
 * @param callback 数据回调函数（可为NULL）
 * @retval 0 成功（命令已加入队列）
 * @retval -1 失败
 */
int contact_imp_get_data(contact_imp_data_callback_t callback)
{
    if (g_contact_imp_host == NULL) {
        LOG_E("Contact imp app not initialized");
        return -1;
    }
    
    /* 保存回调函数 */
    g_data_callback = callback;
    
    return host_send_cmd(g_contact_imp_host,
                        CMD_REQ_CONTACT_IMP_DATA,
                        NULL, 0U,
                        contact_imp_cmd_callback);
}

/**
 * @brief 设置数据回调函数
 * @param callback 回调函数指针
 */
void contact_imp_set_data_callback(contact_imp_data_callback_t callback)
{
    g_data_callback = callback;
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 命令应答回调函数
 * @param cmd 命令码
 * @param ack 应答码
 * @param data 应答数据指针
 * @param len 应答数据长度
 */
static void contact_imp_cmd_callback(uint8_t cmd, uint8_t ack, 
                                     const uint8_t *data, uint16_t len)
{
    if (ack == ACK_OK) {
        switch (cmd) {
        case CMD_CTRL_CONTACT_IMP_RELAY:
            LOG_I("Relay control OK");
            break;
            
        case CMD_CTRL_CONTACT_IMP_DETECT:
            LOG_I("Detect control OK");
            break;
            
        case CMD_REQ_CONTACT_IMP_DATA:
            /* 调用数据回调函数 */
            if (g_data_callback != NULL) {
                g_data_callback(data, len);
            }
            LOG_D("Contact impedance data received, len=%d", (int)len);
            
            float imp_data[6] = {0};
            memcpy(imp_data, data, 24);
            LOG_D("contact_imp_data:[0] = %f,[1] = %f,[0] = %f,[2] = %f,[3] = %f,[4] = %f,[5] = %f",
                  imp_data[0],imp_data[1],imp_data[2],imp_data[3],imp_data[4],imp_data[5]);
            break;
            
        default:
            break;
        }
    } else {
        LOG_E("Command 0x%02X failed, ack=0x%02X", cmd, ack);
    }
}
