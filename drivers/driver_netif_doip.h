/**
 * @file driver_netif_doip.h
 * @brief Universal Network Interface Driver for DoIP Communications
 * 
 * @details This header provides a hardware-agnostic network interface driver
 * implementation for DoIP (Diagnostics over Internet Protocol) communications.
 * The driver abstracts network operations across different hardware interfaces
 * including GMAC, KSZ8851SNL, and other Ethernet controllers.
 * 
 * @section features Key Features
 * - Hardware-agnostic network interface abstraction
 * - Support for multiple Ethernet controllers (GMAC, KSZ8851SNL)
 * - DoIP-optimized network operations
 * - Link status monitoring and management
 * - Network configuration management (IP, MAC, DHCP)
 * - Error handling and recovery mechanisms
 * - Universal driver pattern for portability
 * 
 * @section architecture Driver Architecture
 * The driver follows a three-layer architecture:
 * 1. Universal Driver Interface (this file) - Hardware-agnostic API
 * 2. Universal Driver Implementation (driver_netif_doip.c) - Common logic
 * 3. BSP Driver Implementation (hw/platform/bsp_netif_doip_*.c) - Hardware-specific code
 * 
 * @author  SAME54 DoIP Project
 * @date    2024
 * @version 1.0
 * 
 * @copyright MIT License - Free to use, modify, and distribute
 */

#ifndef _DRIVER_NETIF_DOIP_H_
#define _DRIVER_NETIF_DOIP_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"

/**
 * @defgroup netif_constants Network Interface Constants
 * @brief Network interface configuration constants
 * @{
 */
#define NETIF_DOIP_MAC_ADDR_LEN     6     /**< MAC address length in bytes */
#define NETIF_DOIP_IP_ADDR_LEN      4     /**< IPv4 address length in bytes */
#define NETIF_DOIP_MAX_HOSTNAME_LEN 32    /**< Maximum hostname length */
#define NETIF_DOIP_MAX_RETRIES      3     /**< Maximum operation retry attempts */
#define NETIF_DOIP_LINK_CHECK_MS    1000  /**< Link status check interval */
/** @} */

/**
 * @brief Network interface driver status codes
 * @details Return codes for all network interface operations
 */
typedef enum {
    DRV_NETIF_DOIP_STATUS_OK = 0,           /**< Operation completed successfully */
    DRV_NETIF_DOIP_STATUS_ERROR = 1,        /**< General error occurred */
    DRV_NETIF_DOIP_STATUS_BUSY = 2,         /**< Driver is busy with another operation */
    DRV_NETIF_DOIP_STATUS_TIMEOUT = 3,      /**< Operation timed out */
    DRV_NETIF_DOIP_STATUS_NO_LINK = 4,      /**< Network link is down */
    DRV_NETIF_DOIP_STATUS_INVALID_PARAM = 5,/**< Invalid parameter provided */
    DRV_NETIF_DOIP_STATUS_NOT_INITIALIZED = 6, /**< Driver not initialized */
    DRV_NETIF_DOIP_STATUS_INTERFACE_ERROR = 7, /**< Hardware interface error */
} drv_netif_doip_status_t;

/**
 * @brief Network interface types
 * @details Supported network interface hardware types
 */
typedef enum {
    DRV_NETIF_DOIP_TYPE_UNKNOWN = 0,    /**< Unknown or uninitialized interface */
    DRV_NETIF_DOIP_TYPE_GMAC = 1,       /**< Built-in GMAC Ethernet controller */
    DRV_NETIF_DOIP_TYPE_KSZ8851SNL = 2, /**< KSZ8851SNL SPI-to-Ethernet controller */
    DRV_NETIF_DOIP_TYPE_EXTERNAL = 3,   /**< External Ethernet interface */
} drv_netif_doip_type_t;

/**
 * @brief Network interface state
 * @details Current operational state of the network interface
 */
typedef enum {
    DRV_NETIF_DOIP_STATE_UNINITIALIZED = 0, /**< Interface not initialized */
    DRV_NETIF_DOIP_STATE_INITIALIZED = 1,    /**< Interface initialized but down */
    DRV_NETIF_DOIP_STATE_LINK_DOWN = 2,      /**< Interface up but link down */
    DRV_NETIF_DOIP_STATE_LINK_UP = 3,        /**< Interface up with link established */
    DRV_NETIF_DOIP_STATE_CONFIGURED = 4,     /**< Interface configured with IP address */
    DRV_NETIF_DOIP_STATE_ERROR = 5,          /**< Interface in error state */
} drv_netif_doip_state_t;

/**
 * @brief Network interface callback event types
 * @details Event types that trigger registered callbacks
 */
typedef enum {
    DRV_NETIF_DOIP_CB_LINK_UP = 0,       /**< Network link established */
    DRV_NETIF_DOIP_CB_LINK_DOWN = 1,     /**< Network link lost */
    DRV_NETIF_DOIP_CB_IP_CONFIGURED = 2, /**< IP address configured */
    DRV_NETIF_DOIP_CB_ERROR = 3,         /**< Network interface error */
} drv_netif_doip_cb_type_t;

/**
 * @brief Network interface callback function type
 * @param type Event type that triggered the callback
 * @param data Pointer to event-specific data (can be NULL)
 * @param data_len Length of data in bytes
 */
typedef void (*drv_netif_doip_callback_t)(drv_netif_doip_cb_type_t type, const void *data, size_t data_len);

/**
 * @brief Network interface configuration structure
 * @details Configuration parameters for network interface initialization
 */
typedef struct {
    /** @name Hardware Configuration
     * Hardware-specific configuration parameters
     * @{
     */
    drv_netif_doip_type_t interface_type;  /**< Network interface hardware type */
    uint8_t mac_address[NETIF_DOIP_MAC_ADDR_LEN]; /**< MAC address (00:00:00:00:00:00 for auto) */
    bool auto_negotiation;                 /**< Enable auto-negotiation */
    uint16_t link_speed;                   /**< Link speed in Mbps (10, 100, 1000) */
    bool full_duplex;                      /**< Full duplex mode */
    /** @} */
    
    /** @name Network Configuration
     * IP and network layer configuration
     * @{
     */
    bool use_dhcp;                         /**< Use DHCP for IP configuration */
    uint32_t static_ip;                    /**< Static IP address (network byte order) */
    uint32_t static_netmask;               /**< Static netmask (network byte order) */
    uint32_t static_gateway;               /**< Static gateway (network byte order) */
    uint32_t static_dns;                   /**< Static DNS server (network byte order) */
    char hostname[NETIF_DOIP_MAX_HOSTNAME_LEN]; /**< Network hostname */
    /** @} */
    
    /** @name Timing Configuration
     * Timeout and retry configuration
     * @{
     */
    uint32_t init_timeout_ms;              /**< Interface initialization timeout */
    uint32_t link_timeout_ms;              /**< Link establishment timeout */
    uint32_t dhcp_timeout_ms;              /**< DHCP configuration timeout */
    uint8_t max_retries;                   /**< Maximum operation retry attempts */
    /** @} */
} drv_netif_doip_config_t;

/**
 * @brief Network interface status information
 * @details Current status and statistics of the network interface
 */
typedef struct {
    /** @name Interface State
     * Current operational state
     * @{
     */
    drv_netif_doip_state_t state;          /**< Current interface state */
    drv_netif_doip_type_t type;            /**< Interface hardware type */
    bool is_initialized;                   /**< Interface initialized flag */
    bool is_enabled;                       /**< Interface enabled flag */
    /** @} */
    
    /** @name Link Status
     * Physical link information
     * @{
     */
    bool link_up;                          /**< Physical link status */
    uint16_t link_speed;                   /**< Actual link speed in Mbps */
    bool full_duplex;                      /**< Actual duplex mode */
    uint32_t link_up_time_ms;              /**< Time since link established */
    /** @} */
    
    /** @name Network Configuration
     * Current network configuration
     * @{
     */
    uint8_t mac_address[NETIF_DOIP_MAC_ADDR_LEN]; /**< Current MAC address */
    uint32_t ip_address;                   /**< Current IP address (network byte order) */
    uint32_t netmask;                      /**< Current netmask (network byte order) */
    uint32_t gateway;                      /**< Current gateway (network byte order) */
    uint32_t dns_server;                   /**< Current DNS server (network byte order) */
    bool dhcp_enabled;                     /**< DHCP enabled status */
    /** @} */
    
    /** @name Statistics
     * Interface usage statistics
     * @{
     */
    uint32_t packets_sent;                 /**< Total packets transmitted */
    uint32_t packets_received;             /**< Total packets received */
    uint32_t bytes_sent;                   /**< Total bytes transmitted */
    uint32_t bytes_received;               /**< Total bytes received */
    uint32_t errors_tx;                    /**< Transmission errors */
    uint32_t errors_rx;                    /**< Reception errors */
    uint32_t link_state_changes;           /**< Number of link state changes */
    /** @} */
} drv_netif_doip_status_info_t;

/**
 * @brief Network interface driver structure
 * @details Universal network interface driver with function pointers
 */
typedef struct {
    bool is_init;                          /**< Driver initialization status */
    drv_netif_doip_state_t current_state;  /**< Current driver state */
    const void *hw_context;                /**< Hardware-specific context */
    
    /** @name Core Operations
     * Basic driver lifecycle operations
     * @{
     */
    drv_netif_doip_status_t (*init)(const void *hw_context, const drv_netif_doip_config_t *config);
    drv_netif_doip_status_t (*deinit)(const void *hw_context);
    drv_netif_doip_status_t (*enable)(const void *hw_context);
    drv_netif_doip_status_t (*disable)(const void *hw_context);
    /** @} */
    
    /** @name Network Operations
     * Network configuration and management
     * @{
     */
    drv_netif_doip_status_t (*configure_ip)(const void *hw_context, uint32_t ip, uint32_t netmask, uint32_t gateway);
    drv_netif_doip_status_t (*enable_dhcp)(const void *hw_context);
    drv_netif_doip_status_t (*disable_dhcp)(const void *hw_context);
    drv_netif_doip_status_t (*set_mac_address)(const void *hw_context, const uint8_t mac_addr[NETIF_DOIP_MAC_ADDR_LEN]);
    /** @} */
    
    /** @name Status and Monitoring
     * Interface status and health monitoring
     * @{
     */
    drv_netif_doip_status_t (*get_status)(const void *hw_context, drv_netif_doip_status_info_t *status);
    drv_netif_doip_status_t (*check_link)(const void *hw_context, bool *link_up);
    drv_netif_doip_status_t (*reset_statistics)(const void *hw_context);
    /** @} */
    
    /** @name Callback Management
     * Event callback registration and management
     * @{
     */
    drv_netif_doip_status_t (*register_callback)(const void *hw_context, 
                                                 drv_netif_doip_cb_type_t type, 
                                                 drv_netif_doip_callback_t callback);
    drv_netif_doip_status_t (*unregister_callback)(const void *hw_context, 
                                                   drv_netif_doip_cb_type_t type);
    /** @} */
    
    /** @name Advanced Operations
     * Advanced network interface operations
     * @{
     */
    drv_netif_doip_status_t (*set_hostname)(const void *hw_context, const char *hostname);
    drv_netif_doip_status_t (*get_lwip_netif)(const void *hw_context, void **netif_ptr);
    drv_netif_doip_status_t (*force_link_check)(const void *hw_context);
    /** @} */
} drv_netif_doip_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup netif_api Universal Network Interface API Functions
 * @brief Universal network interface driver API functions
 * @{
 */

/**
 * @brief Initialize the network interface driver
 * @param handle Pointer to network interface driver instance
 * @param config Pointer to configuration structure
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 * @note Must be called before any other driver operations
 */
drv_netif_doip_status_t hw_netif_doip_init(drv_netif_doip_t *handle, const drv_netif_doip_config_t *config);

/**
 * @brief Deinitialize the network interface driver
 * @param handle Pointer to network interface driver instance
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 * @note Releases all resources and disables the interface
 */
drv_netif_doip_status_t hw_netif_doip_deinit(drv_netif_doip_t *handle);

/**
 * @brief Enable the network interface
 * @param handle Pointer to network interface driver instance
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 * @note Interface must be initialized before enabling
 */
drv_netif_doip_status_t hw_netif_doip_enable(drv_netif_doip_t *handle);

/**
 * @brief Disable the network interface
 * @param handle Pointer to network interface driver instance
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t hw_netif_doip_disable(drv_netif_doip_t *handle);

/**
 * @brief Configure static IP address
 * @param handle Pointer to network interface driver instance
 * @param ip IP address in network byte order
 * @param netmask Netmask in network byte order
 * @param gateway Gateway address in network byte order
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t hw_netif_doip_configure_ip(drv_netif_doip_t *handle, uint32_t ip, uint32_t netmask, uint32_t gateway);

/**
 * @brief Enable DHCP for automatic IP configuration
 * @param handle Pointer to network interface driver instance
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t hw_netif_doip_enable_dhcp(drv_netif_doip_t *handle);

/**
 * @brief Disable DHCP and use static IP configuration
 * @param handle Pointer to network interface driver instance
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t hw_netif_doip_disable_dhcp(drv_netif_doip_t *handle);

/**
 * @brief Set MAC address for the network interface
 * @param handle Pointer to network interface driver instance
 * @param mac_addr MAC address (6 bytes)
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t hw_netif_doip_set_mac_address(drv_netif_doip_t *handle, const uint8_t mac_addr[NETIF_DOIP_MAC_ADDR_LEN]);

/**
 * @brief Get current network interface status
 * @param handle Pointer to network interface driver instance
 * @param status Pointer to status structure to populate
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t hw_netif_doip_get_status(drv_netif_doip_t *handle, drv_netif_doip_status_info_t *status);

/**
 * @brief Check network link status
 * @param handle Pointer to network interface driver instance
 * @param link_up Pointer to store link status
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t hw_netif_doip_check_link(drv_netif_doip_t *handle, bool *link_up);

/**
 * @brief Register callback for network interface events
 * @param handle Pointer to network interface driver instance
 * @param type Event type to register for
 * @param callback Function to call when event occurs
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t hw_netif_doip_register_callback(drv_netif_doip_t *handle, 
                                                       drv_netif_doip_cb_type_t type, 
                                                       drv_netif_doip_callback_t callback);

/**
 * @brief Get underlying lwIP network interface pointer
 * @param handle Pointer to network interface driver instance
 * @param netif_ptr Pointer to store lwIP netif pointer
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 * @note For advanced users requiring direct lwIP access
 */
drv_netif_doip_status_t hw_netif_doip_get_lwip_netif(drv_netif_doip_t *handle, void **netif_ptr);

/**
 * @brief Create a default network interface configuration
 * @param config Pointer to configuration structure to initialize
 * @param interface_type Network interface type to configure
 * @note Fills structure with recommended default values
 */
void hw_netif_doip_create_default_config(drv_netif_doip_config_t *config, drv_netif_doip_type_t interface_type);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_NETIF_DOIP_H_