#include "bsp_net.h"
#include "driver_net_lwip.h"
#include "bsp_ethernet.h"
#include "utils_assert.h"
#include "printf.h"

// Hardware context for LwIP network driver
static drv_net_lwip_hw_context_t lwip_net_hw_context_0;

// Global LwIP network driver instance
drv_net_t lwip_network_0 = {
    .is_init = false,
    .is_started = false,
    .stack_type = DRV_NET_STACK_LWIP,
    .hw_context = &lwip_net_hw_context_0,
    
    // Function pointers to LwIP implementation
    .init = drv_net_lwip_init_impl,
    .deinit = drv_net_lwip_deinit_impl,
    .start = drv_net_lwip_start_impl,
    .stop = drv_net_lwip_stop_impl,
    .get_status = drv_net_lwip_get_status_impl,
    .wait_for_link = drv_net_lwip_wait_for_link_impl,
    .wait_for_ip = drv_net_lwip_wait_for_ip_impl,
    .print_network_info = drv_net_lwip_print_network_info_impl,
    .ping = drv_net_lwip_ping_impl,
    .register_callback = drv_net_lwip_register_callback_impl,
};