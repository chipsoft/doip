#ifndef _DRIVER_ETHERNET_H_
#define _DRIVER_ETHERNET_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    DRV_ETH_STATUS_OK = 0,    ///< Success
    DRV_ETH_STATUS_ERROR = 1, ///< Generic error
    DRV_ETH_STATUS_BUSY = 2,  ///< Device busy
    DRV_ETH_STATUS_TIMEOUT = 3, ///< Operation timeout
} drv_eth_status_t;

// Alias for compatibility with network interface drivers
typedef drv_eth_status_t drv_ethernet_status_t;
#define DRV_ETHERNET_STATUS_OK DRV_ETH_STATUS_OK
#define DRV_ETHERNET_STATUS_ERROR DRV_ETH_STATUS_ERROR
#define DRV_ETHERNET_STATUS_BUSY DRV_ETH_STATUS_BUSY
#define DRV_ETHERNET_STATUS_TIMEOUT DRV_ETH_STATUS_TIMEOUT

typedef enum
{
    DRV_ETH_CB_RECEIVE = 0,   ///< Receive callback
    DRV_ETH_CB_TRANSMIT = 1,  ///< Transmit callback
} drv_eth_cb_type_t;

// Alias for compatibility with network interface drivers
typedef drv_eth_cb_type_t drv_ethernet_cb_type_t;
#define DRV_ETHERNET_CB_RX_COMPLETE DRV_ETH_CB_RECEIVE
#define DRV_ETHERNET_CB_TX_COMPLETE DRV_ETH_CB_TRANSMIT

typedef drv_eth_callback_t drv_ethernet_callback_t;

// Configuration structure for ethernet driver
typedef struct {
    uint8_t mac_addr[6];  ///< MAC address
} drv_ethernet_config_t;

typedef void (*drv_eth_callback_t)(void);
typedef void (*drv_eth_tcpip_init_done_fn)(void *arg);

typedef struct
{
    bool is_init;
    bool is_enabled;
    const void *hw_context;
    
    // Core MAC operations
    drv_eth_status_t (*init)(const void *hw_context);
    drv_eth_status_t (*deinit)(const void *hw_context);
    drv_eth_status_t (*enable)(const void *hw_context);
    drv_eth_status_t (*disable)(const void *hw_context);
    
    // PHY operations
    drv_eth_status_t (*phy_init)(const void *hw_context);
    drv_eth_status_t (*phy_reset)(const void *hw_context);
    drv_eth_status_t (*get_link_status)(const void *hw_context, bool *link_up);
    drv_eth_status_t (*restart_autoneg)(const void *hw_context);
    
    // Register access
    drv_eth_status_t (*read_phy_reg)(const void *hw_context, uint16_t reg, uint16_t *value);
    drv_eth_status_t (*write_phy_reg)(const void *hw_context, uint16_t reg, uint16_t value);
    
    // PHY power management
    drv_eth_status_t (*set_phy_powerdown)(const void *hw_context, bool state);
    drv_eth_status_t (*set_phy_isolate)(const void *hw_context, bool state);
    drv_eth_status_t (*set_phy_loopback)(const void *hw_context, bool state);
    
    // Advanced PHY register operations
    drv_eth_status_t (*set_phy_reg_bit)(const void *hw_context, uint16_t reg, uint16_t mask);
    drv_eth_status_t (*clear_phy_reg_bit)(const void *hw_context, uint16_t reg, uint16_t mask);
    
    // Callback management
    drv_eth_status_t (*register_callback)(const void *hw_context, drv_eth_cb_type_t type, drv_eth_callback_t callback);
    
    // Data operations
    drv_eth_status_t (*write)(const void *hw_context, const uint8_t *data, uint32_t length);
    
    // Network packet operations (for LWIP integration)
    drv_eth_status_t (*send_packet)(const void *hw_context, const uint8_t *data, uint16_t length);
    drv_eth_status_t (*receive_packet)(const void *hw_context, uint8_t *data, uint16_t *length);
    drv_eth_status_t (*check_rx_available)(const void *hw_context, bool *available);
    drv_eth_status_t (*get_config)(const void *hw_context, drv_ethernet_config_t *config);
    
    // TCP/IP stack initialization
    drv_eth_tcpip_init_done_fn (*get_tcpip_init_done_fn)(const void *hw_context);
    
    // Link monitoring
    drv_eth_status_t (*start_link_monitor)(const void *hw_context);
    drv_eth_status_t (*stop_link_monitor)(const void *hw_context);
} drv_eth_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Universal Ethernet driver API
drv_eth_status_t hw_eth_init(drv_eth_t *handle);
drv_eth_status_t hw_eth_deinit(drv_eth_t *handle);
drv_eth_status_t hw_eth_enable(drv_eth_t *handle);
drv_eth_status_t hw_eth_disable(drv_eth_t *handle);

// PHY management functions
drv_eth_status_t hw_eth_phy_init(drv_eth_t *handle);
drv_eth_status_t hw_eth_phy_reset(drv_eth_t *handle);
drv_eth_status_t hw_eth_get_link_status(drv_eth_t *handle, bool *link_up);
drv_eth_status_t hw_eth_restart_autoneg(drv_eth_t *handle);

// Register access functions
drv_eth_status_t hw_eth_read_phy_reg(drv_eth_t *handle, uint16_t reg, uint16_t *value);
drv_eth_status_t hw_eth_write_phy_reg(drv_eth_t *handle, uint16_t reg, uint16_t value);

// PHY power management functions
drv_eth_status_t hw_eth_set_phy_powerdown(drv_eth_t *handle, bool state);
drv_eth_status_t hw_eth_set_phy_isolate(drv_eth_t *handle, bool state);
drv_eth_status_t hw_eth_set_phy_loopback(drv_eth_t *handle, bool state);

// Advanced PHY register operations
drv_eth_status_t hw_eth_set_phy_reg_bit(drv_eth_t *handle, uint16_t reg, uint16_t mask);
drv_eth_status_t hw_eth_clear_phy_reg_bit(drv_eth_t *handle, uint16_t reg, uint16_t mask);

// Callback management
drv_eth_status_t hw_eth_register_callback(drv_eth_t *handle, drv_eth_cb_type_t type, drv_eth_callback_t callback);

// Data operations
drv_eth_status_t hw_eth_write(drv_eth_t *handle, const uint8_t *data, uint32_t length);

// Network packet operations (for LWIP integration)
drv_eth_status_t hw_eth_send_packet(drv_eth_t *handle, const uint8_t *data, uint16_t length);
drv_eth_status_t hw_eth_receive_packet(drv_eth_t *handle, uint8_t *data, uint16_t *length);
drv_eth_status_t hw_eth_check_rx_available(drv_eth_t *handle, bool *available);
drv_eth_status_t hw_eth_get_config(drv_eth_t *handle, drv_ethernet_config_t *config);

// Network interface compatibility aliases
#define hw_ethernet_init hw_eth_init
#define hw_ethernet_enable hw_eth_enable
#define hw_ethernet_disable hw_eth_disable
#define hw_ethernet_send_packet hw_eth_send_packet
#define hw_ethernet_receive_packet hw_eth_receive_packet
#define hw_ethernet_check_rx_available hw_eth_check_rx_available
#define hw_ethernet_register_callback hw_eth_register_callback
#define hw_ethernet_get_config hw_eth_get_config

// TCP/IP stack initialization
drv_eth_tcpip_init_done_fn hw_eth_get_tcpip_init_done_fn(drv_eth_t *handle);

// Link monitoring
drv_eth_status_t hw_eth_start_link_monitor(drv_eth_t *handle);
drv_eth_status_t hw_eth_stop_link_monitor(drv_eth_t *handle);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_ETHERNET_H_