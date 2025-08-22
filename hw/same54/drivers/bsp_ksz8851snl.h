#ifndef _BSP_KSZ8851SNL_H_
#define _BSP_KSZ8851SNL_H_

#include "driver_ksz8851snl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern drv_ksz8851snl_t ksz8851snl_0;

// MAC address configuration functions
drv_ksz8851snl_status_t bsp_ksz8851snl_set_mac_address(const uint8_t mac_addr[6]);
drv_ksz8851snl_status_t bsp_ksz8851snl_get_mac_address(uint8_t mac_addr[6]);

// Debug and test functions
void ksz8851snl_debug_test_registers(void);
void ksz8851snl_debug_gpio_test(void);
void ksz8851snl_debug_test_packet_transmission(void);
void ksz8851snl_debug_performance_stats(void);
void ksz8851snl_diagnose_false_interrupts(void);
void ksz8851snl_reset_rxq_state(void);
void ksz8851snl_emergency_reset(void);
void ksz8851snl_debug_print_irq_stats(void);
void ksz8851snl_debug_force_interrupt_test(void);

#ifdef __cplusplus
}
#endif

#endif // _BSP_KSZ8851SNL_H_