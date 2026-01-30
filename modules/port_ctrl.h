/**
  ******************************************************************************
  * @file        : port_ctrl.h
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2025-01-XX
  * @brief       : 端口控制模块头文件
  * @attention   : None
  ******************************************************************************
  * @history     :
  *         V1.0 : 1. 端口模式控制接口
  *                2. 导管选择控制接口
  *                3. 电极位图控制接口
  ******************************************************************************
  */
#ifndef __PORT_CTRL_H__
#define __PORT_CTRL_H__

#ifdef __cplusplus
 extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>

/* Exported types ------------------------------------------------------------*/

/* 导管类型 */
typedef enum {
    CATH_TYPE_PVI1 = 1,         /* PVI 一代 */
    CATH_TYPE_PVI2,             /* PVI 二代 */
    CATH_TYPE_FOCAL             /* 局灶 */
} cath_type_t;

/* 导管极性 */
typedef enum {
    CATH_POL_A = 1,         /* 负极板-断开, 顶(网)电极极性-负, 1-3-5杆极性-正（三个导管都支持）*/
    CATH_POL_B,             /* 负极板-导通, 顶(网)电极极性-负, 1-3-5杆极性-负（仅PVI2代/局灶）*/
    CATH_POL_C,             /* 负极板-断开, 顶(网)电极极性-正, 1-3-5杆极性-负（仅PVI2代/局灶）*/
    CATH_POL_D              /* 负极板-断开, 顶(网)电极极性-正, 1-3-5杆极性-正（仅PVI2代）*/
} cath_pol_t;

/* 导管 */
typedef struct {
    cath_type_t type;
    cath_pol_t  polarity;
} port_cath_t;

/* 工作模式 */
typedef enum {
    PORT_MODE_MAPPING = 1,      /* 标测 */
    PORT_MODE_CONTACT_IMP,      /* 贴靠阻抗检测 */
    PORT_MODE_LOOP_IMP,         /* 回路阻抗检测 */
    PORT_MODE_ABLATION,         /* 消融 */
} port_mode_t;

/* Exported constants --------------------------------------------------------*/
/* 后续改为使用GPIO控制贴靠继电器 */
#define CONTACT_IMP_USE_GPIO            (1)

/* 模式选择继电器 GPIO 引脚定义 */
#define ECG_MAP_RELAY_PIN_ID            (15)    /* PA15，心电标测继电器 */
#if (CONTACT_IMP_USE_GPIO == 1)
    #define CONTACT_IMP_RELAY_PIN_ID    (2)     /* PA2，贴靠阻抗继电器 */
#endif
#define LOOP_IMP_RELAY_PIN_ID           (3)     /* PA3，回路阻抗继电器 */

/* 极性选择继电器 GPIO 引脚定义 */
#define NEG_PLATE_RELAY_PIN_ID          (35)    /* PC3，负极板继电器 */
#define TOP_WIRE_POL_PIN_ID             (39)    /* PC7，顶（网）电极极性选择 */
#define POLE_POL_SELECT_PIN_ID          (32)    /* PC0，1-3-5电杆极性选择 */

/* 电杆电极开关继电器 GPIO 引脚定义 */
#define TOP_WIRE_PIN_ID                 (34)    /* PC2，顶（网）电极继电器 */

/* Exported macros -----------------------------------------------------------*/



/* Exported variables --------------------------------------------------------*/



/* Exported functions --------------------------------------------------------*/

int port_ctrl_init(void);
int port_ctrl_select_catheter(port_cath_t catheter);
int port_ctrl_set_mode(port_mode_t mode);
int port_ctrl_elec(uint32_t pole_elec_bitmap);
const port_cath_t *port_ctrl_get_catheter(void);
port_mode_t port_ctrl_get_mode(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PORT_CTRL_H__ */

