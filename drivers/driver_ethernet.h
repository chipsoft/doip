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

typedef enum
{
    DRV_ETH_CB_RECEIVE = 0,   ///< Receive callback
    DRV_ETH_CB_TRANSMIT = 1,  ///< Transmit callback
} drv_eth_cb_type_t;

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
    
    // Callback management
    drv_eth_status_t (*register_callback)(const void *hw_context, drv_eth_cb_type_t type, drv_eth_callback_t callback);
    
    // Data operations
    drv_eth_status_t (*write)(const void *hw_context, const uint8_t *data, uint32_t length);
    
    // TCP/IP stack initialization
    drv_eth_tcpip_init_done_fn (*get_tcpip_init_done_fn)(const void *hw_context);
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

// Callback management
drv_eth_status_t hw_eth_register_callback(drv_eth_t *handle, drv_eth_cb_type_t type, drv_eth_callback_t callback);

// Data operations
drv_eth_status_t hw_eth_write(drv_eth_t *handle, const uint8_t *data, uint32_t length);

// TCP/IP stack initialization
drv_eth_tcpip_init_done_fn hw_eth_get_tcpip_init_done_fn(drv_eth_t *handle);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_ETHERNET_H_