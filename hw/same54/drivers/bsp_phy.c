#include "bsp_phy.h"
#include "driver_phy.h"
#include "hal_mac_async.h"
#include "ieee8023_mii_standard_register.h"
#include "ieee8023_mii_standard_config.h"
#include "utils_assert.h"
#include "printf.h"

// Convert ASF4 error codes to PHY driver status
static drv_phy_status_t convert_error_code(int32_t asf4_error)
{
    switch (asf4_error) {
        case ERR_NONE:
            return DRV_PHY_STATUS_OK;
        case ERR_BUSY:
            return DRV_PHY_STATUS_BUSY;
        case ERR_TIMEOUT:
            return DRV_PHY_STATUS_TIMEOUT;
        default:
            return DRV_PHY_STATUS_ERROR;
    }
}

// Hardware context structure
typedef struct {
    struct mac_async_descriptor *mac_desc;
    uint16_t phy_address;
    drv_phy_callback_t link_change_callback;
    drv_phy_callback_t error_callback;
} drv_phy_hw_context_t;

// External MAC descriptor (provided by BSP Ethernet)
extern struct mac_async_descriptor COMMUNICATION_IO;

// Static hardware context
static drv_phy_hw_context_t drv_phy_hw_context_0 = {
    .mac_desc = &COMMUNICATION_IO,
    .phy_address = CONF_ETHERNET_PHY_0_IEEE8023_MII_PHY_ADDRESS,
    .link_change_callback = NULL,
    .error_callback = NULL,
};

// Forward declarations for implementation functions
static drv_phy_status_t drv_phy_init_impl(const void *hw_context);
static drv_phy_status_t drv_phy_deinit_impl(const void *hw_context);
static drv_phy_status_t drv_phy_enable_impl(const void *hw_context);
static drv_phy_status_t drv_phy_disable_impl(const void *hw_context);
static drv_phy_status_t drv_phy_reset_impl(const void *hw_context);
static drv_phy_status_t drv_phy_get_link_status_impl(const void *hw_context, bool *link_up);
static drv_phy_status_t drv_phy_restart_autoneg_impl(const void *hw_context);
static drv_phy_status_t drv_phy_read_reg_impl(const void *hw_context, uint16_t reg, uint16_t *value);
static drv_phy_status_t drv_phy_write_reg_impl(const void *hw_context, uint16_t reg, uint16_t value);
static drv_phy_status_t drv_phy_set_powerdown_impl(const void *hw_context, bool state);
static drv_phy_status_t drv_phy_set_isolate_impl(const void *hw_context, bool state);
static drv_phy_status_t drv_phy_set_loopback_impl(const void *hw_context, bool state);
static drv_phy_status_t drv_phy_set_reg_bit_impl(const void *hw_context, uint16_t reg, uint16_t mask);
static drv_phy_status_t drv_phy_clear_reg_bit_impl(const void *hw_context, uint16_t reg, uint16_t mask);
static drv_phy_status_t drv_phy_register_callback_impl(const void *hw_context, 
                                                      drv_phy_cb_type_t type, 
                                                      drv_phy_callback_t callback);

// Global PHY driver instance
drv_phy_t phy_0 = {
    .is_init = false,
    .is_enabled = false,
    .hw_context = &drv_phy_hw_context_0,
    .init = drv_phy_init_impl,
    .deinit = drv_phy_deinit_impl,
    .enable = drv_phy_enable_impl,
    .disable = drv_phy_disable_impl,
    .reset = drv_phy_reset_impl,
    .get_link_status = drv_phy_get_link_status_impl,
    .restart_autoneg = drv_phy_restart_autoneg_impl,
    .read_reg = drv_phy_read_reg_impl,
    .write_reg = drv_phy_write_reg_impl,
    .set_powerdown = drv_phy_set_powerdown_impl,
    .set_isolate = drv_phy_set_isolate_impl,
    .set_loopback = drv_phy_set_loopback_impl,
    .set_reg_bit = drv_phy_set_reg_bit_impl,
    .clear_reg_bit = drv_phy_clear_reg_bit_impl,
    .register_callback = drv_phy_register_callback_impl,
};

// Implementation functions
static drv_phy_status_t drv_phy_init_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_phy_hw_context_t *context = (const drv_phy_hw_context_t *)hw_context;
    
    printf("[PHY] Initializing PHY at address 0x%02X\r\n", context->phy_address);
    
    // PHY initialization is just setting up the descriptor
    // Actual hardware initialization happens in enable
    return DRV_PHY_STATUS_OK;
}

static drv_phy_status_t drv_phy_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    printf("[PHY] Deinitializing PHY\r\n");
    return DRV_PHY_STATUS_OK;
}

static drv_phy_status_t drv_phy_enable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_phy_hw_context_t *context = (const drv_phy_hw_context_t *)hw_context;
    
    printf("[PHY] Enabling PHY\r\n");
    
    // Configure PHY with default settings
    int32_t result = mac_async_write_phy_reg(context->mac_desc, context->phy_address,
                                           MDIO_REG0_BMCR, CONF_ETHERNET_PHY_0_IEEE8023_MII_CONTROL_REG0);
    if (result != ERR_NONE) {
        printf("[PHY] Failed to configure PHY control register\r\n");
        return convert_error_code(result);
    }
    
    // Read and display PHY status for debugging
    uint16_t reg_value;
    if (mac_async_read_phy_reg(context->mac_desc, context->phy_address, 0, &reg_value) == ERR_NONE) {
        printf("[PHY] Control Register: 0x%04X\r\n", reg_value);
    }
    if (mac_async_read_phy_reg(context->mac_desc, context->phy_address, 1, &reg_value) == ERR_NONE) {
        printf("[PHY] Status Register: 0x%04X\r\n", reg_value);
    }
    if (mac_async_read_phy_reg(context->mac_desc, context->phy_address, 4, &reg_value) == ERR_NONE) {
        printf("[PHY] Auto-negotiation Advertisement: 0x%04X\r\n", reg_value);
    }
    
    printf("[PHY] PHY enabled successfully\r\n");
    return DRV_PHY_STATUS_OK;
}

static drv_phy_status_t drv_phy_disable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    printf("[PHY] Disabling PHY\r\n");
    
    // Put PHY in power-down mode
    return drv_phy_set_powerdown_impl(hw_context, true);
}

static drv_phy_status_t drv_phy_reset_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_phy_hw_context_t *context = (const drv_phy_hw_context_t *)hw_context;
    
    printf("[PHY] Resetting PHY\r\n");
    
    int32_t result = mac_async_write_phy_reg(context->mac_desc, context->phy_address,
                                           MDIO_REG0_BMCR, MDIO_REG0_BIT_RESET);
    return convert_error_code(result);
}

static drv_phy_status_t drv_phy_get_link_status_impl(const void *hw_context, bool *link_up)
{
    ASSERT(hw_context != NULL);
    ASSERT(link_up != NULL);
    const drv_phy_hw_context_t *context = (const drv_phy_hw_context_t *)hw_context;
    
    uint16_t reg_value;
    int32_t result = mac_async_read_phy_reg(context->mac_desc, context->phy_address,
                                          MDIO_REG1_BMSR, &reg_value);
    if (result == ERR_NONE) {
        *link_up = (reg_value & MDIO_REG1_BIT_LINK_STATUS) ? true : false;
    }
    
    return convert_error_code(result);
}

static drv_phy_status_t drv_phy_restart_autoneg_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    printf("[PHY] Restarting auto-negotiation\r\n");
    
    return drv_phy_set_reg_bit_impl(hw_context, MDIO_REG0_BMCR, MDIO_REG0_BIT_RESTART_AUTONEG);
}

static drv_phy_status_t drv_phy_read_reg_impl(const void *hw_context, uint16_t reg, uint16_t *value)
{
    ASSERT(hw_context != NULL);
    ASSERT(value != NULL);
    ASSERT(reg <= 0x1F);
    const drv_phy_hw_context_t *context = (const drv_phy_hw_context_t *)hw_context;
    
    int32_t result = mac_async_read_phy_reg(context->mac_desc, context->phy_address, reg, value);
    return convert_error_code(result);
}

static drv_phy_status_t drv_phy_write_reg_impl(const void *hw_context, uint16_t reg, uint16_t value)
{
    ASSERT(hw_context != NULL);
    ASSERT(reg <= 0x1F);
    const drv_phy_hw_context_t *context = (const drv_phy_hw_context_t *)hw_context;
    
    int32_t result = mac_async_write_phy_reg(context->mac_desc, context->phy_address, reg, value);
    return convert_error_code(result);
}

static drv_phy_status_t drv_phy_set_powerdown_impl(const void *hw_context, bool state)
{
    ASSERT(hw_context != NULL);
    
    if (state) {
        return drv_phy_set_reg_bit_impl(hw_context, MDIO_REG0_BMCR, MDIO_REG0_BIT_POWER_DOWN);
    } else {
        return drv_phy_clear_reg_bit_impl(hw_context, MDIO_REG0_BMCR, MDIO_REG0_BIT_POWER_DOWN);
    }
}

static drv_phy_status_t drv_phy_set_isolate_impl(const void *hw_context, bool state)
{
    ASSERT(hw_context != NULL);
    
    if (state) {
        return drv_phy_set_reg_bit_impl(hw_context, MDIO_REG0_BMCR, MDIO_REG0_BIT_ISOLATE);
    } else {
        return drv_phy_clear_reg_bit_impl(hw_context, MDIO_REG0_BMCR, MDIO_REG0_BIT_ISOLATE);
    }
}

static drv_phy_status_t drv_phy_set_loopback_impl(const void *hw_context, bool state)
{
    ASSERT(hw_context != NULL);
    
    if (state) {
        return drv_phy_set_reg_bit_impl(hw_context, MDIO_REG0_BMCR, MDIO_REG0_BIT_LOOPBACK);
    } else {
        return drv_phy_clear_reg_bit_impl(hw_context, MDIO_REG0_BMCR, MDIO_REG0_BIT_LOOPBACK);
    }
}

static drv_phy_status_t drv_phy_set_reg_bit_impl(const void *hw_context, uint16_t reg, uint16_t mask)
{
    ASSERT(hw_context != NULL);
    ASSERT(reg <= 0x1F);
    const drv_phy_hw_context_t *context = (const drv_phy_hw_context_t *)hw_context;
    
    uint16_t reg_value;
    int32_t result = mac_async_read_phy_reg(context->mac_desc, context->phy_address, reg, &reg_value);
    if (result == ERR_NONE) {
        reg_value |= mask;
        result = mac_async_write_phy_reg(context->mac_desc, context->phy_address, reg, reg_value);
    }
    
    return convert_error_code(result);
}

static drv_phy_status_t drv_phy_clear_reg_bit_impl(const void *hw_context, uint16_t reg, uint16_t mask)
{
    ASSERT(hw_context != NULL);
    ASSERT(reg <= 0x1F);
    const drv_phy_hw_context_t *context = (const drv_phy_hw_context_t *)hw_context;
    
    uint16_t reg_value;
    int32_t result = mac_async_read_phy_reg(context->mac_desc, context->phy_address, reg, &reg_value);
    if (result == ERR_NONE) {
        reg_value &= ~mask;
        result = mac_async_write_phy_reg(context->mac_desc, context->phy_address, reg, reg_value);
    }
    
    return convert_error_code(result);
}

static drv_phy_status_t drv_phy_register_callback_impl(const void *hw_context, 
                                                      drv_phy_cb_type_t type, 
                                                      drv_phy_callback_t callback)
{
    ASSERT(hw_context != NULL);
    drv_phy_hw_context_t *context = (drv_phy_hw_context_t *)hw_context;
    
    switch (type) {
        case DRV_PHY_CB_LINK_CHANGE:
            context->link_change_callback = callback;
            break;
        case DRV_PHY_CB_ERROR:
            context->error_callback = callback;
            break;
        default:
            return DRV_PHY_STATUS_ERROR;
    }
    
    return DRV_PHY_STATUS_OK;
}