/**
  ******************************************************************************
  * @file        : bsp_conf.h
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2025-10-16
  * @brief       : BSP configuration file
  * @attention   : None
  ******************************************************************************
  * @history     :
  *         V1.0 : 1.Initial version
  ******************************************************************************
  */
#ifndef __BSP_CONF_H__
#define __BSP_CONF_H__

#ifdef __cplusplus
 extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/

/* Exported define -----------------------------------------------------------*/
/* 芯片选择 */
#define SOC_SERIES_STM32F1

#if defined(SOC_SERIES_STM32F1)
    #include "stm32f1xx.h"
    #define EXPECT_SYSTEM_CLOCK_FREQ    (72000000U)
#elif defined(SOC_SERIES_STM32F4)
    #include "stm32f4xx.h"
#elif defined(STM32G4)
    #include "stm32g4xx.h"
#else
#error "Please select first the soc series used in your application!"    
#endif

#if defined(USE_FULL_LL_DRIVER)
    #include "stm32f1xx_ll_adc.h"
    #include "stm32f1xx_ll_bus.h"
    #include "stm32f1xx_ll_cortex.h"
    #include "stm32f1xx_ll_crc.h"
    #include "stm32f1xx_ll_dac.h"
    #include "stm32f1xx_ll_dma.h"
    #include "stm32f1xx_ll_exti.h"
    #include "stm32f1xx_ll_fsmc.h"
    #include "stm32f1xx_ll_gpio.h"
    #include "stm32f1xx_ll_i2c.h"
    #include "stm32f1xx_ll_iwdg.h"
    #include "stm32f1xx_ll_pwr.h"
    #include "stm32f1xx_ll_rcc.h"
    #include "stm32f1xx_ll_rtc.h"
    #include "stm32f1xx_ll_sdmmc.h"
    #include "stm32f1xx_ll_spi.h"
    #include "stm32f1xx_ll_system.h"
    #include "stm32f1xx_ll_tim.h"
    #include "stm32f1xx_ll_usart.h"
    #include "stm32f1xx_ll_usb.h"
    #include "stm32f1xx_ll_utils.h"
    #include "stm32f1xx_ll_wwdg.h"
#endif

/* 外设宏定义 */
/* 内部 flash 宏定义 */
#if defined(STM32F1)
    #define STM32_FLASH_PAGE_NUM        (128UL)      /* F1系列总页数（根据实际芯片修改） */
    #define STM32_FLASH_USE_NUM         (16)         /* 使用的最后的 page/sector 数量 */
    #define STM32_FLASH_START_ADDR      (FLASH_BASE + (STM32_FLASH_PAGE_NUM - STM32_FLASH_USE_NUM) * FLASH_PAGE_SIZE)
    #define STM32_FLASH_END_ADDR        (FLASH_BASE + (STM32_FLASH_PAGE_NUM * FLASH_PAGE_SIZE) - 1)
    #define STM32_FLASH_ERASE_SIZE      FLASH_PAGE_SIZE
    #define STM32_FLASH_WRITE_UNIT      2            /* F1按半字（2字节）编程 */

#elif defined(SOC_SERIES_STM32F4)
    #define STM32_FLASH_SECTOR_NUM      (12UL)       /* F4系列总扇区数（根据实际芯片修改） */
    #define STM32_FLASH_USE_SECTORS     (2)          /* 使用的最后的扇区数量 */
    #define STM32_FLASH_START_ADDR      (0x080E0000) /* Sector 11起始地址（根据实际修改） */
    #define STM32_FLASH_END_ADDR        (0x080FFFFF) /* Flash末尾地址 */
    #define STM32_FLASH_ERASE_SIZE      (128*1024)   /* 最小擦除单元（扇区大小） */
    #define STM32_FLASH_WRITE_UNIT      4            /* F4按字（4字节）编程 */

#elif defined(STM32G4)
    #define STM32_FLASH_PAGE_NUM        (64UL)       /* G4系列总页数（根据实际芯片修改） */
    #define STM32_FLASH_USE_NUM         (16)         /* 使用的最后的 page/sector 数量 */
    #define STM32_FLASH_START_ADDR      (FLASH_BASE + (STM32_FLASH_PAGE_NUM - STM32_FLASH_USE_NUM) * FLASH_PAGE_SIZE)
    #define STM32_FLASH_END_ADDR        (FLASH_BASE + (STM32_FLASH_PAGE_NUM * FLASH_PAGE_SIZE) - 1)
    #define STM32_FLASH_ERASE_SIZE      FLASH_PAGE_SIZE
    #define STM32_FLASH_WRITE_UNIT      8            /* G4按双字（8字节）编程 */
#else
    #error "Please define STM32 Flash configuration for your MCU series!"
#endif

/* ============================================================================
 * GPIO 引脚配置
 * ============================================================================
 */
#define LED_PIN_ID                      (45)    /* PC13 */

/* ============================================================================
 * UART使能配置
 * ============================================================================
 */
#define BSP_USING_UART1
#define BSP_UART1_RX_USING_DMA
#define BSP_UART1_TX_USING_DMA
#define BSP_USING_UART3
#define BSP_USING_UART5

/* ============================================================================
 * UART1 配置
 * ============================================================================
 */
#ifdef BSP_USING_UART1
    #define BSP_UART1_TX_PORT               GPIOA
    #define BSP_UART1_TX_PIN                GPIO_PIN_9
    #define BSP_UART1_RX_PORT               GPIOA
    #define BSP_UART1_RX_PIN                GPIO_PIN_10
    #define UART1_RX_BUF_SIZE               256
    #define UART1_TX_BUF_SIZE               256
    #define UART1_RX_CACHE_BUF_SIZE         64
    #define BSP_UART1_IRQ_PRIORITY          0
#ifdef BSP_UART1_RX_USING_DMA
    #define BSP_UART1_DMA_RX_INSTANCE       DMA1_Channel5
    #define BSP_UART1_DMA_RX_IRQn           DMA1_Channel5_IRQn
    #define UART1_DMA_RX_IRQHandler         DMA1_Channel5_IRQHandler
#endif
#ifdef BSP_UART1_TX_USING_DMA
    #define BSP_UART1_DMA_TX_INSTANCE       DMA1_Channel4
    #define BSP_UART1_DMA_TX_IRQn           DMA1_Channel4_IRQn
    #define UART1_DMA_TX_IRQHandler         DMA1_Channel4_IRQHandler
#endif
#endif

/* ============================================================================
 * UART3 配置
 * ============================================================================
 */
#ifdef BSP_USING_UART3
    #define BSP_UART3_TX_PORT               GPIOC
    #define BSP_UART3_TX_PIN                GPIO_PIN_10
    #define BSP_UART3_RX_PORT               GPIOC
    #define BSP_UART3_RX_PIN                GPIO_PIN_11
    #define UART3_RX_BUF_SIZE               256
    #define UART3_TX_BUF_SIZE               256
    #define UART3_RX_CACHE_BUF_SIZE         64
    #define BSP_UART3_IRQ_PRIORITY          0
#endif

/* ============================================================================
 * UART5 配置
 * ============================================================================
 */
#ifdef BSP_USING_UART5
    #define BSP_UART5_TX_PORT               GPIOC
    #define BSP_UART5_TX_PIN                GPIO_PIN_12
    #define BSP_UART5_RX_PORT               GPIOD
    #define BSP_UART5_RX_PIN                GPIO_PIN_2
    #define UART5_RX_BUF_SIZE               256
    #define UART5_TX_BUF_SIZE               256
    #define UART5_RX_CACHE_BUF_SIZE         64
    #define BSP_UART5_IRQ_PRIORITY          0
#endif

/* Exported typedef ----------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported variable prototypes ----------------------------------------------*/

/* Exported function prototypes ----------------------------------------------*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __BSP_CONF_H__ */

