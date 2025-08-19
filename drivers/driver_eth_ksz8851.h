#ifndef _DRIVER_ETH_KSZ8851_H_
#define _DRIVER_ETH_KSZ8851_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DRV_ETH_KSZ8851_STATUS_OK = 0,
    DRV_ETH_KSZ8851_STATUS_ERROR = 1,
    DRV_ETH_KSZ8851_STATUS_BUSY = 2,
    DRV_ETH_KSZ8851_STATUS_TIMEOUT = 3,
    DRV_ETH_KSZ8851_STATUS_LINK_DOWN = 4,
    DRV_ETH_KSZ8851_STATUS_NO_PACKET = 5,
    DRV_ETH_KSZ8851_STATUS_INVALID_PARAM = 6,
} drv_eth_ksz8851_status_t;

typedef enum {
    DRV_ETH_KSZ8851_CB_RX_PACKET = 0,
    DRV_ETH_KSZ8851_CB_TX_COMPLETE = 1,
    DRV_ETH_KSZ8851_CB_LINK_CHANGE = 2,
    DRV_ETH_KSZ8851_CB_ERROR = 3,
} drv_eth_ksz8851_cb_type_t;

typedef void (*drv_eth_ksz8851_callback_t)(void);

typedef struct {
    uint8_t mac_addr[6];
    bool interrupt_driven;
    bool auto_negotiation;
    bool full_duplex;
    bool speed_100mbps;
} drv_eth_ksz8851_config_t;

typedef struct {
    uint16_t length;
    bool valid;
    bool broadcast;
    bool multicast;
    bool unicast;
    bool crc_error;
    bool length_error;
    bool phy_error;
} drv_eth_ksz8851_rx_status_t;

typedef struct {
    bool is_init;
    bool is_enabled;
    const void *hw_context;
    
    // Core operations
    drv_eth_ksz8851_status_t (*init)(const void *hw_context, const drv_eth_ksz8851_config_t *config);
    drv_eth_ksz8851_status_t (*deinit)(const void *hw_context);
    drv_eth_ksz8851_status_t (*enable)(const void *hw_context);
    drv_eth_ksz8851_status_t (*disable)(const void *hw_context);
    
    // Register access
    drv_eth_ksz8851_status_t (*read_reg)(const void *hw_context, uint16_t reg, uint16_t *value);
    drv_eth_ksz8851_status_t (*write_reg)(const void *hw_context, uint16_t reg, uint16_t value);
    drv_eth_ksz8851_status_t (*set_bits)(const void *hw_context, uint16_t reg, uint16_t mask);
    drv_eth_ksz8851_status_t (*clear_bits)(const void *hw_context, uint16_t reg, uint16_t mask);
    
    // Packet operations
    drv_eth_ksz8851_status_t (*send_packet)(const void *hw_context, const uint8_t *data, uint16_t length);
    drv_eth_ksz8851_status_t (*receive_packet)(const void *hw_context, uint8_t *data, uint16_t max_length, uint16_t *actual_length);
    drv_eth_ksz8851_status_t (*get_rx_status)(const void *hw_context, drv_eth_ksz8851_rx_status_t *rx_status);
    
    // Link management
    drv_eth_ksz8851_status_t (*get_link_status)(const void *hw_context, bool *link_up);
    drv_eth_ksz8851_status_t (*restart_autoneg)(const void *hw_context);
    drv_eth_ksz8851_status_t (*get_link_speed)(const void *hw_context, bool *speed_100mbps, bool *full_duplex);
    
    // Interrupt handling
    drv_eth_ksz8851_status_t (*enable_irq)(const void *hw_context);
    drv_eth_ksz8851_status_t (*disable_irq)(const void *hw_context);
    drv_eth_ksz8851_status_t (*irq_handler)(const void *hw_context);
    
    // MAC address
    drv_eth_ksz8851_status_t (*set_mac_addr)(const void *hw_context, const uint8_t mac_addr[6]);
    drv_eth_ksz8851_status_t (*get_mac_addr)(const void *hw_context, uint8_t mac_addr[6]);
    
    // Callback management
    drv_eth_ksz8851_status_t (*register_callback)(const void *hw_context, 
                                                   drv_eth_ksz8851_cb_type_t type, 
                                                   drv_eth_ksz8851_callback_t callback);
} drv_eth_ksz8851_t;

#ifdef __cplusplus
extern "C" {
#endif

// Universal KSZ8851 Ethernet driver API
drv_eth_ksz8851_status_t hw_eth_ksz8851_init(drv_eth_ksz8851_t *handle, const drv_eth_ksz8851_config_t *config);
drv_eth_ksz8851_status_t hw_eth_ksz8851_deinit(drv_eth_ksz8851_t *handle);
drv_eth_ksz8851_status_t hw_eth_ksz8851_enable(drv_eth_ksz8851_t *handle);
drv_eth_ksz8851_status_t hw_eth_ksz8851_disable(drv_eth_ksz8851_t *handle);

// Register access functions
drv_eth_ksz8851_status_t hw_eth_ksz8851_read_reg(drv_eth_ksz8851_t *handle, uint16_t reg, uint16_t *value);
drv_eth_ksz8851_status_t hw_eth_ksz8851_write_reg(drv_eth_ksz8851_t *handle, uint16_t reg, uint16_t value);
drv_eth_ksz8851_status_t hw_eth_ksz8851_set_bits(drv_eth_ksz8851_t *handle, uint16_t reg, uint16_t mask);
drv_eth_ksz8851_status_t hw_eth_ksz8851_clear_bits(drv_eth_ksz8851_t *handle, uint16_t reg, uint16_t mask);

// Packet operations
drv_eth_ksz8851_status_t hw_eth_ksz8851_send_packet(drv_eth_ksz8851_t *handle, const uint8_t *data, uint16_t length);
drv_eth_ksz8851_status_t hw_eth_ksz8851_receive_packet(drv_eth_ksz8851_t *handle, uint8_t *data, uint16_t max_length, uint16_t *actual_length);
drv_eth_ksz8851_status_t hw_eth_ksz8851_get_rx_status(drv_eth_ksz8851_t *handle, drv_eth_ksz8851_rx_status_t *rx_status);

// Link management functions
drv_eth_ksz8851_status_t hw_eth_ksz8851_get_link_status(drv_eth_ksz8851_t *handle, bool *link_up);
drv_eth_ksz8851_status_t hw_eth_ksz8851_restart_autoneg(drv_eth_ksz8851_t *handle);
drv_eth_ksz8851_status_t hw_eth_ksz8851_get_link_speed(drv_eth_ksz8851_t *handle, bool *speed_100mbps, bool *full_duplex);

// Interrupt handling functions
drv_eth_ksz8851_status_t hw_eth_ksz8851_enable_irq(drv_eth_ksz8851_t *handle);
drv_eth_ksz8851_status_t hw_eth_ksz8851_disable_irq(drv_eth_ksz8851_t *handle);
drv_eth_ksz8851_status_t hw_eth_ksz8851_irq_handler(drv_eth_ksz8851_t *handle);

// MAC address functions
drv_eth_ksz8851_status_t hw_eth_ksz8851_set_mac_addr(drv_eth_ksz8851_t *handle, const uint8_t mac_addr[6]);
drv_eth_ksz8851_status_t hw_eth_ksz8851_get_mac_addr(drv_eth_ksz8851_t *handle, uint8_t mac_addr[6]);

// Callback management
drv_eth_ksz8851_status_t hw_eth_ksz8851_register_callback(drv_eth_ksz8851_t *handle, 
                                                           drv_eth_ksz8851_cb_type_t type, 
                                                           drv_eth_ksz8851_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_ETH_KSZ8851_H_