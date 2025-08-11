#include "bsp_ksz8851snl.h"
#include "driver_ksz8851snl.h"
#include "bsp_spi.h"
#include "ksz8851snl_config.h"
#include "utils_assert.h"
#include "printf.h"
#include "FreeRTOS.h"
#include "task.h"

#include <hal_gpio.h>
#include <hal_delay.h>
#include <string.h>

// Include existing register definitions
#include "app_libs/FreeRTOS-Plus-TCP/source/portable/NetworkInterface/ksz8851snl/ksz8851snl_reg.h"

// Hardware context structure
typedef struct {
    drv_ksz8851snl_callback_t rx_callback;
    drv_ksz8851snl_callback_t tx_callback;
    drv_ksz8851snl_callback_t link_callback;
    drv_ksz8851snl_callback_t error_callback;
    bool is_initialized;
    bool is_enabled;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
} drv_ksz8851snl_hw_context_t;

static drv_ksz8851snl_hw_context_t drv_ksz8851snl_hw_context_0 = {
    .rx_callback = NULL,
    .tx_callback = NULL,
    .link_callback = NULL,
    .error_callback = NULL,
    .is_initialized = false,
    .is_enabled = false,
    .rx_packets = 0,
    .tx_packets = 0,
    .rx_errors = 0,
    .tx_errors = 0,
};

// Forward declarations
static drv_ksz8851snl_status_t drv_ksz8851snl_init_impl(const void *hw_context, const drv_ksz8851snl_config_t *config);
static drv_ksz8851snl_status_t drv_ksz8851snl_deinit_impl(const void *hw_context);
static drv_ksz8851snl_status_t drv_ksz8851snl_enable_impl(const void *hw_context);
static drv_ksz8851snl_status_t drv_ksz8851snl_disable_impl(const void *hw_context);
static drv_ksz8851snl_status_t drv_ksz8851snl_get_chip_id_impl(const void *hw_context, drv_ksz8851snl_id_info_t *id_info);
static drv_ksz8851snl_status_t drv_ksz8851snl_get_status_impl(const void *hw_context, drv_ksz8851snl_status_info_t *status_info);
static drv_ksz8851snl_status_t drv_ksz8851snl_send_packet_impl(const void *hw_context, const uint8_t *data, uint16_t length);
static drv_ksz8851snl_status_t drv_ksz8851snl_receive_packet_impl(const void *hw_context, uint8_t *data, uint16_t *length);
static drv_ksz8851snl_status_t drv_ksz8851snl_check_rx_available_impl(const void *hw_context, bool *rx_available);
static drv_ksz8851snl_status_t drv_ksz8851snl_register_callback_impl(const void *hw_context, drv_ksz8851snl_cb_type_t type, drv_ksz8851snl_callback_t callback);

// Global driver instance
drv_ksz8851snl_t ksz8851snl_0 = {
    .is_init = false,
    .is_enabled = false,
    .hw_context = &drv_ksz8851snl_hw_context_0,
    .init = drv_ksz8851snl_init_impl,
    .deinit = drv_ksz8851snl_deinit_impl,
    .enable = drv_ksz8851snl_enable_impl,
    .disable = drv_ksz8851snl_disable_impl,
    .get_chip_id = drv_ksz8851snl_get_chip_id_impl,
    .get_status = drv_ksz8851snl_get_status_impl,
    .send_packet = drv_ksz8851snl_send_packet_impl,
    .receive_packet = drv_ksz8851snl_receive_packet_impl,
    .check_rx_available = drv_ksz8851snl_check_rx_available_impl,
    .register_callback = drv_ksz8851snl_register_callback_impl,
};

// SPI communication helper functions (adapted from existing FreeRTOS driver)
static uint16_t ksz8851_reg_read(uint16_t reg)
{
    uint16_t cmd = 0;
    uint8_t cmd_buf[4];
    uint8_t resp_buf[4] = {0};
    
    printf("[KSZ8851SNL] Reading register 0x%02X\r\n", reg);
    
    // Move register address to cmd bits 9-2, make 32-bit address
    cmd = (reg << 2) & REG_ADDR_MASK;
    
    // Select byte enable for command
    if (reg & 2) {
        // Odd word address writes bytes 2 and 3
        cmd |= (0xc << 10);
    } else {
        // Even word address write bytes 0 and 1
        cmd |= (0x3 << 10);
    }
    
    // Add command read code
    cmd |= CMD_READ;
    
    cmd_buf[0] = cmd >> 8;
    cmd_buf[1] = cmd & 0xff;
    cmd_buf[2] = 0x00; // Dummy bytes
    cmd_buf[3] = 0x00;
    
    printf("[KSZ8851SNL] SPI CMD: [0x%02X 0x%02X 0x%02X 0x%02X]\r\n", 
           cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
    
    // Perform SPI transfer
    drv_spi_cs_set_low();
    drv_spi_status_t status = hw_spi_transfer(&spi_4, cmd_buf, resp_buf, 4);
    drv_spi_cs_set_high();
    
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI read error: %d\r\n", status);
        return 0xFFFF;
    }
    
    printf("[KSZ8851SNL] SPI RSP: [0x%02X 0x%02X 0x%02X 0x%02X]\r\n", 
           resp_buf[0], resp_buf[1], resp_buf[2], resp_buf[3]);
    
    uint16_t result = (resp_buf[3] << 8) | resp_buf[2];
    printf("[KSZ8851SNL] Register 0x%02X = 0x%04X\r\n", reg, result);
    return result;
}

static void ksz8851_reg_write(uint16_t reg, uint16_t wrdata)
{
    uint16_t cmd = 0;
    uint8_t cmd_buf[4];
    uint8_t resp_buf[4];
    
    // Move register address to cmd bits 9-2, make 32-bit address
    cmd = (reg << 2) & REG_ADDR_MASK;
    
    // Select byte enable for command
    if (reg & 2) {
        // Odd word address writes bytes 2 and 3
        cmd |= (0xc << 10);
    } else {
        // Even word address write bytes 0 and 1
        cmd |= (0x3 << 10);
    }
    
    // Add command write code
    cmd |= CMD_WRITE;
    
    cmd_buf[0] = cmd >> 8;
    cmd_buf[1] = cmd & 0xff;
    cmd_buf[2] = wrdata & 0xff;
    cmd_buf[3] = wrdata >> 8;
    
    // Perform SPI transfer
    drv_spi_cs_set_low();
    drv_spi_status_t status = hw_spi_transfer(&spi_4, cmd_buf, resp_buf, 4);
    drv_spi_cs_set_high();
    
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI write error: %d\r\n", status);
    }
}

static void ksz8851_reg_setbits(uint16_t reg, uint16_t bits_to_set)
{
    uint16_t temp = ksz8851_reg_read(reg);
    temp |= bits_to_set;
    ksz8851_reg_write(reg, temp);
}

static void ksz8851_reg_clrbits(uint16_t reg, uint16_t bits_to_clr)
{
    uint16_t temp = ksz8851_reg_read(reg);
    temp &= ~(uint32_t)bits_to_clr;
    ksz8851_reg_write(reg, temp);
}

static void drv_ksz8851snl_configure_pins(void)
{
    // Configure control pins
    gpio_set_pin_direction(KSZ8851SNL_RESET_PIN, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(KSZ8851SNL_RESET_PIN, true); // Reset inactive
    
    gpio_set_pin_direction(KSZ8851SNL_CE_PIN, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(KSZ8851SNL_CE_PIN, true); // Chip enable active
    
    gpio_set_pin_direction(KSZ8851SNL_INT_PIN, GPIO_DIRECTION_IN);
    gpio_set_pin_pull_mode(KSZ8851SNL_INT_PIN, GPIO_PULL_UP);
}

static drv_ksz8851snl_status_t drv_ksz8851snl_hardware_reset(void)
{
    printf("[KSZ8851SNL] Performing hardware reset\r\n");
    
    // Perform hardware reset with proper timing
    gpio_set_pin_level(KSZ8851SNL_RESET_PIN, false); // Assert reset
    vTaskDelay(pdMS_TO_TICKS(KSZ8851SNL_RESET_DELAY_MS));
    
    gpio_set_pin_level(KSZ8851SNL_RESET_PIN, true);  // Release reset
    vTaskDelay(pdMS_TO_TICKS(KSZ8851SNL_POWERUP_DELAY_MS));
    
    printf("[KSZ8851SNL] Hardware reset completed\r\n");
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_init_impl(const void *hw_context, const drv_ksz8851snl_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);
    
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    
    if (context->is_initialized) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    printf("[KSZ8851SNL] Initializing KSZ8851SNL Ethernet controller\r\n");
    
    // Configure pins
    drv_ksz8851snl_configure_pins();
    
    // Initialize SPI first
    drv_spi_config_t spi_config = {
        .baudrate = KSZ8851SNL_SPI_CLOCK_SPEED,
        .clock_polarity = KSZ8851SNL_SPI_CLOCK_POLARITY,
        .clock_phase = KSZ8851SNL_SPI_CLOCK_PHASE,
        .bits_per_transfer = KSZ8851SNL_SPI_BITS_PER_TRANSFER,
        .cs_delay_before = 0,
        .cs_delay_after = 0,
    };
    
    drv_spi_status_t spi_status = hw_spi_init(&spi_4, &spi_config);
    if (spi_status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI initialization failed: %d\r\n", spi_status);
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    spi_status = hw_spi_enable(&spi_4);
    if (spi_status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI enable failed: %d\r\n", spi_status);
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Perform hardware reset
    drv_ksz8851snl_status_t reset_status = drv_ksz8851snl_hardware_reset();
    if (reset_status != DRV_KSZ8851SNL_STATUS_OK) {
        return reset_status;
    }
    
    // Test chip ID first (SPI communication test)
    drv_ksz8851snl_id_info_t id_info;
    drv_ksz8851snl_status_t id_status = drv_ksz8851snl_get_chip_id_impl(hw_context, &id_info);
    if (id_status != DRV_KSZ8851SNL_STATUS_OK || !id_info.chip_detected) {
        printf("[KSZ8851SNL] Chip ID verification failed\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    printf("[KSZ8851SNL] Chip ID verified: 0x%04X\r\n", id_info.chip_id);
    
    // TODO: Add full initialization sequence from existing FreeRTOS driver
    // This would include MAC address setup, TX/RX configuration, etc.
    
    context->is_initialized = true;
    printf("[KSZ8851SNL] KSZ8851SNL initialization completed successfully\r\n");
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    printf("[KSZ8851SNL] Deinitializing KSZ8851SNL controller\r\n");
    
    // Disable SPI
    hw_spi_disable(&spi_4);
    hw_spi_deinit(&spi_4);
    
    // Reset control pins
    gpio_set_pin_level(KSZ8851SNL_RESET_PIN, false);
    gpio_set_pin_level(KSZ8851SNL_CE_PIN, false);
    
    context->is_initialized = false;
    context->is_enabled = false;
    
    printf("[KSZ8851SNL] KSZ8851SNL deinitialized successfully\r\n");
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_enable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    if (context->is_enabled) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    printf("[KSZ8851SNL] Enabling KSZ8851SNL controller\r\n");
    
    // TODO: Enable TX/RX operations
    
    context->is_enabled = true;
    printf("[KSZ8851SNL] KSZ8851SNL enabled successfully\r\n");
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_disable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    if (!context->is_enabled) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    printf("[KSZ8851SNL] Disabling KSZ8851SNL controller\r\n");
    
    // TODO: Disable TX/RX operations
    
    context->is_enabled = false;
    printf("[KSZ8851SNL] KSZ8851SNL disabled successfully\r\n");
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_get_chip_id_impl(const void *hw_context, drv_ksz8851snl_id_info_t *id_info)
{
    ASSERT(hw_context != NULL);
    ASSERT(id_info != NULL);
    
    printf("[KSZ8851SNL] Reading chip ID for SPI communication test\r\n");
    
    // Initialize SPI first if not already done
    printf("[KSZ8851SNL] Ensuring SPI is initialized...\r\n");
    drv_spi_config_t spi_config = {
        .baudrate = 1000000  // 1MHz for testing
    };
    
    drv_spi_status_t spi_status = hw_spi_init(&spi_4, &spi_config);
    if (spi_status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI init failed: %d\r\n", spi_status);
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    spi_status = hw_spi_enable(&spi_4);
    if (spi_status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI enable failed: %d\r\n", spi_status);
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    printf("[KSZ8851SNL] SPI initialized and enabled\r\n");
    
    // Initialize result structure
    memset(id_info, 0, sizeof(drv_ksz8851snl_id_info_t));
    
    // Read chip ID register multiple times to verify stable communication
    uint16_t chip_id_1 = ksz8851_reg_read(REG_CHIP_ID);
    vTaskDelay(1); // Small delay
    uint16_t chip_id_2 = ksz8851_reg_read(REG_CHIP_ID);
    vTaskDelay(1); // Small delay  
    uint16_t chip_id_3 = ksz8851_reg_read(REG_CHIP_ID);
    
    printf("[KSZ8851SNL] Chip ID readings: 0x%04X, 0x%04X, 0x%04X\r\n", chip_id_1, chip_id_2, chip_id_3);
    
    // Check if all readings are consistent (SPI communication OK)
    if (chip_id_1 == chip_id_2 && chip_id_2 == chip_id_3 && chip_id_1 != 0xFFFF && chip_id_1 != 0x0000) {
        id_info->spi_communication_ok = true;
        id_info->chip_id = chip_id_1;
        id_info->revision_id = chip_id_1 & 0x000F; // Lower 4 bits are revision
        
        // Check if chip ID matches expected value
        if ((chip_id_1 & KSZ8851SNL_CHIP_ID_MASK) == KSZ8851SNL_CHIP_ID_EXPECTED) {
            id_info->chip_detected = true;
            printf("[KSZ8851SNL] Valid KSZ8851SNL chip detected (ID: 0x%04X, Rev: %d)\r\n", 
                   id_info->chip_id, id_info->revision_id);
        } else {
            printf("[KSZ8851SNL] Unexpected chip ID: 0x%04X (expected: 0x%04X)\r\n", 
                   chip_id_1, KSZ8851SNL_CHIP_ID_EXPECTED);
            id_info->chip_detected = false;
        }
    } else {
        printf("[KSZ8851SNL] Inconsistent chip ID readings - SPI communication failed\r\n");
        id_info->spi_communication_ok = false;
        id_info->chip_detected = false;
    }
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_get_status_impl(const void *hw_context, drv_ksz8851snl_status_info_t *status_info)
{
    ASSERT(hw_context != NULL);
    ASSERT(status_info != NULL);
    
    const drv_ksz8851snl_hw_context_t *context = (const drv_ksz8851snl_hw_context_t *)hw_context;
    
    // Initialize status structure
    memset(status_info, 0, sizeof(drv_ksz8851snl_status_info_t));
    
    // TODO: Read actual status from chip registers
    status_info->link_up = true;        // Placeholder
    status_info->link_speed = 100;      // Placeholder
    status_info->full_duplex = true;    // Placeholder
    status_info->rx_packets = context->rx_packets;
    status_info->tx_packets = context->tx_packets;
    status_info->rx_errors = context->rx_errors;
    status_info->tx_errors = context->tx_errors;
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

// Placeholder implementations for packet operations
static drv_ksz8851snl_status_t drv_ksz8851snl_send_packet_impl(const void *hw_context, const uint8_t *data, uint16_t length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);
    
    // TODO: Implement packet transmission using existing FIFO write functions
    printf("[KSZ8851SNL] Send packet - length: %d (TODO: implement)\r\n", length);
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_receive_packet_impl(const void *hw_context, uint8_t *data, uint16_t *length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length != NULL);
    
    // TODO: Implement packet reception using existing FIFO read functions
    printf("[KSZ8851SNL] Receive packet (TODO: implement)\r\n");
    
    *length = 0;
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_check_rx_available_impl(const void *hw_context, bool *rx_available)
{
    ASSERT(hw_context != NULL);
    ASSERT(rx_available != NULL);
    
    // TODO: Check interrupt status or RX FIFO status
    *rx_available = false;
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_register_callback_impl(const void *hw_context, drv_ksz8851snl_cb_type_t type, drv_ksz8851snl_callback_t callback)
{
    ASSERT(hw_context != NULL);
    
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    
    switch (type) {
        case DRV_KSZ8851SNL_CB_RX_COMPLETE:
            context->rx_callback = callback;
            break;
        case DRV_KSZ8851SNL_CB_TX_COMPLETE:
            context->tx_callback = callback;
            break;
        case DRV_KSZ8851SNL_CB_LINK_CHANGE:
            context->link_callback = callback;
            break;
        case DRV_KSZ8851SNL_CB_ERROR:
            context->error_callback = callback;
            break;
        default:
            return DRV_KSZ8851SNL_STATUS_INVALID_PARAM;
    }
    
    return DRV_KSZ8851SNL_STATUS_OK;
}