/**
  ******************************************************************************
  * @file        : spi_test.h
  * @author      : ZJY
  * @version     : V1.0
  * @date        : 2025-01-XX
  * @brief       : SPI test header file
  * @attention   : None
  ******************************************************************************
  * @history     :
  *         V1.0 : 1. SPI framework test
  *
  *
  ******************************************************************************
  */
#ifndef __SPI_TEST_H__
#define __SPI_TEST_H__

#ifdef __cplusplus
 extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported macros -----------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize SPI test
 * @return 0 on success, error code on failure
 */
int spi_test_init(void);

/**
 * @brief SPI basic read/write test
 * @return 0 on success, error code on failure
 */
int spi_test_basic_rw(void);

/**
 * @brief SPI message transfer test
 * @return 0 on success, error code on failure
 */
int spi_test_message_transfer(void);

/**
 * @brief SPI helper functions test
 * @return 0 on success, error code on failure
 */
int spi_test_helper_functions(void);

/**
 * @brief SPI multi-transfer message test
 * @return 0 on success, error code on failure
 */
int spi_test_multi_transfer(void);

/**
 * @brief SPI loopback test (MOSI and MISO shorted)
 * @return 0 on success, error code on failure
 */
int spi_test_loopback(void);

/**
 * @brief SPI test task (periodic test)
 */
void spi_test_task(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __SPI_TEST_H__ */
