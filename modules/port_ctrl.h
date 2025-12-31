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

/* 工作模式 */
typedef enum {
    PORT_MODE_MAPPING = 0,      /* 标测 */
    PORT_MODE_CONTACT_IMP,      /* 贴靠阻抗检测 */
    PORT_MODE_LOOP_IMP,         /* 回路阻抗检测 */
    PORT_MODE_ABLATION,         /* 消融 */
} port_mode_t;

/* 导管类型 */
typedef enum {
    PORT_CATH_TYPE_PVI1 = 0,         /* PVI 一代 */
    PORT_CATH_TYPE_PVI2,             /* PVI 二代 */
    PORT_CATH_TYPE_FOCAL             /* 局灶 */
} port_cath_type_t;

/* 导管规格（仅PVI2/局灶消融）*/
typedef enum {
    PORT_CATH_SPEC_A = 0,         /* PC2=H, PC1=L, PC0=L */
    PORT_CATH_SPEC_B,             /* PC2=L, PC1=H, PC0=L */
    PORT_CATH_SPEC_C              /* PC2=L, PC1=H, PC0=H */
} port_cath_spec_t;

/* 导管 */
typedef struct {
    port_cath_type_t type;
    port_cath_spec_t spec;
} port_cath_t;

/* Exported constants --------------------------------------------------------*/

#define ECG_MAP_RELAY_PIN_ID            (15)    /* PA15，心电标测继电器 */
#define LOOP_IMP_RELAY_PIN_ID           (3)     /* PA3，回路阻抗继电器 */
#define NEG_PLATE_RELAY_PIN_ID          (34)    /* PC2，负极板继电器 */

#define TOP_WIRE_POL_PIN_ID             (33)    /* PC1，顶（网）电极极性选择 */
#define POLE_POL_SELECT_PIN_ID          (32)    /* PC0，1-3-5电杆极性选择 */

#define TOP_WIRE_PIN_ID                 (41)    /* PC9，顶（网）电极 */


#define CONTACT_IMP_USE_GPIO            (0)
#if (CONTACT_IMP_USE_GPIO == 1)
    #define CONTACT_IMP_RELAY_PIN_ID    (35)    /* PC3，贴靠阻抗继电器 */
#endif

/* Exported macros -----------------------------------------------------------*/



/* Exported variables --------------------------------------------------------*/



/* Exported functions --------------------------------------------------------*/

int port_ctrl_init(void);
int port_crtl_select_catheter(port_cath_t catheter);
int port_ctrl_set_mode(port_mode_t mode);
int port_ctrl_elec(uint32_t pole_elec_bitmap);
const port_cath_t *port_ctrl_get_catheter(void);
port_mode_t port_ctrl_get_mode(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PORT_CTRL_H__ */

