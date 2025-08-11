#include "bsp_spi.h"
#include "driver_spi.h"
#include "utils_assert.h"
#include "printf.h"

#include <hal_gpio.h>
#include <hal_spi_m_sync.h>
#include <hpl_spi_m_sync.h>
#include <hri_mclk_e54.h>
#include <hri_gclk_e54.h>
#include <peripheral_clk_config.h>

// Make sure we have SERCOM4 clock configuration
#ifndef CONF_GCLK_SERCOM4_CORE_SRC
#define CONF_GCLK_SERCOM4_CORE_SRC GCLK_PCHCTRL_GEN_GCLK0_Val
#endif

static drv_spi_status_t convert_asf4_error(int32_t asf4_error);

typedef struct {
    struct spi_m_sync_descriptor *spi_desc;
    drv_spi_callback_t transfer_callback;
    drv_spi_callback_t error_callback;
    bool is_spi_init;
} drv_spi_hw_context_t;

static struct spi_m_sync_descriptor spi_4_descriptor;

static drv_spi_hw_context_t drv_spi_hw_context_4 = {
    .spi_desc = &spi_4_descriptor,
    .transfer_callback = NULL,
    .error_callback = NULL,
    .is_spi_init = false,
};

static drv_spi_status_t drv_spi_init_impl(const void *hw_context, const drv_spi_config_t *config);
static drv_spi_status_t drv_spi_deinit_impl(const void *hw_context);
static drv_spi_status_t drv_spi_enable_impl(const void *hw_context);
static drv_spi_status_t drv_spi_disable_impl(const void *hw_context);
static drv_spi_status_t drv_spi_transfer_impl(const void *hw_context, const uint8_t *tx_data, 
                                              uint8_t *rx_data, uint32_t length);
static drv_spi_status_t drv_spi_transfer_async_impl(const void *hw_context, const uint8_t *tx_data, 
                                                    uint8_t *rx_data, uint32_t length);
static drv_spi_status_t drv_spi_set_baudrate_impl(const void *hw_context, uint32_t baudrate);
static drv_spi_status_t drv_spi_register_callback_impl(const void *hw_context, 
                                                       drv_spi_cb_type_t type, 
                                                       drv_spi_callback_t callback);

drv_spi_t spi_4 = {
    .is_init = false,
    .is_enabled = false,
    .hw_context = &drv_spi_hw_context_4,
    .init = drv_spi_init_impl,
    .deinit = drv_spi_deinit_impl,
    .enable = drv_spi_enable_impl,
    .disable = drv_spi_disable_impl,
    .transfer = drv_spi_transfer_impl,
    .transfer_async = drv_spi_transfer_async_impl,
    .set_baudrate = drv_spi_set_baudrate_impl,
    .register_callback = drv_spi_register_callback_impl,
};

static drv_spi_status_t convert_asf4_error(int32_t asf4_error)
{
    switch (asf4_error) {
        case ERR_NONE:
            return DRV_SPI_STATUS_OK;
        case ERR_BUSY:
            return DRV_SPI_STATUS_BUSY;
        case ERR_TIMEOUT:
            return DRV_SPI_STATUS_TIMEOUT;
        case ERR_INVALID_ARG:
        case ERR_BAD_ADDRESS:
            return DRV_SPI_STATUS_INVALID_PARAM;
        default:
            return DRV_SPI_STATUS_ERROR;
    }
}

static void drv_spi_configure_pins(void)
{
    // Configure SERCOM4 pins for SPI
    // PB26 - SCK (PAD1)
    gpio_set_pin_function(GPIO(GPIO_PORTB, 26), PINMUX_PB26D_SERCOM4_PAD1);
    
    // PB27 - MOSI (PAD0)  
    gpio_set_pin_function(GPIO(GPIO_PORTB, 27), PINMUX_PB27D_SERCOM4_PAD0);
    
    // PB28 - SS (PAD2) - This will be managed manually for CS
    gpio_set_pin_function(GPIO(GPIO_PORTB, 28), GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_direction(GPIO(GPIO_PORTB, 28), GPIO_DIRECTION_OUT);
    gpio_set_pin_level(GPIO(GPIO_PORTB, 28), true); // CS idle high
    
    // PB29 - MISO (PAD3)
    gpio_set_pin_function(GPIO(GPIO_PORTB, 29), PINMUX_PB29D_SERCOM4_PAD3);
}

static void drv_spi_configure_sercom4_clock(void)
{
    printf("[SPI4] Configuring SERCOM4 clocks...\r\n");
    
    // Enable SERCOM4 APB clock
    hri_mclk_set_APBDMASK_SERCOM4_bit(MCLK);
    printf("[SPI4] SERCOM4 APB clock enabled\r\n");
    
    // Configure GCLK for SERCOM4 - use same clock as SERCOM2
    hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_CORE, CONF_GCLK_SERCOM4_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
    printf("[SPI4] SERCOM4 core clock configured (GCLK ID: %d, SRC: 0x%02X)\r\n", 
           SERCOM4_GCLK_ID_CORE, CONF_GCLK_SERCOM4_CORE_SRC);
}

static drv_spi_status_t drv_spi_init_impl(const void *hw_context, const drv_spi_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);
    
    const drv_spi_hw_context_t *context = (const drv_spi_hw_context_t *)hw_context;
    
    if (context->is_spi_init) {
        return DRV_SPI_STATUS_OK;
    }
    
    printf("[SPI4] Initializing SERCOM4 SPI controller\r\n");
    
    // Configure clocks and pins
    drv_spi_configure_sercom4_clock();
    drv_spi_configure_pins();
    
    // Initialize SPI with ASF4
    int32_t result = spi_m_sync_init(context->spi_desc, SERCOM4);
    if (result != ERR_NONE) {
        printf("[SPI4] ASF4 spi_m_sync_init failed: %d\r\n", result);
        return convert_asf4_error(result);
    }
    
    // Set SPI mode (clock polarity and phase) - KSZ8851SNL uses Mode 1 (CPOL=0, CPHA=1)
    result = spi_m_sync_set_mode(context->spi_desc, SPI_MODE_1); // Mode 1 for KSZ8851SNL
    if (result != ERR_NONE) {
        printf("[SPI4] Failed to set SPI mode: %d\r\n", result);
        return convert_asf4_error(result);
    }
    
    // Set baudrate
    result = spi_m_sync_set_baudrate(context->spi_desc, config->baudrate);
    if (result != ERR_NONE) {
        printf("[SPI4] Failed to set baudrate %lu: %d\r\n", config->baudrate, result);
        return convert_asf4_error(result);
    }
    
    ((drv_spi_hw_context_t *)context)->is_spi_init = true;
    
    printf("[SPI4] SPI initialized successfully at %lu Hz\r\n", config->baudrate);
    return DRV_SPI_STATUS_OK;
}

static drv_spi_status_t drv_spi_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    const drv_spi_hw_context_t *context = (const drv_spi_hw_context_t *)hw_context;
    
    if (!context->is_spi_init) {
        return DRV_SPI_STATUS_OK;
    }
    
    printf("[SPI4] Deinitializing SPI controller\r\n");
    
    spi_m_sync_deinit(context->spi_desc);
    
    ((drv_spi_hw_context_t *)context)->is_spi_init = false;
    
    printf("[SPI4] SPI deinitialized successfully\r\n");
    return DRV_SPI_STATUS_OK;
}

static drv_spi_status_t drv_spi_enable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    const drv_spi_hw_context_t *context = (const drv_spi_hw_context_t *)hw_context;
    
    if (!context->is_spi_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    printf("[SPI4] Enabling SPI controller\r\n");
    
    spi_m_sync_enable(context->spi_desc);
    
    printf("[SPI4] SPI enabled successfully\r\n");
    return DRV_SPI_STATUS_OK;
}

static drv_spi_status_t drv_spi_disable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    const drv_spi_hw_context_t *context = (const drv_spi_hw_context_t *)hw_context;
    
    if (!context->is_spi_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    printf("[SPI4] Disabling SPI controller\r\n");
    
    spi_m_sync_disable(context->spi_desc);
    
    printf("[SPI4] SPI disabled successfully\r\n");
    return DRV_SPI_STATUS_OK;
}

static drv_spi_status_t drv_spi_transfer_impl(const void *hw_context, const uint8_t *tx_data, 
                                              uint8_t *rx_data, uint32_t length)
{
    ASSERT(hw_context != NULL);
    ASSERT(tx_data != NULL || rx_data != NULL);
    ASSERT(length > 0);
    
    const drv_spi_hw_context_t *context = (const drv_spi_hw_context_t *)hw_context;
    
    if (!context->is_spi_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    struct spi_xfer xfer = {
        .txbuf = (uint8_t *)tx_data,
        .rxbuf = rx_data,
        .size = length
    };
    
    int32_t result = spi_m_sync_transfer(context->spi_desc, &xfer);
    if (result != ERR_NONE) {
        printf("[SPI4] Transfer failed: %d\r\n", result);
        return convert_asf4_error(result);
    }
    
    return DRV_SPI_STATUS_OK;
}

static drv_spi_status_t drv_spi_transfer_async_impl(const void *hw_context, const uint8_t *tx_data, 
                                                    uint8_t *rx_data, uint32_t length)
{
    // For now, just use synchronous transfer
    // TODO: Implement async transfer with callbacks if needed
    return drv_spi_transfer_impl(hw_context, tx_data, rx_data, length);
}

static drv_spi_status_t drv_spi_set_baudrate_impl(const void *hw_context, uint32_t baudrate)
{
    ASSERT(hw_context != NULL);
    ASSERT(baudrate > 0);
    
    const drv_spi_hw_context_t *context = (const drv_spi_hw_context_t *)hw_context;
    
    if (!context->is_spi_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    printf("[SPI4] Setting baudrate to %lu Hz\r\n", baudrate);
    
    int32_t result = spi_m_sync_set_baudrate(context->spi_desc, baudrate);
    if (result != ERR_NONE) {
        printf("[SPI4] Set baudrate failed: %d\r\n", result);
        return convert_asf4_error(result);
    }
    
    printf("[SPI4] Baudrate set successfully\r\n");
    return DRV_SPI_STATUS_OK;
}

static drv_spi_status_t drv_spi_register_callback_impl(const void *hw_context, 
                                                       drv_spi_cb_type_t type, 
                                                       drv_spi_callback_t callback)
{
    ASSERT(hw_context != NULL);
    
    drv_spi_hw_context_t *context = (drv_spi_hw_context_t *)hw_context;
    
    if (!context->is_spi_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    switch (type) {
        case DRV_SPI_CB_TRANSFER_COMPLETE:
            context->transfer_callback = callback;
            break;
        case DRV_SPI_CB_ERROR:
            context->error_callback = callback;
            break;
        default:
            return DRV_SPI_STATUS_INVALID_PARAM;
    }
    
    return DRV_SPI_STATUS_OK;
}

void drv_spi_cs_set_low(void)
{
    gpio_set_pin_level(GPIO(GPIO_PORTB, 28), false);
}

void drv_spi_cs_set_high(void)
{
    gpio_set_pin_level(GPIO(GPIO_PORTB, 28), true);
}