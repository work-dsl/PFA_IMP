/**
  ******************************************************************************
  * @file        : custom_host.h
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2024-12-XX
  * @brief       : 自定义协议主机接口
  * @details     本文件定义了自定义协议主机的接口函数。
  *              主机负责向从机发送命令、接收应答并处理从机主动上传的数据。
  * @attention   使用前需要先调用 host_init() 进行初始化
  ******************************************************************************
  * @history     :
  *         V1.0 : 1.实现主机初始化、命令发送、应答处理功能
  *                2.支持被动接收从机握手请求并响应
  *                3.支持超时重传机制
  *                4.支持异步回调机制
  *
  ******************************************************************************
  */

#ifndef CUSTOM_HOST_H
#define CUSTOM_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 从机信息结构体
 */
typedef struct {
    char sw_version[64];      /**< 软件版本信息（ASCII字符串） */
    char hw_version[64];      /**< 硬件版本信息（ASCII字符串） */
    char sn_number[64];       /**< 序列号信息（ASCII字符串） */
    uint8_t info_valid;       /**< 信息是否有效（1=有效，0=无效） */
} slave_info_t;

/**
 * @brief 主机状态枚举
 */
typedef enum {
    HOST_STATE_WAIT_HANDSHAKE = 0,      /**< 等待握手状态（等待接收从机的握手请求） */
    HOST_STATE_HANDSHAKE_PENDING,      /**< 握手待应答状态（已发送握手命令，等待从机应答） */
    HOST_STATE_QUERY_INFO,             /**< 查询从机信息状态（握手成功后自动查询通用信息） */
    HOST_STATE_ACTIVE                   /**< 活动状态（已握手完成，信息查询完成，可以正常通信） */
} host_state_t;

/**
 * @brief 主机实例句柄（不透明指针）
 */
typedef struct host_inst_s *host_handle_t;

/**
 * @brief 命令应答回调函数类型
 * @param cmd 命令码
 * @param ack 应答码
 * @param data 应答数据指针（可为NULL）
 * @param len 应答数据长度
 */
typedef void (*host_cmd_callback_t)(uint8_t cmd, uint8_t ack, const uint8_t *data, uint16_t len);

/**
 * @brief 从机主动上传回调函数类型
 * @param cmd 上传命令码
 * @param data 上传数据指针（可为NULL）
 * @param len 上传数据长度
 */
typedef void (*host_upload_callback_t)(uint8_t cmd, const uint8_t *data, uint16_t len);

/* Exported constants --------------------------------------------------------*/

/**
 * @defgroup 主机专有命令
 * @{
 */

/**
 * @defgroup 本板卡 -> 贴靠检测板
 * @{
 */
#define HOST_CMD_CTRL_CONTACT_IMP_RELAY     (0x31U)  /**< 控制贴靠阻抗继电器开/关 */
#define HOST_CMD_CTRL_CONTACT_IMP_DETECT    (0x32U)  /**< 控制贴靠阻抗检测启动/停止 */
#define HOST_CMD_REQ_CONTACT_IMP_DATA       (0x33U)  /**< 获取贴靠阻抗数据 */
#define HOST_CMD_SET_CALIBRATION_COEFFICIENT (0x35U) /**< 设置校准系数 */
#define HOST_CMD_GET_CALIBRATION_COEFFICIENT (0x36U) /**< 获取校准系数 */
/**
 * @}
 */
 
/**
 * @defgroup 贴靠检测板 -> 本板卡
 * @{
 */
#define HOST_CMD_UPLOAD_CONTACT_IMP_DATA    (0x34U)  /**< 主动上传贴靠阻抗数据 */
/**
 * @}
 */

/**
 * @}
 */

/* Exported macros -----------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化主机实例
 * @param serial_name 串口设备名称（如"uart1"）
 * @param board_addr 从机板卡地址（必须指定，用于验证接收帧的地址匹配）
 * @param upload_cb 从机主动上传数据回调函数（可为NULL）
 * @return 主机实例句柄，失败返回NULL
 * @details 初始化串口设备、协议解析器和相关回调函数
 */
host_handle_t host_init(const char *serial_name, uint8_t board_addr, host_upload_callback_t upload_cb);

/**
 * @brief 轮询主机协议解析器
 * @param host 主机实例句柄
 * @details 从串口FIFO中读取数据并送入协议解析器处理。
 *          从帧队列中读取完整帧并处理。
 *          处理握手流程、命令发送、超时检测等。
 *          建议在主循环或任务中定期调用此函数。
 */
void host_task(host_handle_t host);

/**
 * @brief 发送命令（异步）
 * @param host 主机实例句柄
 * @param cmd 命令码
 * @param data 数据载荷指针（可为NULL）
 * @param data_len 数据载荷长度
 * @param callback 命令应答回调函数（可为NULL）
 * @retval 0 成功（命令已加入队列）
 * @retval -1 失败（参数无效、队列满或未握手）
 * @details 将命令加入发送队列，立即返回。实际发送在 host_task() 中处理。
 *          只有握手完成后（HOST_STATE_ACTIVE）才能发送业务命令。
 */
int host_send_cmd(host_handle_t host, uint8_t cmd, const uint8_t *data, uint16_t data_len,
                  host_cmd_callback_t callback);

/**
 * @brief 获取主机当前状态
 * @param host 主机实例句柄
 * @return 当前主机状态
 */
host_state_t host_get_state(host_handle_t host);

/**
 * @brief 重置主机状态
 * @param host 主机实例句柄
 * @details 清除待应答命令和命令队列，状态重置为等待握手状态
 */
void host_reset(host_handle_t host);

/**
 * @brief 获取从机信息
 * @param host 主机实例句柄
 * @param info 从机信息结构体指针（输出）
 * @retval 0 成功
 * @retval -1 失败（参数无效或信息未查询）
 * @details 获取已查询的从机信息（软件版本、硬件版本、序列号）
 */
int host_get_slave_info(host_handle_t host, slave_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* CUSTOM_HOST_H */
