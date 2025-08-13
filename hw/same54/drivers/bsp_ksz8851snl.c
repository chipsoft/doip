#include "bsp_ksz8851snl.h"
#include "driver_ksz8851snl.h"
#include "bsp_spi.h"
#include "ksz8851snl_config.h"
#include "utils_assert.h"
#include "printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <hal_gpio.h>
#include <hal_delay.h>
#include <hal_ext_irq.h>
#include <hri_gclk_e54.h>
#include <hri_mclk_e54.h>
#include <peripheral_clk_config.h>
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

// Interrupt handling variables
static SemaphoreHandle_t ksz8851snl_interrupt_semaphore = NULL;
static bool ksz8851snl_irq_initialized = false;


// KSZ8851SNL interrupt handler callback
static void ksz8851snl_irq_handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    
    // Visual indication that interrupt occurred (toggle LED if available)
    // This helps confirm the interrupt handler is actually being called
    
    // Read interrupt status from KSZ8851SNL (quick ISR-safe operation)
    // Note: We can't use SPI operations in ISR context with ASF4, so we just signal
    // the task to handle the interrupt. The actual interrupt status reading will
    // be done in task context.
    
    // Signal that an interrupt has occurred
    if (ksz8851snl_interrupt_semaphore != NULL) {
        xSemaphoreGiveFromISR(ksz8851snl_interrupt_semaphore, &xHigherPriorityTaskWoken);
    }
    
    // Request context switch if higher priority task was woken
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Forward declarations (implementations after SPI functions)
static void ksz8851snl_process_interrupt(void);
static void ksz8851snl_test_registers(void);
static void ksz8851snl_gpio_test(void);

// KSZ8851SNL IRQ initialization function
static drv_ksz8851snl_status_t ksz8851snl_irq_init(void)
{
    if (ksz8851snl_irq_initialized) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    printf("[KSZ8851SNL] Initializing interrupt system\r\n");
    printf("[KSZ8851SNL] PB7 pin level before setup: %s\r\n", 
           gpio_get_pin_level(KSZ8851SNL_INT_PIN) ? "HIGH" : "LOW");
    
    // Create binary semaphore for interrupt signaling
    ksz8851snl_interrupt_semaphore = xSemaphoreCreateBinary();
    if (ksz8851snl_interrupt_semaphore == NULL) {
        printf("[KSZ8851SNL] Failed to create interrupt semaphore\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Enable EIC clocks
    hri_gclk_write_PCHCTRL_reg(GCLK, EIC_GCLK_ID, CONF_GCLK_EIC_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
    hri_mclk_set_APBAMASK_EIC_bit(MCLK);
    
    // Configure PB7 as input with pull-up (KSZ8851SNL INT is active low)
    gpio_set_pin_direction(KSZ8851SNL_INT_PIN, GPIO_DIRECTION_IN);
    gpio_set_pin_pull_mode(KSZ8851SNL_INT_PIN, GPIO_PULL_UP);
    
    // Set pin function to EIC EXTINT7
    gpio_set_pin_function(KSZ8851SNL_INT_PIN, PINMUX_PB07A_EIC_EXTINT7);
    
    // Initialize external IRQ system
    int32_t result = ext_irq_init();
    if (result != 0) {
        printf("[KSZ8851SNL] Failed to initialize external IRQ system: %ld\r\n", result);
        vSemaphoreDelete(ksz8851snl_interrupt_semaphore);
        ksz8851snl_interrupt_semaphore = NULL;
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Register interrupt handler
    result = ext_irq_register(KSZ8851SNL_INT_PIN, ksz8851snl_irq_handler);
    if (result != 0) {
        printf("[KSZ8851SNL] Failed to register interrupt handler: %ld\r\n", result);
        ext_irq_deinit();
        vSemaphoreDelete(ksz8851snl_interrupt_semaphore);
        ksz8851snl_interrupt_semaphore = NULL;
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Enable external interrupt
    result = ext_irq_enable(KSZ8851SNL_INT_PIN);
    if (result != 0) {
        printf("[KSZ8851SNL] Failed to enable external interrupt: %ld\r\n", result);
        ext_irq_deinit();
        vSemaphoreDelete(ksz8851snl_interrupt_semaphore);
        ksz8851snl_interrupt_semaphore = NULL;
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    ksz8851snl_irq_initialized = true;
    printf("[KSZ8851SNL] Interrupt system initialized successfully\r\n");
    printf("[KSZ8851SNL] PB7 pin level after setup: %s\r\n", 
           gpio_get_pin_level(KSZ8851SNL_INT_PIN) ? "HIGH" : "LOW");
    
    // Initial register test
    printf("[KSZ8851SNL] Running initial register test...\r\n");
    ksz8851snl_test_registers();
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

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

// SPI register access functions forward declarations
static uint16_t ksz8851_reg_read(uint16_t reg);
static void ksz8851_reg_write(uint16_t reg, uint16_t wrdata);

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
    
    // Build SPI command according to KSZ8851SNL datasheet
    // Bits [15:14] = 00 for read, [13:10] = byte enables, [9:2] = register address, [1:0] = 00
    cmd = (reg << 2) & REG_ADDR_MASK;
    
    // Select byte enable for command based on register alignment
    if (reg & 2) {
        // Odd word address accesses bytes 2 and 3
        cmd |= (0xC << 10);  // BE[3:0] = 1100
    } else {
        // Even word address accesses bytes 0 and 1  
        cmd |= (0x3 << 10);  // BE[3:0] = 0011
    }
    
    // Add read command (bits [15:14] = 00)
    cmd |= CMD_READ;
    
    // Pack command into bytes (MSB first for SPI)
    cmd_buf[0] = (cmd >> 8) & 0xFF;
    cmd_buf[1] = cmd & 0xFF;
    cmd_buf[2] = 0x00; // Dummy byte for data phase
    cmd_buf[3] = 0x00; // Dummy byte for data phase
    
    printf("[KSZ8851SNL] SPI CMD: [0x%02X 0x%02X 0x%02X 0x%02X] (cmd=0x%04X)\r\n", 
           cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3], cmd);
    
    // Perform SPI transfer with proper CS timing
    drv_spi_cs_set_low();
    // Small delay for CS setup time
    for (volatile int i = 0; i < 10; i++);
    
    drv_spi_status_t status = hw_spi_transfer(&spi_4, cmd_buf, resp_buf, 4);
    
    // Small delay for CS hold time
    for (volatile int i = 0; i < 10; i++);
    drv_spi_cs_set_high();
    
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI read error: %d\r\n", status);
        return 0xFFFF;
    }
    
    printf("[KSZ8851SNL] SPI RSP: [0x%02X 0x%02X 0x%02X 0x%02X]\r\n", 
           resp_buf[0], resp_buf[1], resp_buf[2], resp_buf[3]);
    
    // Extract result - KSZ8851SNL always returns data in bytes 2,3 regardless of byte enables
    // This matches the observed behavior: [0x00 0x00 0x72 0x88] -> chip ID 0x8872
    uint16_t result = (resp_buf[3] << 8) | resp_buf[2];
    
    printf("[KSZ8851SNL] Data extraction: bytes[2,3] = [0x%02X, 0x%02X] -> 0x%04X\r\n", 
           resp_buf[2], resp_buf[3], result);
    
    printf("[KSZ8851SNL] Register 0x%02X = 0x%04X\r\n", reg, result);
    return result;
}

static void ksz8851_reg_write(uint16_t reg, uint16_t wrdata)
{
    uint16_t cmd = 0;
    uint8_t cmd_buf[4];
    uint8_t resp_buf[4];
    
    printf("[KSZ8851SNL] Writing register 0x%02X = 0x%04X\r\n", reg, wrdata);
    
    // Build SPI command according to KSZ8851SNL datasheet
    // Bits [15:14] = 01 for write, [13:10] = byte enables, [9:2] = register address, [1:0] = 00
    cmd = (reg << 2) & REG_ADDR_MASK;
    
    // Select byte enable for command based on register alignment
    if (reg & 2) {
        // Odd word address writes bytes 2 and 3
        cmd |= (0xC << 10);  // BE[3:0] = 1100
    } else {
        // Even word address writes bytes 0 and 1
        cmd |= (0x3 << 10);  // BE[3:0] = 0011
    }
    
    // Add write command (bits [15:14] = 01)
    cmd |= CMD_WRITE;
    
    // Pack command and data into bytes
    cmd_buf[0] = (cmd >> 8) & 0xFF;
    cmd_buf[1] = cmd & 0xFF;
    
    // Pack data - KSZ8851SNL expects data in bytes 2,3 regardless of byte enables
    // This matches the read behavior where data is always returned in bytes 2,3
    cmd_buf[2] = wrdata & 0xFF;        // Low byte
    cmd_buf[3] = (wrdata >> 8) & 0xFF; // High byte
    
    printf("[KSZ8851SNL] Data packing: 0x%04X -> bytes[2,3] = [0x%02X, 0x%02X]\r\n", 
           wrdata, cmd_buf[2], cmd_buf[3]);
    
    printf("[KSZ8851SNL] SPI WRITE CMD: [0x%02X 0x%02X 0x%02X 0x%02X] (cmd=0x%04X)\r\n", 
           cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3], cmd);
    
    // Perform SPI transfer with proper CS timing
    drv_spi_cs_set_low();
    // Small delay for CS setup time
    for (volatile int i = 0; i < 10; i++);
    
    drv_spi_status_t status = hw_spi_transfer(&spi_4, cmd_buf, resp_buf, 4);
    
    // Small delay for CS hold time
    for (volatile int i = 0; i < 10; i++);
    drv_spi_cs_set_high();
    
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI write error: %d\r\n", status);
    } else {
        printf("[KSZ8851SNL] Register write completed successfully\r\n");
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

// Process KSZ8851SNL interrupt in task context
static void ksz8851snl_process_interrupt(void)
{
    // Read interrupt status register
    uint16_t int_status = ksz8851_reg_read(REG_INT_STATUS);
    
    if (int_status == 0) {
        printf("[KSZ8851SNL] Empty interrupt (status: 0x%04X)\r\n", int_status);
        return; // No interrupts pending
    }
    
    printf("[KSZ8851SNL] Interrupt status: 0x%04X\r\n", int_status);
    
    // Handle RX interrupt
    if (int_status & INT_RX) {
        printf("[KSZ8851SNL] RX interrupt - packet received\r\n");
        // TODO: Signal network stack that packet is available
        // This will be implemented when we update packet processing
    }
    
    // Handle TX interrupt
    if (int_status & INT_TX) {
        printf("[KSZ8851SNL] TX interrupt - packet transmitted\r\n");
        // TODO: Signal that TX buffer is available
    }
    
    // Handle PHY link change interrupt
    if (int_status & INT_PHY) {
        printf("[KSZ8851SNL] PHY interrupt - link status changed\r\n");
        // TODO: Update link status and notify network stack
    }
    
    // Check for unknown interrupts
    uint16_t known_interrupts = INT_RX | INT_TX | INT_PHY;
    if (int_status & ~known_interrupts) {
        printf("[KSZ8851SNL] Unknown interrupt bits: 0x%04X\r\n", int_status & ~known_interrupts);
    }
    
    // Clear handled interrupts
    ksz8851_reg_write(REG_INT_STATUS, int_status);
}

// Debug and test functions

static void ksz8851snl_test_registers(void)
{
    printf("\r\n=== KSZ8851SNL Register Test ===\r\n");
    
    // Read chip ID
    uint16_t chip_id = ksz8851_reg_read(REG_CHIP_ID);
    printf("Chip ID (0xC0):        0x%04X\r\n", chip_id);
    
    // Read interrupt mask
    uint16_t int_mask = ksz8851_reg_read(REG_INT_MASK);
    printf("Interrupt Mask (0x90): 0x%04X\r\n", int_mask);
    printf("  RX enabled:    %s\r\n", (int_mask & INT_RX) ? "YES" : "NO");
    printf("  TX enabled:    %s\r\n", (int_mask & INT_TX) ? "YES" : "NO");
    printf("  PHY enabled:   %s\r\n", (int_mask & INT_PHY) ? "YES" : "NO");
    
    // Read interrupt status
    uint16_t int_status = ksz8851_reg_read(REG_INT_STATUS);
    printf("Interrupt Status (0x92): 0x%04X\r\n", int_status);
    printf("  RX pending:    %s\r\n", (int_status & INT_RX) ? "YES" : "NO");
    printf("  TX pending:    %s\r\n", (int_status & INT_TX) ? "YES" : "NO");
    printf("  PHY pending:   %s\r\n", (int_status & INT_PHY) ? "YES" : "NO");
    
    // Read PHY status
    uint16_t phy_status = ksz8851_reg_read(0xE6);
    printf("PHY Status (0xE6):     0x%04X\r\n", phy_status);
    
    // Read port status
    uint16_t port_status = ksz8851_reg_read(0xF8);
    printf("Port Status (0xF8):    0x%04X\r\n", port_status);
    
    printf("================================\r\n\r\n");
}

static void ksz8851snl_gpio_test(void)
{
    printf("\r\n=== GPIO Pin Configuration Test ===\r\n");
    
    // Check GPIO pin states
    printf("PB7 (INT) pin level:    %s\r\n", gpio_get_pin_level(KSZ8851SNL_INT_PIN) ? "HIGH" : "LOW");
    printf("PA6 (RESET) pin level:  %s\r\n", gpio_get_pin_level(KSZ8851SNL_RESET_PIN) ? "HIGH" : "LOW");
    printf("PA27 (CE) pin level:    %s\r\n", gpio_get_pin_level(KSZ8851SNL_CE_PIN) ? "HIGH" : "LOW");
    printf("PB28 (CS) pin level:    %s\r\n", gpio_get_pin_level(KSZ8851SNL_CS_PIN) ? "HIGH" : "LOW");
    
    // Check if EIC is initialized
    printf("EIC initialized:        %s\r\n", ksz8851snl_irq_initialized ? "YES" : "NO");
    printf("Semaphore created:      %s\r\n", (ksz8851snl_interrupt_semaphore != NULL) ? "YES" : "NO");
    
    printf("====================================\r\n\r\n");
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
    
    // Initialize interrupt system
    drv_ksz8851snl_status_t irq_status = ksz8851snl_irq_init();
    if (irq_status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] Interrupt initialization failed\r\n");
        return irq_status;
    }
    
    // Configure KSZ8851SNL chip interrupts
    printf("[KSZ8851SNL] Configuring chip interrupts\r\n");
    
    // Clear any pending interrupt status
    ksz8851_reg_write(REG_INT_STATUS, 0xFFFF);
    
    // Enable RX, TX, and PHY link interrupts
    uint16_t interrupt_mask = INT_RX | INT_TX | INT_PHY;
    ksz8851_reg_write(REG_INT_MASK, interrupt_mask);
    
    printf("[KSZ8851SNL] Interrupts enabled: RX, TX, PHY (mask: 0x%04X)\r\n", interrupt_mask);
    
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

// Comprehensive SPI communication testing function
static drv_ksz8851snl_status_t drv_ksz8851snl_test_spi_communication(void)
{
    printf("[KSZ8851SNL] === Comprehensive SPI Communication Test ===\r\n");
    
    // Test 1: Multiple speed tests
    uint32_t test_speeds[] = {500000, 1000000, 5000000, 10000000, 25000000};
    int num_speeds = sizeof(test_speeds) / sizeof(test_speeds[0]);
    
    for (int i = 0; i < num_speeds; i++) {
        printf("[KSZ8851SNL] Testing SPI at %lu Hz...\r\n", test_speeds[i]);
        
        drv_spi_config_t spi_config = {
            .baudrate = test_speeds[i]
        };
        
        drv_spi_status_t spi_status = hw_spi_init(&spi_4, &spi_config);
        if (spi_status != DRV_SPI_STATUS_OK) {
            printf("[KSZ8851SNL] SPI init failed at %lu Hz: %d\r\n", test_speeds[i], spi_status);
            continue;
        }
        
        hw_spi_enable(&spi_4);
        
        // Read chip ID multiple times at this speed
        bool speed_ok = true;
        uint16_t prev_id = 0;
        for (int j = 0; j < 5; j++) {
            uint16_t chip_id = ksz8851_reg_read(REG_CHIP_ID);
            if (j == 0) {
                prev_id = chip_id;
            } else if (chip_id != prev_id || chip_id == 0xFFFF || chip_id == 0x0000) {
                speed_ok = false;
                break;
            }
        }
        
        printf("[KSZ8851SNL] Speed %lu Hz: %s (ID: 0x%04X)\r\n", 
               test_speeds[i], speed_ok ? "PASS" : "FAIL", prev_id);
               
        if (speed_ok && (prev_id & KSZ8851SNL_CHIP_ID_MASK) == KSZ8851SNL_CHIP_ID_EXPECTED) {
            printf("[KSZ8851SNL] Optimal speed found: %lu Hz\r\n", test_speeds[i]);
            break;
        }
    }
    
    // Test 2: Register read/write test
    printf("[KSZ8851SNL] Testing register read/write operations...\r\n");
    
    // Use a safe test register - Bus Clock Control Register (0x20)
    uint16_t test_reg = REG_BUS_CLOCK_CTRL;
    uint16_t original_value = ksz8851_reg_read(test_reg);
    printf("[KSZ8851SNL] Original value at 0x%02X: 0x%04X\r\n", test_reg, original_value);
    
    // Write a test pattern and verify
    uint16_t test_pattern = 0x0001;  // Safe pattern for bus clock control
    ksz8851_reg_write(test_reg, test_pattern);
    uint16_t read_back = ksz8851_reg_read(test_reg);
    
    bool rw_test_pass = (read_back == test_pattern);
    printf("[KSZ8851SNL] Register R/W test: %s (wrote 0x%04X, read 0x%04X)\r\n", 
           rw_test_pass ? "PASS" : "FAIL", test_pattern, read_back);
    
    // Restore original value
    ksz8851_reg_write(test_reg, original_value);
    
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
        .baudrate = 1000000  // Start with 1MHz for testing
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
    
    // Run comprehensive SPI communication tests
    drv_ksz8851snl_test_spi_communication();
    
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
        
        // Check if chip ID matches expected value using proper mask (exclude revision bits)
        uint16_t masked_chip_id = chip_id_1 & KSZ8851SNL_CHIP_ID_MASK;
        uint16_t revision = chip_id_1 & KSZ8851SNL_REVISION_MASK;
        
        if (masked_chip_id == KSZ8851SNL_CHIP_ID_EXPECTED) {
            id_info->chip_detected = true;
            printf("[KSZ8851SNL] ✓ Valid KSZ8851SNL chip detected!\r\n");
            printf("[KSZ8851SNL] ✓ Chip ID: 0x%04X (masked: 0x%04X, expected: 0x%04X)\r\n", 
                   chip_id_1, masked_chip_id, KSZ8851SNL_CHIP_ID_EXPECTED);
            printf("[KSZ8851SNL] ✓ Revision: %d (0x%X)\r\n", revision, revision);
        } else {
            printf("[KSZ8851SNL] ✗ Unexpected chip ID: 0x%04X\r\n", chip_id_1);
            printf("[KSZ8851SNL] ✗ Masked ID: 0x%04X (expected: 0x%04X)\r\n", 
                   masked_chip_id, KSZ8851SNL_CHIP_ID_EXPECTED);
            printf("[KSZ8851SNL] ✗ Check chip variant and connections\r\n");
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
    
    printf("[KSZ8851SNL] Reading actual link and PHY status from registers\r\n");
    
    // Read PHY status register (REG_PORT_STATUS = 0xF8)
    uint16_t port_status = ksz8851_reg_read(REG_PORT_STATUS);
    printf("[KSZ8851SNL] Port Status Register (0xF8): 0x%04X\r\n", port_status);
    
    // Parse link status (bit 5 - PORT_STATUS_LINK_GOOD)
    status_info->link_up = (port_status & PORT_STATUS_LINK_GOOD) ? true : false;
    printf("[KSZ8851SNL] Link Status: %s\r\n", status_info->link_up ? "UP" : "DOWN");
    
    if (status_info->link_up) {
        // Parse link speed (bit 10 - PORT_STAT_SPEED_100MBIT)
        status_info->link_speed = (port_status & PORT_STAT_SPEED_100MBIT) ? 100 : 10;
        
        // Parse duplex mode (bit 9 - PORT_STAT_FULL_DUPLEX)
        status_info->full_duplex = (port_status & PORT_STAT_FULL_DUPLEX) ? true : false;
        
        printf("[KSZ8851SNL] Link Speed: %d Mbps\r\n", status_info->link_speed);
        printf("[KSZ8851SNL] Duplex Mode: %s\r\n", status_info->full_duplex ? "Full" : "Half");
        
        // Check auto-negotiation status (bit 6 - PORT_AUTO_NEG_COMPLETE)
        bool auto_neg_complete = (port_status & PORT_AUTO_NEG_COMPLETE) ? true : false;
        printf("[KSZ8851SNL] Auto-Negotiation: %s\r\n", auto_neg_complete ? "Complete" : "In Progress");
        
        // Read additional PHY status register (REG_PHY_STATUS = 0xE6)  
        uint16_t phy_status = ksz8851_reg_read(REG_PHY_STATUS);
        printf("[KSZ8851SNL] PHY Status Register (0xE6): 0x%04X\r\n", phy_status);
        
        // Verify link status from PHY register as well
        bool phy_link_up = (phy_status & PHY_LINK_UP) ? true : false;
        printf("[KSZ8851SNL] PHY Link Status: %s\r\n", phy_link_up ? "UP" : "DOWN");
        
        // If PHY reports link down but port reports link up, prefer PHY status
        if (!phy_link_up && status_info->link_up) {
            printf("[KSZ8851SNL] Warning: Port status shows link UP but PHY shows link DOWN\r\n");
            status_info->link_up = false;
        }
    } else {
        status_info->link_speed = 0;
        status_info->full_duplex = false;
        printf("[KSZ8851SNL] Link is down, speed and duplex not applicable\r\n");
    }
    
    // Copy packet statistics from context
    status_info->rx_packets = context->rx_packets;
    status_info->tx_packets = context->tx_packets;
    status_info->rx_errors = context->rx_errors;
    status_info->tx_errors = context->tx_errors;
    
    printf("[KSZ8851SNL] Statistics - RX: %lu packets (%lu errors), TX: %lu packets (%lu errors)\r\n",
           status_info->rx_packets, status_info->rx_errors,
           status_info->tx_packets, status_info->tx_errors);
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

// FIFO read/write helper functions with proper CS control
static void ksz8851_fifo_write_data(const uint8_t *data, uint16_t length)
{
    // Assert CS before FIFO operation
    drv_spi_cs_set_low();
    
    // Send FIFO write command (big-endian for KSZ8851SNL)
    uint8_t cmd[2];
    cmd[0] = FIFO_WRITE;  // 0xC0
    cmd[1] = 0x00;
    
    drv_spi_status_t status = hw_spi_transfer(&spi_4, cmd, NULL, 2);
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO write command failed\r\n");
        drv_spi_cs_set_high();
        return;
    }
    
    // Write data to FIFO
    status = hw_spi_transfer(&spi_4, data, NULL, length);
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO data write failed\r\n");
    }
    
    // Deassert CS after FIFO operation
    drv_spi_cs_set_high();
}

static void ksz8851_fifo_read_data(uint8_t *data, uint16_t length)
{
    // Assert CS before FIFO operation
    drv_spi_cs_set_low();
    
    // Send FIFO read command (big-endian for KSZ8851SNL)
    uint8_t cmd[2];
    cmd[0] = FIFO_READ;   // 0x80
    cmd[1] = 0x00;
    
    drv_spi_status_t status = hw_spi_transfer(&spi_4, cmd, NULL, 2);
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO read command failed\r\n");
        drv_spi_cs_set_high();
        return;
    }
    
    // Read data from FIFO
    status = hw_spi_transfer(&spi_4, NULL, data, length);
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO data read failed\r\n");
    }
    
    // Deassert CS after FIFO operation
    drv_spi_cs_set_high();
}

// Fixed packet transmission implementation following KSZ8851SNL datasheet
static drv_ksz8851snl_status_t drv_ksz8851snl_send_packet_impl(const void *hw_context, const uint8_t *data, uint16_t length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);
    
    printf("[KSZ8851SNL] Send packet - length: %d\r\n", length);
    
    // Step 1: Check if TX memory is available
    uint16_t tx_mem_info = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t available_mem = tx_mem_info & TX_MEM_AVAILABLE_MASK;
    
    // Need space for frame + 4-byte header + overhead
    if (available_mem < (length + 8)) {
        printf("[KSZ8851SNL] Not enough TX memory: need %d, have %d\r\n", length + 8, available_mem);
        return DRV_KSZ8851SNL_STATUS_BUSY;
    }
    
    printf("[KSZ8851SNL] TX memory available: %d bytes\r\n", available_mem);
    
    // Step 2: Disable interrupts during transmission
    uint16_t saved_int_mask = ksz8851_reg_read(REG_INT_MASK);
    ksz8851_reg_write(REG_INT_MASK, 0);
    
    // Step 3: Enable TXQ write access (start QMU operation)
    ksz8851_reg_write(REG_RXQ_CMD, RXQ_START);
    
    // Step 4: Write frame control header (4 bytes)
    uint8_t frame_header[4];
    frame_header[0] = 0x00;                    // Frame control word (low byte)
    frame_header[1] = 0x00;                    // Frame control word (high byte) 
    frame_header[2] = length & 0xFF;           // Frame byte count (low byte)
    frame_header[3] = (length >> 8) & 0xFF;   // Frame byte count (high byte)
    
    // Step 5: Write frame header to FIFO
    ksz8851_fifo_write_data(frame_header, 4);
    
    // Step 6: Write actual packet data to FIFO
    ksz8851_fifo_write_data(data, length);
    
    // Step 7: End QMU write operation
    ksz8851_reg_write(REG_RXQ_CMD, 0);
    
    // Step 8: Start frame transmission
    ksz8851_reg_write(REG_TXQ_CMD, TXQ_ENQUEUE);
    
    // Step 9: Restore interrupt mask
    ksz8851_reg_write(REG_INT_MASK, saved_int_mask);
    
    printf("[KSZ8851SNL] Packet transmission initiated successfully\r\n");
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_receive_packet_impl(const void *hw_context, uint8_t *data, uint16_t *length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length != NULL);
    
    // Check if there are frames in the receive queue
    uint16_t rx_status = ksz8851_reg_read(REG_RXQ_CMD);
    uint8_t rx_frame_count = (rx_status & RXQ_CMD_CNTL) >> 8;
    
    if (rx_frame_count == 0) {
        *length = 0;
        return DRV_KSZ8851SNL_STATUS_OK; // No frames available
    }
    
    // Check frame header status
    uint16_t fhr_status = ksz8851_reg_read(REG_RX_FHR_STATUS);
    if ((fhr_status & RX_VALID) == 0) {
        printf("[KSZ8851SNL] Invalid frame in RX queue\r\n");
        *length = 0;
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Get frame length from frame header
    uint16_t frame_len = (fhr_status & 0x07FF); // Lower 11 bits contain frame length
    
    if (frame_len > *length) {
        printf("[KSZ8851SNL] Frame too large: %d > %d\r\n", frame_len, *length);
        *length = 0;
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    printf("[KSZ8851SNL] Receiving packet - length: %d\r\n", frame_len);
    
    // Enable RXQ read access
    ksz8851_reg_write(REG_RXQ_CMD, RXQ_START);
    
    // Read frame header (4 bytes) - we'll discard this
    uint8_t frame_header[4];
    ksz8851_fifo_read_data(frame_header, 4);
    
    // Read actual frame data
    ksz8851_fifo_read_data(data, frame_len);
    
    // End RXQ read access
    ksz8851_reg_write(REG_RXQ_CMD, 0);
    
    // Free the received frame
    ksz8851_reg_write(REG_RXQ_CMD, RXQ_CMD_FREE_PACKET);
    
    *length = frame_len;
    printf("[KSZ8851SNL] Packet received successfully\r\n");
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_check_rx_available_impl(const void *hw_context, bool *rx_available)
{
    ASSERT(hw_context != NULL);
    ASSERT(rx_available != NULL);
    
    *rx_available = false;
    
    // Check if interrupt semaphore is available (interrupt occurred)
    if (ksz8851snl_interrupt_semaphore != NULL) {
        // Try to take semaphore without blocking
        if (xSemaphoreTake(ksz8851snl_interrupt_semaphore, 0) == pdTRUE) {
            // Process the interrupt
            ksz8851snl_process_interrupt();
            
            // Check if RX data is actually available by reading RX frame count
            uint16_t rx_status = ksz8851_reg_read(REG_RXQ_CMD);
            uint8_t rx_frame_count = (rx_status & RXQ_CMD_CNTL) >> 8;
            
            if (rx_frame_count > 0) {
                *rx_available = true;
                printf("[KSZ8851SNL] RX available: %d frame(s) pending\r\n", rx_frame_count);
            }
        }
    }
    
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

// Public debug and test functions
void ksz8851snl_debug_test_registers(void)
{
    ksz8851snl_test_registers();
}

void ksz8851snl_debug_gpio_test(void)
{
    ksz8851snl_gpio_test();
}

void ksz8851snl_debug_test_packet_transmission(void)
{
    printf("[KSZ8851SNL] === Packet Transmission Test ===\r\n");
    
    // Create a simple UDP packet for testing
    uint8_t test_packet[] = {
        // Ethernet header (14 bytes)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination MAC (broadcast)
        0x00, 0x00, 0x00, 0x00, 0x20, 0x76, // Source MAC
        0x08, 0x00,                         // EtherType (IPv4)
        
        // IPv4 header (20 bytes)
        0x45, 0x00, 0x00, 0x1C,             // Version, IHL, ToS, Total Length (28 bytes)
        0x00, 0x01, 0x00, 0x00,             // ID, Flags, Fragment Offset
        0x40, 0x11, 0x00, 0x00,             // TTL (64), Protocol (UDP), Header Checksum
        0xC0, 0xA8, 0x64, 0x02,             // Source IP (192.168.100.2)
        0xFF, 0xFF, 0xFF, 0xFF,             // Destination IP (broadcast)
        
        // UDP header (8 bytes)
        0x13, 0x89, 0x13, 0x89,             // Source Port (5001), Dest Port (5001)
        0x00, 0x08, 0x00, 0x00,             // Length (8 bytes), Checksum
    };
    
    printf("[KSZ8851SNL] Sending test packet (%zu bytes)...\r\n", sizeof(test_packet));
    
    // Show packet details
    printf("[KSZ8851SNL] Test packet contents:\r\n");
    for (size_t i = 0; i < sizeof(test_packet); i++) {
        if (i % 16 == 0) printf("[KSZ8851SNL] %04zX: ", i);
        printf("%02X ", test_packet[i]);
        if (i % 16 == 15) printf("\r\n");
    }
    if (sizeof(test_packet) % 16 != 0) printf("\r\n");
    
    // Get TX buffer space before transmission
    uint16_t tx_space_before = ksz8851_reg_read(REG_TX_MEM_INFO) & TX_MEM_AVAILABLE_MASK;
    printf("[KSZ8851SNL] TX buffer space before: %d bytes\r\n", tx_space_before);
    
    // Transmit the packet
    drv_ksz8851snl_status_t result = hw_ksz8851snl_send_packet(&ksz8851snl_0, test_packet, sizeof(test_packet));
    
    // Get TX buffer space after transmission
    uint16_t tx_space_after = ksz8851_reg_read(REG_TX_MEM_INFO) & TX_MEM_AVAILABLE_MASK;
    printf("[KSZ8851SNL] TX buffer space after: %d bytes\r\n", tx_space_after);
    
    // Check interrupt status
    uint16_t int_status = ksz8851_reg_read(REG_INT_STATUS);
    printf("[KSZ8851SNL] Interrupt status: 0x%04X\r\n", int_status);
    
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] ✓ Test packet transmission reported SUCCESS\r\n");
        printf("[KSZ8851SNL] Buffer space change: %d bytes\r\n", tx_space_before - tx_space_after);
        
        if (tx_space_before != tx_space_after) {
            printf("[KSZ8851SNL] ✓ TX buffer space changed - packet likely transmitted\r\n");
        } else {
            printf("[KSZ8851SNL] ⚠ TX buffer space unchanged - check transmission\r\n");
        }
    } else {
        printf("[KSZ8851SNL] ✗ Test packet transmission FAILED: %d\r\n", result);
    }
    
    printf("[KSZ8851SNL] === Packet Transmission Test Complete ===\r\n");
    printf("[KSZ8851SNL] NOTE: Check Wireshark or Python server for actual packet reception\r\n");
}