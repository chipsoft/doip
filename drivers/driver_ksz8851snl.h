#ifndef _DRIVER_KSZ8851SNL_H_
#define _DRIVER_KSZ8851SNL_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DRV_KSZ8851SNL_STATUS_OK = 0,
    DRV_KSZ8851SNL_STATUS_ERROR = 1,
    DRV_KSZ8851SNL_STATUS_BUSY = 2,
    DRV_KSZ8851SNL_STATUS_TIMEOUT = 3,
    DRV_KSZ8851SNL_STATUS_INVALID_PARAM = 4,
    DRV_KSZ8851SNL_STATUS_NO_LINK = 5,
    DRV_KSZ8851SNL_STATUS_INVALID_MAC = 6,
} drv_ksz8851snl_status_t;

typedef enum {
    DRV_KSZ8851SNL_CB_RX_COMPLETE = 0,
    DRV_KSZ8851SNL_CB_TX_COMPLETE = 1,
    DRV_KSZ8851SNL_CB_LINK_CHANGE = 2,
    DRV_KSZ8851SNL_CB_ERROR = 3,
} drv_ksz8851snl_cb_type_t;

typedef void (*drv_ksz8851snl_callback_t)(void);

typedef struct {
    uint8_t mac_addr[6];
    bool auto_negotiation;
    uint16_t link_speed;     // 10 or 100 Mbps
    bool full_duplex;
} drv_ksz8851snl_config_t;

typedef struct {
    uint16_t chip_id;
    uint16_t revision_id;
    bool spi_communication_ok;
    bool chip_detected;
} drv_ksz8851snl_id_info_t;

typedef struct {
    bool link_up;
    uint16_t link_speed;     // Actual negotiated speed
    bool full_duplex;        // Actual negotiated duplex
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
} drv_ksz8851snl_status_info_t;

typedef struct {
    bool is_init;
    bool is_enabled;
    const void *hw_context;
    
    drv_ksz8851snl_status_t (*init)(const void *hw_context, const drv_ksz8851snl_config_t *config);
    drv_ksz8851snl_status_t (*deinit)(const void *hw_context);
    drv_ksz8851snl_status_t (*enable)(const void *hw_context);
    drv_ksz8851snl_status_t (*disable)(const void *hw_context);
    
    drv_ksz8851snl_status_t (*get_chip_id)(const void *hw_context, drv_ksz8851snl_id_info_t *id_info);
    drv_ksz8851snl_status_t (*get_status)(const void *hw_context, drv_ksz8851snl_status_info_t *status_info);
    
    drv_ksz8851snl_status_t (*send_packet)(const void *hw_context, const uint8_t *data, uint16_t length);
    drv_ksz8851snl_status_t (*receive_packet)(const void *hw_context, uint8_t *data, uint16_t *length);
    drv_ksz8851snl_status_t (*check_rx_available)(const void *hw_context, bool *rx_available);
    
    drv_ksz8851snl_status_t (*register_callback)(const void *hw_context, 
                                                 drv_ksz8851snl_cb_type_t type, 
                                                 drv_ksz8851snl_callback_t callback);
    
    // MAC address configuration
    drv_ksz8851snl_status_t (*set_mac_address)(const void *hw_context, const uint8_t mac_addr[6]);
    drv_ksz8851snl_status_t (*get_mac_address)(const void *hw_context, uint8_t mac_addr[6]);
} drv_ksz8851snl_t;

#ifdef __cplusplus
extern "C" {
#endif

drv_ksz8851snl_status_t hw_ksz8851snl_init(drv_ksz8851snl_t *handle, const drv_ksz8851snl_config_t *config);
drv_ksz8851snl_status_t hw_ksz8851snl_deinit(drv_ksz8851snl_t *handle);
drv_ksz8851snl_status_t hw_ksz8851snl_enable(drv_ksz8851snl_t *handle);
drv_ksz8851snl_status_t hw_ksz8851snl_disable(drv_ksz8851snl_t *handle);

drv_ksz8851snl_status_t hw_ksz8851snl_get_chip_id(drv_ksz8851snl_t *handle, drv_ksz8851snl_id_info_t *id_info);
drv_ksz8851snl_status_t hw_ksz8851snl_get_status(drv_ksz8851snl_t *handle, drv_ksz8851snl_status_info_t *status_info);

drv_ksz8851snl_status_t hw_ksz8851snl_send_packet(drv_ksz8851snl_t *handle, const uint8_t *data, uint16_t length);
drv_ksz8851snl_status_t hw_ksz8851snl_receive_packet(drv_ksz8851snl_t *handle, uint8_t *data, uint16_t *length);
drv_ksz8851snl_status_t hw_ksz8851snl_check_rx_available(drv_ksz8851snl_t *handle, bool *rx_available);

drv_ksz8851snl_status_t hw_ksz8851snl_register_callback(drv_ksz8851snl_t *handle, 
                                                       drv_ksz8851snl_cb_type_t type, 
                                                       drv_ksz8851snl_callback_t callback);

// MAC address configuration
drv_ksz8851snl_status_t hw_ksz8851snl_set_mac_address(drv_ksz8851snl_t *handle, const uint8_t mac_addr[6]);
drv_ksz8851snl_status_t hw_ksz8851snl_get_mac_address(drv_ksz8851snl_t *handle, uint8_t mac_addr[6]);

// MAC address validation helper
bool hw_ksz8851snl_is_mac_valid(const uint8_t mac_addr[6]);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_KSZ8851SNL_H_