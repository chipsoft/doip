#include "driver_net_freertos_tcp.h"
#include "utils_assert.h"
#include "printf.h"

// FreeRTOS-Plus-TCP includes
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DHCP.h"

#include <string.h>
#include <stdlib.h>

// Convert string IP address to 32-bit value
static uint32_t inet_addr_from_string(const char *ip_str)
{
    if (ip_str == NULL) {
        return 0;
    }
    
    unsigned int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        if (a <= 255 && b <= 255 && c <= 255 && d <= 255) {
            return (a << 24) | (b << 16) | (c << 8) | d;
        }
    }
    return 0;
}

// Convert 32-bit IP address to string
static void inet_ntoa_r_custom(uint32_t ip_addr, char *buffer, size_t buffer_size)
{
    if (buffer && buffer_size >= 16) {
        snprintf(buffer, buffer_size, "%lu.%lu.%lu.%lu",
                (ip_addr >> 24) & 0xFF,
                (ip_addr >> 16) & 0xFF,
                (ip_addr >> 8) & 0xFF,
                ip_addr & 0xFF);
    }
}

drv_net_status_t drv_net_freertos_tcp_init_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_net_freertos_tcp_context_t *context = (drv_net_freertos_tcp_context_t *)hw_context;
    
    printf("[NET-FreeRTOS] Initializing FreeRTOS-Plus-TCP network driver\r\n");
    
    // Initialize context
    memset(context, 0, sizeof(drv_net_freertos_tcp_context_t));
    context->network_up = false;
    context->link_up = false;
    context->has_ip_address = false;
    
    // Set default hostname
    strncpy(context->hostname, "same54-doip", sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';
    
    printf("[NET-FreeRTOS] Driver initialized successfully\r\n");
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_freertos_tcp_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_net_freertos_tcp_context_t *context = (drv_net_freertos_tcp_context_t *)hw_context;
    
    printf("[NET-FreeRTOS] Deinitializing network driver\r\n");
    
    // Stop network if running
    if (context->network_up) {
        drv_net_freertos_tcp_stop_impl(hw_context);
    }
    
    // Clear context
    memset(context, 0, sizeof(drv_net_freertos_tcp_context_t));
    
    printf("[NET-FreeRTOS] Driver deinitialized\r\n");
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_freertos_tcp_start_impl(const void *hw_context, const drv_net_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);
    
    drv_net_freertos_tcp_context_t *context = (drv_net_freertos_tcp_context_t *)hw_context;
    
    printf("[NET-FreeRTOS] Starting network with %s configuration\r\n", 
           config->use_dhcp ? "DHCP" : "static IP");
    
    // Copy MAC address
    memcpy(context->mac_address, config->mac_addr, 6);
    
    // Copy hostname if provided
    if (config->hostname != NULL) {
        strncpy(context->hostname, config->hostname, sizeof(context->hostname) - 1);
        context->hostname[sizeof(context->hostname) - 1] = '\0';
    }
    
    // Prepare network configuration
    uint32_t ip_address = 0;
    uint32_t netmask = 0;
    uint32_t gateway = 0;
    uint32_t dns_server = 0;
    
    if (!config->use_dhcp) {
        // Parse static IP configuration
        ip_address = inet_addr_from_string(config->static_ip);
        netmask = inet_addr_from_string(config->static_netmask);
        gateway = inet_addr_from_string(config->static_gateway);
        dns_server = gateway; // Use gateway as DNS server by default
        
        if (ip_address == 0 || netmask == 0 || gateway == 0) {
            printf("[NET-FreeRTOS] ERROR: Invalid static IP configuration\r\n");
            return DRV_NET_STATUS_ERROR;
        }
        
        printf("[NET-FreeRTOS] Static IP configuration:\r\n");
        printf("  IP: %s\r\n", config->static_ip);
        printf("  Netmask: %s\r\n", config->static_netmask);
        printf("  Gateway: %s\r\n", config->static_gateway);
    }
    
    // Initialize FreeRTOS-Plus-TCP stack
    // Note: The actual network interface initialization should be done in the BSP layer
    // This function just starts the IP stack with the provided configuration
    BaseType_t result;
    
    // Initialize the IP stack - Use FreeRTOS_IPInit_Multi for newer versions
    result = FreeRTOS_IPInit_Multi();
    
    if (result != pdTRUE) {
        printf("[NET-FreeRTOS] ERROR: Failed to initialize IP stack\r\n");
        return DRV_NET_STATUS_ERROR;
    }
    
    context->network_up = true;
    
    // If using DHCP, the IP address will be acquired asynchronously
    if (config->use_dhcp) {
        printf("[NET-FreeRTOS] DHCP mode - waiting for IP address assignment\r\n");
    } else {
        printf("[NET-FreeRTOS] Static IP mode - network ready\r\n");
        context->has_ip_address = true;
    }
    
    printf("[NET-FreeRTOS] Network started successfully\r\n");
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_freertos_tcp_stop_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_net_freertos_tcp_context_t *context = (drv_net_freertos_tcp_context_t *)hw_context;
    
    printf("[NET-FreeRTOS] Stopping network\r\n");
    
    // Note: FreeRTOS-Plus-TCP doesn't have a direct "stop" function
    // The network interface should be brought down at the BSP level
    
    context->network_up = false;
    context->link_up = false;
    context->has_ip_address = false;
    
    printf("[NET-FreeRTOS] Network stopped\r\n");
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_freertos_tcp_get_status_impl(const void *hw_context, drv_net_status_info_t *status)
{
    ASSERT(hw_context != NULL);
    ASSERT(status != NULL);
    
    drv_net_freertos_tcp_context_t *context = (drv_net_freertos_tcp_context_t *)hw_context;
    
    // Clear status structure
    memset(status, 0, sizeof(drv_net_status_info_t));
    
    status->is_initialized = true;
    status->is_started = context->network_up;
    status->link_up = FreeRTOS_IsNetworkUp();
    status->has_ip = status->link_up && (FreeRTOS_GetIPAddress() != 0);
    
    // Get network configuration
    if (status->has_ip) {
        uint32_t ip_addr, netmask, gateway, dns_server;
        
        // Use the newer API to get address configuration
        FreeRTOS_GetEndPointConfiguration(&ip_addr, &netmask, &gateway, &dns_server, NULL);
        
        inet_ntoa_r_custom(ip_addr, status->ip_addr, sizeof(status->ip_addr));
        inet_ntoa_r_custom(netmask, status->netmask, sizeof(status->netmask));
        inet_ntoa_r_custom(gateway, status->gateway, sizeof(status->gateway));
    } else {
        strcpy(status->ip_addr, "0.0.0.0");
        strcpy(status->netmask, "0.0.0.0");
        strcpy(status->gateway, "0.0.0.0");
    }
    
    // Format MAC address
    snprintf(status->mac_addr_str, sizeof(status->mac_addr_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             context->mac_address[0], context->mac_address[1], context->mac_address[2],
             context->mac_address[3], context->mac_address[4], context->mac_address[5]);
    
    // Copy statistics
    status->rx_packets = context->packets_received;
    status->tx_packets = context->packets_sent;
    status->rx_errors = context->recv_errors;
    status->tx_errors = context->send_errors;
    
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_freertos_tcp_wait_for_link_impl(const void *hw_context, uint32_t timeout_ms)
{
    ASSERT(hw_context != NULL);
    
    printf("[NET-FreeRTOS] Waiting for link up (timeout: %lu ms)\r\n", timeout_ms);
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        if (FreeRTOS_IsNetworkUp()) {
            printf("[NET-FreeRTOS] Link is up\r\n");
            return DRV_NET_STATUS_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
    }
    
    printf("[NET-FreeRTOS] Timeout waiting for link\r\n");
    return DRV_NET_STATUS_TIMEOUT;
}

drv_net_status_t drv_net_freertos_tcp_wait_for_ip_impl(const void *hw_context, uint32_t timeout_ms)
{
    ASSERT(hw_context != NULL);
    
    printf("[NET-FreeRTOS] Waiting for IP address (timeout: %lu ms)\r\n", timeout_ms);
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        if (FreeRTOS_IsNetworkUp() && FreeRTOS_GetIPAddress() != 0) {
            char ip_str[16];
            inet_ntoa_r_custom(FreeRTOS_GetIPAddress(), ip_str, sizeof(ip_str));
            printf("[NET-FreeRTOS] IP address acquired: %s\r\n", ip_str);
            return DRV_NET_STATUS_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
    }
    
    printf("[NET-FreeRTOS] Timeout waiting for IP address\r\n");
    return DRV_NET_STATUS_TIMEOUT;
}

drv_net_status_t drv_net_freertos_tcp_print_network_info_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_net_status_info_t status;
    drv_net_status_t result = drv_net_freertos_tcp_get_status_impl(hw_context, &status);
    
    if (result != DRV_NET_STATUS_OK) {
        printf("[NET-FreeRTOS] Failed to get network status\r\n");
        return result;
    }
    
    printf("[NET-FreeRTOS] Network Information:\r\n");
    printf("  Status: %s\r\n", status.is_started ? "Started" : "Stopped");
    printf("  Link: %s\r\n", status.link_up ? "Up" : "Down");
    printf("  IP Address: %s\r\n", status.ip_addr);
    printf("  Netmask: %s\r\n", status.netmask);
    printf("  Gateway: %s\r\n", status.gateway);
    printf("  MAC Address: %s\r\n", status.mac_addr_str);
    printf("  RX Packets: %lu\r\n", status.rx_packets);
    printf("  TX Packets: %lu\r\n", status.tx_packets);
    printf("  RX Errors: %lu\r\n", status.rx_errors);
    printf("  TX Errors: %lu\r\n", status.tx_errors);
    
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_freertos_tcp_ping_impl(const void *hw_context, const char *target_ip, uint32_t timeout_ms)
{
    ASSERT(hw_context != NULL);
    ASSERT(target_ip != NULL);
    
    printf("[NET-FreeRTOS] Ping functionality not implemented (target: %s)\r\n", target_ip);
    
    // Note: FreeRTOS-Plus-TCP doesn't have a built-in ping function
    // This would need to be implemented using raw ICMP if needed
    
    return DRV_NET_STATUS_ERROR;
}

drv_net_status_t drv_net_freertos_tcp_register_callback_impl(const void *hw_context, 
                                                            drv_net_cb_type_t type, 
                                                            drv_net_callback_t callback)
{
    ASSERT(hw_context != NULL);
    
    drv_net_freertos_tcp_context_t *context = (drv_net_freertos_tcp_context_t *)hw_context;
    
    printf("[NET-FreeRTOS] Registering callback for type %d\r\n", type);
    
    switch (type) {
        case DRV_NET_CB_LINK_UP:
            context->link_up_callback = callback;
            break;
        case DRV_NET_CB_LINK_DOWN:
            context->link_down_callback = callback;
            break;
        case DRV_NET_CB_IP_ACQUIRED:
            context->ip_acquired_callback = callback;
            break;
        case DRV_NET_CB_IP_LOST:
            context->ip_lost_callback = callback;
            break;
        case DRV_NET_CB_ERROR:
            context->error_callback = callback;
            break;
        default:
            printf("[NET-FreeRTOS] Unknown callback type: %d\r\n", type);
            return DRV_NET_STATUS_ERROR;
    }
    
    return DRV_NET_STATUS_OK;
}