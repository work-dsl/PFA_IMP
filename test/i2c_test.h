/**
 ******************************************************************************
 * @file        : i2c_test.h
 * @author      : ZJY
 * @version     : V1.0
 * @date        : 2025-01-XX
 * @brief       : I2C Driver Test Framework
 * @attention   : None
 ******************************************************************************
 * @history     :
 *         V1.0 : 1. Complete test framework for I2C driver
 *                2. Compiler optimization tests
 *                3. Concurrency tests
 *                4. Environment compatibility tests
 *
 ******************************************************************************
 */
#ifndef __I2C_TEST_H__
#define __I2C_TEST_H__

#ifdef __cplusplus
 extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include "i2c.h"

/* Exported define -----------------------------------------------------------*/
/**
 * @defgroup I2C_Test_Config I2C Test Configuration
 * @{
 */
#define I2C_TEST_ENABLE_OPTIMIZATION_TEST    1  /**< Enable compiler optimization tests */
#define I2C_TEST_ENABLE_CONCURRENCY_TEST    1  /**< Enable concurrency tests */
#define I2C_TEST_ENABLE_ENV_COMPAT_TEST     1  /**< Enable environment compatibility tests */
#define I2C_TEST_ENABLE_FUNCTIONAL_TEST     1  /**< Enable functional tests */

/* Test task configuration */
#define I2C_TEST_TASK_COUNT                  3  /**< Number of concurrent test tasks */
#define I2C_TEST_ITERATION_COUNT             100 /**< Number of iterations for stress test */
#define I2C_TEST_TIMEOUT_MS                  5000U /**< Test timeout in milliseconds */
/** @} */

/* Exported typedef ----------------------------------------------------------*/
/**
 * @brief Test result structure
 */
typedef struct {
    uint32_t total_tests;      /**< Total number of tests */
    uint32_t passed_tests;     /**< Number of passed tests */
    uint32_t failed_tests;     /**< Number of failed tests */
    uint32_t skipped_tests;    /**< Number of skipped tests */
} i2c_test_result_t;

/**
 * @brief Test case function type
 */
typedef int (*i2c_test_case_func_t)(void);

/**
 * @brief Test case structure
 */
typedef struct {
    const char *name;              /**< Test case name */
    i2c_test_case_func_t func;     /**< Test case function */
    uint8_t enabled : 1;           /**< Test case enabled flag */
} i2c_test_case_t;

/* Exported macro ------------------------------------------------------------*/

/* Exported variable prototypes ----------------------------------------------*/

/* Exported function prototypes ----------------------------------------------*/

/**
 * @brief Initialize I2C test framework
 * @return 0 on success, error code on failure
 */
int i2c_test_init(void);

/**
 * @brief Run all I2C tests
 * @param result Pointer to store test results
 * @return 0 on success, error code on failure
 */
int i2c_test_run_all(i2c_test_result_t *result);

/**
 * @brief Run compiler optimization tests
 * @return 0 on success, error code on failure
 */
int i2c_test_compiler_optimization(void);

/**
 * @brief Run concurrency tests
 * @return 0 on success, error code on failure
 */
int i2c_test_concurrency(void);

/**
 * @brief Run environment compatibility tests
 * @return 0 on success, error code on failure
 */
int i2c_test_environment_compatibility(void);

/**
 * @brief Run functional tests
 * @return 0 on success, error code on failure
 */
int i2c_test_functional(void);

/**
 * @brief Test 7-bit address read/write
 * @return 0 on success, error code on failure
 */
int i2c_test_7bit_addr(void);

/**
 * @brief Test 10-bit address read/write
 * @return 0 on success, error code on failure
 */
int i2c_test_10bit_addr(void);

/**
 * @brief Test DMA transfer
 * @return 0 on success, error code on failure
 */
int i2c_test_dma_transfer(void);

/**
 * @brief Test error handling
 * @return 0 on success, error code on failure
 */
int i2c_test_error_handling(void);

/**
 * @brief Test timeout handling
 * @return 0 on success, error code on failure
 */
int i2c_test_timeout(void);

/**
 * @brief Test state consistency (for compiler optimization)
 * @return 0 on success, error code on failure
 */
int i2c_test_state_consistency(void);

/**
 * @brief Test memory barrier effectiveness
 * @return 0 on success, error code on failure
 */
int i2c_test_memory_barrier(void);

/**
 * @brief Test multi-task access to same adapter
 * @return 0 on success, error code on failure
 */
int i2c_test_multi_task_access(void);

/**
 * @brief Test interrupt context and task context mixed access
 * @return 0 on success, error code on failure
 */
int i2c_test_mixed_context_access(void);

/**
 * @brief Test long-running stability
 * @return 0 on success, error code on failure
 */
int i2c_test_long_running_stability(void);

/**
 * @brief Test bare-metal environment compatibility
 * @return 0 on success, error code on failure
 */
int i2c_test_bare_metal_env(void);

/**
 * @brief Test FreeRTOS environment compatibility
 * @return 0 on success, error code on failure
 */
int i2c_test_freertos_env(void);

/**
 * @brief Test RT-Thread environment compatibility
 * @return 0 on success, error code on failure
 */
int i2c_test_rtthread_env(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __I2C_TEST_H__ */
