/**
  ******************************************************************************
  * @file        : custom_slave.h
  * @brief       : 自定义协议从机接口
  * @details     本文件定义了自定义协议从机的接口函数。
  *              从机负责接收主机命令、处理命令并回送响应。
  * @attention   使用前需要先调用 slave_proto_init() 进行初始化
  ******************************************************************************
  */

#ifndef CUSTOM_SLAVE_H
#define CUSTOM_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 从机握手状态枚举
 */
typedef enum {
    SLAVE_STATE_WAIT_HANDSHAKE = 0,  /**< 等待握手状态 */
    SLAVE_STATE_ACTIVE               /**< 活动状态（已握手） */
} slave_state_t;

/* Exported constants --------------------------------------------------------*/

#define HANDSHAKE_INTERVAL_MS               (1000U) /**< 握手间隔：1秒（1Hz） */

/**
 * @defgroup 从机专有命令
 * @{
 */

/**
 * @defgroup 上位机 -> 本板卡
 * @{
 */
#define CMD_SELECT_CATHETER                 (0x30U)  /**< 选择导管 */
#define CMD_GET_CATHETER_INFO               (0x31U)  /**< 获取导管信息 */
#define CMD_SET_WORK_MODE                   (0x32U)  /**< 设置工作模式 */
#define CMD_GET_WORK_MODE                   (0x33U)  /**< 获取工作模式 */
#define CMD_PORT_CTRL                       (0x34U)  /**< 端口控制 */
#define CMD_GET_LOOP_IMP_DATA               (0x35U)  /**< 获取回路阻抗数据 */
#define CMD_GET_CONTACT_IMP_DATA            (0x36U)  /**< 获取贴靠阻抗数据 */
/**
 * @}
 */
 
/**
 * @defgroup 本板卡 -> 上位机（上位机不用应答）
 * @{
 */
#define CMD_UPLOAD_LOOP_IMP_DATA            (0x37U)  /**< 主动上传回路阻抗数据（以20ms的周期） */
#define CMD_UPLOAD_CONTACT_IMP_DATA         (0x38U)  /**< 主动上传贴靠阻抗数据（以20ms的周期） */
/**
 * @}
 */

/* Exported macros -----------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

int  slave_proto_init(void);
void slave_proto_task(void);
void slave_process_frame(const uint8_t *frame, uint16_t len);
slave_state_t slave_get_state(void);
int slave_send_upload_frame(uint8_t cmd, const uint8_t *data, uint16_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* CUSTOM_SLAVE_H */