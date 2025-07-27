#ifndef _DRIVER_PHY_H_
#define _DRIVER_PHY_H_

#include <stdbool.h>
#include <stdint.h>

// Status enumeration
typedef enum {
    DRV_PHY_STATUS_OK = 0,
    DRV_PHY_STATUS_ERROR = 1,
    DRV_PHY_STATUS_BUSY = 2,
    DRV_PHY_STATUS_TIMEOUT = 3,
} drv_phy_status_t;

// Callback types
typedef enum {
    DRV_PHY_CB_LINK_CHANGE = 0,
    DRV_PHY_CB_ERROR = 1,
} drv_phy_cb_type_t;

typedef void (*drv_phy_callback_t)(void);

// Driver structure with function pointers
typedef struct {
    bool is_init;
    bool is_enabled;
    const void *hw_context;
    
    // Core operations
    drv_phy_status_t (*init)(const void *hw_context);
    drv_phy_status_t (*deinit)(const void *hw_context);
    drv_phy_status_t (*enable)(const void *hw_context);
    drv_phy_status_t (*disable)(const void *hw_context);
    
    // PHY control operations
    drv_phy_status_t (*reset)(const void *hw_context);
    drv_phy_status_t (*get_link_status)(const void *hw_context, bool *link_up);
    drv_phy_status_t (*restart_autoneg)(const void *hw_context);
    
    // Register access
    drv_phy_status_t (*read_reg)(const void *hw_context, uint16_t reg, uint16_t *value);
    drv_phy_status_t (*write_reg)(const void *hw_context, uint16_t reg, uint16_t value);
    
    // Power management
    drv_phy_status_t (*set_powerdown)(const void *hw_context, bool state);
    drv_phy_status_t (*set_isolate)(const void *hw_context, bool state);
    drv_phy_status_t (*set_loopback)(const void *hw_context, bool state);
    
    // Advanced operations
    drv_phy_status_t (*set_reg_bit)(const void *hw_context, uint16_t reg, uint16_t mask);
    drv_phy_status_t (*clear_reg_bit)(const void *hw_context, uint16_t reg, uint16_t mask);
    
    // Callback management
    drv_phy_status_t (*register_callback)(const void *hw_context, 
                                         drv_phy_cb_type_t type, 
                                         drv_phy_callback_t callback);
} drv_phy_t;

#ifdef __cplusplus
extern "C" {
#endif

// Universal API functions
drv_phy_status_t hw_phy_init(drv_phy_t *handle);
drv_phy_status_t hw_phy_deinit(drv_phy_t *handle);
drv_phy_status_t hw_phy_enable(drv_phy_t *handle);
drv_phy_status_t hw_phy_disable(drv_phy_t *handle);

// PHY control functions
drv_phy_status_t hw_phy_reset(drv_phy_t *handle);
drv_phy_status_t hw_phy_get_link_status(drv_phy_t *handle, bool *link_up);
drv_phy_status_t hw_phy_restart_autoneg(drv_phy_t *handle);

// Register access functions
drv_phy_status_t hw_phy_read_reg(drv_phy_t *handle, uint16_t reg, uint16_t *value);
drv_phy_status_t hw_phy_write_reg(drv_phy_t *handle, uint16_t reg, uint16_t value);

// Power management functions
drv_phy_status_t hw_phy_set_powerdown(drv_phy_t *handle, bool state);
drv_phy_status_t hw_phy_set_isolate(drv_phy_t *handle, bool state);
drv_phy_status_t hw_phy_set_loopback(drv_phy_t *handle, bool state);

// Advanced register operations
drv_phy_status_t hw_phy_set_reg_bit(drv_phy_t *handle, uint16_t reg, uint16_t mask);
drv_phy_status_t hw_phy_clear_reg_bit(drv_phy_t *handle, uint16_t reg, uint16_t mask);

// Callback management
drv_phy_status_t hw_phy_register_callback(drv_phy_t *handle, 
                                         drv_phy_cb_type_t type, 
                                         drv_phy_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_PHY_H_