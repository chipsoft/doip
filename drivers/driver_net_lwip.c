#include "driver_net_lwip.h"
#include "driver_ethernet.h"
#include "bsp_ethernet.h"
#include "eth_ipstack_main.h"
#include "network_events.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include "FreeRTOS.h"
#include "task.h"
#include "utils_assert.h"
#include "printf.h"
#include <string.h>
#include <stdio.h>


// Convert IP address to string
static void lwip_ip_addr_to_string(const ip_addr_t *addr, char *str, uint32_t len)
{
    if (addr == NULL || str == NULL) {
        return;
    }
    
    uint32_t ip = addr->addr;
    snprintf(str, len, "%d.%d.%d.%d",
             (int)(ip & 0xFF),
             (int)((ip >> 8) & 0xFF),
             (int)((ip >> 16) & 0xFF),
             (int)((ip >> 24) & 0xFF));
}

// Format MAC address to string
static void mac_addr_to_string(const uint8_t *mac, char *str, uint32_t len)
{
    if (mac == NULL || str == NULL) {
        return;
    }
    
    snprintf(str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

drv_net_status_t drv_net_lwip_init_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    printf("[NET_LWIP] Initializing LwIP network driver\r\n");
    
    // Initialize context
    memset(context, 0, sizeof(drv_net_lwip_hw_context_t));
    context->eth_driver = &eth_communication;
    context->link_up = false;
    context->has_ip = false;
    context->init_done = false;
    
    // Initialize Ethernet driver
    drv_eth_status_t eth_result = hw_eth_init((drv_eth_t*)context->eth_driver);
    if (eth_result != DRV_ETH_STATUS_OK) {
        printf("[NET_LWIP] Ethernet driver initialization failed: %d\r\n", eth_result);
        return DRV_NET_STATUS_ERROR;
    }
    
    printf("[NET_LWIP] LwIP network driver initialized successfully\r\n");
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_lwip_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    printf("[NET_LWIP] Deinitializing LwIP network driver\r\n");
    
    // Stop network if running
    if (context->init_done) {
        drv_net_lwip_stop_impl(hw_context);
    }
    
    // Deinitialize Ethernet driver
    if (context->eth_driver) {
        hw_eth_deinit((drv_eth_t*)context->eth_driver);
    }
    
    // Clean up context
    memset(context, 0, sizeof(drv_net_lwip_hw_context_t));
    
    printf("[NET_LWIP] LwIP network driver deinitialized\r\n");
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_lwip_start_impl(const void *hw_context, const drv_net_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    printf("[NET_LWIP] Starting LwIP network stack\r\n");
    
    if (context->init_done) {
        printf("[NET_LWIP] Network already started\r\n");
        return DRV_NET_STATUS_OK;
    }
    
    // Store configuration
    memcpy(&context->current_config, config, sizeof(drv_net_config_t));
    memcpy(context->hwaddr, config->mac_addr, 6);
    
    // Create initialization semaphore
    err_t sem_err = sys_sem_new(&context->init_sem, 0);
    if (sem_err != ERR_OK) {
        printf("[NET_LWIP] Failed to create initialization semaphore\r\n");
        return DRV_NET_STATUS_ERROR;
    }
    
    // Initialize TCP/IP stack
    printf("[NET_LWIP] Initializing TCP/IP stack...\r\n");
    tcpip_init(hw_eth_get_tcpip_init_done_fn((drv_eth_t*)context->eth_driver), &context->init_sem);
    
    // Wait for TCP/IP stack initialization
    sys_sem_wait(&context->init_sem);
    sys_sem_free(&context->init_sem);
    
    context->init_done = true;
    printf("[NET_LWIP] LwIP network stack started successfully\r\n");
    
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_lwip_stop_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    printf("[NET_LWIP] Stopping LwIP network stack\r\n");
    
    if (!context->init_done) {
        printf("[NET_LWIP] Network already stopped\r\n");
        return DRV_NET_STATUS_OK;
    }
    
    // Stop DHCP if enabled
    if (context->current_config.use_dhcp) {
        dhcp_stop(&TCPIP_STACK_INTERFACE_0_desc);
        printf("[NET_LWIP] DHCP stopped\r\n");
    }
    
    // Bring network interface down
    netif_set_down(&TCPIP_STACK_INTERFACE_0_desc);
    printf("[NET_LWIP] Network interface brought down\r\n");
    
    context->init_done = false;
    context->link_up = false;
    context->has_ip = false;
    
    printf("[NET_LWIP] LwIP network stack stopped\r\n");
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_lwip_get_status_impl(const void *hw_context, drv_net_status_info_t *status)
{
    ASSERT(hw_context != NULL);
    ASSERT(status != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    // Clear status structure
    memset(status, 0, sizeof(drv_net_status_info_t));
    
    // Basic status
    status->is_initialized = (context->eth_driver != NULL);
    status->is_started = context->init_done;
    
    if (!context->init_done) {
        mac_addr_to_string(context->hwaddr, status->mac_addr_str, sizeof(status->mac_addr_str));
        return DRV_NET_STATUS_OK;
    }
    
    // Get link status from Ethernet driver
    bool link_up;
    drv_eth_status_t eth_result = hw_eth_get_link_status((drv_eth_t*)context->eth_driver, &link_up);
    if (eth_result == DRV_ETH_STATUS_OK) {
        status->link_up = link_up;
        context->link_up = link_up;
    } else {
        status->link_up = false;
        context->link_up = false;
    }
    
    // Get network interface information
    if (netif_is_up(&TCPIP_STACK_INTERFACE_0_desc)) {
        // IP address information
        lwip_ip_addr_to_string(&TCPIP_STACK_INTERFACE_0_desc.ip_addr, status->ip_addr, sizeof(status->ip_addr));
        lwip_ip_addr_to_string(&TCPIP_STACK_INTERFACE_0_desc.netmask, status->netmask, sizeof(status->netmask));
        lwip_ip_addr_to_string(&TCPIP_STACK_INTERFACE_0_desc.gw, status->gateway, sizeof(status->gateway));
        
        // Check if we have a valid IP address
        status->has_ip = (TCPIP_STACK_INTERFACE_0_desc.ip_addr.addr != 0);
        context->has_ip = status->has_ip;
        
        // Network statistics (basic implementation)
        status->rx_packets = 0; // LwIP doesn't provide easy access to these
        status->tx_packets = 0;
        status->rx_errors = 0;
        status->tx_errors = 0;
    }
    
    // MAC address
    mac_addr_to_string(TCPIP_STACK_INTERFACE_0_desc.hwaddr, status->mac_addr_str, sizeof(status->mac_addr_str));
    
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_lwip_wait_for_link_impl(const void *hw_context, uint32_t timeout_ms)
{
    ASSERT(hw_context != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    if (!context->init_done) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    printf("[NET_LWIP] Waiting for link (timeout: %lu ms)\r\n", timeout_ms);
    
    uint32_t elapsed = 0;
    const uint32_t check_interval = 100;
    
    while (elapsed < timeout_ms) {
        bool link_up;
        drv_eth_status_t result = hw_eth_get_link_status((drv_eth_t*)context->eth_driver, &link_up);
        
        if (result == DRV_ETH_STATUS_OK && link_up) {
            context->link_up = true;
            printf("[NET_LWIP] Link established after %lu ms\r\n", elapsed);
            return DRV_NET_STATUS_OK;
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        elapsed += check_interval;
    }
    
    printf("[NET_LWIP] Link wait timeout after %lu ms\r\n", timeout_ms);
    return DRV_NET_STATUS_TIMEOUT;
}

drv_net_status_t drv_net_lwip_wait_for_ip_impl(const void *hw_context, uint32_t timeout_ms)
{
    ASSERT(hw_context != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    if (!context->init_done) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    printf("[NET_LWIP] Waiting for IP address (timeout: %lu ms)\r\n", timeout_ms);
    
    uint32_t elapsed = 0;
    const uint32_t check_interval = 250;
    
    while (elapsed < timeout_ms) {
        if (netif_is_up(&TCPIP_STACK_INTERFACE_0_desc) && 
            TCPIP_STACK_INTERFACE_0_desc.ip_addr.addr != 0) {
            context->has_ip = true;
            printf("[NET_LWIP] IP address acquired after %lu ms\r\n", elapsed);
            return DRV_NET_STATUS_OK;
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        elapsed += check_interval;
    }
    
    printf("[NET_LWIP] IP address wait timeout after %lu ms\r\n", timeout_ms);
    return DRV_NET_STATUS_TIMEOUT;
}

drv_net_status_t drv_net_lwip_print_network_info_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    printf("\r\n=== Network Information ===\r\n");
    
    if (!context->init_done) {
        printf("Network Status: NOT INITIALIZED\r\n");
        printf("===========================\r\n\r\n");
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    // Get current status
    drv_net_status_info_t status;
    drv_net_status_t result = drv_net_lwip_get_status_impl(hw_context, &status);
    if (result != DRV_NET_STATUS_OK) {
        printf("Failed to get network status\r\n");
        printf("===========================\r\n\r\n");
        return result;
    }
    
    // Print status information
    printf("Network Status: %s\r\n", status.is_started ? "STARTED" : "STOPPED");
    printf("Link Status   : %s\r\n", status.link_up ? "UP" : "DOWN");
    printf("IP Status     : %s\r\n", status.has_ip ? "CONFIGURED" : "NOT CONFIGURED");
    printf("MAC Address   : %s\r\n", status.mac_addr_str);
    
    if (status.has_ip) {
        printf("IP Address    : %s\r\n", status.ip_addr);
        printf("Netmask       : %s\r\n", status.netmask);
        printf("Gateway       : %s\r\n", status.gateway);
    }
    
    printf("DHCP Mode     : %s\r\n", context->current_config.use_dhcp ? "ENABLED" : "DISABLED");
    
    if (context->current_config.hostname) {
        printf("Hostname      : %s\r\n", context->current_config.hostname);
    }
    
    printf("===========================\r\n\r\n");
    
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_lwip_ping_impl(const void *hw_context, const char *target_ip, uint32_t timeout_ms)
{
    ASSERT(hw_context != NULL);
    ASSERT(target_ip != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    if (!context->init_done) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    if (!context->has_ip) {
        printf("[NET_LWIP] Cannot ping: No IP address configured\r\n");
        return DRV_NET_STATUS_NO_IP;
    }
    
    printf("[NET_LWIP] Ping functionality not implemented in basic LwIP driver\r\n");
    printf("[NET_LWIP] Target: %s, Timeout: %lu ms\r\n", target_ip, timeout_ms);
    
    // Basic ping implementation would require ICMP support
    // For now, return success as a placeholder
    return DRV_NET_STATUS_OK;
}

drv_net_status_t drv_net_lwip_register_callback_impl(const void *hw_context, 
                                                    drv_net_cb_type_t type, 
                                                    drv_net_callback_t callback)
{
    ASSERT(hw_context != NULL);
    drv_net_lwip_hw_context_t *context = (drv_net_lwip_hw_context_t*)hw_context;
    
    printf("[NET_LWIP] Registering callback for type: %d\r\n", (int)type);
    
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
            printf("[NET_LWIP] Unknown callback type: %d\r\n", (int)type);
            return DRV_NET_STATUS_ERROR;
    }
    
    return DRV_NET_STATUS_OK;
}