/**
  ******************************************************************************
  * @file        : spi_test.c
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2025-01-XX
  * @brief       : SPI test implementation
  * @attention   : None
  ******************************************************************************
  * @history     :
  *         V1.0 : 1. SPI framework test
  *
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "spi_test.h"
#include "spi.h"
#include <string.h>
#include <stddef.h>

#define  LOG_TAG             "spi_test"
#define  LOG_LVL             4
#include "log.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
#define SPI_TEST_BUFFER_SIZE     (64U)

/* ====== AD5940 SPI 命令（手册 Table 126）====== */
#define AD5940_SPICMD_SETADDR   0x20u
#define AD5940_SPICMD_READREG   0x6Du

/* ====== ID 寄存器地址（手册 Table 11）====== */
#define AD5940_REG_ADIID        0x0400u
#define AD5940_REG_CHIPID       0x0404u

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static struct spi_device *test_spi_dev = NULL;
static uint8_t tx_buffer[SPI_TEST_BUFFER_SIZE];
static uint8_t rx_buffer[SPI_TEST_BUFFER_SIZE];

/* 定义设备 */
static struct spi_device my_spi_device = {
    .name = "my_device",
    .controller = NULL,
    .max_speed_hz = 10000000U,      /* 16MHz */
    .chip_select = 0U,              /* 硬件CS编号 */
    .mode = SPI_MODE_0 | SPI_MODE_MSB | SPI_MODE_SW_CS | SPI_MODE_4WIRE,
    .bits_per_word = 8U,
    .cs_pin = 4U,
    .controller_data = NULL
};

/* Exported variables -------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/
int spi_test_init(void)
{
    int ret;
    
    ret = spi_device_attach(&my_spi_device, "spi1");
    if (ret != 0) {
        LOG_E("spi_device_attach errno!");
        return ret;
    }
    
    return 0;
}

void spi_test_task(void)
{
    uint8_t tx_data[] = {AD5940_SPICMD_SETADDR, 0x04, 0x00};
    spi_write(&my_spi_device, tx_data, 3);
    
    uint8_t tm_data[] = {AD5940_SPICMD_READREG, 0x00};
    uint16_t result;
    spi_write_then_read(&my_spi_device, tm_data, 2, &result, 2);
    
    LOG_D("ad5940 ADIID = 0x%x", result);
}
