/**
 * Minimal KSZ8851SNL Hardware Test - Header
 */

#ifndef _KSZ_MINIMAL_TEST_H_
#define _KSZ_MINIMAL_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start minimal KSZ hardware test
 * This creates a task that:
 * 1. Reads KSZ registers to verify SPI (no 0x55 patterns)
 * 2. Checks TX buffer status
 * 3. Sends simple test packets every 10 seconds
 * 4. NO ECU, NO DOIP, NO complex initialization
 */
void ksz_minimal_test_start(void);

#ifdef __cplusplus
}
#endif

#endif // _KSZ_MINIMAL_TEST_H_