/**
 * @file driver_netif_doip.c
 * @brief Universal Network Interface Driver Implementation for DoIP Communications
 * 
 * @details This file provides the implementation of the hardware-agnostic network
 * interface driver for DoIP communications. It handles parameter validation,
 * error handling, and routing calls to appropriate BSP implementations.
 * 
 * @author  SAME54 DoIP Project
 * @date    2024
 * @version 1.0
 * 
 * @copyright MIT License - Free to use, modify, and distribute
 */

#include "driver_netif_doip.h"
#include "utils_assert.h"
#include "printf.h"
#include <string.h>

/**
 * @brief Validate network interface driver handle
 * @param handle Pointer to network interface driver instance
 * @return true if handle is valid, false otherwise
 */
static bool validate_handle(const drv_netif_doip_t *handle)
{
    return (handle != NULL && 
            handle->hw_context != NULL && 
            handle->init != NULL);
}

/**
 * @brief Validate network interface configuration
 * @param config Pointer to configuration structure
 * @return true if configuration is valid, false otherwise
 */
static bool validate_config(const drv_netif_doip_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    
    // Validate interface type
    if (config->interface_type == DRV_NETIF_DOIP_TYPE_UNKNOWN) {
        printf("NETIF DoIP: Invalid interface type UNKNOWN\r\n");
        return false;
    }
    
    // Validate link speed
    if (config->link_speed != 10 && config->link_speed != 100 && config->link_speed != 1000) {
        printf("NETIF DoIP: Invalid link speed %d (must be 10, 100, or 1000)\r\n", config->link_speed);
        return false;
    }
    
    // Validate timeouts
    if (config->init_timeout_ms == 0 || config->init_timeout_ms > 60000) {
        printf("NETIF DoIP: Invalid init timeout %lu ms (valid range: 1-60000)\r\n", config->init_timeout_ms);
        return false;
    }
    
    if (config->link_timeout_ms == 0 || config->link_timeout_ms > 60000) {
        printf("NETIF DoIP: Invalid link timeout %lu ms (valid range: 1-60000)\r\n", config->link_timeout_ms);
        return false;
    }
    
    // Validate DHCP timeout if DHCP is enabled
    if (config->use_dhcp && (config->dhcp_timeout_ms == 0 || config->dhcp_timeout_ms > 300000)) {
        printf("NETIF DoIP: Invalid DHCP timeout %lu ms (valid range: 1-300000)\r\n", config->dhcp_timeout_ms);
        return false;
    }
    
    // Validate retry count
    if (config->max_retries > 10) {
        printf("NETIF DoIP: Invalid max retries %d (valid range: 0-10)\r\n", config->max_retries);
        return false;
    }
    
    return true;
}

/**
 * @brief Validate MAC address
 * @param mac_addr MAC address buffer (6 bytes)
 * @return true if MAC address is valid, false otherwise
 */
static bool validate_mac_address(const uint8_t mac_addr[NETIF_DOIP_MAC_ADDR_LEN])
{
    if (mac_addr == NULL) {
        return false;
    }
    
    // Check for all-zero MAC (invalid)
    bool all_zero = true;
    for (int i = 0; i < NETIF_DOIP_MAC_ADDR_LEN; i++) {
        if (mac_addr[i] != 0) {
            all_zero = false;
            break;
        }
    }
    
    if (all_zero) {
        printf("NETIF DoIP: MAC address cannot be all zeros\r\n");
        return false;
    }
    
    // Check for broadcast MAC (invalid)
    bool all_ff = true;
    for (int i = 0; i < NETIF_DOIP_MAC_ADDR_LEN; i++) {
        if (mac_addr[i] != 0xFF) {
            all_ff = false;
            break;
        }
    }
    
    if (all_ff) {
        printf("NETIF DoIP: MAC address cannot be broadcast address\r\n");
        return false;
    }
    
    // Check for multicast MAC (bit 0 of first byte set)
    if (mac_addr[0] & 0x01) {
        printf("NETIF DoIP: MAC address cannot be multicast\r\n");
        return false;
    }
    
    return true;
}

drv_netif_doip_status_t hw_netif_doip_init(drv_netif_doip_t *handle, const drv_netif_doip_config_t *config)
{
    ASSERT(handle != NULL);
    ASSERT(config != NULL);
    
    printf("NETIF DoIP: Initializing network interface driver\r\n");
    
    // Validate handle structure
    if (!validate_handle(handle)) {
        printf("NETIF DoIP: Invalid driver handle\r\n");
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    // Validate configuration
    if (!validate_config(config)) {
        printf("NETIF DoIP: Invalid configuration\r\n");
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    // Check if already initialized
    if (handle->is_init) {
        printf("NETIF DoIP: Driver already initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_OK;
    }
    
    // Call hardware-specific initialization
    drv_netif_doip_status_t status = handle->init(handle->hw_context, config);
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        handle->is_init = true;
        handle->current_state = DRV_NETIF_DOIP_STATE_INITIALIZED;
        printf("NETIF DoIP: Driver initialized successfully (type=%d)\r\n", config->interface_type);
    } else {
        printf("NETIF DoIP: Hardware initialization failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_deinit(drv_netif_doip_t *handle)
{
    ASSERT(handle != NULL);
    
    printf("NETIF DoIP: Deinitializing network interface driver\r\n");
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Call hardware-specific deinitialization
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->deinit != NULL) {
        status = handle->deinit(handle->hw_context);
    }
    
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        handle->is_init = false;
        handle->current_state = DRV_NETIF_DOIP_STATE_UNINITIALIZED;
        printf("NETIF DoIP: Driver deinitialized successfully\r\n");
    } else {
        printf("NETIF DoIP: Hardware deinitialization failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_enable(drv_netif_doip_t *handle)
{
    ASSERT(handle != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Call hardware-specific enable
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->enable != NULL) {
        status = handle->enable(handle->hw_context);
    }
    
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NETIF DoIP: Interface enabled successfully\r\n");
    } else {
        printf("NETIF DoIP: Interface enable failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_disable(drv_netif_doip_t *handle)
{
    ASSERT(handle != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Call hardware-specific disable
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->disable != NULL) {
        status = handle->disable(handle->hw_context);
    }
    
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NETIF DoIP: Interface disabled successfully\r\n");
    } else {
        printf("NETIF DoIP: Interface disable failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_configure_ip(drv_netif_doip_t *handle, uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    ASSERT(handle != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Basic IP address validation (not all zeros, not broadcast)
    if (ip == 0 || ip == 0xFFFFFFFF) {
        printf("NETIF DoIP: Invalid IP address\r\n");
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (netmask == 0) {
        printf("NETIF DoIP: Invalid netmask\r\n");
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    // Call hardware-specific IP configuration
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->configure_ip != NULL) {
        status = handle->configure_ip(handle->hw_context, ip, netmask, gateway);
    }
    
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NETIF DoIP: IP configured - IP=%08lX, Mask=%08lX, GW=%08lX\r\n", 
               ip, netmask, gateway);
    } else {
        printf("NETIF DoIP: IP configuration failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_enable_dhcp(drv_netif_doip_t *handle)
{
    ASSERT(handle != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Call hardware-specific DHCP enable
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->enable_dhcp != NULL) {
        status = handle->enable_dhcp(handle->hw_context);
    }
    
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NETIF DoIP: DHCP enabled\r\n");
    } else {
        printf("NETIF DoIP: DHCP enable failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_disable_dhcp(drv_netif_doip_t *handle)
{
    ASSERT(handle != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Call hardware-specific DHCP disable
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->disable_dhcp != NULL) {
        status = handle->disable_dhcp(handle->hw_context);
    }
    
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NETIF DoIP: DHCP disabled\r\n");
    } else {
        printf("NETIF DoIP: DHCP disable failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_set_mac_address(drv_netif_doip_t *handle, const uint8_t mac_addr[NETIF_DOIP_MAC_ADDR_LEN])
{
    ASSERT(handle != NULL);
    ASSERT(mac_addr != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    if (!validate_mac_address(mac_addr)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    // Call hardware-specific MAC address setting
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->set_mac_address != NULL) {
        status = handle->set_mac_address(handle->hw_context, mac_addr);
    }
    
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NETIF DoIP: MAC address set to %02X:%02X:%02X:%02X:%02X:%02X\r\n",
               mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    } else {
        printf("NETIF DoIP: MAC address setting failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_get_status(drv_netif_doip_t *handle, drv_netif_doip_status_info_t *status)
{
    ASSERT(handle != NULL);
    ASSERT(status != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Call hardware-specific status get
    drv_netif_doip_status_t result = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->get_status != NULL) {
        result = handle->get_status(handle->hw_context, status);
    } else {
        // Provide basic status if no hardware implementation
        memset(status, 0, sizeof(drv_netif_doip_status_t));
        status->state = handle->current_state;
        status->is_initialized = handle->is_init;
    }
    
    return result;
}

drv_netif_doip_status_t hw_netif_doip_check_link(drv_netif_doip_t *handle, bool *link_up)
{
    ASSERT(handle != NULL);
    ASSERT(link_up != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Call hardware-specific link check
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->check_link != NULL) {
        status = handle->check_link(handle->hw_context, link_up);
    } else {
        // Default to link down if no implementation
        *link_up = false;
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_register_callback(drv_netif_doip_t *handle, 
                                                       drv_netif_doip_cb_type_t type, 
                                                       drv_netif_doip_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(callback != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Validate callback type
    if (type > DRV_NETIF_DOIP_CB_ERROR) {
        printf("NETIF DoIP: Invalid callback type: %d\r\n", type);
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    // Call hardware-specific callback registration
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->register_callback != NULL) {
        status = handle->register_callback(handle->hw_context, type, callback);
    }
    
    if (status == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NETIF DoIP: Callback registered for type %d\r\n", type);
    } else {
        printf("NETIF DoIP: Callback registration failed: %d\r\n", status);
    }
    
    return status;
}

drv_netif_doip_status_t hw_netif_doip_get_lwip_netif(drv_netif_doip_t *handle, void **netif_ptr)
{
    ASSERT(handle != NULL);
    ASSERT(netif_ptr != NULL);
    
    if (!validate_handle(handle)) {
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    if (!handle->is_init) {
        printf("NETIF DoIP: Driver not initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Call hardware-specific lwIP netif get
    drv_netif_doip_status_t status = DRV_NETIF_DOIP_STATUS_OK;
    if (handle->get_lwip_netif != NULL) {
        status = handle->get_lwip_netif(handle->hw_context, netif_ptr);
    } else {
        *netif_ptr = NULL;
        status = DRV_NETIF_DOIP_STATUS_ERROR;
    }
    
    return status;
}

void hw_netif_doip_create_default_config(drv_netif_doip_config_t *config, drv_netif_doip_type_t interface_type)
{
    ASSERT(config != NULL);
    
    printf("NETIF DoIP: Creating default configuration for interface type %d\r\n", interface_type);
    
    // Clear configuration structure
    memset(config, 0, sizeof(drv_netif_doip_config_t));
    
    // Set interface type
    config->interface_type = interface_type;
    
    // Hardware configuration defaults
    memset(config->mac_address, 0, NETIF_DOIP_MAC_ADDR_LEN);  // Auto MAC
    config->auto_negotiation = true;
    config->link_speed = 100;  // 100 Mbps default
    config->full_duplex = true;
    
    // Network configuration defaults - use static IP for DoIP
    config->use_dhcp = false;  // Use static IP for reliable DoIP communications
    // Set static IP to match main.c configuration: 192.168.100.2
    config->static_ip = (192 << 0) | (168 << 8) | (100 << 16) | (2 << 24);
    config->static_netmask = (255 << 0) | (255 << 8) | (255 << 16) | (0 << 24);
    config->static_gateway = (192 << 0) | (168 << 8) | (100 << 16) | (1 << 24);
    config->static_dns = (8 << 0) | (8 << 8) | (8 << 16) | (8 << 24);
    strcpy(config->hostname, "doip-device");
    
    // Timing configuration defaults
    config->init_timeout_ms = 10000;   // 10 seconds
    config->link_timeout_ms = 15000;   // 15 seconds
    config->dhcp_timeout_ms = 30000;   // 30 seconds
    config->max_retries = 3;
    
    // Interface-specific adjustments
    switch (interface_type) {
        case DRV_NETIF_DOIP_TYPE_KSZ8851SNL:
            // KSZ8851SNL specific defaults
            config->link_speed = 100;  // KSZ8851SNL supports 10/100 Mbps
            strcpy(config->hostname, "doip-ksz8851snl");
            break;
            
        case DRV_NETIF_DOIP_TYPE_GMAC:
            // GMAC specific defaults
            config->link_speed = 100;  // Conservative default
            strcpy(config->hostname, "doip-gmac");
            break;
            
        default:
            // Generic defaults already set
            break;
    }
    
    printf("NETIF DoIP: Default configuration created successfully\r\n");
}