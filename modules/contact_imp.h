/**
  ******************************************************************************
  * @file        : contact_imp.h
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2024-12-XX
  * @brief       : 贴靠检测板应用层接口
  * @details     本文件定义了贴靠检测板应用层的接口函数。
  *              职责：
  *              - 封装对贴靠检测板的业务操作
  *              - 控制贴靠阻抗继电器
  *              - 控制贴靠阻抗检测
  *              - 获取贴靠阻抗数据
  * @attention   使用前需要先调用 contact_imp_init() 进行初始化
  ******************************************************************************
  * @history     :
  *         V1.0 : 1.实现贴靠检测板应用层接口
  *                2.封装继电器控制、检测控制、数据获取功能
  *
  ******************************************************************************
  */

#ifndef CONTACT_IMP_APP_H
#define CONTACT_IMP_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "custom_host.h"

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 贴靠阻抗数据回调函数类型
 * @param data 阻抗数据指针
 * @param len 数据长度
 */
typedef void (*contact_imp_data_callback_t)(const uint8_t *data, uint16_t len);

/* Exported constants --------------------------------------------------------*/

/* Exported macros -----------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化贴靠检测板应用层
 * @param host 主机实例句柄（与贴靠检测板通信的通道）
 * @retval 0 成功
 * @retval -1 失败
 */
int contact_imp_init(host_handle_t host);

/**
 * @brief 控制贴靠阻抗继电器
 * @param state 继电器状态（0=关闭，1=打开）
 * @retval 0 成功（命令已加入队列）
 * @retval -1 失败
 */
int contact_imp_ctrl_relay(uint8_t state);

/**
 * @brief 控制贴靠阻抗检测启动/停止
 * @param state 检测状态（0=停止，1=启动）
 * @retval 0 成功（命令已加入队列）
 * @retval -1 失败
 */
int contact_imp_ctrl_detect(uint8_t state);

/**
 * @brief 获取贴靠阻抗数据（异步）
 * @param callback 数据回调函数（可为NULL）
 * @retval 0 成功（命令已加入队列）
 * @retval -1 失败
 */
int contact_imp_get_data(contact_imp_data_callback_t callback);

/**
 * @brief 设置数据回调函数
 * @param callback 回调函数指针
 */
void contact_imp_set_data_callback(contact_imp_data_callback_t callback);

/**
 * @brief 存储贴靠检测板主动上传的贴靠阻抗数据（0x34）到内部 imp_data
 * @param data 上传数据指针（6×float，24 字节）
 * @param len 数据长度（应 >= 24 才写入）
 */
void contact_imp_store_upload_data(const uint8_t *data, uint16_t len);

/**
 * @brief 获取当前贴靠阻抗数据用于向上位机上传（0x38）
 * @param buf 输出缓冲区指针
 * @param buf_size 缓冲区大小（应 >= 24 才拷贝）
 */
void contact_imp_get_data_for_upload(uint8_t *buf, uint16_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* CONTACT_IMP_APP_H */
