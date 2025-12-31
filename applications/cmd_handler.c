/**
  ******************************************************************************
  * @file        : cmd_handler.c
  * @brief       : 命令处理服务实现
  * @details     本文件实现了cmd_handler.h中定义的命令处理功能。
  *              职责：
  *              - 根据命令码分发到具体处理函数
  *              - 调用业务模块接口
  *              - 构建统一格式的响应数据
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "cmd_handler.h"
#include "proto_custom.h"
#include "data_mgmt.h"
#include "port_ctrl.h"
#include "major_logic.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define  LOG_TAG             "cmd_handler"
#define  LOG_LVL             4
#include "log.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/**
 * @brief 响应回调函数指针
 */
static cmd_response_cb_t g_response_callback = NULL;

/* Private function prototypes -----------------------------------------------*/

/* Exported variables  -------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 设置响应回调函数
 * @param cb 回调函数指针
 */
void cmd_handler_set_response_callback(cmd_response_cb_t cb)
{
    g_response_callback = cb;
}

/**
 * @brief 处理获取软件版本命令
 */
void cmd_handle_get_software_version(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    const char *sw_version;
    uint16_t resp_len;

    (void)payload;
    (void)len;

    sw_version = data_mgmt_get_sw_version();
    LOG_D("SW VERSION = %s", sw_version);

    resp_len = (uint16_t)strlen(sw_version);
    memcpy(result->resp_data, sw_version, resp_len);
    result->resp_len = resp_len;
    result->ack_code = ACK_OK;
}

/**
 * @brief 处理设置硬件版本命令
 */
void cmd_handle_set_hardware_version(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    int ret;

    if (len == 0U || len >= HW_VERSION_BUFSZ) {
        result->ack_code = ACK_ERR_INVALID_PARAM;
        result->resp_len = 0;
    } else {
        ret = data_mgmt_set_hw_version((const char *)payload, len);
        result->ack_code = (ret == 0) ? ACK_OK : ACK_ERR_OPERATE_ABNORMAL;
        result->resp_len = 0;
    }
}

/**
 * @brief 处理获取硬件版本命令
 */
void cmd_handle_get_hardware_version(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    const char *hw_version;
    uint16_t resp_len;

    (void)payload;
    (void)len;

    hw_version = data_mgmt_get_hw_version();
    LOG_D("HW VERSION = %s", hw_version);

    resp_len = (uint16_t)strlen(hw_version);
    memcpy(result->resp_data, hw_version, resp_len);
    result->resp_len = resp_len;
    result->ack_code = ACK_OK;
}

/**
 * @brief 处理设置序列号命令
 */
void cmd_handle_set_serial_number(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    int ret;

    if (len == 0U || len >= SN_NUMBER_BUFSZ) {
        result->ack_code = ACK_ERR_INVALID_PARAM;
        result->resp_len = 0;
    } else {
        ret = data_mgmt_set_sn_number((const char *)payload, len);
        result->ack_code = (ret == 0) ? ACK_OK : ACK_ERR_OPERATE_ABNORMAL;
        result->resp_len = 0;
    }
}

/**
 * @brief 处理获取序列号命令
 */
void cmd_handle_get_serial_number(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    const char *sn_number;
    uint16_t resp_len;

    (void)payload;
    (void)len;

    sn_number = data_mgmt_get_sn_number();
    LOG_D("SN NUMBER = %s", sn_number);

    resp_len = (uint16_t)strlen(sn_number);
    memcpy(result->resp_data, sn_number, resp_len);
    result->resp_len = resp_len;
    result->ack_code = ACK_OK;
}

/**
 * @brief 处理系统复位命令
 * @param payload 数据载荷（未使用）
 * @param len 数据载荷长度（未使用）
 * @param result 命令处理结果
 */
void cmd_handle_soft_reset(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    (void)payload;
    (void)len;

    result->ack_code = ACK_OK;
    result->resp_len = 0;

    /* 延时后执行软件复位（由应用层协调） */
    major_logic_request_reset();
    LOG_D("System reset requested");
}

/**
 * @brief 处理系统自检命令
 * @param payload 数据载荷（未使用）
 * @param len 数据载荷长度（未使用）
 * @param result 命令处理结果
 */
void cmd_handle_self_check(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    (void)payload;
    (void)len;

    /* 耗时命令先应答正在执行 */
    result->ack_code = ACK_IN_PROGERESS;
    result->resp_len = 0;

    /* TODO: 在后台任务中执行自检，完成后发送ACK_OK */
    LOG_D("Self check started");
}

/**
 * @brief 处理低功耗模式命令
 * @param payload 数据载荷（未使用）
 * @param len 数据载荷长度（未使用）
 * @param result 命令处理结果
 */
void cmd_handle_low_power_mode(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    (void)payload;
    (void)len;

    result->ack_code = ACK_OK;
    result->resp_len = 0;

    /* TODO: 实现低功耗模式控制 */
    LOG_D("Low power mode command received");
}

/**
 * @brief 处理在线升级命令
 * @param payload 数据载荷（未使用）
 * @param len 数据载荷长度（未使用）
 * @param result 命令处理结果
 */
void cmd_handle_iap(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    (void)payload;
    (void)len;

    /* 耗时命令先应答正在执行 */
    result->ack_code = ACK_IN_PROGERESS;
    result->resp_len = 0;

    /* TODO: 在后台任务中执行IAP，完成后发送ACK_OK */
    LOG_D("IAP started");
}

/**
 * @brief 处理上传模式命令
 * @param payload 数据载荷（未使用）
 * @param len 数据载荷长度（未使用）
 * @param result 命令处理结果
 */
void cmd_handle_upload_mode(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    (void)payload;
    (void)len;

    result->ack_code = ACK_OK;
    result->resp_len = 0;

    /* TODO: 实现上传模式控制 */
    LOG_D("Upload mode command received");
}

/**
 * @brief 处理状态上传命令
 * @param payload 数据载荷（未使用）
 * @param len 数据载荷长度（未使用）
 * @param result 命令处理结果
 */
void cmd_handle_status_upload(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    (void)payload;
    (void)len;

    /* 状态上传是非应答型命令，不需要回复 */
    result->ack_code = ACK_OK;
    result->resp_len = 0;

    LOG_D("Status upload received (no ACK required)");
}


/**
 * @brief 处理选择导管命令
 * @param payload 数据载荷（2字节：type + spec）
 * @param len 数据载荷长度
 * @param result 命令处理结果
 */
void cmd_handle_select_catheter(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    int ret = 0;
    port_cath_t catheter;
    
    /* 参数检查：需要2字节（type + spec） */
    if ((payload == NULL) || (len != 2U)) {
        result->ack_code = ACK_ERR_INVALID_PARAM;
        result->resp_len = 0;
        LOG_E("Invalid parameter: len=%d", len);
        return;
    }
    
    /* 解析导管类型和规格 */
    catheter.type = (port_cath_type_t)payload[0];
    catheter.spec = (port_cath_spec_t)payload[1];
    
    /* 验证参数范围 */
    if ((catheter.type > PORT_CATH_TYPE_FOCAL) || 
        (catheter.spec > PORT_CATH_SPEC_C)) {
        result->ack_code = ACK_ERR_INVALID_PARAM;
        result->resp_len = 0;
        LOG_E("Invalid catheter: type=%d, spec=%d", catheter.type, catheter.spec);
        return;
    }
    
    /* 验证PVI1不需要规格参数 */
    if ((catheter.type == PORT_CATH_TYPE_PVI1) && (catheter.spec != PORT_CATH_SPEC_A)) {
        result->ack_code = ACK_ERR_INVALID_PARAM;
        result->resp_len = 0;
        LOG_E("PVI1 should use spec A");
        return;
    }
    
    /* 调用端口控制模块选择导管 */
    ret = port_crtl_select_catheter(catheter);
    if (ret == 0) {
        result->ack_code = ACK_OK;
        result->resp_len = 0;
        LOG_D("Catheter selected: type=%d, spec=%d", catheter.type, catheter.spec);
    } else {
        result->ack_code = ACK_ERR_OPERATE_ABNORMAL;
        result->resp_len = 0;
        LOG_E("Failed to select catheter: ret=%d", ret);
    }
}

/**
 * @brief 处理获取导管信息命令
 * @param payload 数据载荷（未使用）
 * @param len 数据载荷长度（未使用）
 * @param result 命令处理结果
 */
void cmd_handle_get_catheter(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    const port_cath_t *catheter = NULL;
    
    (void)payload;
    (void)len;
    
    /* 获取当前导管信息 */
    catheter = port_ctrl_get_catheter();
    if (catheter == NULL) {
        result->ack_code = ACK_ERR_OPERATE_ABNORMAL;
        result->resp_len = 0;
        LOG_E("Failed to get catheter info");
        return;
    }
    
    /* 返回当前导管信息（2字节：type + spec） */
    result->resp_data[0] = (uint8_t)catheter->type;
    result->resp_data[1] = (uint8_t)catheter->spec;
    result->resp_len = 2U;
    result->ack_code = ACK_OK;
    
    LOG_D("Get catheter: type=%d, spec=%d", catheter->type, catheter->spec);
}

/**
 * @brief 处理设置工作模式命令
 * @param payload 数据载荷（1字节：工作模式）
 * @param len 数据载荷长度
 * @param result 命令处理结果
 */
void cmd_handle_set_work_mode(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    int ret = 0;
    port_mode_t mode;
    
    /* 参数检查：需要1字节 */
    if ((payload == NULL) || (len != 1U)) {
        result->ack_code = ACK_ERR_INVALID_PARAM;
        result->resp_len = 0;
        LOG_E("Invalid parameter: len=%d", len);
        return;
    }
    
    /* 解析工作模式 */
    mode = (port_mode_t)payload[0];
    
    /* 验证参数范围 */
    if (mode > PORT_MODE_ABLATION) {
        result->ack_code = ACK_ERR_INVALID_PARAM;
        result->resp_len = 0;
        LOG_E("Invalid work mode: %d", mode);
        return;
    }
    
    /* 调用端口控制模块设置工作模式 */
    ret = port_ctrl_set_mode(mode);
    if (ret == 0) {
        result->ack_code = ACK_OK;
        result->resp_len = 0;
        LOG_D("Work mode set: %d", mode);
    } else {
        result->ack_code = ACK_ERR_OPERATE_ABNORMAL;
        result->resp_len = 0;
        LOG_E("Failed to set work mode: ret=%d", ret);
    }
}

/**
 * @brief 处理获取工作模式命令
 * @param payload 数据载荷（未使用）
 * @param len 数据载荷长度（未使用）
 * @param result 命令处理结果
 */
void cmd_handle_get_work_mode(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    port_mode_t mode;
    
    (void)payload;
    (void)len;
    
    /* 获取当前工作模式 */
    mode = port_ctrl_get_mode();
    
    /* 返回当前工作模式（1字节） */
    result->resp_data[0] = (uint8_t)mode;
    result->resp_len = 1U;
    result->ack_code = ACK_OK;
    
    LOG_D("Get work mode: %d", mode);
}

/**
 * @brief 处理端口控制命令（电极位图控制）
 * @param payload 数据载荷（4字节：电极位图，小端序）
 * @param len 数据载荷长度
 * @param result 命令处理结果
 */
void cmd_handle_port_ctrl(const uint8_t *payload, uint16_t len, cmd_result_t *result)
{
    int ret = 0;
    uint32_t pole_elec_bitmap = 0U;
    
    /* 参数检查：需要4字节 */
    if ((payload == NULL) || (len != 4U)) {
        result->ack_code = ACK_ERR_INVALID_PARAM;
        result->resp_len = 0;
        LOG_E("Invalid parameter: len=%d", len);
        return;
    }
    
    /* 解析电极位图（小端序） */
    pole_elec_bitmap = ((uint32_t)payload[0]) |
                       ((uint32_t)payload[1] << 8U) |
                       ((uint32_t)payload[2] << 16U) |
                       ((uint32_t)payload[3] << 24U);
    
    /* 调用端口控制模块控制电极 */
    ret = port_ctrl_elec(pole_elec_bitmap);
    if (ret == 0) {
        result->ack_code = ACK_OK;
        result->resp_len = 0;
        LOG_D("Port control: bitmap=0x%08X", pole_elec_bitmap);
    } else {
        result->ack_code = ACK_ERR_OPERATE_ABNORMAL;
        result->resp_len = 0;
        LOG_E("Failed to control port: ret=%d", ret);
    }
}

/* Private functions ---------------------------------------------------------*/

