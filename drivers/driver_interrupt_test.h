#ifndef _DRIVER_INTERRUPT_TEST_H_
#define _DRIVER_INTERRUPT_TEST_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DRV_INT_TEST_STATUS_OK = 0,
    DRV_INT_TEST_STATUS_ERROR = 1,
    DRV_INT_TEST_STATUS_TIMEOUT = 2,
    DRV_INT_TEST_STATUS_NOT_SUPPORTED = 3,
} drv_int_test_status_t;

typedef enum {
    DRV_INT_TEST_TYPE_SOFTWARE = 0,
    DRV_INT_TEST_TYPE_GPIO = 1,
    DRV_INT_TEST_TYPE_TIMER = 2,
    DRV_INT_TEST_TYPE_PERIPHERAL = 3,
} drv_int_test_type_t;

typedef struct {
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t total_interrupts_generated;
    uint32_t total_interrupts_handled;
    uint32_t min_latency_us;
    uint32_t max_latency_us;
    uint32_t avg_latency_us;
    uint32_t missed_interrupts;
} drv_int_test_stats_t;

typedef struct {
    uint8_t eic_channel;
    uint32_t irq_number;
    bool enabled;
    uint32_t interrupt_count;
    uint32_t last_latency_us;
    bool test_passed;
} drv_int_test_channel_info_t;

drv_int_test_status_t hw_interrupt_test_init(void);
drv_int_test_status_t hw_interrupt_test_deinit(void);

drv_int_test_status_t hw_interrupt_test_software_trigger(uint8_t eic_channel);
drv_int_test_status_t hw_interrupt_test_all_channels(void);
drv_int_test_status_t hw_interrupt_test_latency_measurement(uint8_t eic_channel, uint32_t num_tests);
drv_int_test_status_t hw_interrupt_test_stress_test(uint8_t eic_channel, uint32_t frequency_hz, uint32_t duration_ms);

drv_int_test_status_t hw_interrupt_test_get_stats(drv_int_test_stats_t *stats);
drv_int_test_status_t hw_interrupt_test_get_channel_info(uint8_t eic_channel, drv_int_test_channel_info_t *info);
drv_int_test_status_t hw_interrupt_test_reset_stats(void);

drv_int_test_status_t hw_interrupt_test_print_report(void);
drv_int_test_status_t hw_interrupt_test_automated_suite(void);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_INTERRUPT_TEST_H_