/**
  ******************************************************************************
  * @file        : port_ctrl.c
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2025-01-XX
  * @brief       : 端口控制模块实现
  * @attention   : None
  ******************************************************************************
  * @history     :
  *         V1.0 : 1. 实现端口模式控制
  *                2. 实现导管选择控制
  *                3. 实现电极位图控制
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/

#include "port_ctrl.h"
#include "gpio.h"
#include "tca6424.h"
#include <errno-base.h>

#define  LOG_TAG             "port_ctrl"
#define  LOG_LVL             4
#include "log.h"

/* Private typedef -----------------------------------------------------------*/

/**
 * @brief 极性GPIO配置结构体
 */
typedef struct {
    uint8_t neg_plate;      /* NEG_PLATE_RELAY_PIN_ID 状态 */
    uint8_t top_wire_pol;   /* TOP_WIRE_POL_PIN_ID 状态 */
    uint8_t pole_pol;       /* POLE_POL_SELECT_PIN_ID 状态 */
} polarity_cfg_t;

/* Private define ------------------------------------------------------------*/
#define PORT_BIT(n)                 (1u << (n))

/* Private macro -------------------------------------------------------------*/



/* Private variables ---------------------------------------------------------*/

static tca6424_t tca6424_dev;

static uint32_t last_pole_elec_bitmap = 0x00000000u;  /* 电杆电极位图（TCA6424） */
static uint8_t last_top_wire_state = 0U;               /* 顶电极状态（PC9） */

/**
 * @brief 当前导管信息
 */
static port_cath_t g_current_catheter = {
    .type = PORT_CATH_TYPE_PVI1,
    .spec = PORT_CATH_SPEC_A
};

/**
 * @brief 当前工作模式
 */
static port_mode_t g_current_work_mode = PORT_MODE_MAPPING;

/* Exported variables  -------------------------------------------------------*/



/* Private function prototypes -----------------------------------------------*/

/**
 * @brief 根据导管信息获取极性配置
 * @param catheter 导管结构体，包含类型和规格
 * @param p_cfg 输出的极性配置结构体指针
 * @return 0成功，负数表示错误码
 */
static int port_ctrl_get_polarity_cfg(port_cath_t catheter, polarity_cfg_t *p_cfg);

/**
 * @brief 设置极性选择GPIO
 * @param p_cfg 极性配置结构体指针
 * @note 此函数仅设置NEG_PLATE_RELAY_PIN_ID、TOP_WIRE_POL_PIN_ID、POLE_POL_SELECT_PIN_ID三个GPIO
 */
static void port_ctrl_set_polarity_gpio(const polarity_cfg_t *p_cfg);

/**
 * @brief 设置模式继电器GPIO
 * @param ecg_map ECG_MAP_RELAY_PIN_ID 状态
 * @param contact_imp CONTACT_IMP_RELAY_PIN_ID 状态（如果启用）
 * @param loop_imp LOOP_IMP_RELAY_PIN_ID 状态
 */
static void port_ctrl_set_mode_relays(uint8_t ecg_map, uint8_t contact_imp, uint8_t loop_imp);



/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化端口控制模块
 * @return 0成功，负数表示错误码
 */
int port_ctrl_init(void)
{
    int ret = 0;
    
    /* 初始化模式继电器控制，默认输出为0 */
    gpio_set_mode(ECG_MAP_RELAY_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_DOWN);
    gpio_write(ECG_MAP_RELAY_PIN_ID, 0);
#if (CONTACT_IMP_USE_GPIO == 1)
    gpio_set_mode(CONTACT_IMP_RELAY_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_DOWN);
    gpio_write(CONTACT_IMP_RELAY_PIN_ID, 0);
#endif
    
    gpio_set_mode(LOOP_IMP_RELAY_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_DOWN);
    gpio_write(LOOP_IMP_RELAY_PIN_ID, 0);
    
    /* 初始化极性选择继电器控制，默认输出为0 */
    gpio_set_mode(NEG_PLATE_RELAY_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_DOWN);
    gpio_write(NEG_PLATE_RELAY_PIN_ID, 0);
    
    gpio_set_mode(TOP_WIRE_POL_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_DOWN);
    gpio_write(TOP_WIRE_POL_PIN_ID, 0);
    
    gpio_set_mode(POLE_POL_SELECT_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_DOWN);
    gpio_write(POLE_POL_SELECT_PIN_ID, 0);
    
    /* 初始化顶(网)电极控制，默认输出为0 */
    gpio_set_mode(TOP_WIRE_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_DOWN);
    gpio_write(TOP_WIRE_PIN_ID, 0);
    last_top_wire_state = 0U;
    
    /* 初始化电杆电极开关控制，默认全部输出为0 */
    ret = tca6424_init(&tca6424_dev, TCA6424_I2C_ADDR_H, "i2c1");
    if (ret != 0) {
        LOG_E("tca6424_init fail!");
        return ret;
    }
    
    /* 预置 TCA6424 输出锁存器全为0 */
    ret = tca6424_write_outputs24(&tca6424_dev, last_pole_elec_bitmap);
    if (ret != 0) {
        LOG_E("tca6424_write_outputs24 fail! ret=%d", ret);
        return ret;
    }
    
    /* 将TCA6424的端口全部配置为输出模式 */
    ret = tca6424_write_config24(&tca6424_dev, 0x00000000u);
    if (ret != 0) {
        LOG_E("tca6424_write_config24 fail! ret=%d", ret);
        return ret;
    }
    
    return 0;
}

/**
 * @brief 选择导管类型和规格
 * @param catheter 导管结构体，包含类型和规格
 * @return 0成功，负数表示错误码
 * @note 此函数仅保存导管信息，不直接设置GPIO状态。
 *       极性选择GPIO的状态由port_ctrl_set_mode根据工作模式决定。
 */
int port_crtl_select_catheter(port_cath_t catheter)
{
    int ret = 0;
    polarity_cfg_t polarity_cfg;
    
    /* 参数验证 */
    if (catheter.type == PORT_CATH_TYPE_PVI1) {
        /* PVI1 无需规格验证 */
    } else if ((catheter.type == PORT_CATH_TYPE_PVI2) || 
               (catheter.type == PORT_CATH_TYPE_FOCAL)) {
        /* PVI2和FOCAL需要验证规格 */
        if ((catheter.spec != PORT_CATH_SPEC_A) && 
            (catheter.spec != PORT_CATH_SPEC_B) && 
            (catheter.spec != PORT_CATH_SPEC_C)) {
            return -EINVAL;
        }
    } else {
        return -EINVAL;
    }
    
    /* 验证极性配置是否有效 */
    ret = port_ctrl_get_polarity_cfg(catheter, &polarity_cfg);
    if (ret != 0) {
        return ret;
    }
    
    /* 切换导管时，断开所有电极 */
    if (last_pole_elec_bitmap != 0U) {
        ret = port_ctrl_elec(0U);
        if (ret != 0) {
            return ret;
        }
    }
    
    /* 保存当前导管信息 */
    g_current_catheter = catheter;
    
    /* 如果当前工作模式需要设置极性GPIO，则立即应用 */
    if ((g_current_work_mode == PORT_MODE_LOOP_IMP) || 
        (g_current_work_mode == PORT_MODE_ABLATION)) {
        port_ctrl_set_polarity_gpio(&polarity_cfg);
    }
    
    return 0;
}

/**
 * @brief 设置端口工作模式
 * @param mode 工作模式
 * @return 0成功，负数表示错误码
 * @note 在标测模式和贴靠检测模式下，极性选择GPIO必须断开（设为0）。
 *       在回路阻抗检测模式和消融模式下，根据当前导管信息设置极性选择GPIO。
 */
int port_ctrl_set_mode(port_mode_t mode)
{
    int ret = 0;
    polarity_cfg_t polarity_cfg;
    uint8_t need_polarity = 0U;  /* 是否需要设置极性GPIO */
    uint8_t need_disconnect_elec = 0U;  /* 是否需要断开电极 */
    
    /* 根据模式设置继电器和极性GPIO */
    switch (mode) {
    case PORT_MODE_MAPPING:
        port_ctrl_set_mode_relays(1U, 0U, 0U);
        need_disconnect_elec = 1U;
        break;
        
    case PORT_MODE_CONTACT_IMP:
        port_ctrl_set_mode_relays(0U, 1U, 0U);
        need_disconnect_elec = 1U;
        break;
    
    case PORT_MODE_LOOP_IMP:
        port_ctrl_set_mode_relays(0U, 0U, 1U);
        need_polarity = 1U;
        break;

    case PORT_MODE_ABLATION:
        port_ctrl_set_mode_relays(0U, 0U, 0U);
        need_polarity = 1U;
        /* 消融模式不断开电极，由应用层控制 */
        break;

    default:
        return -EINVAL;
    }
    
    /* 设置极性选择GPIO */
    if (need_polarity != 0U) {
        ret = port_ctrl_get_polarity_cfg(g_current_catheter, &polarity_cfg);
        if (ret == 0) {
            port_ctrl_set_polarity_gpio(&polarity_cfg);
        }
    } else {
        /* 断开极性选择GPIO */
        polarity_cfg.neg_plate = 0U;
        polarity_cfg.top_wire_pol = 0U;
        polarity_cfg.pole_pol = 0U;
        port_ctrl_set_polarity_gpio(&polarity_cfg);
    }
    
    /* 断开所有电极（如果需要） */
    if (need_disconnect_elec != 0U) {
        ret = port_ctrl_elec(0U);
    }
    
    /* 保存当前工作模式 */
    if (ret == 0) {
        g_current_work_mode = mode;
    }
    
    return ret;
}

/**
 * @brief 控制电极位图
 * @param pole_elec_bitmap 电极位图
 *        bit0: 顶(网)电极，使用PC9 GPIO控制
 *        bit1-bit4: 1杆1电极~1杆4电极，映射到TCA6424 P00-P03 (bit0-3)
 *        bit5-bit8: 3杆1电极~3杆4电极，映射到TCA6424 P04-P07 (bit4-7)
 *        bit9-bit12: 5杆1电极~5杆4电极，映射到TCA6424 P10-P13 (bit8-11)
 *        bit13-bit16: 2杆1电极~2杆4电极，映射到TCA6424 P14-P17 (bit12-15)
 *        bit17-bit20: 4杆1电极~4杆4电极，映射到TCA6424 P20-P23 (bit16-19)
 *        bit21-bit24: 6杆1电极~6杆4电极，映射到TCA6424 P24-P27 (bit20-23)
 * @return 0成功，负数表示错误码
 */
int port_ctrl_elec(uint32_t pole_elec_bitmap)
{
    int ret = 0;
    uint32_t pole_bitmap = 0U;  /* 电杆电极位图（映射到TCA6424的bit0-bit23） */
    uint8_t top_wire_state = 0U;  /* 顶电极状态（bit0） */
    
    /* 提取顶电极状态（bit0） */
    top_wire_state = (uint8_t)(pole_elec_bitmap & 0x01U);
    
    /* 提取电杆电极位图（bit1-bit24），右移1位映射到TCA6424的bit0-bit23 */
    /* bit1→bit0, bit2→bit1, ..., bit24→bit23 */
    pole_bitmap = (pole_elec_bitmap >> 1U) & 0x00FFFFFFU;
    
    /* 控制电杆电极（bit1-bit24映射到TCA6424的bit0-bit23） */
    if (pole_bitmap != last_pole_elec_bitmap) {
        ret = tca6424_write_outputs24(&tca6424_dev, pole_bitmap);
        if (ret != 0) {
            LOG_E("tca6424_write_outputs24 fail! ret=%d", ret);
            return ret;
        }
        last_pole_elec_bitmap = pole_bitmap;
    }
    
    /* 控制顶(网)电极（bit0） */
    if (top_wire_state != last_top_wire_state) {
        gpio_write(TOP_WIRE_PIN_ID, top_wire_state);
        last_top_wire_state = top_wire_state;
    }
    
    return 0;
}

/**
 * @brief 获取当前导管信息
 * @return 当前导管信息结构体指针
 */
const port_cath_t *port_ctrl_get_catheter(void)
{
    return &g_current_catheter;
}

/**
 * @brief 获取当前工作模式
 * @return 当前工作模式
 */
port_mode_t port_ctrl_get_mode(void)
{
    return g_current_work_mode;
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 极性配置查找表
 * @note 索引计算: (type * 3) + spec
 *       PVI1: type=0, spec忽略，使用索引0
 *       PVI2: type=1, spec=0/1/2，使用索引3/4/5
 *       FOCAL: type=2, spec=0/1/2，使用索引6/7/8
 */
static const polarity_cfg_t polarity_lut[9] = {
    /* PVI1 (索引0-2，实际只使用索引0) */
    {0U, 0U, 1U},  /* PVI1 */
    {0U, 0U, 0U},  /* 未使用 */
    {0U, 0U, 0U},  /* 未使用 */
    /* PVI2 (索引3-5) */
    {1U, 0U, 0U},  /* PVI2 SPEC_A */
    {0U, 1U, 0U},  /* PVI2 SPEC_B */
    {0U, 1U, 1U},  /* PVI2 SPEC_C */
    /* FOCAL (索引6-8) */
    {1U, 0U, 0U},  /* FOCAL SPEC_A */
    {0U, 1U, 0U},  /* FOCAL SPEC_B */
    {0U, 1U, 1U},  /* FOCAL SPEC_C */
};

/**
 * @brief 根据导管信息获取极性配置
 * @param catheter 导管结构体，包含类型和规格
 * @param p_cfg 输出的极性配置结构体指针
 * @return 0成功，负数表示错误码
 */
static int port_ctrl_get_polarity_cfg(port_cath_t catheter, polarity_cfg_t *p_cfg)
{
    uint32_t index;
    
    if (p_cfg == NULL) {
        return -EINVAL;
    }
    
    if (catheter.type == PORT_CATH_TYPE_PVI1) {
        /* PVI1 固定配置 */
        index = 0U;
    } else if ((catheter.type == PORT_CATH_TYPE_PVI2) || 
               (catheter.type == PORT_CATH_TYPE_FOCAL)) {
        /* PVI2和FOCAL根据规格计算索引 */
        if (catheter.spec > PORT_CATH_SPEC_C) {
            return -EINVAL;
        }
        index = (uint32_t)catheter.type * 3U + (uint32_t)catheter.spec;
    } else {
        return -EINVAL;
    }
    
    if (index >= (sizeof(polarity_lut) / sizeof(polarity_lut[0]))) {
        return -EINVAL;
    }
    
    *p_cfg = polarity_lut[index];
    return 0;
}

/**
 * @brief 设置极性选择GPIO
 * @param p_cfg 极性配置结构体指针
 * @note 此函数仅设置NEG_PLATE_RELAY_PIN_ID、TOP_WIRE_POL_PIN_ID、POLE_POL_SELECT_PIN_ID三个GPIO
 */
static void port_ctrl_set_polarity_gpio(const polarity_cfg_t *p_cfg)
{
    if (p_cfg != NULL) {
        gpio_write(NEG_PLATE_RELAY_PIN_ID, p_cfg->neg_plate);
        gpio_write(TOP_WIRE_POL_PIN_ID, p_cfg->top_wire_pol);
        gpio_write(POLE_POL_SELECT_PIN_ID, p_cfg->pole_pol);
    }
}

/**
 * @brief 设置模式继电器GPIO
 * @param ecg_map ECG_MAP_RELAY_PIN_ID 状态
 * @param contact_imp CONTACT_IMP_RELAY_PIN_ID 状态（如果启用）
 * @param loop_imp LOOP_IMP_RELAY_PIN_ID 状态
 */
static void port_ctrl_set_mode_relays(uint8_t ecg_map, uint8_t contact_imp, uint8_t loop_imp)
{
    gpio_write(ECG_MAP_RELAY_PIN_ID, ecg_map);
#if (CONTACT_IMP_USE_GPIO == 1)
    gpio_write(CONTACT_IMP_RELAY_PIN_ID, contact_imp);
#else
    /* 向FPGA发送控制贴靠阻抗继电器命令 */
    (void)contact_imp;  /* 避免未使用变量警告 */
#endif
    gpio_write(LOOP_IMP_RELAY_PIN_ID, loop_imp);
}
