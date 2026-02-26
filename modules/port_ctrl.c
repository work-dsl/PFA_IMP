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
  *                4. 重构：分离约束检查与硬件控制
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/

#include "port_ctrl.h"

#include <string.h>

#include "bsp_conf.h"
#include "gpio.h"
#include "tca6424.h"
#include <errno-base.h>

#define  LOG_TAG             "port_ctrl"
#define  LOG_LVL             4
#include "log.h"

/* Private typedef -----------------------------------------------------------*/


/* Private define ------------------------------------------------------------*/
#define PORT_BIT(n)                 (1u << (n))

/* Private macro -------------------------------------------------------------*/

/* 局灶导管：1-6杆的3-4电极必须断开的掩码 */
#define FOCAL_POLE_34_MASK          (0x01999998U)
/* PVI一代导管：1-6杆的4电极必须断开的掩码 */
#define PVI1_POLE_4_MASK            (0x01111110U)

/* Private variables ---------------------------------------------------------*/

static tca6424_t tca6424_dev;

/* 电极开关位图（bit0: 顶电极，bit1-bit24: 电杆电极） */
static uint32_t g_current_elec_bitmap = 0x00000000U;
static uint32_t g_preset_elec_bitmap = 0x00000000U;

/* 当前导管信息 */
static port_cath_t g_current_catheter = {
    .type = 0,
    .polarity = 0
};

/* 当前工作模式 */
static port_mode_t g_current_work_mode = 0;

/* Exported variables  -------------------------------------------------------*/



/* Private function prototypes -----------------------------------------------*/

static int __set_cath_polarity(void);
static void port_ctrl_set_mode_relays(uint8_t ecg_map, uint8_t contact_imp, uint8_t loop_imp);
static int __apply_elec_bitmap_to_hardware(uint32_t bitmap);
static bool __check_constraints(uint32_t bitmap);
static void __apply_constraints_to_bitmap(uint32_t bitmap);

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化端口控制模块
 * @return 0成功，负数表示错误码
 */
int port_ctrl_init(void)
{
    int ret = 0;
    struct i2c_adapter *adap = NULL;
    
    /* 初始化模式继电器控制 */
    gpio_set_mode(ECG_MAP_RELAY_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(ECG_MAP_RELAY_PIN_ID, 1);
#if (CONTACT_IMP_USE_GPIO == 1)
    gpio_set_mode(CONTACT_IMP_RELAY_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(CONTACT_IMP_RELAY_PIN_ID, 1);
#endif
    gpio_set_mode(LOOP_IMP_RELAY_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(LOOP_IMP_RELAY_PIN_ID, 1);
    
    /* 初始化极性选择继电器控制 */
    gpio_set_mode(NEG_PLATE_RELAY_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(NEG_PLATE_RELAY_PIN_ID, 1);
    gpio_set_mode(TOP_WIRE_POL_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(TOP_WIRE_POL_PIN_ID, 1);
    gpio_set_mode(POLE_POL_SELECT_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(POLE_POL_SELECT_PIN_ID, 1);
    
    /* 初始化顶(网)电极控制 */
    gpio_set_mode(TOP_WIRE_PIN_ID, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(TOP_WIRE_PIN_ID, 1);
    
    /* 初始化电杆电极开关控制 */
    ret = tca6424_init(&tca6424_dev, TCA6424_I2C_ADDR_H, "i2c1",
                       TCA6424_RST_PIN_ID, UINT32_MAX);
    if (ret != 0) {
        LOG_E("tca6424_init fail!");
        return ret;
    }
    
    /* 预置 TCA6424 输出锁存器全为1（关） */
    ret = tca6424_write_outputs(&tca6424_dev, 0xFFFFFFu);
    if (ret != 0) {
        LOG_E("tca6424_write_outputs fail! ret=%d", ret);
        return ret;
    }
    
    /* 将TCA6424的端口全部配置为输出模式 */
    ret = tca6424_write_config(&tca6424_dev, 0x00000000u);
    if (ret != 0) {
        LOG_E("tca6424_write_config fail! ret=%d", ret);
        return ret;
    }
    
    return 0;
}

/**
 * @brief 选择导管类型和极性
 * @param catheter 导管结构体
 * @return 0成功，负数表示错误码
 * @note 此函数仅保存导管信息，不直接设置GPIO状态。
 *       极性选择GPIO的状态由port_ctrl_set_mode根据工作模式决定。
 *       在消融模式和回路阻抗模式下，切换导管后会重新审查电极位图的约束条件并实际应用。
 */
int port_ctrl_select_catheter(port_cath_t catheter)
{
    int ret = 0;
    port_cath_t old_catheter = g_current_catheter;
    
    /* 验证导管类型和极性的组合 */
    if (catheter.type == CATH_TYPE_PVI1) {
        /* PVI1仅支持CATH_POL_A（三个导管都支持） */
        if (catheter.polarity != CATH_POL_A) {
            LOG_W("PVI1 catheter polarity error! polarity=%d", catheter.polarity);
            return -EINVAL;
        }
    } else if (catheter.type == CATH_TYPE_PVI2) {
        /* PVI2支持CATH_POL_A（三个导管都支持）、CATH_POL_B、CATH_POL_C、CATH_POL_D */
        if ((catheter.polarity != CATH_POL_A) && 
            (catheter.polarity != CATH_POL_B) && 
            (catheter.polarity != CATH_POL_C) && 
            (catheter.polarity != CATH_POL_D)) {
            LOG_W("PVI2 catheter polarity error! polarity=%d", catheter.polarity);
            return -EINVAL;
        }
    } else if (catheter.type == CATH_TYPE_FOCAL){
        /* 局灶导管支持CATH_POL_A（三个导管都支持）、CATH_POL_B、CATH_POL_C */
        if ((catheter.polarity != CATH_POL_A) && 
            (catheter.polarity != CATH_POL_B) && 
            (catheter.polarity != CATH_POL_C)) {
            LOG_W("Focal catheter polarity error! polarity=%d", catheter.polarity);
            return -EINVAL;
        }
    } else {
        LOG_W("Catheter type error! type=%d", catheter.type);
        return -EINVAL;
    }
    
    /* 保存当前导管信息 */
    g_current_catheter = catheter;
    port_ctrl_set_mode(g_current_work_mode);
    LOG_I("Catheter changed!");
    
    return 0;
}

/**
 * @brief 设置端口工作模式
 * @param mode 工作模式
 * @return 0成功，负数表示错误码
 * @note 在标测模式和贴靠检测模式下，极性选择GPIO必须断开（设为1）。
 *       在回路阻抗检测模式和消融模式下，根据当前导管信息设置极性选择GPIO。
 */
int port_ctrl_set_mode(port_mode_t mode)
{
    int ret = 0;
    
    /* 根据模式设置继电器和极性GPIO */
    switch (mode) {
    case PORT_MODE_MAPPING:
        port_ctrl_set_mode_relays(1U, 0U, 0U);
        /* 标测模式下，极性选择GPIO强制为1（保持断开状态） */
        gpio_write(NEG_PLATE_RELAY_PIN_ID, 1U);
        gpio_write(TOP_WIRE_POL_PIN_ID, 1U);
        gpio_write(POLE_POL_SELECT_PIN_ID, 1U);
        /* 标测模式下，电极位图必须为0，直接控制硬件 */
        g_current_work_mode = mode;
        ret = __apply_elec_bitmap_to_hardware(0x00000000U);
        if (ret != 0) {
            LOG_E("Failed to apply electrode bitmap! ret=%d", ret);
            return ret;
        }
        break;
        
    case PORT_MODE_CONTACT_IMP:
        if (g_current_catheter.type == CATH_TYPE_PVI1) {
            LOG_I("PVI1 catheter not support contact imp mode!");
            return -EINVAL;
        }
        port_ctrl_set_mode_relays(0U, 1U, 0U);
        /* 贴靠模式下，极性选择GPIO强制为1（保持断开状态） */
        gpio_write(NEG_PLATE_RELAY_PIN_ID, 1U);
        gpio_write(TOP_WIRE_POL_PIN_ID, 1U);
        gpio_write(POLE_POL_SELECT_PIN_ID, 1U);
        /* 贴靠模式下，电极位图必须为0，直接控制硬件 */
        g_current_work_mode = mode;
        ret = __apply_elec_bitmap_to_hardware(0x00000000U);
        if (ret != 0) {
            LOG_E("Failed to apply electrode bitmap! ret=%d", ret);
            return ret;
        }
        break;
    
    case PORT_MODE_LOOP_IMP:
        port_ctrl_set_mode_relays(0U, 0U, 1U);
        ret = __set_cath_polarity();
        if (ret != 0) {
            LOG_D("__set_cath_polarity errno = %d", ret);
            return ret;
        }
        g_current_work_mode = mode;
        
        /* 应用预设的电极位图（应用约束规则） */
        ret = __apply_elec_bitmap_to_hardware(g_preset_elec_bitmap);
        if (ret != 0) {
            LOG_E("Failed to apply electrode bitmap! ret=%d", ret);
            return ret;
        }
        break;

    case PORT_MODE_ABLATION:
        port_ctrl_set_mode_relays(0U, 0U, 0U);
        ret = __set_cath_polarity();
        if (ret != 0) {
            LOG_D("__set_cath_polarity errno = %d", ret);
            return ret;
        }
        g_current_work_mode = mode;
        
        /* 应用预设的电极位图（应用约束规则） */
        ret = __apply_elec_bitmap_to_hardware(g_preset_elec_bitmap);
        if (ret != 0) {
            LOG_E("Failed to apply electrode bitmap! ret=%d", ret);
            return ret;
        }
        break;

    default:
        return -EINVAL;
    }
    
    return ret;
}

/**
 * @brief 控制电极位图（进行约束检查）
 * @param pole_elec_bitmap 电极位图
 *        bit0: 顶(网)电极，使用 GPIO 控制
 *        bit1-bit4: 1杆1电极~1杆4电极，映射到TCA6424 P00-P03 (bit0-3)
 *        bit5-bit8: 3杆1电极~3杆4电极，映射到TCA6424 P04-P07 (bit4-7)
 *        bit9-bit12: 5杆1电极~5杆4电极，映射到TCA6424 P10-P13 (bit8-11)
 *        bit13-bit16: 2杆1电极~2杆4电极，映射到TCA6424 P14-P17 (bit12-15)
 *        bit17-bit20: 4杆1电极~4杆4电极，映射到TCA6424 P20-P23 (bit16-19)
 *        bit21-bit24: 6杆1电极~6杆4电极，映射到TCA6424 P24-P27 (bit20-23)
 * @return 0成功，-EINVAL表示参数不符合约束
 * @note 此函数在任何模式下都可以被调用，进行约束检查并赋值给g_preset_elec_bitmap。
 *       在回路阻抗模式和消融模式下，约束检查通过后还会实际应用到硬件。
 *       在其他模式下，仅保存预设值，不控制硬件。
 */
int port_ctrl_elec(uint32_t pole_elec_bitmap)
{
    int ret = 0;
    
    /* 约束检查 */
    if (!__check_constraints(pole_elec_bitmap)) {
        /* 约束检查不通过，直接返回 */
        LOG_W("Electrode bitmap violates constraints! bitmap=0x%08X", pole_elec_bitmap);
        return -EINVAL;
    }
    
    /* 约束检查通过，保存预设位图 */
    g_preset_elec_bitmap = pole_elec_bitmap;
    LOG_D("Preset electrode bitmap: 0x%08X (mode=%d)", pole_elec_bitmap, g_current_work_mode);
    
    /* 若在回路阻抗模式和消融模式下，实际应用到硬件 */
    if ((g_current_work_mode == PORT_MODE_LOOP_IMP) || 
        (g_current_work_mode == PORT_MODE_ABLATION)) {
        ret = __apply_elec_bitmap_to_hardware(pole_elec_bitmap);
        if (ret != 0) {
            LOG_E("Failed to apply electrode bitmap to hardware! ret=%d", ret);
            return ret;
        }
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
 * @brief 检查电极位图是否符合约束规则
 * @param bitmap 电极位图
 * @return true表示符合约束，false表示不符合约束
 * @note 此函数仅进行约束检查，不修改位图，不控制硬件
 */
static bool __check_constraints(uint32_t bitmap)
{
    /* 根据导管类型检查电极约束 */
    if (g_current_catheter.type == CATH_TYPE_FOCAL) {
        /* 局灶导管：1-6杆的3-4电极必须断开 */
        if ((bitmap & FOCAL_POLE_34_MASK) != 0x00000000U) {
            return false;
        }
    } else if (g_current_catheter.type == CATH_TYPE_PVI1) {
        /* PVI一代导管：1-6杆的4电极必须断开 */
        if ((bitmap & PVI1_POLE_4_MASK) != 0x00000000U) {
            return false;
        }
    }
    
    /* 根据极性模式检查顶电极状态 */
    if (g_current_catheter.polarity == CATH_POL_A) {
        /* CATH_POL_A：顶电极必须断开 */
        if ((bitmap & 0x01U) != 0x00000000U) {
            return false;
        }
    } else if (g_current_catheter.polarity == CATH_POL_C) {
        /* CATH_POL_C：顶电极必须导通 */
        if ((bitmap & 0x01U) != 0x01U) {
            return false;
        }
    } else if (g_current_catheter.polarity == CATH_POL_D) {
        /* CATH_POL_D：顶电极必须导通 */
        if ((bitmap & 0x01U) != 0x01U) {
            return false;
        }
    }
    /* CATH_POL_B：不强制约束顶电极状态（由用户位图决定） */
    
    return true;
}

/**
 * @brief 应用约束规则到位图
 * @param bitmap 原始位图
 * @return None
 * @note 此函数仅应用约束规则，不控制硬件
 */
static void __apply_constraints_to_bitmap(uint32_t bitmap)
{
    uint32_t constrained_bitmap = bitmap;
    
    /* 根据导管类型应用电极约束 */
    if (g_current_catheter.type == CATH_TYPE_FOCAL) {
        /* 局灶导管：1-6杆的3-4电极必须断开 */
        constrained_bitmap &= (~FOCAL_POLE_34_MASK);
    } else if (g_current_catheter.type == CATH_TYPE_PVI1) {
        /* PVI一代导管：1-6杆的4电极必须断开 */
        constrained_bitmap &= (~PVI1_POLE_4_MASK);
    }
    
    /* 根据极性模式强制设置顶电极状态 */
    if (g_current_catheter.polarity == CATH_POL_A) {
        /* CATH_POL_A：顶电极必须断开 */
        constrained_bitmap &= (~0x01U);
    } else if (g_current_catheter.polarity == CATH_POL_C) {
        /* CATH_POL_C：顶电极必须导通 */
        constrained_bitmap |= 0x01U;
    } else if (g_current_catheter.polarity == CATH_POL_D) {
        /* CATH_POL_D：顶电极必须导通 */
        constrained_bitmap |= 0x01U;
    }
    /* CATH_POL_B：不强制约束顶电极状态（由用户位图决定） */
    
    g_preset_elec_bitmap = constrained_bitmap;
}

/**
 * @brief 应用电极位图到硬件
 * @param bitmap 电极位图
 * @return 0成功，负数表示错误码
 * @note 在标测模式和贴靠模式下，强制位图为0。
 *       在回路阻抗模式和消融模式下，应用预设参数。
 */
static int __apply_elec_bitmap_to_hardware(uint32_t bitmap)
{
    int ret = 0;
    uint32_t final_bitmap = 0U;
    uint32_t pole_bitmap = 0U;
    uint8_t top_wire_state = 0U;
    
    /* 在标测模式和贴靠模式下，强制位图为0 */
    if ((g_current_work_mode == PORT_MODE_MAPPING) || 
        (g_current_work_mode == PORT_MODE_CONTACT_IMP)) {
        final_bitmap = 0x00000000U;
    } else if ((g_current_work_mode == PORT_MODE_LOOP_IMP) || 
               (g_current_work_mode == PORT_MODE_ABLATION)) {
        /* 在回路阻抗模式和消融模式下，直接应用预设参数 */
        final_bitmap = g_preset_elec_bitmap;
    } else {
        LOG_W("Mode not init!");
        return -ENOTSUPP;
    }
    
    /* 如果位图没有变化，直接返回 */
    if (final_bitmap == g_current_elec_bitmap) {
        return 0;
    }
    
    /* 提取顶电极状态（bit0） */
    top_wire_state = (uint8_t)(final_bitmap & 0x01U);
    
    /* 提取电杆电极位图（bit1-bit24） */
    pole_bitmap = (final_bitmap >> 1U) & 0x00FFFFFFU;
    
    /* 控制电杆电极,低电平是开，高电平是关 */
    ret = tca6424_write_outputs(&tca6424_dev, (~pole_bitmap) & 0x00FFFFFFU);
    if (ret != 0) {
        LOG_E("tca6424_write_outputs fail! ret=%d", ret);
        /* 写失败时硬件复位TCA6424并恢复到安全状态（全关） */
        tca6424_reset(&tca6424_dev);
        (void)tca6424_write_outputs(&tca6424_dev, 0x00FFFFFFU);
        (void)tca6424_write_config(&tca6424_dev, 0x00000000U);
        return ret;
    }
    
    /* 控制顶(网)电极（bit0）,低电平是开，高电平是关 */
    gpio_write(TOP_WIRE_PIN_ID, (uint8_t)(~top_wire_state & 0x01U));
    
    /* 更新保存的位图 */
    g_current_elec_bitmap = final_bitmap;
    
    LOG_I("Applied electrode bitmap to hardware: bitmap=0x%08X", final_bitmap);
    
    return 0;
}

/**
 * @brief 设置极性选择继电器GPIO
 * @return 0成功，负数表示错误码
 * @note 低电平是开，高电平是关
 *       负极板继电器：断开=1（高电平），导通=0（低电平）
 *       顶(网)电极极性：负=1（高电平），正=0（低电平）
 *       1-3-5电杆极性：正=0（低电平），负=1（高电平）
 */
static int __set_cath_polarity(void)
{
    /* 根据极性设置GPIO（CATH_POL_A三个导管都支持，其他极性根据导管类型支持） */
    if (g_current_catheter.polarity == CATH_POL_A) {
        /* CATH_POL_A：负极板-断开, 顶(网)电极极性-负, 1-3-5杆极性-正（三个导管都支持） */
        gpio_write(NEG_PLATE_RELAY_PIN_ID, 1U);
        gpio_write(TOP_WIRE_POL_PIN_ID, 1U);
        gpio_write(POLE_POL_SELECT_PIN_ID, 0U);
    } else if (g_current_catheter.polarity == CATH_POL_B) {
        /* CATH_POL_B：负极板-导通, 顶(网)电极极性-负, 1-3-5杆极性-负（仅PVI2代/局灶） */
        if ((g_current_catheter.type != CATH_TYPE_PVI2) && 
            (g_current_catheter.type != CATH_TYPE_FOCAL)) {
            LOG_I("CATH_POL_B only support PVI2/FOCAL! type=%d", g_current_catheter.type);
            return -EINVAL;
        }
        gpio_write(NEG_PLATE_RELAY_PIN_ID, 0U);
        gpio_write(TOP_WIRE_POL_PIN_ID, 1U);
        gpio_write(POLE_POL_SELECT_PIN_ID, 1U);
    } else if (g_current_catheter.polarity == CATH_POL_C) {
        /* CATH_POL_C：负极板-断开, 顶(网)电极极性-正, 1-3-5杆极性-负（仅PVI2代/局灶） */
        if ((g_current_catheter.type != CATH_TYPE_PVI2) && 
            (g_current_catheter.type != CATH_TYPE_FOCAL)) {
            LOG_I("CATH_POL_C only support PVI2/FOCAL! type=%d", g_current_catheter.type);
            return -EINVAL;
        }
        gpio_write(NEG_PLATE_RELAY_PIN_ID, 1U);
        gpio_write(TOP_WIRE_POL_PIN_ID, 0U);
        gpio_write(POLE_POL_SELECT_PIN_ID, 1U);
    } else if (g_current_catheter.polarity == CATH_POL_D) {
        /* CATH_POL_D：负极板-断开, 顶(网)电极极性-正, 1-3-5杆极性-正（仅PVI2代） */
        if (g_current_catheter.type != CATH_TYPE_PVI2) {
            LOG_I("CATH_POL_D only support PVI2! type=%d", g_current_catheter.type);
            return -EINVAL;
        }
        gpio_write(NEG_PLATE_RELAY_PIN_ID, 1U);
        gpio_write(TOP_WIRE_POL_PIN_ID, 0U);
        gpio_write(POLE_POL_SELECT_PIN_ID, 0U);
    } else {
        LOG_I("Catheter polarity error! polarity=%d", g_current_catheter.polarity);
        return -EINVAL;
    }
    
    return 0;
}

/**
 * @brief 设置模式继电器GPIO
 * @param ecg_map ECG_MAP_RELAY_PIN_ID 状态
 * @param contact_imp CONTACT_IMP_RELAY_PIN_ID 状态（如果启用）
 * @param loop_imp LOOP_IMP_RELAY_PIN_ID 状态
 */
static void port_ctrl_set_mode_relays(uint8_t ecg_map, uint8_t contact_imp, uint8_t loop_imp)
{
    /* 低电平是开，高电平是关 */
    gpio_write(ECG_MAP_RELAY_PIN_ID, (uint8_t)(~ecg_map & 0x01U));
#if (CONTACT_IMP_USE_GPIO == 1)
    gpio_write(CONTACT_IMP_RELAY_PIN_ID, (uint8_t)(~contact_imp & 0x01U));
#else
    /* 通过贴靠检测板应用层控制继电器 */
    (void)contact_imp_ctrl_relay((uint8_t)(~contact_imp & 0x01U));
#endif
    gpio_write(LOOP_IMP_RELAY_PIN_ID, (uint8_t)(~loop_imp & 0x01U));
    LOG_I("port ctrl set mode relays complete!");
}
