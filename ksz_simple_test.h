/**
 * Simple KSZ8851SNL Test - Header File
 */

#ifndef _KSZ_SIMPLE_TEST_H_
#define _KSZ_SIMPLE_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the KSZ simple test task
 * This creates a single FreeRTOS task that:
 * - Initializes KSZ8851SNL with IP 192.168.100.2
 * - Sets MAC address to 00:00:00:00:20:76
 * - Waits for link up
 * - Sends UDP test packets every 2 seconds
 */
void ksz_simple_test_start(void);

#ifdef __cplusplus
}
#endif

#endif // _KSZ_SIMPLE_TEST_H_