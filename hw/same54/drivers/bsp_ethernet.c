#include "bsp_ethernet.h"
#include "utils_assert.h"
#include "hal_mac_async.h"
#include "hal_gpio.h"
#include "ethernet_phy.h"
#include "printf.h"
#include "ieee8023_mii_standard_config.h"
#include "same54p20a.h"
#include <string.h>
#include <hri_mclk_e54.h>
#include <hri_gclk_e54.h>

/* Local pin definitions (copied from atmel_start_pins.h to avoid dependency) */
#define PA12 GPIO(GPIO_PORTA, 12)
#define PA13 GPIO(GPIO_PORTA, 13)
#define PA14 GPIO(GPIO_PORTA, 14)
#define PA15 GPIO(GPIO_PORTA, 15)
#define PA17 GPIO(GPIO_PORTA, 17)
#define PA18 GPIO(GPIO_PORTA, 18)
#define PA19 GPIO(GPIO_PORTA, 19)
#define PC11 GPIO(GPIO_PORTC, 11)
#define PC12 GPIO(GPIO_PORTC, 12)
#define PC20 GPIO(GPIO_PORTC, 20)

/* Local PIN and MUX definitions for GMAC (extracted from same54p20a.h) */
#define PIN_PC11L_GMAC_GMDC            _L_(75)
#define MUX_PC11L_GMAC_GMDC            _L_(11)
#define PIN_PC12L_GMAC_GMDIO           _L_(76)
#define MUX_PC12L_GMAC_GMDIO           _L_(11)
#define PIN_PA13L_GMAC_GRX0            _L_(13)
#define MUX_PA13L_GMAC_GRX0            _L_(11)
#define PIN_PA12L_GMAC_GRX1            _L_(12)
#define MUX_PA12L_GMAC_GRX1            _L_(11)
#define PIN_PC20L_GMAC_GRXDV           _L_(84)
#define MUX_PC20L_GMAC_GRXDV           _L_(11)
#define PIN_PA15L_GMAC_GRXER           _L_(15)
#define MUX_PA15L_GMAC_GRXER           _L_(11)
#define PIN_PA18L_GMAC_GTX0            _L_(18)
#define MUX_PA18L_GMAC_GTX0            _L_(11)
#define PIN_PA19L_GMAC_GTX1            _L_(19)
#define MUX_PA19L_GMAC_GTX1            _L_(11)
#define PIN_PA14L_GMAC_GTXCK           _L_(14)
#define MUX_PA14L_GMAC_GTXCK           _L_(11)
#define PIN_PA17L_GMAC_GTXEN           _L_(17)
#define MUX_PA17L_GMAC_GTXEN           _L_(11)

/* Local PINMUX definitions */
#define PINMUX_PC11L_GMAC_GMDC     ((PIN_PC11L_GMAC_GMDC << 16) | MUX_PC11L_GMAC_GMDC)
#define PINMUX_PC12L_GMAC_GMDIO    ((PIN_PC12L_GMAC_GMDIO << 16) | MUX_PC12L_GMAC_GMDIO)
#define PINMUX_PA13L_GMAC_GRX0     ((PIN_PA13L_GMAC_GRX0 << 16) | MUX_PA13L_GMAC_GRX0)
#define PINMUX_PA12L_GMAC_GRX1     ((PIN_PA12L_GMAC_GRX1 << 16) | MUX_PA12L_GMAC_GRX1)
#define PINMUX_PC20L_GMAC_GRXDV    ((PIN_PC20L_GMAC_GRXDV << 16) | MUX_PC20L_GMAC_GRXDV)
#define PINMUX_PA15L_GMAC_GRXER    ((PIN_PA15L_GMAC_GRXER << 16) | MUX_PA15L_GMAC_GRXER)
#define PINMUX_PA18L_GMAC_GTX0     ((PIN_PA18L_GMAC_GTX0 << 16) | MUX_PA18L_GMAC_GTX0)
#define PINMUX_PA19L_GMAC_GTX1     ((PIN_PA19L_GMAC_GTX1 << 16) | MUX_PA19L_GMAC_GTX1)
#define PINMUX_PA14L_GMAC_GTXCK    ((PIN_PA14L_GMAC_GTXCK << 16) | MUX_PA14L_GMAC_GTXCK)
#define PINMUX_PA17L_GMAC_GTXEN    ((PIN_PA17L_GMAC_GTXEN << 16) | MUX_PA17L_GMAC_GTXEN)

/* External peripheral descriptors */
extern struct mac_async_descriptor COMMUNICATION_IO;

/* PHY descriptor - previously in ethernet_phy_main.c */
struct ethernet_phy_descriptor ETHERNET_PHY_0_desc;

// Convert ASF4 error codes to driver status
static drv_eth_status_t convert_error_code(int32_t asf4_error)
{
    switch (asf4_error) {
        case ERR_NONE:
            return DRV_ETH_STATUS_OK;
        case ERR_BUSY:
            return DRV_ETH_STATUS_BUSY;
        case ERR_TIMEOUT:
            return DRV_ETH_STATUS_TIMEOUT;
        default:
            return DRV_ETH_STATUS_ERROR;
    }
}

typedef struct
{
    struct mac_async_descriptor *mac_desc;
    struct ethernet_phy_descriptor *phy_desc;
    uint8_t phy_address;
    drv_eth_callback_t receive_callback;
    drv_eth_callback_t transmit_callback;
} drv_eth_hw_context_t;

static drv_eth_hw_context_t drv_eth_hw_context_communication = {
    .mac_desc = &COMMUNICATION_IO,
    .phy_desc = &ETHERNET_PHY_0_desc,
    .phy_address = CONF_ETHERNET_PHY_0_IEEE8023_MII_PHY_ADDRESS,
    .receive_callback = NULL,
    .transmit_callback = NULL,
};

// Forward declarations of static functions
static void gmac_clock_init(void);
static void gmac_pin_init(void);
static drv_eth_status_t drv_eth_init(const void *hw_context);
static drv_eth_status_t drv_eth_deinit(const void *hw_context);
static drv_eth_status_t drv_eth_enable(const void *hw_context);
static drv_eth_status_t drv_eth_disable(const void *hw_context);
static drv_eth_status_t drv_eth_phy_init(const void *hw_context);
static drv_eth_status_t drv_eth_phy_reset(const void *hw_context);
static drv_eth_status_t drv_eth_get_link_status(const void *hw_context, bool *link_up);
static drv_eth_status_t drv_eth_restart_autoneg(const void *hw_context);
static drv_eth_status_t drv_eth_read_phy_reg(const void *hw_context, uint16_t reg, uint16_t *value);
static drv_eth_status_t drv_eth_write_phy_reg(const void *hw_context, uint16_t reg, uint16_t value);
static drv_eth_status_t drv_eth_register_callback(const void *hw_context, drv_eth_cb_type_t type, drv_eth_callback_t callback);
static drv_eth_status_t drv_eth_write(const void *hw_context, const uint8_t *data, uint32_t length);

// Global Ethernet driver instance
drv_eth_t eth_communication = {
    .is_init = false,
    .is_enabled = false,
    .hw_context = &drv_eth_hw_context_communication,
    .init = drv_eth_init,
    .deinit = drv_eth_deinit,
    .enable = drv_eth_enable,
    .disable = drv_eth_disable,
    .phy_init = drv_eth_phy_init,
    .phy_reset = drv_eth_phy_reset,
    .get_link_status = drv_eth_get_link_status,
    .restart_autoneg = drv_eth_restart_autoneg,
    .read_phy_reg = drv_eth_read_phy_reg,
    .write_phy_reg = drv_eth_write_phy_reg,
    .register_callback = drv_eth_register_callback,
    .write = drv_eth_write,
};

static drv_eth_status_t drv_eth_init(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    printf("[ETH] Initializing GMAC clocks and pins\r\n");
    
    // Initialize GMAC clocks first
    gmac_clock_init();
    
    // Configure GMAC pins
    gmac_pin_init();
    
    // Initialize MAC
    int32_t result = mac_async_init(context->mac_desc, GMAC);
    if (result != ERR_NONE) {
        return convert_error_code(result);
    }
    
    printf("[ETH] MAC initialized successfully\r\n");
    return DRV_ETH_STATUS_OK;
}

static drv_eth_status_t drv_eth_deinit(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = mac_async_deinit(context->mac_desc);
    printf("[ETH] MAC deinitialized\r\n");
    return convert_error_code(result);
}

static drv_eth_status_t drv_eth_enable(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = mac_async_enable(context->mac_desc);
    if (result == ERR_NONE) {
        printf("[ETH] MAC enabled\r\n");
    }
    return convert_error_code(result);
}

static drv_eth_status_t drv_eth_disable(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = mac_async_disable(context->mac_desc);
    printf("[ETH] MAC disabled\r\n");
    return convert_error_code(result);
}

static drv_eth_status_t drv_eth_phy_init(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    // Initialize PHY
    int32_t result = ethernet_phy_init(context->phy_desc, context->mac_desc, context->phy_address);
    if (result != ERR_NONE) {
        return convert_error_code(result);
    }
    
    printf("[ETH] PHY initialized at address %d\r\n", context->phy_address);
    
#if CONF_ETHERNET_PHY_0_IEEE8023_MII_CONTROL_REG0_SETTING == 1
    printf("[ETH] Writing PHY control register: 0x%04X\r\n", CONF_ETHERNET_PHY_0_IEEE8023_MII_CONTROL_REG0);
    result = ethernet_phy_write_reg(context->phy_desc, MDIO_REG0_BMCR, CONF_ETHERNET_PHY_0_IEEE8023_MII_CONTROL_REG0);
    if (result != ERR_NONE) {
        return convert_error_code(result);
    }
#endif
    
    printf("[ETH] PHY initialized, auto-negotiation will start naturally\r\n");
    
    // Read and display PHY registers for debugging
    uint16_t reg_value;
    if (ethernet_phy_read_reg(context->phy_desc, 0, &reg_value) == ERR_NONE) {
        printf("[ETH] PHY Control Register (0): 0x%04X\r\n", reg_value);
    }
    if (ethernet_phy_read_reg(context->phy_desc, 1, &reg_value) == ERR_NONE) {
        printf("[ETH] PHY Status Register (1): 0x%04X\r\n", reg_value);
    }
    if (ethernet_phy_read_reg(context->phy_desc, 4, &reg_value) == ERR_NONE) {
        printf("[ETH] PHY Auto-Negotiation Advertisement (4): 0x%04X\r\n", reg_value);
    }
    
    return DRV_ETH_STATUS_OK;
}

static drv_eth_status_t drv_eth_phy_reset(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = ethernet_phy_reset(context->phy_desc);
    if (result == ERR_NONE) {
        printf("[ETH] PHY reset completed\r\n");
    }
    return convert_error_code(result);
}

static drv_eth_status_t drv_eth_get_link_status(const void *hw_context, bool *link_up)
{
    ASSERT(hw_context != NULL);
    ASSERT(link_up != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = ethernet_phy_get_link_status(context->phy_desc, link_up);
    return convert_error_code(result);
}

static drv_eth_status_t drv_eth_restart_autoneg(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = ethernet_phy_restart_autoneg(context->phy_desc);
    if (result == ERR_NONE) {
        printf("[ETH] Auto-negotiation restarted\r\n");
    }
    return convert_error_code(result);
}

static drv_eth_status_t drv_eth_read_phy_reg(const void *hw_context, uint16_t reg, uint16_t *value)
{
    ASSERT(hw_context != NULL);
    ASSERT(value != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = ethernet_phy_read_reg(context->phy_desc, reg, value);
    return convert_error_code(result);
}

static drv_eth_status_t drv_eth_write_phy_reg(const void *hw_context, uint16_t reg, uint16_t value)
{
    ASSERT(hw_context != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = ethernet_phy_write_reg(context->phy_desc, reg, value);
    return convert_error_code(result);
}

static drv_eth_status_t drv_eth_register_callback(const void *hw_context, drv_eth_cb_type_t type, drv_eth_callback_t callback)
{
    ASSERT(hw_context != NULL);
    drv_eth_hw_context_t *context = (drv_eth_hw_context_t *)hw_context;
    
    // Store callback for future use
    if (type == DRV_ETH_CB_RECEIVE) {
        context->receive_callback = callback;
        // Register with ASF4 MAC
        int32_t result = mac_async_register_callback(context->mac_desc, MAC_ASYNC_RECEIVE_CB, (FUNC_PTR)callback);
        return convert_error_code(result);
    } else if (type == DRV_ETH_CB_TRANSMIT) {
        context->transmit_callback = callback;
        // Register with ASF4 MAC  
        int32_t result = mac_async_register_callback(context->mac_desc, MAC_ASYNC_TRANSMIT_CB, (FUNC_PTR)callback);
        return convert_error_code(result);
    }
    
    return DRV_ETH_STATUS_ERROR;
}

static drv_eth_status_t drv_eth_write(const void *hw_context, const uint8_t *data, uint32_t length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    const drv_eth_hw_context_t *context = (const drv_eth_hw_context_t *)hw_context;
    
    int32_t result = mac_async_write(context->mac_desc, (uint8_t *)data, length);
    return convert_error_code(result);
}

/* GMAC hardware initialization functions */

static void gmac_clock_init(void)
{
    hri_mclk_set_AHBMASK_GMAC_bit(MCLK);
    hri_mclk_set_APBCMASK_GMAC_bit(MCLK);
    printf("[ETH] GMAC clocks enabled\r\n");
}

static void gmac_pin_init(void)
{
    gpio_set_pin_function(PC11, PINMUX_PC11L_GMAC_GMDC);
    gpio_set_pin_function(PC12, PINMUX_PC12L_GMAC_GMDIO);
    gpio_set_pin_function(PA13, PINMUX_PA13L_GMAC_GRX0);
    gpio_set_pin_function(PA12, PINMUX_PA12L_GMAC_GRX1);
    gpio_set_pin_function(PC20, PINMUX_PC20L_GMAC_GRXDV);
    gpio_set_pin_function(PA15, PINMUX_PA15L_GMAC_GRXER);
    gpio_set_pin_function(PA18, PINMUX_PA18L_GMAC_GTX0);
    gpio_set_pin_function(PA19, PINMUX_PA19L_GMAC_GTX1);
    gpio_set_pin_function(PA14, PINMUX_PA14L_GMAC_GTXCK);
    gpio_set_pin_function(PA17, PINMUX_PA17L_GMAC_GTXEN);
    printf("[ETH] GMAC pins configured\r\n");
}