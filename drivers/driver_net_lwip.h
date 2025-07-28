#ifndef _DRIVER_NET_LWIP_H_
#define _DRIVER_NET_LWIP_H_

#include "driver_net.h"
#include "lwip/sys.h"
#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// LwIP-specific hardware context structure
typedef struct {
    // Hardware drivers
    void *eth_driver;  // Pointer to drv_eth_t
    void *phy_driver;  // Pointer to drv_phy_t
    
    // LwIP network interface
    struct netif netif;
    uint8_t hwaddr[6];
    
    // Synchronization
    sys_sem_t init_sem;
    volatile bool init_done;
    
    // Status tracking
    bool link_up;
    bool has_ip;
    
    // Configuration
    drv_net_config_t current_config;
    
    // Callbacks
    drv_net_callback_t link_up_callback;
    drv_net_callback_t link_down_callback;
    drv_net_callback_t ip_acquired_callback;
    drv_net_callback_t ip_lost_callback;
    drv_net_callback_t error_callback;
} drv_net_lwip_hw_context_t;

// LwIP driver implementation functions
drv_net_status_t drv_net_lwip_init_impl(const void *hw_context);
drv_net_status_t drv_net_lwip_deinit_impl(const void *hw_context);
drv_net_status_t drv_net_lwip_start_impl(const void *hw_context, const drv_net_config_t *config);
drv_net_status_t drv_net_lwip_stop_impl(const void *hw_context);
drv_net_status_t drv_net_lwip_get_status_impl(const void *hw_context, drv_net_status_info_t *status);
drv_net_status_t drv_net_lwip_wait_for_link_impl(const void *hw_context, uint32_t timeout_ms);
drv_net_status_t drv_net_lwip_wait_for_ip_impl(const void *hw_context, uint32_t timeout_ms);
drv_net_status_t drv_net_lwip_print_network_info_impl(const void *hw_context);
drv_net_status_t drv_net_lwip_ping_impl(const void *hw_context, const char *target_ip, uint32_t timeout_ms);
drv_net_status_t drv_net_lwip_register_callback_impl(const void *hw_context, 
                                                    drv_net_cb_type_t type, 
                                                    drv_net_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_NET_LWIP_H_