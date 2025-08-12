#ifndef _BSP_KSZ8851SNL_H_
#define _BSP_KSZ8851SNL_H_

#include "driver_ksz8851snl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern drv_ksz8851snl_t ksz8851snl_0;

// Debug and test functions
void ksz8851snl_debug_print_irq_stats(void);
void ksz8851snl_debug_test_registers(void);
void ksz8851snl_debug_gpio_test(void);
void ksz8851snl_debug_force_interrupt_test(void);

#ifdef __cplusplus
}
#endif

#endif // _BSP_KSZ8851SNL_H_