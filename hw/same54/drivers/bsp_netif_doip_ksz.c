/**
 * @file bsp_netif_doip_ksz.c
 * @brief KSZ8851SNL Network Interface BSP Implementation for DoIP Communications
 * 
 * @details This file provides the KSZ8851SNL-specific implementation of the
 * network interface driver for DoIP communications. It integrates the KSZ8851SNL
 * Ethernet controller with lwIP and provides DoIP-optimized network operations.
 * 
 * @author  SAME54 DoIP Project
 * @date    2024
 * @version 1.0
 * 
 * @copyright MIT License - Free to use, modify, and distribute
 */

// lwIP includes first to avoid ERR_TIMEOUT conflicts
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/ethip6.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/err.h"

#include "bsp_netif_doip_ksz.h"
#include "bsp_ksz8851snl.h"
#include "utils_assert.h"
#include "printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "netif/etharp.h"
#include "netif/ethernet.h"

#include <string.h>

/**
 * @brief KSZ8851SNL network interface hardware context
 * @details Contains all state information for KSZ8851SNL network interface
 */
typedef struct {
    /** @name Driver State
     * Basic driver state information
     * @{
     */
    bool is_initialized;                   /**< Initialization status */
    bool is_enabled;                       /**< Enable status */
    drv_netif_doip_state_t current_state;  /**< Current driver state */
    drv_netif_doip_type_t interface_type;  /**< Interface type */
    /** @} */
    
    /** @name Configuration
     * Current configuration parameters
     * @{
     */
    drv_netif_doip_config_t config;        /**< Base configuration */
    ksz_netif_config_extension_t extended_config; /**< Extended KSZ configuration */
    /** @} */
    
    /** @name lwIP Integration
     * lwIP network interface integration
     * @{
     */
    struct netif lwip_netif;               /**< lwIP network interface structure */
    bool lwip_netif_added;                 /**< lwIP netif added to stack */
    SemaphoreHandle_t netif_mutex;         /**< Mutex for thread-safe access */
    /** @} */
    
    /** @name Status Information
     * Current status and statistics
     * @{
     */
    drv_netif_doip_status_info_t status;   /**< Current interface status */
    ksz_netif_status_extension_t extended_status; /**< Extended KSZ status */
    uint32_t last_link_check_time;         /**< Last link status check time */
    /** @} */
    
    /** @name Callbacks
     * Registered callback functions
     * @{
     */
    drv_netif_doip_callback_t link_up_callback;    /**< Link up callback */
    drv_netif_doip_callback_t link_down_callback;  /**< Link down callback */
    drv_netif_doip_callback_t ip_config_callback;  /**< IP config callback */
    drv_netif_doip_callback_t error_callback;      /**< Error callback */
    drv_ksz8851snl_callback_t hw_callback;         /**< Hardware callback */
    /** @} */
} ksz_netif_hw_context_t;

// Static hardware context
static ksz_netif_hw_context_t ksz_netif_hw_context_0 = {
    .is_initialized = false,
    .is_enabled = false,
    .current_state = DRV_NETIF_DOIP_STATE_UNINITIALIZED,
    .interface_type = DRV_NETIF_DOIP_TYPE_KSZ8851SNL,
    .lwip_netif_added = false,
    .netif_mutex = NULL,
    .last_link_check_time = 0,
    .link_up_callback = NULL,
    .link_down_callback = NULL,
    .ip_config_callback = NULL,
    .error_callback = NULL,
    .hw_callback = NULL,
};

// Forward declarations
static drv_netif_doip_status_t ksz_netif_init_impl(const void *hw_context, const drv_netif_doip_config_t *config);
static drv_netif_doip_status_t ksz_netif_deinit_impl(const void *hw_context);
static drv_netif_doip_status_t ksz_netif_enable_impl(const void *hw_context);
static drv_netif_doip_status_t ksz_netif_disable_impl(const void *hw_context);
static drv_netif_doip_status_t ksz_netif_configure_ip_impl(const void *hw_context, uint32_t ip, uint32_t netmask, uint32_t gateway);
static drv_netif_doip_status_t ksz_netif_enable_dhcp_impl(const void *hw_context);
static drv_netif_doip_status_t ksz_netif_disable_dhcp_impl(const void *hw_context);
static drv_netif_doip_status_t ksz_netif_set_mac_address_impl(const void *hw_context, const uint8_t mac_addr[NETIF_DOIP_MAC_ADDR_LEN]);
static drv_netif_doip_status_t ksz_netif_get_status_impl(const void *hw_context, drv_netif_doip_status_info_t *status);
static drv_netif_doip_status_t ksz_netif_check_link_impl(const void *hw_context, bool *link_up);
static drv_netif_doip_status_t ksz_netif_register_callback_impl(const void *hw_context, drv_netif_doip_cb_type_t type, drv_netif_doip_callback_t callback);
static drv_netif_doip_status_t ksz_netif_get_lwip_netif_impl(const void *hw_context, void **netif_ptr);
static drv_netif_doip_status_t ksz_netif_force_link_check_impl(const void *hw_context);

// lwIP network interface functions
static err_t ksz_netif_lwip_init(struct netif *netif);
static err_t ksz_netif_lwip_output(struct netif *netif, struct pbuf *p);
static void ksz_netif_lwip_input_task(void *pvParameters);

// Helper functions
static void ksz_netif_update_status(ksz_netif_hw_context_t *context);
static void ksz_netif_link_callback(struct netif *netif);
static void ksz_netif_status_callback(struct netif *netif);

// Global driver instance
drv_netif_doip_t netif_doip_ksz_0 = {
    .is_init = false,
    .current_state = DRV_NETIF_DOIP_STATE_UNINITIALIZED,
    .hw_context = &ksz_netif_hw_context_0,
    .init = ksz_netif_init_impl,
    .deinit = ksz_netif_deinit_impl,
    .enable = ksz_netif_enable_impl,
    .disable = ksz_netif_disable_impl,
    .configure_ip = ksz_netif_configure_ip_impl,
    .enable_dhcp = ksz_netif_enable_dhcp_impl,
    .disable_dhcp = ksz_netif_disable_dhcp_impl,
    .set_mac_address = ksz_netif_set_mac_address_impl,
    .get_status = ksz_netif_get_status_impl,
    .check_link = ksz_netif_check_link_impl,
    .register_callback = ksz_netif_register_callback_impl,
    .get_lwip_netif = ksz_netif_get_lwip_netif_impl,
    .force_link_check = ksz_netif_force_link_check_impl,
};

//-----------------------------------------------------------------------------
// Driver Implementation Functions
//-----------------------------------------------------------------------------

static drv_netif_doip_status_t ksz_netif_init_impl(const void *hw_context, const drv_netif_doip_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    printf("KSZ NetIF DoIP: Initializing KSZ8851SNL network interface\r\n");
    
    if (context->is_initialized) {
        printf("KSZ NetIF DoIP: Already initialized\r\n");
        return DRV_NETIF_DOIP_STATUS_OK;
    }
    
    // Store configuration
    memcpy(&context->config, config, sizeof(drv_netif_doip_config_t));
    
    // Create mutex for thread-safe access
    if (context->netif_mutex == NULL) {
        context->netif_mutex = xSemaphoreCreateMutex();
        if (context->netif_mutex == NULL) {
            printf("KSZ NetIF DoIP: Failed to create mutex\r\n");
            return DRV_NETIF_DOIP_STATUS_ERROR;
        }
    }
    
    // Initialize extended configuration with defaults if not set
    bsp_netif_doip_ksz_create_default_extended_config(&context->extended_config);
    
    // Initialize KSZ8851SNL driver configuration
    drv_ksz8851snl_config_t ksz_config = {
        .auto_negotiation = config->auto_negotiation,
        .link_speed = config->link_speed,
        .full_duplex = config->full_duplex
    };
    
    // Set MAC address
    if (config->mac_address[0] == 0 && config->mac_address[1] == 0 && 
        config->mac_address[2] == 0 && config->mac_address[3] == 0 && 
        config->mac_address[4] == 0 && config->mac_address[5] == 0) {
        // Use default MAC address
        uint8_t default_mac[] = KSZ_NETIF_DEFAULT_MAC_ADDR;
        memcpy(ksz_config.mac_addr, default_mac, NETIF_DOIP_MAC_ADDR_LEN);
    } else {
        memcpy(ksz_config.mac_addr, config->mac_address, NETIF_DOIP_MAC_ADDR_LEN);
    }
    
    // Initialize KSZ8851SNL universal driver
    printf("KSZ NetIF DoIP: Initializing KSZ8851SNL universal driver\r\n");
    drv_ksz8851snl_status_t ksz_status = hw_ksz8851snl_init(&ksz8851snl_0, &ksz_config);
    if (ksz_status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("KSZ NetIF DoIP: KSZ8851SNL driver init failed: %d\r\n", ksz_status);
        return DRV_NETIF_DOIP_STATUS_INTERFACE_ERROR;
    }
    
    // Enable KSZ8851SNL driver
    ksz_status = hw_ksz8851snl_enable(&ksz8851snl_0);
    if (ksz_status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("KSZ NetIF DoIP: KSZ8851SNL driver enable failed: %d\r\n", ksz_status);
        return DRV_NETIF_DOIP_STATUS_INTERFACE_ERROR;
    }
    
    // Initialize lwIP network interface
    printf("KSZ NetIF DoIP: Initializing lwIP network interface\r\n");
    
    // Set IP addresses
    ip4_addr_t ip, netmask, gateway;
    if (config->use_dhcp) {
        ip4_addr_set_zero(&ip);
        ip4_addr_set_zero(&netmask);
        ip4_addr_set_zero(&gateway);
    } else {
        ip.addr = config->static_ip;
        netmask.addr = config->static_netmask;
        gateway.addr = config->static_gateway;
    }
    
    // Add network interface to lwIP stack
    struct netif *netif_result = netif_add(&context->lwip_netif, &ip, &netmask, &gateway,
                                          context, ksz_netif_lwip_init, ethernet_input);
    
    if (netif_result == NULL) {
        printf("KSZ NetIF DoIP: Failed to add lwIP network interface\r\n");
        hw_ksz8851snl_deinit(&ksz8851snl_0);
        return DRV_NETIF_DOIP_STATUS_ERROR;
    }
    
    context->lwip_netif_added = true;
    
    // Set as default interface
    netif_set_default(&context->lwip_netif);
    
    // Set link and status callbacks
    netif_set_link_callback(&context->lwip_netif, ksz_netif_link_callback);
    netif_set_status_callback(&context->lwip_netif, ksz_netif_status_callback);
    
    // Set hostname if provided and hostname support is enabled
#if LWIP_NETIF_HOSTNAME
    if (strlen(config->hostname) > 0) {
        netif_set_hostname(&context->lwip_netif, config->hostname);
    }
#endif
    
    // Bring interface up
    netif_set_up(&context->lwip_netif);
    
    // Enable DHCP if requested
    if (config->use_dhcp) {
        printf("KSZ NetIF DoIP: Starting DHCP client\r\n");
        dhcp_start(&context->lwip_netif);
    }
    
    // Update status
    context->is_initialized = true;
    context->current_state = DRV_NETIF_DOIP_STATE_INITIALIZED;
    ksz_netif_update_status(context);
    
    printf("KSZ NetIF DoIP: Initialization completed successfully\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    printf("KSZ NetIF DoIP: Deinitializing KSZ8851SNL network interface\r\n");
    
    if (!context->is_initialized) {
        return DRV_NETIF_DOIP_STATUS_OK;
    }
    
    // Take mutex
    if (context->netif_mutex != NULL) {
        xSemaphoreTake(context->netif_mutex, portMAX_DELAY);
    }
    
    // Stop DHCP if running
    if (dhcp_supplied_address(&context->lwip_netif)) {
        dhcp_stop(&context->lwip_netif);
    }
    
    // Remove network interface from lwIP stack
    if (context->lwip_netif_added) {
        netif_remove(&context->lwip_netif);
        context->lwip_netif_added = false;
    }
    
    // Deinitialize KSZ8851SNL driver
    hw_ksz8851snl_deinit(&ksz8851snl_0);
    
    // Update state
    context->is_initialized = false;
    context->is_enabled = false;
    context->current_state = DRV_NETIF_DOIP_STATE_UNINITIALIZED;
    
    // Release mutex
    if (context->netif_mutex != NULL) {
        xSemaphoreGive(context->netif_mutex);
        vSemaphoreDelete(context->netif_mutex);
        context->netif_mutex = NULL;
    }
    
    printf("KSZ NetIF DoIP: Deinitialization completed\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_enable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    if (context->is_enabled) {
        return DRV_NETIF_DOIP_STATUS_OK;
    }
    
    printf("KSZ NetIF DoIP: Enabling network interface\r\n");
    
    // Enable lwIP interface
    netif_set_link_up(&context->lwip_netif);
    
    context->is_enabled = true;
    ksz_netif_update_status(context);
    
    printf("KSZ NetIF DoIP: Interface enabled\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_disable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    printf("KSZ NetIF DoIP: Disabling network interface\r\n");
    
    // Disable lwIP interface
    netif_set_link_down(&context->lwip_netif);
    
    context->is_enabled = false;
    context->current_state = DRV_NETIF_DOIP_STATE_INITIALIZED;
    ksz_netif_update_status(context);
    
    printf("KSZ NetIF DoIP: Interface disabled\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_configure_ip_impl(const void *hw_context, uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    ASSERT(hw_context != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    printf("KSZ NetIF DoIP: Configuring static IP address\r\n");
    
    // Stop DHCP if running
    if (dhcp_supplied_address(&context->lwip_netif)) {
        dhcp_stop(&context->lwip_netif);
    }
    
    // Set static IP configuration
    ip4_addr_t lwip_ip, lwip_netmask, lwip_gateway;
    lwip_ip.addr = ip;
    lwip_netmask.addr = netmask;
    lwip_gateway.addr = gateway;
    
    netif_set_addr(&context->lwip_netif, &lwip_ip, &lwip_netmask, &lwip_gateway);
    
    // Update configuration
    context->config.use_dhcp = false;
    context->config.static_ip = ip;
    context->config.static_netmask = netmask;
    context->config.static_gateway = gateway;
    
    ksz_netif_update_status(context);
    
    if (context->ip_config_callback) {
        context->ip_config_callback(DRV_NETIF_DOIP_CB_IP_CONFIGURED, &lwip_ip.addr, sizeof(uint32_t));
    }
    
    printf("KSZ NetIF DoIP: Static IP configured successfully\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_enable_dhcp_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    printf("KSZ NetIF DoIP: Enabling DHCP\r\n");
    
    // Start DHCP client
    err_t err = dhcp_start(&context->lwip_netif);
    if (err != ERR_OK) {
        printf("KSZ NetIF DoIP: DHCP start failed: %d\r\n", err);
        return DRV_NETIF_DOIP_STATUS_ERROR;
    }
    
    context->config.use_dhcp = true;
    ksz_netif_update_status(context);
    
    printf("KSZ NetIF DoIP: DHCP enabled\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_disable_dhcp_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    printf("KSZ NetIF DoIP: Disabling DHCP\r\n");
    
    // Stop DHCP client
    dhcp_stop(&context->lwip_netif);
    
    context->config.use_dhcp = false;
    ksz_netif_update_status(context);
    
    printf("KSZ NetIF DoIP: DHCP disabled\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_set_mac_address_impl(const void *hw_context, const uint8_t mac_addr[NETIF_DOIP_MAC_ADDR_LEN])
{
    ASSERT(hw_context != NULL);
    ASSERT(mac_addr != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    printf("KSZ NetIF DoIP: Setting MAC address\r\n");
    
    // Update lwIP interface MAC address
    memcpy(context->lwip_netif.hwaddr, mac_addr, NETIF_DOIP_MAC_ADDR_LEN);
    memcpy(context->config.mac_address, mac_addr, NETIF_DOIP_MAC_ADDR_LEN);
    
    ksz_netif_update_status(context);
    
    printf("KSZ NetIF DoIP: MAC address updated\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_get_status_impl(const void *hw_context, drv_netif_doip_status_info_t *status)
{
    ASSERT(hw_context != NULL);
    ASSERT(status != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    // Update current status
    ksz_netif_update_status(context);
    
    // Copy status information
    memcpy(status, &context->status, sizeof(drv_netif_doip_status_t));
    
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_check_link_impl(const void *hw_context, bool *link_up)
{
    ASSERT(hw_context != NULL);
    ASSERT(link_up != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        *link_up = false;
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    // Check KSZ8851SNL driver status
    drv_ksz8851snl_status_info_t ksz_status;
    drv_ksz8851snl_status_t result = hw_ksz8851snl_get_status(&ksz8851snl_0, &ksz_status);
    
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        *link_up = ksz_status.link_up;
        context->status.link_up = ksz_status.link_up;
        context->last_link_check_time = xTaskGetTickCount();
    } else {
        *link_up = false;
        context->status.link_up = false;
    }
    
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_register_callback_impl(const void *hw_context, drv_netif_doip_cb_type_t type, drv_netif_doip_callback_t callback)
{
    ASSERT(hw_context != NULL);
    ASSERT(callback != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    switch (type) {
        case DRV_NETIF_DOIP_CB_LINK_UP:
            context->link_up_callback = callback;
            break;
        case DRV_NETIF_DOIP_CB_LINK_DOWN:
            context->link_down_callback = callback;
            break;
        case DRV_NETIF_DOIP_CB_IP_CONFIGURED:
            context->ip_config_callback = callback;
            break;
        case DRV_NETIF_DOIP_CB_ERROR:
            context->error_callback = callback;
            break;
        default:
            return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    printf("KSZ NetIF DoIP: Callback registered for type %d\r\n", type);
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_get_lwip_netif_impl(const void *hw_context, void **netif_ptr)
{
    ASSERT(hw_context != NULL);
    ASSERT(netif_ptr != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    if (!context->is_initialized || !context->lwip_netif_added) {
        *netif_ptr = NULL;
        return DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED;
    }
    
    *netif_ptr = &context->lwip_netif;
    return DRV_NETIF_DOIP_STATUS_OK;
}

static drv_netif_doip_status_t ksz_netif_force_link_check_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)hw_context;
    
    bool link_up;
    return ksz_netif_check_link_impl(hw_context, &link_up);
}

//-----------------------------------------------------------------------------
// Helper Functions
//-----------------------------------------------------------------------------

static void ksz_netif_update_status(ksz_netif_hw_context_t *context)
{
    // Update basic status
    context->status.state = context->current_state;
    context->status.type = context->interface_type;
    context->status.is_initialized = context->is_initialized;
    context->status.is_enabled = context->is_enabled;
    
    if (context->is_initialized && context->lwip_netif_added) {
        // Update network configuration
        memcpy(context->status.mac_address, context->lwip_netif.hwaddr, NETIF_DOIP_MAC_ADDR_LEN);
        context->status.ip_address = context->lwip_netif.ip_addr.addr;
        context->status.netmask = context->lwip_netif.netmask.addr;
        context->status.gateway = context->lwip_netif.gw.addr;
        context->status.dhcp_enabled = dhcp_supplied_address(&context->lwip_netif);
        
        // Check link status
        bool link_up;
        if (ksz_netif_check_link_impl(context, &link_up) == DRV_NETIF_DOIP_STATUS_OK) {
            context->status.link_up = link_up;
            
            if (link_up) {
                context->current_state = (context->status.ip_address != 0) ? 
                    DRV_NETIF_DOIP_STATE_CONFIGURED : DRV_NETIF_DOIP_STATE_LINK_UP;
            } else {
                context->current_state = DRV_NETIF_DOIP_STATE_LINK_DOWN;
            }
        }
    }
}

static void ksz_netif_link_callback(struct netif *netif)
{
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)netif->state;
    
    if (netif_is_link_up(netif)) {
        printf("KSZ NetIF DoIP: Link up\r\n");
        if (context->link_up_callback) {
            context->link_up_callback(DRV_NETIF_DOIP_CB_LINK_UP, NULL, 0);
        }
    } else {
        printf("KSZ NetIF DoIP: Link down\r\n");
        if (context->link_down_callback) {
            context->link_down_callback(DRV_NETIF_DOIP_CB_LINK_DOWN, NULL, 0);
        }
    }
    
    ksz_netif_update_status(context);
}

static void ksz_netif_status_callback(struct netif *netif)
{
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)netif->state;
    
    if (netif_is_up(netif)) {
        printf("KSZ NetIF DoIP: Interface up - IP: %s\r\n", ip4addr_ntoa(&netif->ip_addr));
        if (context->ip_config_callback) {
            context->ip_config_callback(DRV_NETIF_DOIP_CB_IP_CONFIGURED, &netif->ip_addr.addr, sizeof(uint32_t));
        }
    } else {
        printf("KSZ NetIF DoIP: Interface down\r\n");
    }
    
    ksz_netif_update_status(context);
}

//-----------------------------------------------------------------------------
// lwIP Network Interface Functions
//-----------------------------------------------------------------------------

static err_t ksz_netif_lwip_init(struct netif *netif)
{
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)netif->state;
    
    printf("KSZ NetIF DoIP: lwIP interface initialization\r\n");
    
    // Set interface name
    netif->name[0] = KSZ_NETIF_NAME0;
    netif->name[1] = KSZ_NETIF_NAME1;
    
    // Set output function
    netif->output = etharp_output;
    netif->linkoutput = ksz_netif_lwip_output;
    
    // Set MTU
    netif->mtu = KSZ_NETIF_MTU;
    
    // Set hardware address length
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    
    // Set hardware address
    memcpy(netif->hwaddr, context->config.mac_address, NETIF_DOIP_MAC_ADDR_LEN);
    
    // Set device capabilities
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    
    printf("KSZ NetIF DoIP: lwIP interface initialized\r\n");
    return ERR_OK;
}

static err_t ksz_netif_lwip_output(struct netif *netif, struct pbuf *p)
{
    ksz_netif_hw_context_t *context = (ksz_netif_hw_context_t *)netif->state;
    
    if (!context->is_enabled || !context->status.link_up) {
        return ERR_CONN;
    }
    
    // Prepare packet data
    uint16_t total_len = p->tot_len;
    uint8_t *packet_data = NULL;
    
    if (p->next == NULL) {
        // Single pbuf - direct transmission
        packet_data = (uint8_t *)p->payload;
    } else {
        // Multiple pbufs - need to copy to contiguous buffer
        // For now, reject fragmented packets
        printf("KSZ NetIF DoIP: Fragmented packets not supported\r\n");
        return ERR_BUF;
    }
    
    // Send packet via KSZ8851SNL driver
    drv_ksz8851snl_status_t result = hw_ksz8851snl_send_packet(&ksz8851snl_0, packet_data, total_len);
    
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        context->status.packets_sent++;
        context->status.bytes_sent += total_len;
        return ERR_OK;
    } else {
        context->status.errors_tx++;
        printf("KSZ NetIF DoIP: Packet transmission failed: %d\r\n", result);
        return ERR_IF;
    }
}

//-----------------------------------------------------------------------------
// Public API Functions
//-----------------------------------------------------------------------------

drv_netif_doip_status_t bsp_netif_doip_ksz_init_extended(const ksz_netif_config_extension_t *extended_config)
{
    if (extended_config != NULL) {
        memcpy(&ksz_netif_hw_context_0.extended_config, extended_config, sizeof(ksz_netif_config_extension_t));
    } else {
        bsp_netif_doip_ksz_create_default_extended_config(&ksz_netif_hw_context_0.extended_config);
    }
    
    printf("KSZ NetIF DoIP: Extended configuration applied\r\n");
    return DRV_NETIF_DOIP_STATUS_OK;
}

drv_netif_doip_status_t bsp_netif_doip_ksz_get_extended_status(ksz_netif_status_extension_t *extended_status)
{
    ASSERT(extended_status != NULL);
    
    // Get KSZ8851SNL hardware status
    drv_ksz8851snl_id_info_t id_info;
    drv_ksz8851snl_status_info_t status_info;
    
    if (hw_ksz8851snl_get_chip_id(&ksz8851snl_0, &id_info) == DRV_KSZ8851SNL_STATUS_OK) {
        extended_status->chip_id = id_info.chip_id;
        extended_status->revision_id = id_info.revision_id;
        extended_status->spi_communication_ok = id_info.spi_communication_ok;
        extended_status->chip_detected = id_info.chip_detected;
    }
    
    if (hw_ksz8851snl_get_status(&ksz8851snl_0, &status_info) == DRV_KSZ8851SNL_STATUS_OK) {
        extended_status->auto_neg_complete = true;  // Assume complete for now
        extended_status->link_partner_capable = status_info.link_up;
    }
    
    // Copy current extended status
    memcpy(extended_status, &ksz_netif_hw_context_0.extended_status, sizeof(ksz_netif_status_extension_t));
    
    return DRV_NETIF_DOIP_STATUS_OK;
}

drv_ksz8851snl_t* bsp_netif_doip_ksz_get_driver(void)
{
    return &ksz8851snl_0;
}

bool bsp_netif_doip_ksz_hardware_available(uint16_t *chip_id)
{
    drv_ksz8851snl_id_info_t id_info;
    
    if (hw_ksz8851snl_get_chip_id(&ksz8851snl_0, &id_info) == DRV_KSZ8851SNL_STATUS_OK) {
        if (chip_id != NULL) {
            *chip_id = id_info.chip_id;
        }
        return id_info.chip_detected && id_info.spi_communication_ok;
    }
    
    return false;
}

void bsp_netif_doip_ksz_create_default_extended_config(ksz_netif_config_extension_t *extended_config)
{
    ASSERT(extended_config != NULL);
    
    memset(extended_config, 0, sizeof(ksz_netif_config_extension_t));
    
    // SPI configuration defaults
    extended_config->spi_clock_speed = 10000000;  // 10 MHz
    extended_config->spi_mode = 0;                // SPI mode 0
    extended_config->spi_cs_active_low = true;
    
    // Interrupt configuration defaults
    extended_config->interrupt_enabled = true;
    extended_config->interrupt_priority = 5;      // Safe for FreeRTOS
    
    // Power management defaults
    extended_config->power_down_enabled = false;
    extended_config->power_down_delay_ms = 30000; // 30 seconds
}