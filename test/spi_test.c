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
#define SPI_TEST_DEVICE_NAME     "ad5940"
#define SPI_TEST_CONTROLLER_NAME "spi1"

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static struct spi_device *test_spi_dev = NULL;
static uint8_t tx_buffer[SPI_TEST_BUFFER_SIZE];
static uint8_t rx_buffer[SPI_TEST_BUFFER_SIZE];

/* Exported variables -------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static int spi_test_find_device(void);
static void spi_test_fill_pattern(uint8_t *buf, size_t len, uint8_t pattern);

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Fill buffer with pattern
 * @param buf Buffer pointer
 * @param len Buffer length
 * @param pattern Pattern value
 */
static void spi_test_fill_pattern(uint8_t *buf, size_t len, uint8_t pattern)
{
    size_t i;
    
    if (buf == NULL) {
        return;
    }
    
    for (i = 0U; i < len; i++) {
        buf[i] = (uint8_t)(pattern + (uint8_t)i);
    }
}

/**
 * @brief Find and attach SPI device for testing
 * @return 0 on success, error code on failure
 */
static int spi_test_find_device(void)
{
    struct spi_controller *ctrl;
    static struct spi_device test_device = {
        .name = SPI_TEST_DEVICE_NAME,
        .controller = NULL,
        .max_speed_hz = 16000000U,      /* 1MHz */
        .chip_select = 0U,
        .mode = SPI_MODE_0 | SPI_MODE_SW_CS | SPI_MODE_4WIRE | SPI_MODE_MSB,
        .bits_per_word = 8U,
        .cs_pin = 4U,                  /* Use software CS */
        .controller_data = NULL
    };
    int ret;
    
    /* Find controller */
    ctrl = spi_controller_find(SPI_TEST_CONTROLLER_NAME);
    if (ctrl == NULL) {
        LOG_E("SPI controller '%s' not found", SPI_TEST_CONTROLLER_NAME);
        return -1;
    }
    
    /* Attach device to controller */
    ret = spi_device_attach(&test_device, SPI_TEST_CONTROLLER_NAME);
    if (ret != 0) {
        LOG_E("Failed to attach SPI device: %d", ret);
        return ret;
    }
    
    test_spi_dev = &test_device;
    LOG_I("SPI device attached successfully");
    
    return 0;
}

/**
 * @brief Initialize SPI test
 * @return 0 on success, error code on failure
 */
int spi_test_init(void)
{
    int ret;
    
    LOG_I("SPI test initialization");
    
    /* Find and attach device */
    ret = spi_test_find_device();
    if (ret != 0) {
        LOG_E("SPI test init failed: %d", ret);
        return ret;
    }
    
    /* Initialize test buffers */
    (void)memset(tx_buffer, 0, sizeof(tx_buffer));
    (void)memset(rx_buffer, 0, sizeof(rx_buffer));
    
    LOG_I("SPI test initialized successfully");
    
    return 0;
}

/**
 * @brief SPI basic read/write test
 * @return 0 on success, error code on failure
 */
int spi_test_basic_rw(void)
{
    int ret;
    size_t test_len;
    uint8_t i;
    uint8_t match;
    
    if (test_spi_dev == NULL) {
        LOG_E("SPI device not initialized");
        return -1;
    }
    
    LOG_I("SPI basic read/write test");
    
    /* Test 1: Write test */
    test_len = 16U;
    spi_test_fill_pattern(tx_buffer, test_len, 0xAAU);
    
    ret = spi_write(test_spi_dev, tx_buffer, test_len);
    if (ret < 0) {
        LOG_E("SPI write failed: %d", ret);
        return ret;
    }
    
    if ((size_t)ret != test_len) {
        LOG_E("SPI write length mismatch: expected %u, got %d", test_len, ret);
        return -1;
    }
    
    LOG_I("SPI write test passed: %d bytes", ret);
    
    /* Test 2: Read test */
    (void)memset(rx_buffer, 0, sizeof(rx_buffer));
    
    ret = spi_read(test_spi_dev, rx_buffer, test_len);
    if (ret < 0) {
        LOG_E("SPI read failed: %d", ret);
        return ret;
    }
    
    if ((size_t)ret != test_len) {
        LOG_E("SPI read length mismatch: expected %u, got %d", test_len, ret);
        return -1;
    }
    
    LOG_I("SPI read test passed: %d bytes", ret);
    
    /* Test 3: Write-then-read test */
    test_len = 8U;
    spi_test_fill_pattern(tx_buffer, test_len, 0x55U);
    (void)memset(rx_buffer, 0, sizeof(rx_buffer));
    
    ret = spi_write_then_read(test_spi_dev, tx_buffer, test_len, 
                              rx_buffer, test_len);
    if (ret < 0) {
        LOG_E("SPI write_then_read failed: %d", ret);
        return ret;
    }
    
    LOG_I("SPI write_then_read test passed: %d bytes", ret);
    
    /* Test 4: Verify read data (if device echoes) */
    match = 1U;
    for (i = 0U; i < (uint8_t)test_len; i++) {
        if (rx_buffer[i] != tx_buffer[i]) {
            match = 0U;
            break;
        }
    }
    
    if (match != 0U) {
        LOG_I("SPI data verification passed");
    } else {
        LOG_W("SPI data verification: data mismatch (may be normal for some devices)");
    }
    
    LOG_I("SPI basic read/write test completed");
    
    return 0;
}

/**
 * @brief SPI message transfer test
 * @return 0 on success, error code on failure
 */
int spi_test_message_transfer(void)
{
    struct spi_message msg;
    struct spi_transfer transfer;
    int ret;
    size_t test_len;
    
    if (test_spi_dev == NULL) {
        LOG_E("SPI device not initialized");
        return -1;
    }
    
    LOG_I("SPI message transfer test");
    
    /* Initialize message */
    spi_message_init(&msg);
    
    /* Prepare transfer */
    test_len = 10U;
    spi_test_fill_pattern(tx_buffer, test_len, 0x33U);
    (void)memset(rx_buffer, 0, sizeof(rx_buffer));
    
    transfer.tx_buf = tx_buffer;
    transfer.rx_buf = rx_buffer;
    transfer.len = test_len;
    transfer.cs_change = 1U;
    list_node_init(&transfer.transfer_list);
    
    /* Add transfer to message */
    spi_message_add_tail(&transfer, &msg);
    
    /* Execute transfer */
    ret = spi_sync(test_spi_dev, &msg);
    if (ret < 0) {
        LOG_E("SPI sync failed: %d", ret);
        return ret;
    }
    
    if (msg.status != 0) {
        LOG_E("SPI message status error: %d", msg.status);
        return msg.status;
    }
    
    LOG_I("SPI message transfer test passed: %d bytes", ret);
    
    return 0;
}

/**
 * @brief SPI helper functions test
 * @return 0 on success, error code on failure
 */
int spi_test_helper_functions(void)
{
    int ret;
    uint8_t cmd;
    uint8_t result8;
    uint16_t result16;
    
    if (test_spi_dev == NULL) {
        LOG_E("SPI device not initialized");
        return -1;
    }
    
    LOG_I("SPI helper functions test");
    
    /* Test spi_w8r8 */
    cmd = 0x9FU;  /* Common SPI NOR flash read ID command */
    ret = spi_w8r8(test_spi_dev, cmd);
    if (ret < 0) {
        LOG_E("spi_w8r8 failed: %d", ret);
        return ret;
    }
    
    result8 = (uint8_t)ret;
    LOG_I("spi_w8r8 test passed: cmd=0x%02X, result=0x%02X", cmd, result8);
    
    /* Test spi_w8r16 */
    cmd = 0x90U;  /* Common SPI NOR flash read ID command (extended) */
    ret = spi_w8r16(test_spi_dev, cmd, &result16);
    if (ret < 0) {
        LOG_E("spi_w8r16 failed: %d", ret);
        return ret;
    }
    
    LOG_I("spi_w8r16 test passed: cmd=0x%02X, result=0x%04X", cmd, result16);
    
    LOG_I("SPI helper functions test completed");
    
    return 0;
}

/**
 * @brief SPI multi-transfer message test
 * @return 0 on success, error code on failure
 */
int spi_test_multi_transfer(void)
{
    struct spi_message msg;
    struct spi_transfer transfer1;
    struct spi_transfer transfer2;
    struct spi_transfer transfer3;
    int ret;
    size_t len1;
    size_t len2;
    size_t len3;
    
    if (test_spi_dev == NULL) {
        LOG_E("SPI device not initialized");
        return -1;
    }
    
    LOG_I("SPI multi-transfer message test");
    
    /* Initialize message */
    spi_message_init(&msg);
    
    /* Prepare first transfer (write command) */
    len1 = 1U;
    tx_buffer[0] = 0x03U;  /* Read command */
    transfer1.tx_buf = tx_buffer;
    transfer1.rx_buf = NULL;
    transfer1.len = len1;
    transfer1.cs_change = 0U;
    list_node_init(&transfer1.transfer_list);
    spi_message_add_tail(&transfer1, &msg);
    
    /* Prepare second transfer (write address) */
    len2 = 3U;
    tx_buffer[1] = 0x00U;
    tx_buffer[2] = 0x00U;
    tx_buffer[3] = 0x00U;
    transfer2.tx_buf = &tx_buffer[1];
    transfer2.rx_buf = NULL;
    transfer2.len = len2;
    transfer2.cs_change = 0U;
    list_node_init(&transfer2.transfer_list);
    spi_message_add_tail(&transfer2, &msg);
    
    /* Prepare third transfer (read data) */
    len3 = 4U;
    (void)memset(rx_buffer, 0, sizeof(rx_buffer));
    transfer3.tx_buf = NULL;
    transfer3.rx_buf = rx_buffer;
    transfer3.len = len3;
    transfer3.cs_change = 1U;
    list_node_init(&transfer3.transfer_list);
    spi_message_add_tail(&transfer3, &msg);
    
    /* Execute multi-transfer message */
    ret = spi_sync(test_spi_dev, &msg);
    if (ret < 0) {
        LOG_E("SPI multi-transfer sync failed: %d", ret);
        return ret;
    }
    
    if (msg.status != 0) {
        LOG_E("SPI message status error: %d", msg.status);
        return msg.status;
    }
    
    LOG_I("SPI multi-transfer test passed: total %d bytes", ret);
    LOG_I("  Transfer 1: %u bytes (write)", len1);
    LOG_I("  Transfer 2: %u bytes (write)", len2);
    LOG_I("  Transfer 3: %u bytes (read)", len3);
    
    return 0;
}

/**
 * @brief SPI loopback test (MOSI and MISO shorted)
 * @return 0 on success, error code on failure
 * @note This test requires MOSI and MISO to be shorted together
 */
int spi_test_loopback(void)
{
    struct spi_message msg;
    struct spi_transfer transfer;
    int ret;
    size_t test_len;
    size_t i;
    uint8_t match;
    uint8_t pattern;
    
    if (test_spi_dev == NULL) {
        LOG_E("SPI device not initialized");
        return -1;
    }
    
    LOG_I("SPI loopback test (MOSI <-> MISO shorted)");
    
    /* Initialize message */
    spi_message_init(&msg);
    
    /* Test with different data patterns and lengths */
    for (pattern = 0x00U; pattern < 0x10U; pattern++) {
        test_len = (size_t)(pattern + 1U);
        if (test_len > SPI_TEST_BUFFER_SIZE) {
            test_len = SPI_TEST_BUFFER_SIZE;
        }
        
        /* Fill TX buffer with pattern */
        spi_test_fill_pattern(tx_buffer, test_len, pattern);
        
        /* Clear RX buffer */
        (void)memset(rx_buffer, 0, sizeof(rx_buffer));
        
        /* Prepare transfer: simultaneous TX and RX */
        transfer.tx_buf = tx_buffer;
        transfer.rx_buf = rx_buffer;
        transfer.len = test_len;
        transfer.cs_change = 1U;
        list_node_init(&transfer.transfer_list);
        
        /* Clear message */
        spi_message_init(&msg);
        spi_message_add_tail(&transfer, &msg);
        
        /* Execute transfer */
        ret = spi_sync(test_spi_dev, &msg);
        if (ret < 0) {
            LOG_E("SPI loopback transfer failed: %d (pattern=0x%02X, len=%u)", 
                  ret, pattern, test_len);
            return ret;
        }
        
        if (msg.status != 0) {
            LOG_E("SPI message status error: %d (pattern=0x%02X, len=%u)", 
                  msg.status, pattern, test_len);
            return msg.status;
        }
        
        /* Verify received data matches transmitted data */
        match = 1U;
        for (i = 0U; i < test_len; i++) {
            if (rx_buffer[i] != tx_buffer[i]) {
                match = 0U;
                LOG_E("SPI loopback data mismatch at index %u: "
                      "TX=0x%02X, RX=0x%02X (pattern=0x%02X)", 
                      i, tx_buffer[i], rx_buffer[i], pattern);
                break;
            }
        }
        
        if (match == 0U) {
            LOG_E("SPI loopback test failed for pattern=0x%02X, len=%u", 
                  pattern, test_len);
            return -1;
        }
        
        LOG_D("SPI loopback test passed: pattern=0x%02X, len=%u", 
              pattern, test_len);
    }
    
    /* Test with specific patterns */
    LOG_I("SPI loopback test: Testing specific patterns");
    
    /* Test 1: All zeros */
    test_len = 16U;
    (void)memset(tx_buffer, 0x00U, test_len);
    (void)memset(rx_buffer, 0xFFU, test_len);
    transfer.tx_buf = tx_buffer;
    transfer.rx_buf = rx_buffer;
    transfer.len = test_len;
    transfer.cs_change = 1U;
    list_node_init(&transfer.transfer_list);
    spi_message_init(&msg);
    spi_message_add_tail(&transfer, &msg);
    
    ret = spi_sync(test_spi_dev, &msg);
    if ((ret < 0) || (msg.status != 0)) {
        LOG_E("SPI loopback test (all zeros) failed");
        return (ret < 0) ? ret : msg.status;
    }
    
    match = 1U;
    for (i = 0U; i < test_len; i++) {
        if (rx_buffer[i] != 0x00U) {
            match = 0U;
            break;
        }
    }
    if (match == 0U) {
        LOG_E("SPI loopback test (all zeros) data mismatch");
        return -1;
    }
    LOG_I("SPI loopback test (all zeros) passed");
    
    /* Test 2: All ones */
    (void)memset(tx_buffer, 0xFFU, test_len);
    (void)memset(rx_buffer, 0x00U, test_len);
    transfer.tx_buf = tx_buffer;
    transfer.rx_buf = rx_buffer;
    transfer.len = test_len;
    transfer.cs_change = 1U;
    list_node_init(&transfer.transfer_list);
    spi_message_init(&msg);
    spi_message_add_tail(&transfer, &msg);
    
    ret = spi_sync(test_spi_dev, &msg);
    if ((ret < 0) || (msg.status != 0)) {
        LOG_E("SPI loopback test (all ones) failed");
        return (ret < 0) ? ret : msg.status;
    }
    
    match = 1U;
    for (i = 0U; i < test_len; i++) {
        if (rx_buffer[i] != 0xFFU) {
            match = 0U;
            break;
        }
    }
    if (match == 0U) {
        LOG_E("SPI loopback test (all ones) data mismatch");
        return -1;
    }
    LOG_I("SPI loopback test (all ones) passed");
    
    /* Test 3: Alternating pattern */
    for (i = 0U; i < test_len; i++) {
        tx_buffer[i] = (uint8_t)((i % 2U) == 0U ? 0xAAU : 0x55U);
    }
    (void)memset(rx_buffer, 0x00U, test_len);
    transfer.tx_buf = tx_buffer;
    transfer.rx_buf = rx_buffer;
    transfer.len = test_len;
    transfer.cs_change = 1U;
    list_node_init(&transfer.transfer_list);
    spi_message_init(&msg);
    spi_message_add_tail(&transfer, &msg);
    
    ret = spi_sync(test_spi_dev, &msg);
    if ((ret < 0) || (msg.status != 0)) {
        LOG_E("SPI loopback test (alternating) failed");
        return (ret < 0) ? ret : msg.status;
    }
    
    match = 1U;
    for (i = 0U; i < test_len; i++) {
        uint8_t expected = (uint8_t)((i % 2U) == 0U ? 0xAAU : 0x55U);
        if (rx_buffer[i] != expected) {
            match = 0U;
            LOG_E("SPI loopback test (alternating) mismatch at index %u: "
                  "expected=0x%02X, got=0x%02X", i, expected, rx_buffer[i]);
            break;
        }
    }
    if (match == 0U) {
        LOG_E("SPI loopback test (alternating) data mismatch");
        return -1;
    }
    LOG_I("SPI loopback test (alternating) passed");
    
    LOG_I("SPI loopback test completed successfully");
    
    return 0;
}

/**
 * @brief SPI test task (periodic test)
 */
void spi_test_task(void)
{
    static uint32_t test_counter = 0U;
    int ret;
    
    /* Run basic test every 1000 iterations */
    if ((test_counter % 1000U) == 0U) {
        ret = spi_test_basic_rw();
        if (ret != 0) {
            LOG_E("SPI basic test failed: %d", ret);
        }
    }
    
    /* Run helper functions test every 2000 iterations */
    if ((test_counter % 2000U) == 0U) {
        ret = spi_test_helper_functions();
        if (ret != 0) {
            LOG_E("SPI helper functions test failed: %d", ret);
        }
    }
    
    test_counter++;
    
    if (test_counter == 0U) {
        test_counter = 1U;  /* Prevent overflow */
    }
}

/* Private functions ---------------------------------------------------------*/
