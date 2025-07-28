#ifndef _DRIVER_NET_H_
#define _DRIVER_NET_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Network driver status enumeration
typedef enum {
    DRV_NET_STATUS_OK = 0,
    DRV_NET_STATUS_ERROR = 1,
    DRV_NET_STATUS_BUSY = 2,
    DRV_NET_STATUS_TIMEOUT = 3,
    DRV_NET_STATUS_NOT_INITIALIZED = 4,
    DRV_NET_STATUS_LINK_DOWN = 5,
    DRV_NET_STATUS_NO_IP = 6,
} drv_net_status_t;

// Supported TCP/IP stack types
typedef enum {
    DRV_NET_STACK_LWIP = 0,
    DRV_NET_STACK_FREERTOS_TCP = 1,
    DRV_NET_STACK_CYCLONE_TCP = 2,
    DRV_NET_STACK_UIP = 3,
} drv_net_stack_type_t;

// Network configuration structure
typedef struct {
    uint8_t mac_addr[6];
    bool use_dhcp;
    const char *static_ip;      // Only used if use_dhcp = false
    const char *static_netmask; // Only used if use_dhcp = false  
    const char *static_gateway; // Only used if use_dhcp = false
    const char *hostname;
    uint32_t dhcp_timeout_ms;   // DHCP timeout in milliseconds
} drv_net_config_t;

// Network status information structure
typedef struct {
    bool is_initialized;
    bool is_started;
    bool link_up;
    bool has_ip;
    char ip_addr[16];       // IPv4 address string
    char netmask[16];       // Network mask string
    char gateway[16];       // Gateway address string
    char mac_addr_str[18];  // MAC address string (XX:XX:XX:XX:XX:XX)
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
} drv_net_status_info_t;

// Callback types
typedef enum {
    DRV_NET_CB_LINK_UP = 0,
    DRV_NET_CB_LINK_DOWN = 1,
    DRV_NET_CB_IP_ACQUIRED = 2,
    DRV_NET_CB_IP_LOST = 3,
    DRV_NET_CB_ERROR = 4,
} drv_net_cb_type_t;

typedef void (*drv_net_callback_t)(void);

// Universal network driver structure
typedef struct {
    bool is_init;
    bool is_started;
    drv_net_stack_type_t stack_type;
    const void *hw_context;
    
    // Core network operations
    drv_net_status_t (*init)(const void *hw_context);
    drv_net_status_t (*deinit)(const void *hw_context);
    drv_net_status_t (*start)(const void *hw_context, const drv_net_config_t *config);
    drv_net_status_t (*stop)(const void *hw_context);
    
    // Status and monitoring
    drv_net_status_t (*get_status)(const void *hw_context, drv_net_status_info_t *status);
    drv_net_status_t (*wait_for_link)(const void *hw_context, uint32_t timeout_ms);
    drv_net_status_t (*wait_for_ip)(const void *hw_context, uint32_t timeout_ms);
    
    // Utility functions
    drv_net_status_t (*print_network_info)(const void *hw_context);
    drv_net_status_t (*ping)(const void *hw_context, const char *target_ip, uint32_t timeout_ms);
    
    // Callback management
    drv_net_status_t (*register_callback)(const void *hw_context, 
                                         drv_net_cb_type_t type, 
                                         drv_net_callback_t callback);
} drv_net_t;

// Universal API functions
drv_net_status_t hw_net_init(drv_net_t *handle);
drv_net_status_t hw_net_deinit(drv_net_t *handle);
drv_net_status_t hw_net_start(drv_net_t *handle, const drv_net_config_t *config);
drv_net_status_t hw_net_stop(drv_net_t *handle);
drv_net_status_t hw_net_get_status(drv_net_t *handle, drv_net_status_info_t *status);
drv_net_status_t hw_net_wait_for_link(drv_net_t *handle, uint32_t timeout_ms);
drv_net_status_t hw_net_wait_for_ip(drv_net_t *handle, uint32_t timeout_ms);
drv_net_status_t hw_net_print_network_info(drv_net_t *handle);
drv_net_status_t hw_net_ping(drv_net_t *handle, const char *target_ip, uint32_t timeout_ms);
drv_net_status_t hw_net_register_callback(drv_net_t *handle, 
                                         drv_net_cb_type_t type, 
                                         drv_net_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_NET_H_