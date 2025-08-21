#ifndef _DRIVER_ETHERNET_H_
#define _DRIVER_ETHERNET_H_

#include <stdbool.h>
#include <stdint.h>

// MAC address length in bytes
#define DRV_ETH_MAC_ADDR_LEN 6

typedef enum
{
    DRV_ETH_STATUS_OK = 0,    ///< Success
    DRV_ETH_STATUS_ERROR = 1, ///< Generic error
    DRV_ETH_STATUS_BUSY = 2,  ///< Device busy
    DRV_ETH_STATUS_TIMEOUT = 3, ///< Operation timeout
    DRV_ETH_STATUS_INVALID_MAC = 4, ///< Invalid MAC address
} drv_eth_status_t;

typedef enum
{
    DRV_ETH_CB_RECEIVE = 0,   ///< Receive callback
    DRV_ETH_CB_TRANSMIT = 1,  ///< Transmit callback
} drv_eth_cb_type_t;

typedef void (*drv_eth_callback_t)(void);
typedef void (*drv_eth_tcpip_init_done_fn)(void *arg);

// MAC address configuration structure
typedef struct
{
    uint8_t addr[DRV_ETH_MAC_ADDR_LEN];  ///< MAC address bytes
    bool is_valid;                        ///< MAC address validity flag
} drv_eth_mac_config_t;

// MAC address validation macros
#define DRV_ETH_MAC_IS_MULTICAST(mac) ((mac)[0] & 0x01)
#define DRV_ETH_MAC_IS_ZERO(mac) (((mac)[0] | (mac)[1] | (mac)[2] | (mac)[3] | (mac)[4] | (mac)[5]) == 0)

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
    
    // TCP/IP stack initialization
    drv_eth_tcpip_init_done_fn (*get_tcpip_init_done_fn)(const void *hw_context);
    
    // Link monitoring
    drv_eth_status_t (*start_link_monitor)(const void *hw_context);
    drv_eth_status_t (*stop_link_monitor)(const void *hw_context);
    
    // MAC address configuration
    drv_eth_status_t (*set_mac_address)(const void *hw_context, const uint8_t mac_addr[DRV_ETH_MAC_ADDR_LEN]);
    drv_eth_status_t (*get_mac_address)(const void *hw_context, uint8_t mac_addr[DRV_ETH_MAC_ADDR_LEN]);
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

// TCP/IP stack initialization
drv_eth_tcpip_init_done_fn hw_eth_get_tcpip_init_done_fn(drv_eth_t *handle);

// Link monitoring
drv_eth_status_t hw_eth_start_link_monitor(drv_eth_t *handle);
drv_eth_status_t hw_eth_stop_link_monitor(drv_eth_t *handle);

// MAC address configuration
drv_eth_status_t hw_eth_set_mac_address(drv_eth_t *handle, const uint8_t mac_addr[DRV_ETH_MAC_ADDR_LEN]);
drv_eth_status_t hw_eth_get_mac_address(drv_eth_t *handle, uint8_t mac_addr[DRV_ETH_MAC_ADDR_LEN]);

// MAC address validation helper
bool hw_eth_is_mac_valid(const uint8_t mac_addr[DRV_ETH_MAC_ADDR_LEN]);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_ETHERNET_H_