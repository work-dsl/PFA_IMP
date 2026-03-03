/**
  ******************************************************************************
  * @file        : custom_host.c
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2024-12-XX
  * @brief       : 自定义协议主机实现
  * @details     本文件实现了custom_host.h中定义的主机功能。
  *              职责：
  *              - 初始化协议解析器
  *              - 被动接收从机握手请求并响应
  *              - 发送命令并处理应答
  *              - 处理从机主动上传数据
  *              - 超时重传机制
  * @attention   使用前需要先调用 host_init() 进行初始化
  ******************************************************************************
  * @history     :
  *         V1.0 : 1.实现主机初始化、命令发送、应答处理功能
  *                2.支持被动接收从机握手请求并响应
  *                3.支持超时重传机制（500ms超时，3次重传）
  *                4.支持异步回调机制
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "custom_host.h"
#include <stdint.h>
#include <string.h>
#include "proto.h"
#include "proto_custom.h"
#include "kfifo.h"
#include "serial.h"
#include "board.h"

#define  LOG_TAG             "custom_host"
#define  LOG_LVL             3
#include "log.h"

/* Private typedef -----------------------------------------------------------*/

/**
 * @brief 查询状态枚举
 */
typedef enum {
    QUERY_STATE_IDLE = 0,                /**< 空闲状态 */
    QUERY_STATE_SW_VERSION,              /**< 查询软件版本 */
    QUERY_STATE_HW_VERSION,              /**< 查询硬件版本 */
    QUERY_STATE_SN_NUMBER,               /**< 查询序列号 */
    QUERY_STATE_COMPLETE                 /**< 查询完成 */
} query_state_t;

/**
 * @brief 命令请求结构体
 */
typedef struct {
    uint8_t cmd;                        /**< 命令码 */
    const uint8_t *data;                /**< 数据载荷指针 */
    uint16_t data_len;                  /**< 数据载荷长度 */
    host_cmd_callback_t callback;       /**< 应答回调函数 */
    uint32_t timeout_ms;                /**< 超时时间（毫秒） */
    uint8_t max_retry;                  /**< 最大重传次数 */
} cmd_request_t;

/**
 * @brief 命令队列结构体
 */
typedef struct {
    cmd_request_t items[8U];           /**< 命令队列数组 */
    uint8_t head;                      /**< 队列头索引 */
    uint8_t tail;                      /**< 队列尾索引 */
    uint8_t count;                     /**< 队列中命令数量 */
} cmd_queue_t;

/**
 * @brief 主机实例结构体
 */
typedef struct host_inst_s {
    serial_t *port;                     /**< 串口设备指针 */
    proto_t proto;                     /**< 协议解析器实例 */
    uint8_t rx_buf[PROTO_CUSTOM_FRAME_MAX_LEN];  /**< 解析器内部组包用的临时buffer */
    uint8_t valid_fifo_buf[8U * PROTO_CUSTOM_FRAME_MAX_LEN];  /**< 有效数据队列缓冲区 */
    kfifo_t valid_fifo;                /**< 有效数据输出队列 */
    volatile host_state_t state;       /**< 主机当前状态 */
    uint8_t board_addr;                /**< 从机板卡地址 */
    host_upload_callback_t upload_cb;   /**< 从机主动上传回调函数 */
    
    /* 从机信息 */
    slave_info_t slave_info;            /**< 从机信息 */
    query_state_t query_state;          /**< 查询状态 */
    uint32_t query_timeout_tick;        /**< 查询超时时间戳 */
    
    /* 命令队列 */
    cmd_queue_t cmd_queue;             /**< 命令队列 */
    
    /* 当前待应答命令 */
    cmd_request_t pending_cmd;         /**< 当前待应答命令 */
    uint8_t has_pending_cmd;            /**< 是否有待应答命令 */
    uint32_t pending_cmd_tick;          /**< 待应答命令的时间戳 */
    uint8_t pending_cmd_retry;           /**< 待应答命令的重传计数 */
    
    /* 握手相关 */
    uint8_t has_handshake_pending;      /**< 是否在等待握手应答 */
    uint32_t handshake_tick;            /**< 握手命令的时间戳 */
    uint8_t handshake_retry;            /**< 握手命令的重传计数 */
} host_inst_t;

/* Private define ------------------------------------------------------------*/

#define HOST_CMD_QUEUE_SIZE             (8U)    /**< 命令队列大小 */
#define HOST_CMD_TIMEOUT_MS             (500U)  /**< 命令超时时间（毫秒） */
#define HOST_CMD_MAX_RETRY              (3U)    /**< 命令最大重传次数 */
#define HOST_HANDSHAKE_TIMEOUT_MS       (500U)  /**< 握手超时时间（毫秒） */
#define HOST_HANDSHAKE_MAX_RETRY        (3U)    /**< 握手最大重传次数 */

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* 支持多个主机实例（最大数量可配置） */
#define HOST_MAX_INSTANCES            (4U)    /**< 最大主机实例数量 */
static host_inst_t g_host_instances[HOST_MAX_INSTANCES];  /**< 主机实例数组 */
static uint8_t g_host_instance_count = 0U;    /**< 当前已创建的主机实例数量 */

/* Exported variables  -------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static void host_proto_error_callback(void *inst, proto_err_t err);
static void host_process_frame(host_inst_t *host, const uint8_t *frame, uint16_t len);
static void host_process_handshake_request(host_inst_t *host, const uint8_t *frame, uint16_t len);
static void host_process_handshake_response(host_inst_t *host, const uint8_t *frame, uint16_t len);
static void host_send_handshake_cmd(host_inst_t *host);
static int host_send_cmd_frame(host_inst_t *host, uint8_t cmd, const uint8_t *data, uint16_t data_len);
static void host_check_timeout(host_inst_t *host);
static void host_send_pending_cmd(host_inst_t *host);
static int host_cmd_queue_enqueue(cmd_queue_t *queue, const cmd_request_t *req);
static int host_cmd_queue_dequeue(cmd_queue_t *queue, cmd_request_t *req);
static int host_cmd_queue_is_full(const cmd_queue_t *queue);
static int host_cmd_queue_is_empty(const cmd_queue_t *queue);
static void host_process_query_info(host_inst_t *host);
static void host_query_callback_internal(host_inst_t *host, uint8_t cmd, uint8_t ack, 
                                         const uint8_t *data, uint16_t len);
static void host_query_callback(uint8_t cmd, uint8_t ack, const uint8_t *data, uint16_t len);

/* Exported functions --------------------------------------------------------*/
extern uint32_t HAL_GetTick(void);

/**
 * @brief 初始化主机实例
 * @param serial_name 串口设备名称
 * @param board_addr 从机板卡地址
 * @param upload_cb 从机主动上传数据回调函数
 * @return 主机实例句柄，失败返回NULL
 */
host_handle_t host_init(const char *serial_name, uint8_t board_addr, host_upload_callback_t upload_cb)
{
    int ret;
    host_inst_t *host;
    uint8_t i;
    
    if (serial_name == NULL) {
        return NULL;
    }
    
    /* 查找可用的实例槽位 */
    host = NULL;
    for (i = 0U; i < HOST_MAX_INSTANCES; i++) {
        if (g_host_instances[i].port == NULL) {
            /* 找到空闲槽位 */
            host = &g_host_instances[i];
            break;
        }
    }
    
    if (host == NULL) {
        LOG_E("No available host instance slot");
        return NULL;
    }
    
    /* 清零初始化 */
    (void)memset(host, 0, sizeof(host_inst_t));
    
    /* 查找串口设备 */
    host->port = serial_find(serial_name);
    if (host->port == NULL) {
        LOG_D("Failed to find %s\r\n", serial_name);
        return NULL;
    }
    
    /* 检查串口是否已被其他实例使用 */
    for (i = 0U; i < HOST_MAX_INSTANCES; i++) {
        if ((&g_host_instances[i] != host) && 
            (g_host_instances[i].port == host->port)) {
            LOG_E("Serial port %s already in use", serial_name);
            host->port = NULL;  /* 清除引用，避免误用 */
            return NULL;
        }
    }
    
    /* 初始化串口设备 */
    ret = serial_open(host->port);
    if (ret != 0) {
        LOG_D("Failed to initialize %s: %d\r\n", serial_name, ret);
        host->port = NULL;  /* 清除引用 */
        return NULL;
    }
    
    /* 保存配置 */
    host->board_addr = board_addr;
    host->upload_cb = upload_cb;
    host->state = HOST_STATE_WAIT_HANDSHAKE;
    host->query_state = QUERY_STATE_IDLE;
    host->slave_info.info_valid = 0U;
    
    /* 清零从机信息 */
    (void)memset(&host->slave_info, 0, sizeof(slave_info_t));
    
    /* 初始化有效数据输出队列 */
    ret = kfifo_init(&host->valid_fifo,
                     host->valid_fifo_buf,
                     (unsigned int)sizeof(host->valid_fifo_buf),
                     1U);  /* 元素大小为1字节 */
    if (ret != 0) {
        LOG_D("Failed to initialize frame queue: %d\r\n", ret);
        host->port = NULL;  /* 清除引用 */
        return NULL;
    }
    
    /* 初始化协议解析器（使用队列模式） */
    ret = proto_init(&host->proto,
                     &CUSTOM_FMT,
                     host->rx_buf,
                     (uint16_t)sizeof(host->rx_buf),
                     &host->valid_fifo,
                     host_proto_error_callback);
    if (ret != 0) {
        LOG_D("Failed to initialize proto: %d\r\n", ret);
        host->port = NULL;  /* 清除引用 */
        return NULL;
    }
    
    /* 设置时间戳回调函数，启用超时检测 */
    proto_set_tick_cb(&host->proto, HAL_GetTick);
    
    /* 初始化命令队列 */
    host->cmd_queue.head = 0U;
    host->cmd_queue.tail = 0U;
    host->cmd_queue.count = 0U;
    
    g_host_instance_count++;
    LOG_I("Host initialized: serial=%s, board_addr=0x%02X, instance=%d", 
          serial_name, board_addr, (int)(g_host_instance_count));
    
    return (host_handle_t)host;
}

/**
 * @brief 轮询主机协议解析器
 * @param host 主机实例句柄
 */
void host_task(host_handle_t host)
{
    uint8_t frame_buf[PROTO_CUSTOM_FRAME_MAX_LEN];
    unsigned int frame_len;
    host_inst_t *h = (host_inst_t *)host;
    
    if (h == NULL) {
        return;
    }
    
    /* 轮询协议解析器 */
    if (h->port != NULL) {
        (void)proto_poll(&h->proto, &h->port->rx_fifo);
    }
    
    /* 从输出队列按帧读取并处理（FIFO 格式：2 字节记录长度前缀(小端) + payload，由 proto 校验后写入） */
    while (kfifo_len(&h->valid_fifo) >= 2U) {
        unsigned int avail = kfifo_len(&h->valid_fifo);
        uint16_t record_len;

        /* peek 前 2 字节得到本帧 payload 长度（解析器 push 时写入，非协议 LEN 字段） */
        if (kfifo_out_peek(&h->valid_fifo, frame_buf, 2U) != 2U) {
            break;
        }
        record_len = (uint16_t)((uint16_t)frame_buf[0] | ((uint16_t)frame_buf[1] << 8U));

        /* 长度合法性：自定义协议 payload 长度范围 [5, 60] */
        if ((record_len < 5U) || (record_len > (PROTO_CUSTOM_FRAME_MAX_LEN - 4U))) {
            (void)kfifo_out(&h->valid_fifo, frame_buf, 1U);
            continue;
        }
        if (avail < (unsigned int)(2U + record_len)) {
            break;
        }
        (void)kfifo_out(&h->valid_fifo, frame_buf, 2U);
        frame_len = kfifo_out(&h->valid_fifo, frame_buf, (unsigned int)record_len);
        if (frame_len == (unsigned int)record_len) {
            host_process_frame(h, frame_buf, (uint16_t)frame_len);
        }
    }
    
    /* 检查超时 */
    host_check_timeout(h);
    
    /* 处理查询状态机 */
    if (h->state == HOST_STATE_QUERY_INFO) {
        host_process_query_info(h);
    }
    
    /* 发送待发送的命令 */
    if (h->state == HOST_STATE_ACTIVE) {
        if ((h->has_pending_cmd == 0U) && (host_cmd_queue_is_empty(&h->cmd_queue) == 0)) {
            host_send_pending_cmd(h);
        }
    } else if (h->state == HOST_STATE_QUERY_INFO) {
        /* 查询状态下也可以发送查询命令 */
        if ((h->has_pending_cmd == 0U) && (host_cmd_queue_is_empty(&h->cmd_queue) == 0)) {
            host_send_pending_cmd(h);
        }
    }
}

/**
 * @brief 发送命令（异步）
 * @param host 主机实例句柄
 * @param cmd 命令码
 * @param data 数据载荷指针
 * @param data_len 数据载荷长度
 * @param callback 命令应答回调函数
 * @retval 0 成功
 * @retval -1 失败
 */
int host_send_cmd(host_handle_t host, uint8_t cmd, const uint8_t *data, uint16_t data_len,
                  host_cmd_callback_t callback)
{
    host_inst_t *h = (host_inst_t *)host;
    cmd_request_t req;
    
    if (h == NULL) {
        return -1;
    }
    
    /* 检查状态：活动状态可以发送所有命令，查询状态只能发送查询命令 */
    if (h->state == HOST_STATE_ACTIVE) {
        /* 活动状态，可以发送所有命令 */
    } else if (h->state == HOST_STATE_QUERY_INFO) {
        /* 查询状态，只能发送查询命令 */
        if ((cmd != CMD_GET_SOFTWARE_VERSION) && 
            (cmd != CMD_GET_HARDWARE_VERSION) && 
            (cmd != CMD_GET_SERIAL_NUMBER)) {
            LOG_D("Host in query state, cannot send cmd=0x%02X", cmd);
            return -1;
        }
    } else {
        /* 其他状态不能发送命令 */
        LOG_D("Host not active, cannot send cmd=0x%02X", cmd);
        return -1;
    }
    
    /* 构建命令请求 */
    req.cmd = cmd;
    req.data = data;
    req.data_len = data_len;
    req.callback = callback;
    req.timeout_ms = HOST_CMD_TIMEOUT_MS;
    req.max_retry = HOST_CMD_MAX_RETRY;
    
    /* 加入命令队列 */
    if (host_cmd_queue_enqueue(&h->cmd_queue, &req) != 0) {
        LOG_D("Command queue full, cmd=0x%02X", cmd);
        return -1;
    }
    
    LOG_D("Command enqueued: cmd=0x%02X", cmd);
    return 0;
}

/**
 * @brief 获取主机当前状态
 * @param host 主机实例句柄
 * @return 当前主机状态
 */
host_state_t host_get_state(host_handle_t host)
{
    host_inst_t *h = (host_inst_t *)host;
    
    if (h == NULL) {
        return HOST_STATE_WAIT_HANDSHAKE;
    }
    
    return h->state;
}

/**
 * @brief 重置主机状态
 * @param host 主机实例句柄
 */
void host_reset(host_handle_t host)
{
    host_inst_t *h = (host_inst_t *)host;
    
    if (h == NULL) {
        return;
    }
    
    /* 清除待应答命令 */
    h->has_pending_cmd = 0U;
    h->has_handshake_pending = 0U;
    
    /* 清空命令队列 */
    h->cmd_queue.head = 0U;
    h->cmd_queue.tail = 0U;
    h->cmd_queue.count = 0U;
    
    /* 重置查询状态 */
    h->query_state = QUERY_STATE_IDLE;
    h->slave_info.info_valid = 0U;
    (void)memset(&h->slave_info, 0, sizeof(slave_info_t));
    
    /* 重置状态 */
    h->state = HOST_STATE_WAIT_HANDSHAKE;
    
    /* 重置协议解析器 */
    proto_reset(&h->proto);
    
    LOG_I("Host reset");
    /* 注意：不清除 port 引用，保持实例可用 */
}

/**
 * @brief 获取从机信息
 * @param host 主机实例句柄
 * @param info 从机信息结构体指针（输出）
 * @retval 0 成功
 * @retval -1 失败（参数无效或信息未查询）
 */
int host_get_slave_info(host_handle_t host, slave_info_t *info)
{
    host_inst_t *h = (host_inst_t *)host;
    
    if ((h == NULL) || (info == NULL)) {
        return -1;
    }
    
    if (h->slave_info.info_valid == 0U) {
        return -1;  /* 信息未查询或无效 */
    }
    
    *info = h->slave_info;
    return 0;
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 协议解析器错误回调函数
 * @param inst 协议解析器实例指针
 * @param err 错误码
 */
static void host_proto_error_callback(void *inst, proto_err_t err)
{
    (void)inst;
    (void)err;
    /* 主机端不发送错误应答，只记录日志 */
    LOG_D("Proto error: %d", (int)err);
}

/**
 * @brief 处理接收到的帧
 * @param host 主机实例指针
 * @param frame 接收到的payload数据（已去除帧头、校验码、帧尾）
 * @param len payload长度
 */
static void host_process_frame(host_inst_t *host, const uint8_t *frame, uint16_t len)
{
    /* frame结构（payload，已去除帧头、校验码、帧尾）：
     * [0..1]  = LEN（长度字段，小端）
     * [2]     = PRODUCT（产品地址，PRODUCT_ADDR）
     * [3]     = CMD（命令码）
     * [4]     = BOARD（板卡地址）
     * [5..]   = DATA或ACK+DATA（数据载荷，如果有）
     */
    /* 最小payload长度：LEN(2) + PRODUCT(1) + CMD(1) + BOARD(1) = 5字节 */
    if (len < 5U) {
        LOG_D("Frame too short: %d", (int)len);
        return;
    }
    
    uint8_t product = frame[2];  /* 产品地址 */
    uint8_t cmd = frame[3];      /* 命令码 */
    uint8_t board = frame[4];    /* 板卡地址 */
    
    LOG_D("Frame received: state=%d, cmd=0x%02X, board=0x%02X, has_pending=%d, pending_cmd=0x%02X", 
          (int)host->state, cmd, board, (int)host->has_pending_cmd, 
          host->has_pending_cmd ? host->pending_cmd.cmd : 0xFF);
    
    /* 验证产品地址 */
    if (product != PRODUCT_ADDR) {
        LOG_D("Product address mismatch: expected=0x%02X, got=0x%02X", PRODUCT_ADDR, product);
        return;
    }
    
    /* 验证板卡地址 */
    if (board != host->board_addr) {
        LOG_D("Board address mismatch: expected=0x%02X, got=0x%02X", host->board_addr, board);
        return;
    }
    
    /* 根据主机状态和命令类型处理 */
    if (host->state == HOST_STATE_WAIT_HANDSHAKE) {
        /* 等待握手状态：只处理从机的握手请求 */
        if (cmd == CMD_HAND_SHAKE) {
            host_process_handshake_request(host, frame, len);
        }
    } else if (host->state == HOST_STATE_HANDSHAKE_PENDING) {
        /* 握手待应答状态：处理握手应答 */
        if (cmd == CMD_HAND_SHAKE) {
            host_process_handshake_response(host, frame, len);
        }
    } else if ((host->state == HOST_STATE_ACTIVE) || (host->state == HOST_STATE_QUERY_INFO)) {
        /* 活动状态或查询状态：处理业务命令应答或主动上传 */
        if (cmd == CMD_HAND_SHAKE) {
            /* 收到握手命令，需要区分是握手请求还是握手应答 */
            uint16_t payload_len = (uint16_t)(len - 5U);
            
            if ((host->has_handshake_pending != 0U) && (payload_len >= 1U)) {
                /* 有ACK字段，是握手应答 */
                host_process_handshake_response(host, frame, len);
            } else if (payload_len == 0U) {
                /* 无ACK字段，是从机重新上电/复位后的握手请求 */
                LOG_I("Handshake request received in ACTIVE state, slave may have reset");
                /* 清除当前待应答命令（如果有） */
                if (host->has_pending_cmd != 0U) {
                    if (host->pending_cmd.callback != NULL) {
                        host->pending_cmd.callback(host->pending_cmd.cmd, ACK_ERR_UNKNOWN, NULL, 0U);
                    }
                    host->has_pending_cmd = 0U;
                }
                /* 重新处理握手请求 */
                host_process_handshake_request(host, frame, len);
            } else {
                /* 其他情况，可能是异常帧 */
                LOG_D("Unknown handshake frame: cmd=0x%02X, payload_len=%d", cmd, (int)payload_len);
            }
        } else if ((host->has_pending_cmd != 0U) && (cmd == host->pending_cmd.cmd)) {
            /* 业务命令应答 */
            uint16_t payload_len = (uint16_t)(len - 5U);
            const uint8_t *payload = &frame[5];
            uint8_t ack;
            const uint8_t *data = NULL;
            uint16_t data_len = 0U;
            
            /* 应答帧包含ACK字段 */
            if (payload_len >= 1U) {
                ack = payload[0];
                if (payload_len > 1U) {
                    data = &payload[1];
                    data_len = (uint16_t)(payload_len - 1U);
                }
            } else {
                ack = ACK_ERR_UNKNOWN;
            }
            
            /* 检查是否是查询命令（在查询状态下） */
            if ((host->state == HOST_STATE_QUERY_INFO) && 
                ((cmd == CMD_GET_SOFTWARE_VERSION) || 
                 (cmd == CMD_GET_HARDWARE_VERSION) || 
                 (cmd == CMD_GET_SERIAL_NUMBER))) {
                /* 查询命令回调 */
                host_query_callback_internal(host, cmd, ack, data, data_len);
            } else {
                /* 普通业务命令回调 */
                if (host->pending_cmd.callback != NULL) {
                    host->pending_cmd.callback(cmd, ack, data, data_len);
                }
            }
            
            /* 清除待应答命令 */
            host->has_pending_cmd = 0U;
            LOG_D("Command response received: cmd=0x%02X, ack=0x%02X", cmd, ack);
        } else if ((host->state == HOST_STATE_QUERY_INFO) && 
                   ((cmd == CMD_GET_SOFTWARE_VERSION) || 
                    (cmd == CMD_GET_HARDWARE_VERSION) || 
                    (cmd == CMD_GET_SERIAL_NUMBER))) {
            /* 查询状态下，即使has_pending_cmd为0，也处理查询命令应答（可能是超时后收到的应答） */
            uint16_t payload_len = (uint16_t)(len - 5U);
            const uint8_t *payload = &frame[5];
            uint8_t ack;
            const uint8_t *data = NULL;
            uint16_t data_len = 0U;
            
            /* 应答帧包含ACK字段 */
            if (payload_len >= 1U) {
                ack = payload[0];
                if (payload_len > 1U) {
                    data = &payload[1];
                    data_len = (uint16_t)(payload_len - 1U);
                }
            } else {
                ack = ACK_ERR_UNKNOWN;
            }
            
            /* 查询命令回调 */
            host_query_callback_internal(host, cmd, ack, data, data_len);
            LOG_D("Query command response received (late): cmd=0x%02X, ack=0x%02X", cmd, ack);
        } else {
            /* 可能是从机主动上传 */
            uint16_t payload_len = (uint16_t)(len - 5U);
            const uint8_t *data = NULL;
            uint16_t data_len = 0U;
            
            /* 检查是否是主动上传命令（状态上传 0x0C 或贴靠阻抗数据上传 0x34） */
            if ((cmd == CMD_STATUS_UPLOAD) || (cmd == HOST_CMD_UPLOAD_CONTACT_IMP_DATA)) {
                if (payload_len > 0U) {
                    data = &frame[5];
                    data_len = payload_len;
                }
                
                /* 调用上传回调函数 */
                if (host->upload_cb != NULL) {
                    host->upload_cb(cmd, data, data_len);
                }
                LOG_D("Upload data received: cmd=0x%02X, len=%d", cmd, (int)data_len);
            } else {
                LOG_D("Unknown frame: cmd=0x%02X", cmd);
            }
        }
    }
}

/**
 * @brief 处理从机握手请求
 * @param host 主机实例指针
 * @param frame 接收到的帧数据
 * @param len 帧长度
 */
static void host_process_handshake_request(host_inst_t *host, const uint8_t *frame, uint16_t len)
{
    (void)frame;
    (void)len;
    
    LOG_I("Handshake request received from board 0x%02X", host->board_addr);
    
    /* 发送握手命令响应 */
    host_send_handshake_cmd(host);
    
    /* 设置状态为握手待应答 */
    host->state = HOST_STATE_HANDSHAKE_PENDING;
    host->has_handshake_pending = 1U;
    host->handshake_tick = HAL_GetTick();
    host->handshake_retry = 0U;
}

/**
 * @brief 处理从机握手应答
 * @param host 主机实例指针
 * @param frame 接收到的帧数据
 * @param len 帧长度
 */
static void host_process_handshake_response(host_inst_t *host, const uint8_t *frame, uint16_t len)
{
    uint16_t payload_len = (uint16_t)(len - 5U);
    uint8_t ack = ACK_ERR_UNKNOWN;
    
    /* 应答帧包含ACK字段 */
    if (payload_len >= 1U) {
        ack = frame[5];
    }
    
    if (ack == ACK_OK) {
        /* 握手成功，启动从机信息查询流程 */
        host->state = HOST_STATE_QUERY_INFO;
        host->has_handshake_pending = 0U;
        host->query_state = QUERY_STATE_IDLE;
        host->slave_info.info_valid = 0U;
        
        /* 清零从机信息 */
        (void)memset(&host->slave_info, 0, sizeof(slave_info_t));
        
        LOG_I("Handshake completed! Start querying slave info (board=0x%02X)", host->board_addr);
    } else {
        LOG_D("Handshake response error: ack=0x%02X", ack);
    }
}

/**
 * @brief 发送握手命令（响应从机请求）
 * @param host 主机实例指针
 */
static void host_send_handshake_cmd(host_inst_t *host)
{
    int flen;
    uint8_t frame[32U];
    
    if (host->port == NULL) {
        return;
    }
    
    /* 构建握手命令帧 */
    flen = custom_build_frame(frame, (uint16_t)sizeof(frame),
                             PRODUCT_ADDR,
                             CMD_HAND_SHAKE,
                             host->board_addr,
                             NULL, 0U);
    
    if (flen > 0) {
        (void)serial_write(host->port, frame, (uint16_t)flen);
        LOG_D("Send handshake command to board 0x%02X", host->board_addr);
    }
}

/**
 * @brief 发送命令帧
 * @param host 主机实例指针
 * @param cmd 命令码
 * @param data 数据载荷指针
 * @param data_len 数据载荷长度
 * @retval 0 成功
 * @retval -1 失败
 */
static int host_send_cmd_frame(host_inst_t *host, uint8_t cmd, const uint8_t *data, uint16_t data_len)
{
    int flen;
    uint8_t frame[512U];
    
    uint32_t tick;
    
    if (host->port == NULL) {
        return -1;
    }
    
    /* 构建命令帧 */
    flen = custom_build_frame(frame, (uint16_t)sizeof(frame),
                             PRODUCT_ADDR,
                             cmd,
                             host->board_addr,
                             data, data_len);
    
    if (flen > 0) {
        (void)serial_write(host->port, frame, (uint16_t)flen);
        LOG_D("Send command frame: cmd=0x%02X, len=%d", cmd, (int)data_len);
        tick = HAL_GetTick();
        LOG_D("Tick=%d", tick);
        return 0;
    }
    
    return -1;
}

/**
 * @brief 检查超时
 * @param host 主机实例指针
 */
static void host_check_timeout(host_inst_t *host)
{
    uint32_t current_tick;
    uint32_t elapsed;
    
    current_tick = HAL_GetTick();
    
    /* 检查握手超时 */
    if (host->has_handshake_pending != 0U) {
        /* 处理时间戳回绕 */
        if (current_tick >= host->handshake_tick) {
            elapsed = current_tick - host->handshake_tick;
        } else {
            elapsed = (0xFFFFFFFFU - host->handshake_tick) + current_tick + 1U;
        }
        
        if (elapsed >= HOST_HANDSHAKE_TIMEOUT_MS) {
            if (host->handshake_retry < HOST_HANDSHAKE_MAX_RETRY) {
                /* 重传握手命令 */
                host->handshake_retry++;
                host_send_handshake_cmd(host);
                host->handshake_tick = current_tick;
                LOG_D("Handshake timeout, retry %d/%d", (int)host->handshake_retry, (int)HOST_HANDSHAKE_MAX_RETRY);
            } else {
                /* 超过最大重传次数，重置状态 */
                LOG_W("Handshake timeout, max retry reached, reset state");
                host->has_handshake_pending = 0U;
                host->state = HOST_STATE_WAIT_HANDSHAKE;
            }
        }
    }
    
    /* 检查业务命令超时 */
    if (host->has_pending_cmd != 0U) {
        /* 处理时间戳回绕 */
        if (current_tick >= host->pending_cmd_tick) {
            elapsed = current_tick - host->pending_cmd_tick;
        } else {
            elapsed = (0xFFFFFFFFU - host->pending_cmd_tick) + current_tick + 1U;
        }
        
        if (elapsed >= host->pending_cmd.timeout_ms) {
            if (host->pending_cmd_retry < host->pending_cmd.max_retry) {
                /* 重传命令 */
                host->pending_cmd_retry++;
                (void)host_send_cmd_frame(host, host->pending_cmd.cmd,
                                        host->pending_cmd.data,
                                        host->pending_cmd.data_len);
                host->pending_cmd_tick = current_tick;
                LOG_D("Command timeout, retry %d/%d: cmd=0x%02X",
                     (int)host->pending_cmd_retry, (int)host->pending_cmd.max_retry, host->pending_cmd.cmd);
            } else {
                /* 超过最大重传次数，调用超时回调 */
                if (host->pending_cmd.callback != NULL) {
                    host->pending_cmd.callback(host->pending_cmd.cmd, ACK_ERR_TIMEOUT, NULL, 0U);
                }
                host->has_pending_cmd = 0U;
                LOG_W("Command timeout, max retry reached: cmd=0x%02X", host->pending_cmd.cmd);
            }
        }
    }
}

/**
 * @brief 发送待发送的命令
 * @param host 主机实例指针
 */
static void host_send_pending_cmd(host_inst_t *host)
{
    cmd_request_t req;
    
    /* 从队列中取出命令 */
    if (host_cmd_queue_dequeue(&host->cmd_queue, &req) == 0) {
        /* 发送命令帧 */
        if (host_send_cmd_frame(host, req.cmd, req.data, req.data_len) == 0) {
            /* 设置为待应答命令 */
            host->pending_cmd = req;
            host->has_pending_cmd = 1U;
            host->pending_cmd_tick = HAL_GetTick();
            host->pending_cmd_retry = 0U;
        }
    }
}

/**
 * @brief 命令入队
 * @param queue 命令队列指针
 * @param req 命令请求指针
 * @retval 0 成功
 * @retval -1 失败（队列满）
 */
static int host_cmd_queue_enqueue(cmd_queue_t *queue, const cmd_request_t *req)
{
    if ((queue == NULL) || (req == NULL)) {
        return -1;
    }
    
    if (host_cmd_queue_is_full(queue) != 0) {
        return -1;
    }
    
    /* 复制命令请求（注意：data指针需要外部保证有效） */
    queue->items[queue->tail] = *req;
    queue->tail = (uint8_t)((queue->tail + 1U) % HOST_CMD_QUEUE_SIZE);
    queue->count++;
    
    return 0;
}

/**
 * @brief 命令出队
 * @param queue 命令队列指针
 * @param req 命令请求指针（输出）
 * @retval 0 成功
 * @retval -1 失败（队列空）
 */
static int host_cmd_queue_dequeue(cmd_queue_t *queue, cmd_request_t *req)
{
    if ((queue == NULL) || (req == NULL)) {
        return -1;
    }
    
    if (host_cmd_queue_is_empty(queue) != 0) {
        return -1;
    }
    
    *req = queue->items[queue->head];
    queue->head = (uint8_t)((queue->head + 1U) % HOST_CMD_QUEUE_SIZE);
    queue->count--;
    
    return 0;
}

/**
 * @brief 检查队列是否满
 * @param queue 命令队列指针
 * @retval 0 未满
 * @retval 1 已满
 */
static int host_cmd_queue_is_full(const cmd_queue_t *queue)
{
    if (queue == NULL) {
        return 1;
    }
    
    return (queue->count >= HOST_CMD_QUEUE_SIZE) ? 1 : 0;
}

/**
 * @brief 检查队列是否空
 * @param queue 命令队列指针
 * @retval 0 非空
 * @retval 1 空
 */
static int host_cmd_queue_is_empty(const cmd_queue_t *queue)
{
    if (queue == NULL) {
        return 1;
    }
    
    return (queue->count == 0U) ? 1 : 0;
}

/**
 * @brief 处理从机信息查询
 * @param host 主机实例指针
 */
static void host_process_query_info(host_inst_t *host)
{
    uint32_t current_tick = HAL_GetTick();
    uint32_t elapsed;
    
    /* 处理查询状态机 */
    switch (host->query_state) {
    case QUERY_STATE_IDLE:
        /* 开始查询软件版本 */
        host->query_state = QUERY_STATE_SW_VERSION;
        host->query_timeout_tick = current_tick;
        if (host_send_cmd(host, CMD_GET_SOFTWARE_VERSION, NULL, 0, host_query_callback) == 0) {
            LOG_D("Querying software version (board=0x%02X)...", host->board_addr);
        } else {
            LOG_W("Failed to send software version query, skip to next");
            host->query_state = QUERY_STATE_HW_VERSION;
            host->query_timeout_tick = current_tick;
            (void)host_send_cmd(host, CMD_GET_HARDWARE_VERSION, NULL, 0, host_query_callback);
        }
        break;
        
    case QUERY_STATE_SW_VERSION:
        /* 检查超时（2秒） */
        if (current_tick >= host->query_timeout_tick) {
            elapsed = current_tick - host->query_timeout_tick;
        } else {
            elapsed = (0xFFFFFFFFU - host->query_timeout_tick) + current_tick + 1U;
        }
        
        if (elapsed > 2000U) {
            LOG_W("Query software version timeout (board=0x%02X), skip to next", host->board_addr);
            host->query_state = QUERY_STATE_HW_VERSION;
            host->query_timeout_tick = current_tick;
            (void)host_send_cmd(host, CMD_GET_HARDWARE_VERSION, NULL, 0, host_query_callback);
        }
        break;
        
    case QUERY_STATE_HW_VERSION:
        /* 检查超时（2秒） */
        if (current_tick >= host->query_timeout_tick) {
            elapsed = current_tick - host->query_timeout_tick;
        } else {
            elapsed = (0xFFFFFFFFU - host->query_timeout_tick) + current_tick + 1U;
        }
        
        if (elapsed > 2000U) {
            LOG_W("Query hardware version timeout (board=0x%02X), skip to next", host->board_addr);
            host->query_state = QUERY_STATE_SN_NUMBER;
            host->query_timeout_tick = current_tick;
            (void)host_send_cmd(host, CMD_GET_SERIAL_NUMBER, NULL, 0, host_query_callback);
        }
        break;
        
    case QUERY_STATE_SN_NUMBER:
        /* 检查超时（2秒） */
        if (current_tick >= host->query_timeout_tick) {
            elapsed = current_tick - host->query_timeout_tick;
        } else {
            elapsed = (0xFFFFFFFFU - host->query_timeout_tick) + current_tick + 1U;
        }
        
        if (elapsed > 2000U) {
            LOG_W("Query serial number timeout (board=0x%02X), complete query", host->board_addr);
            host->query_state = QUERY_STATE_COMPLETE;
            host->slave_info.info_valid = 1U;
            host->state = HOST_STATE_ACTIVE;
            LOG_I("Slave info query completed (with timeout) - board=0x%02X", host->board_addr);
        }
        break;
        
    case QUERY_STATE_COMPLETE:
        /* 查询完成，进入活动状态 */
        if (host->state != HOST_STATE_ACTIVE) {
            host->state = HOST_STATE_ACTIVE;
            LOG_I("Slave info query completed - board=0x%02X", host->board_addr);
        }
        break;
    default:
        break;
    }
}

/**
 * @brief 查询命令回调函数包装器（用于传递给host_send_cmd）
 */
static void host_query_callback(uint8_t cmd, uint8_t ack, const uint8_t *data, uint16_t len)
{
    /* 这个回调会被调用，但我们需要找到对应的主机实例 */
    /* 由于我们使用pending_cmd来跟踪，可以在host_process_frame中处理 */
    (void)cmd;
    (void)ack;
    (void)data;
    (void)len;
}

/**
 * @brief 查询命令回调函数（内部处理）
 * @param host 主机实例指针
 * @param cmd 命令码
 * @param ack 应答码
 * @param data 应答数据指针
 * @param len 应答数据长度
 */
static void host_query_callback_internal(host_inst_t *host, uint8_t cmd, uint8_t ack, 
                                         const uint8_t *data, uint16_t len)
{
    if (ack != ACK_OK) {
        LOG_W("Query command 0x%02X failed (board=0x%02X), ack=0x%02X", cmd, host->board_addr, ack);
        /* 继续下一个查询 */
        goto next_query;
    }
    
    if ((data == NULL) || (len == 0U)) {
        LOG_W("Query command 0x%02X response data empty (board=0x%02X)", cmd, host->board_addr);
        goto next_query;
    }
    
    /* 保存查询结果 */
    switch (cmd) {
    case CMD_GET_SOFTWARE_VERSION:
        if (len < sizeof(host->slave_info.sw_version)) {
            (void)memcpy(host->slave_info.sw_version, data, len);
            host->slave_info.sw_version[len] = '\0';
            LOG_I("Slave SW version (board=0x%02X): %.*s", host->board_addr, (int)len, data);
        }
        host->query_state = QUERY_STATE_HW_VERSION;
        host->query_timeout_tick = HAL_GetTick();
        (void)host_send_cmd(host, CMD_GET_HARDWARE_VERSION, NULL, 0, host_query_callback);
        break;
        
    case CMD_GET_HARDWARE_VERSION:
        if (len < sizeof(host->slave_info.hw_version)) {
            (void)memcpy(host->slave_info.hw_version, data, len);
            host->slave_info.hw_version[len] = '\0';
            LOG_I("Slave HW version (board=0x%02X): %.*s", host->board_addr, (int)len, data);
        }
        host->query_state = QUERY_STATE_SN_NUMBER;
        host->query_timeout_tick = HAL_GetTick();
        (void)host_send_cmd(host, CMD_GET_SERIAL_NUMBER, NULL, 0, host_query_callback);
        break;
        
    case CMD_GET_SERIAL_NUMBER:
        if (len < sizeof(host->slave_info.sn_number)) {
            (void)memcpy(host->slave_info.sn_number, data, len);
            host->slave_info.sn_number[len] = '\0';
            LOG_I("Slave SN number (board=0x%02X): %.*s", host->board_addr, (int)len, data);
        }
        host->query_state = QUERY_STATE_COMPLETE;
        host->slave_info.info_valid = 1U;
        host->state = HOST_STATE_ACTIVE;
        LOG_I("Slave info query completed successfully (board=0x%02X)", host->board_addr);
        break;
        
    default:
        break;
    }
    return;
    
next_query:
    /* 查询失败，继续下一个 */
    if (cmd == CMD_GET_SOFTWARE_VERSION) {
        host->query_state = QUERY_STATE_HW_VERSION;
        host->query_timeout_tick = HAL_GetTick();
        (void)host_send_cmd(host, CMD_GET_HARDWARE_VERSION, NULL, 0, host_query_callback);
    } else if (cmd == CMD_GET_HARDWARE_VERSION) {
        host->query_state = QUERY_STATE_SN_NUMBER;
        host->query_timeout_tick = HAL_GetTick();
        (void)host_send_cmd(host, CMD_GET_SERIAL_NUMBER, NULL, 0, host_query_callback);
    } else if (cmd == CMD_GET_SERIAL_NUMBER) {
        host->query_state = QUERY_STATE_COMPLETE;
        host->slave_info.info_valid = 1U;
        host->state = HOST_STATE_ACTIVE;
        LOG_I("Slave info query completed (with errors) - board=0x%02X", host->board_addr);
    }
}
