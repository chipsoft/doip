#ifndef _DRIVER_NET_FREERTOS_TCP_H_
#define _DRIVER_NET_FREERTOS_TCP_H_

#include "driver_net.h"

#ifdef __cplusplus
extern "C" {
#endif

// FreeRTOS-Plus-TCP specific network driver interface
// This driver implements the universal network driver API using FreeRTOS-Plus-TCP

// Hardware context structure for FreeRTOS-Plus-TCP implementation
typedef struct {
    // Network interface configuration
    uint8_t mac_address[6];
    char hostname[32];
    
    // Status tracking
    bool network_up;
    bool link_up;
    bool has_ip_address;
    
    // Statistics
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t send_errors;
    uint32_t recv_errors;
    
    // Callbacks
    drv_net_callback_t link_up_callback;
    drv_net_callback_t link_down_callback;
    drv_net_callback_t ip_acquired_callback;
    drv_net_callback_t ip_lost_callback;
    drv_net_callback_t error_callback;
    
    // FreeRTOS-Plus-TCP specific context
    void *netif_context;  // Will hold reference to network interface
} drv_net_freertos_tcp_context_t;

// Forward declarations of implementation functions
drv_net_status_t drv_net_freertos_tcp_init_impl(const void *hw_context);
drv_net_status_t drv_net_freertos_tcp_deinit_impl(const void *hw_context);
drv_net_status_t drv_net_freertos_tcp_start_impl(const void *hw_context, const drv_net_config_t *config);
drv_net_status_t drv_net_freertos_tcp_stop_impl(const void *hw_context);
drv_net_status_t drv_net_freertos_tcp_get_status_impl(const void *hw_context, drv_net_status_info_t *status);
drv_net_status_t drv_net_freertos_tcp_wait_for_link_impl(const void *hw_context, uint32_t timeout_ms);
drv_net_status_t drv_net_freertos_tcp_wait_for_ip_impl(const void *hw_context, uint32_t timeout_ms);
drv_net_status_t drv_net_freertos_tcp_print_network_info_impl(const void *hw_context);
drv_net_status_t drv_net_freertos_tcp_ping_impl(const void *hw_context, const char *target_ip, uint32_t timeout_ms);
drv_net_status_t drv_net_freertos_tcp_register_callback_impl(const void *hw_context, 
                                                            drv_net_cb_type_t type, 
                                                            drv_net_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_NET_FREERTOS_TCP_H_