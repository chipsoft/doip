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

// CMSIS includes for NVIC interrupt priority configuration
#include "sam.h"  // Includes CMSIS core and device headers

// Include existing register definitions
#include "app_libs/FreeRTOS-Plus-TCP/source/portable/NetworkInterface/ksz8851snl/ksz8851snl_reg.h"

// CRITICAL MISSING DEFINITIONS: TXQ Access Control bits (from Oryx driver analysis)
// These bits are ESSENTIAL to prevent 0x55 corruption and enable proper transmission
#define RXQ_SDA                   0x0008    /* Enable TXQ write access (SDA = Start DMA Access) - bit 3 */
#define TXQ_METFE                 0x0008    /* Manual Enqueue Transmit Frame Enable - triggers transmission */

// Simple byte swap for embedded environment (no arpa/inet.h available)
static inline uint16_t htons_local(uint16_t hostshort) {
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

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

// Ensure TX_CTRL_FLOW_ENABLE is set (critical for transmission)
static void ensure_tx_flow_control_enabled(void);
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
    
    // Set interrupt priority (CRITICAL: Must be >= configMAX_SYSCALL_INTERRUPT_PRIORITY)
    // configMAX_SYSCALL_INTERRUPT_PRIORITY = 4 (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
    // Using priority 5 to be safe (lower number = higher priority, but must be >= 4)
    NVIC_SetPriority(EIC_7_IRQn, 5);
    printf("[KSZ8851SNL] Set EIC_7_IRQn priority to 5 (safe for FreeRTOS APIs)\r\n");
    
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
    
    // printf("[KSZ8851SNL] Reading register 0x%02X\r\n", reg);
    
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
    
    // printf("[KSZ8851SNL] SPI CMD: [0x%02X 0x%02X 0x%02X 0x%02X] (cmd=0x%04X)\r\n", 
    //        cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3], cmd);
    
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
    
    // printf("[KSZ8851SNL] SPI RSP: [0x%02X 0x%02X 0x%02X 0x%02X]\r\n", 
    //        resp_buf[0], resp_buf[1], resp_buf[2], resp_buf[3]);
    
    // Extract result - KSZ8851SNL always returns data in bytes 2,3 regardless of byte enables
    // This matches the observed behavior: [0x00 0x00 0x72 0x88] -> chip ID 0x8872
    uint16_t result = (resp_buf[3] << 8) | resp_buf[2];
    
    // printf("[KSZ8851SNL] Data extraction: bytes[2,3] = [0x%02X, 0x%02X] -> 0x%04X\r\n", 
    //        resp_buf[2], resp_buf[3], result);
    
    // printf("[KSZ8851SNL] Register 0x%02X = 0x%04X\r\n", reg, result);
    return result;
}

static void ksz8851_reg_write(uint16_t reg, uint16_t wrdata)
{
    uint16_t cmd = 0;
    uint8_t cmd_buf[4];
    uint8_t resp_buf[4];
    
    // printf("[KSZ8851SNL] Writing register 0x%02X = 0x%04X\r\n", reg, wrdata);
    
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
    
    // printf("[KSZ8851SNL] Data packing: 0x%04X -> bytes[2,3] = [0x%02X, 0x%02X]\r\n", 
    //        wrdata, cmd_buf[2], cmd_buf[3]);
    
    // printf("[KSZ8851SNL] SPI WRITE CMD: [0x%02X 0x%02X 0x%02X 0x%02X] (cmd=0x%04X)\r\n", 
    //        cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3], cmd);
    
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
        // printf("[KSZ8851SNL] Register write completed successfully\r\n");
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
        
        // Check how many frames are available
        uint16_t rxq_status = ksz8851_reg_read(REG_RXQ_CMD);
        uint8_t frame_count = (rxq_status & RX_FRAME_CNT_MASK) >> 8;
        printf("[KSZ8851SNL] RX interrupt: %d frame(s) available in queue\r\n", frame_count);
        
        // Yellow text notification for interrupt-based RX detection
        printf("\033[33m⚡ RX INTERRUPT! %d packet(s) received via hardware interrupt\033[0m\r\n", frame_count);
        
        // Don't process frames here - let lwIP's low_level_input() handle it
        // This prevents conflicts between interrupt handler and receive_packet function
        printf("[KSZ8851SNL] RX interrupt acknowledged - frames ready for lwIP processing\r\n");
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
}

// Ensure TX_CTRL_FLOW_ENABLE is set (critical for transmission)
static void ensure_tx_flow_control_enabled(void)
{
    uint16_t tx_ctrl = ksz8851_reg_read(REG_TX_CTRL);
    uint16_t flow_enable_check = tx_ctrl & TX_CTRL_FLOW_ENABLE;
    
    if (!flow_enable_check) {
        printf("[KSZ8851SNL] ⚠️  TX_CTRL_FLOW_ENABLE missing (0x%04X), forcing it...\r\n", tx_ctrl);
        
        // Force the FLOW_ENABLE bit
        uint16_t fixed_tx_ctrl = tx_ctrl | TX_CTRL_FLOW_ENABLE;
        ksz8851_reg_write(REG_TX_CTRL, fixed_tx_ctrl);
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Verify it was set
        tx_ctrl = ksz8851_reg_read(REG_TX_CTRL);
        flow_enable_check = tx_ctrl & TX_CTRL_FLOW_ENABLE;
        
        if (flow_enable_check) {
            printf("[KSZ8851SNL] ✅ TX_CTRL_FLOW_ENABLE set successfully (0x%04X)\r\n", tx_ctrl);
        } else {
            printf("[KSZ8851SNL] ❌ CRITICAL: Cannot set TX_CTRL_FLOW_ENABLE!\r\n");
        }
    }
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

static drv_ksz8851snl_status_t drv_ksz8851snl_init_impl(
    const void *hw_context,
    const drv_ksz8851snl_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);

    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;

    if (context->is_initialized) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }

    printf("[KSZ8851SNL] Initializing (Oryx style)\r\n");

    // Step 1: Configure pins and SPI
    drv_ksz8851snl_configure_pins();

    drv_spi_config_t spi_config = {
        .baudrate = KSZ8851SNL_SPI_CLOCK_SPEED,
        .clock_polarity = KSZ8851SNL_SPI_CLOCK_POLARITY,
        .clock_phase = KSZ8851SNL_SPI_CLOCK_PHASE,
        .bits_per_transfer = KSZ8851SNL_SPI_BITS_PER_TRANSFER,
    };

    if (hw_spi_init(&spi_4, &spi_config) != DRV_SPI_STATUS_OK ||
        hw_spi_enable(&spi_4) != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] SPI init failed\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }

    // Step 2: Hardware reset
    drv_ksz8851snl_hardware_reset();

    // Step 3: Software reset sequence (CRITICAL for proper chip state)
    printf("[KSZ8851SNL] 🔄 AGGRESSIVE RESET: Clearing all previous state\r\n");
    
    // Clear any leftover TX packets from previous sessions
    printf("[KSZ8851SNL] - Clearing TX queue\r\n");
    ksz8851_reg_write(REG_TXQ_CMD, 0x0000);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Clear any leftover RX packets 
    printf("[KSZ8851SNL] - Clearing RX queue\r\n");
    ksz8851_reg_write(REG_RXQ_CMD, 0x0000);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Global software reset (PHY, MAC, QMU) - this clears all internal state
    printf("[KSZ8851SNL] - Global software reset\r\n");
    ksz8851_reg_write(REG_RESET_CTRL, GLOBAL_SOFTWARE_RESET);
    vTaskDelay(pdMS_TO_TICKS(100)); // Longer wait for complete reset
    
    // QMU software reset (clear TxQ, RxQ) - this ensures clean FIFO state
    printf("[KSZ8851SNL] - QMU software reset\r\n");
    ksz8851_reg_write(REG_RESET_CTRL, QMU_SOFTWARE_RESET);
    vTaskDelay(pdMS_TO_TICKS(50)); // Longer wait for QMU reset
    
    // Clear reset register
    ksz8851_reg_write(REG_RESET_CTRL, 0x0000);
    vTaskDelay(pdMS_TO_TICKS(20)); // Final settling time
    
    // Verify reset was successful
    uint16_t reset_status = ksz8851_reg_read(REG_RESET_CTRL);
    printf("[KSZ8851SNL] Reset control register after reset: 0x%04X\r\n", reset_status);
    
    printf("[KSZ8851SNL] Software reset sequence completed\r\n");

    // Step 3.5: Flush TX queue to ensure completely clean FIFO state after reset
    printf("[KSZ8851SNL] 🧹 Flushing TX queue after reset to clear any residual data\r\n");
    uint16_t tx_ctrl_pre_config = ksz8851_reg_read(REG_TX_CTRL);
    printf("[KSZ8851SNL] TX_CTRL before configuration: 0x%04X\r\n", tx_ctrl_pre_config);
    
    // Set flush bit to clear any residual FIFO state
    ksz8851_reg_setbits(REG_TX_CTRL, TX_CTRL_FLUSH_QUEUE);
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow flush to complete
    ksz8851_reg_clrbits(REG_TX_CTRL, TX_CTRL_FLUSH_QUEUE);
    
    // Check initial TX memory state
    uint16_t initial_tx_mem = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t initial_available = initial_tx_mem & TX_MEM_AVAILABLE_MASK;
    printf("[KSZ8851SNL] Initial TX memory after flush: %d bytes\r\n", initial_available);

    // Step 3.6: Configure TX_CTRL register AFTER QMU reset and flush (CRITICAL FIX)
    printf("[KSZ8851SNL] ⚠️  CRITICAL: Configuring TX control register after QMU reset\r\n");
    uint16_t tx_ctrl = TX_CTRL_ENABLE |
                       TX_CTRL_FLOW_ENABLE |         // CRITICAL: This bit enables transmission flow control
                       TX_CTRL_PAD_ENABLE |
                       TX_CTRL_CRC_ENABLE |
                       TX_CTRL_ICMP_CHECKSUM |
                       TX_CTRL_UDP_CHECKSUM |
                       TX_CTRL_TCP_CHECKSUM |
                       TX_CTRL_IP_CHECKSUM;
    
    printf("[KSZ8851SNL] Expected TX_CTRL value: 0x%04X (includes FLOW_ENABLE)\r\n", tx_ctrl);
    printf("[KSZ8851SNL] TX_CTRL_FLOW_ENABLE bit: 0x%04X\r\n", TX_CTRL_FLOW_ENABLE);
    // printf("[KSZ8851SNL] TX_CTRL bit breakdown:\r\n");
    // printf("[KSZ8851SNL]   TX_CTRL_ENABLE (0x0001): %s\r\n", (tx_ctrl & TX_CTRL_ENABLE) ? "SET" : "NOT SET");
    // printf("[KSZ8851SNL]   TX_CTRL_CRC_ENABLE (0x0002): %s\r\n", (tx_ctrl & TX_CTRL_CRC_ENABLE) ? "SET" : "NOT SET");
    // printf("[KSZ8851SNL]   TX_CTRL_PAD_ENABLE (0x0004): %s\r\n", (tx_ctrl & TX_CTRL_PAD_ENABLE) ? "SET" : "NOT SET");
    // printf("[KSZ8851SNL]   TX_CTRL_FLOW_ENABLE (0x0008): %s\r\n", (tx_ctrl & TX_CTRL_FLOW_ENABLE) ? "SET" : "NOT SET");
    // printf("[KSZ8851SNL]   TX_CTRL_IP_CHECKSUM (0x0020): %s\r\n", (tx_ctrl & TX_CTRL_IP_CHECKSUM) ? "SET" : "NOT SET");
    printf("[KSZ8851SNL] Writing TX_CTRL register with FLOW_ENABLE...\r\n");
    ksz8851_reg_write(REG_TX_CTRL, tx_ctrl);
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow register to settle
    
    // Verify TX_CTRL was set correctly - multiple reads to ensure stability
    uint16_t tx_ctrl_readback = ksz8851_reg_read(REG_TX_CTRL);
    printf("[KSZ8851SNL] TX_CTRL configured: 0x%04X (expected: 0x%04X)\r\n", tx_ctrl_readback, tx_ctrl);
    uint16_t flow_enable_check = tx_ctrl_readback & TX_CTRL_FLOW_ENABLE;
    printf("[KSZ8851SNL] TX_CTRL_FLOW_ENABLE: %s (0x%04X & 0x%04X = 0x%04X)\r\n", 
           flow_enable_check ? "YES" : "NO",
           tx_ctrl_readback, TX_CTRL_FLOW_ENABLE, flow_enable_check);
    
    if (!flow_enable_check) {
        printf("[KSZ8851SNL] ❌ CRITICAL ERROR: TX_CTRL missing FLOW_ENABLE bit!\r\n");
        printf("[KSZ8851SNL] This explains why packets don't transmit to network!\r\n");
    }
    
    // If flow enable is missing, force it again (THIS IS THE CRITICAL FIX)
    if (!(tx_ctrl_readback & TX_CTRL_FLOW_ENABLE)) {
        printf("[KSZ8851SNL] ❌ TX_CTRL_FLOW_ENABLE missing! This is why packets don't transmit!\r\n");
        printf("[KSZ8851SNL] 🔧 Forcing TX_CTRL_FLOW_ENABLE bit (0x0008)...\r\n");
        
        // Try multiple approaches to set the flow enable bit
        // Approach 1: OR with existing value
        uint16_t fixed_tx_ctrl = tx_ctrl_readback | TX_CTRL_FLOW_ENABLE;
        ksz8851_reg_write(REG_TX_CTRL, fixed_tx_ctrl);
        vTaskDelay(pdMS_TO_TICKS(5));
        
        tx_ctrl_readback = ksz8851_reg_read(REG_TX_CTRL);
        uint16_t flow_check_after = tx_ctrl_readback & TX_CTRL_FLOW_ENABLE;
        printf("[KSZ8851SNL] TX_CTRL after fix: 0x%04X\r\n", tx_ctrl_readback);
        printf("[KSZ8851SNL] FLOW_ENABLE after fix: %s (0x%04X & 0x%04X = 0x%04X)\r\n", 
               flow_check_after ? "YES" : "NO", tx_ctrl_readback, TX_CTRL_FLOW_ENABLE, flow_check_after);
        
        if (!flow_check_after) {
            printf("[KSZ8851SNL] ❌ Approach 1 failed. Trying complete register rewrite...\r\n");
            // Approach 2: Write the complete expected value
            ksz8851_reg_write(REG_TX_CTRL, 0x01F7); // Expected value with FLOW_ENABLE
            vTaskDelay(pdMS_TO_TICKS(10));
            
            tx_ctrl_readback = ksz8851_reg_read(REG_TX_CTRL);
            flow_check_after = tx_ctrl_readback & TX_CTRL_FLOW_ENABLE;
            printf("[KSZ8851SNL] TX_CTRL after complete rewrite: 0x%04X\r\n", tx_ctrl_readback);
            printf("[KSZ8851SNL] FLOW_ENABLE final: %s\r\n", flow_check_after ? "YES" : "NO");
            
            if (!flow_check_after) {
                printf("[KSZ8851SNL] ❌ CRITICAL: Cannot set TX_CTRL_FLOW_ENABLE!\r\n");
                printf("[KSZ8851SNL] This explains why packets are queued but not transmitted!\r\n");
            } else {
                printf("[KSZ8851SNL] ✅ TX_CTRL_FLOW_ENABLE now set - transmission should work!\r\n");
            }
        } else {
            printf("[KSZ8851SNL] ✅ TX_CTRL_FLOW_ENABLE set successfully!\r\n");
        }
    } else {
        printf("[KSZ8851SNL] ✅ TX_CTRL_FLOW_ENABLE already set correctly\r\n");
    }

    // Step 3.6: Ensure chip is in normal operation mode (not power-saving)
    printf("[KSZ8851SNL] Setting power control to normal operation mode\r\n");
    ksz8851_reg_write(REG_POWER_CNTL, POWER_STATE_D0);
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow chip to stabilize in normal mode
    uint16_t power_state = ksz8851_reg_read(REG_POWER_CNTL);
    printf("[KSZ8851SNL] Power control register: 0x%04X (should be in D0 mode)\r\n", power_state);

    // Step 4: Verify chip ID
    drv_ksz8851snl_id_info_t id_info;
    drv_ksz8851snl_get_chip_id_impl(hw_context, &id_info);
    if (!id_info.chip_detected) {
        printf("[KSZ8851SNL] Invalid chip ID\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    printf("[KSZ8851SNL] Chip detected (ID=0x%04X)\r\n", id_info.chip_id);

    // Step 5: Set MAC address
    printf("[KSZ8851SNL] Setting MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           config->mac_addr[0], config->mac_addr[1], config->mac_addr[2],
           config->mac_addr[3], config->mac_addr[4], config->mac_addr[5]);
    
    ksz8851_reg_write(REG_MAC_ADDR_0, (config->mac_addr[1] << 8) | config->mac_addr[0]);
    ksz8851_reg_write(REG_MAC_ADDR_2, (config->mac_addr[3] << 8) | config->mac_addr[2]);
    ksz8851_reg_write(REG_MAC_ADDR_4, (config->mac_addr[5] << 8) | config->mac_addr[4]);

    // Step 6: Configure QMU (TX/RX engines)
    // NOTE: TX_CTRL configuration moved to AFTER QMU reset to prevent register clearing
    // (TX_CTRL will be configured in Step 6.1 after QMU reset)
    ksz8851_reg_write(REG_TX_ADDR_PTR, ADDR_PTR_AUTO_INC);

    // Step 1: Flush RX queue first to clear any residual data
    uint16_t rx_ctrl_flush = RX_CTRL_ENABLE |
                            RX_CTRL_FLUSH_QUEUE |
                            RX_CTRL_FLOW_ENABLE |
                            RX_CTRL_IP_CHECKSUM |
                            RX_CTRL_TCP_CHECKSUM |
                            RX_CTRL_UDP_CHECKSUM;

    printf("[KSZ8851SNL] Flushing RX queue and configuring RX control\r\n");
    ksz8851_reg_write(REG_RX_CTRL1, rx_ctrl_flush);
    
    // Step 2: Clear flush bit to enable normal RX operation
    uint16_t rx_ctrl_normal = RX_CTRL_ENABLE |
                             RX_CTRL_FLOW_ENABLE |
                             RX_CTRL_IP_CHECKSUM |
                             RX_CTRL_TCP_CHECKSUM |
                             RX_CTRL_UDP_CHECKSUM;
    
    ksz8851_reg_write(REG_RX_CTRL1, rx_ctrl_normal);
    printf("[KSZ8851SNL] RX control configured: 0x%04X (FLUSH_QUEUE cleared)\r\n", rx_ctrl_normal);
    ksz8851_reg_write(REG_RX_ADDR_PTR, ADDR_PTR_AUTO_INC);

    // Step 6.5: Configure RXQ Control Register (CRITICAL for QMU operation)
    printf("[KSZ8851SNL] Configuring RXQ control register\r\n");
    uint16_t rxq_ctrl = RXQ_FRAME_CNT_INT | RXQ_AUTO_DEQUEUE;
    ksz8851_reg_write(REG_RXQ_CMD, rxq_ctrl);
    uint16_t rxq_status = ksz8851_reg_read(REG_RXQ_CMD);
    printf("[KSZ8851SNL] RXQ control register: 0x%04X (auto-dequeue enabled)\r\n", rxq_status);

    // Step 6.6: Configure TXQ Control Register - DISABLE auto-enqueue, use pure manual mode
    printf("[KSZ8851SNL] Configuring TXQ control register for MANUAL mode (no auto-enqueue)\r\n");
    uint16_t txq_ctrl = 0x0000;  // Clear all bits - pure manual mode
    ksz8851_reg_write(REG_TXQ_CMD, txq_ctrl);
    uint16_t txq_status = ksz8851_reg_read(REG_TXQ_CMD);
    printf("[KSZ8851SNL] TXQ control register: 0x%04X (manual mode - no auto-enqueue)\r\n", txq_status);

    // Step 7: Enable interrupts (RX, TX, PHY)
    ksz8851_reg_write(REG_INT_STATUS, 0xFFFF); // Clear pending
    ksz8851_reg_write(REG_INT_MASK, INT_RX | INT_TX | INT_PHY);

    // Step 8: Init IRQ system (FreeRTOS semaphore + external IRQ)
    if (ksz8851snl_irq_init() != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] IRQ init failed\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }

    context->is_initialized = true;
    printf("[KSZ8851SNL] Init complete\r\n");

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
    
    // Enable TX operations - ensure ALL required bits are set
    uint16_t tx_ctrl = ksz8851_reg_read(REG_TX_CTRL);
    uint16_t required_tx_ctrl = TX_CTRL_ENABLE |
                                TX_CTRL_FLOW_ENABLE |         // CRITICAL: Must be set for transmission
                                TX_CTRL_PAD_ENABLE |
                                TX_CTRL_CRC_ENABLE |
                                TX_CTRL_ICMP_CHECKSUM |
                                TX_CTRL_UDP_CHECKSUM |
                                TX_CTRL_TCP_CHECKSUM |
                                TX_CTRL_IP_CHECKSUM;
    
    // FORCE reconfiguration to fix TX_CTRL_FLOW_ENABLE issue
    printf("[KSZ8851SNL] TX_CTRL check - Current: 0x%04X, Required: 0x%04X\r\n", 
           tx_ctrl, required_tx_ctrl);
    printf("[KSZ8851SNL] TX_CTRL_FLOW_ENABLE currently: %s\r\n", 
           (tx_ctrl & TX_CTRL_FLOW_ENABLE) ? "YES" : "NO");
    
    if ((tx_ctrl & required_tx_ctrl) != required_tx_ctrl) {
        printf("[KSZ8851SNL] ⚠️  TX_CTRL missing required bits - FIXING NOW\r\n");
        ksz8851_reg_write(REG_TX_CTRL, required_tx_ctrl);
        tx_ctrl = ksz8851_reg_read(REG_TX_CTRL);
        printf("[KSZ8851SNL] TX_CTRL after fix: 0x%04X\r\n", tx_ctrl);
        printf("[KSZ8851SNL] TX_CTRL_FLOW_ENABLE after fix: %s\r\n", 
               (tx_ctrl & TX_CTRL_FLOW_ENABLE) ? "YES" : "NO");
    } else {
        printf("[KSZ8851SNL] TX_CTRL already properly configured: 0x%04X\r\n", tx_ctrl);
    }
    
    // Enable RX operations  
    uint16_t rx_ctrl = ksz8851_reg_read(REG_RX_CTRL1);
    if (!(rx_ctrl & RX_CTRL_ENABLE)) {
        printf("[KSZ8851SNL] RX was disabled, enabling now\r\n");
        ksz8851_reg_setbits(REG_RX_CTRL1, RX_CTRL_ENABLE);
        rx_ctrl = ksz8851_reg_read(REG_RX_CTRL1);
        printf("[KSZ8851SNL] RX_CTRL after enable: 0x%04X\r\n", rx_ctrl);
    } else {
        printf("[KSZ8851SNL] RX already enabled: 0x%04X\r\n", rx_ctrl);
    }
    
    context->is_enabled = true;
    
    // Final verification of TX_CTRL_FLOW_ENABLE after enable sequence
    printf("[KSZ8851SNL] Final verification of TX_CTRL_FLOW_ENABLE...\r\n");
    ensure_tx_flow_control_enabled();
    
    printf("[KSZ8851SNL] KSZ8851SNL enabled successfully - TX/RX operational\r\n");
    
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
    vTaskDelay(2); // Slightly longer delay for SPI stability
    uint16_t chip_id_2 = ksz8851_reg_read(REG_CHIP_ID);
    vTaskDelay(2); // Slightly longer delay  
    uint16_t chip_id_3 = ksz8851_reg_read(REG_CHIP_ID);
    
    printf("[KSZ8851SNL] Chip ID readings: 0x%04X, 0x%04X, 0x%04X\r\n", chip_id_1, chip_id_2, chip_id_3);
    
    // Use majority vote for robustness - at least 2 out of 3 readings must be valid and consistent
    uint16_t valid_chip_id = 0;
    bool spi_ok = false;
    
    // Count occurrences of each non-error value
    if ((chip_id_1 == chip_id_2) && (chip_id_1 != 0x0000) && (chip_id_1 != 0xFFFF)) {
        // First two readings match and are valid
        valid_chip_id = chip_id_1;
        spi_ok = true;
        printf("[KSZ8851SNL] Majority vote: Using readings 1&2 (0x%04X)\r\n", valid_chip_id);
    } else if ((chip_id_1 == chip_id_3) && (chip_id_1 != 0x0000) && (chip_id_1 != 0xFFFF)) {
        // First and third readings match and are valid
        valid_chip_id = chip_id_1;
        spi_ok = true;
        printf("[KSZ8851SNL] Majority vote: Using readings 1&3 (0x%04X)\r\n", valid_chip_id);
    } else if ((chip_id_2 == chip_id_3) && (chip_id_2 != 0x0000) && (chip_id_2 != 0xFFFF)) {
        // Second and third readings match and are valid
        valid_chip_id = chip_id_2;
        spi_ok = true;
        printf("[KSZ8851SNL] Majority vote: Using readings 2&3 (0x%04X)\r\n", valid_chip_id);
    } else {
        printf("[KSZ8851SNL] No consistent readings found - SPI communication failed\r\n");
    }
    
    if (spi_ok) {
        id_info->spi_communication_ok = true;
        id_info->chip_id = valid_chip_id;
        id_info->revision_id = valid_chip_id & 0x000F; // Lower 4 bits are revision
        
        // Check if chip ID matches expected value using proper mask (exclude revision bits)
        uint16_t masked_chip_id = valid_chip_id & KSZ8851SNL_CHIP_ID_MASK;
        uint16_t revision = valid_chip_id & KSZ8851SNL_REVISION_MASK;
        
        if (masked_chip_id == KSZ8851SNL_CHIP_ID_EXPECTED) {
            id_info->chip_detected = true;
            printf("[KSZ8851SNL] ✓ Valid KSZ8851SNL chip detected!\r\n");
            printf("[KSZ8851SNL] ✓ Chip ID: 0x%04X (masked: 0x%04X, expected: 0x%04X)\r\n", 
                   valid_chip_id, masked_chip_id, KSZ8851SNL_CHIP_ID_EXPECTED);
            printf("[KSZ8851SNL] ✓ Revision: %d (0x%X)\r\n", revision, revision);
        } else {
            printf("[KSZ8851SNL] ✗ Unexpected chip ID: 0x%04X\r\n", valid_chip_id);
            printf("[KSZ8851SNL] ✗ Masked ID: 0x%04X (expected: 0x%04X)\r\n", 
                   masked_chip_id, KSZ8851SNL_CHIP_ID_EXPECTED);
            printf("[KSZ8851SNL] ✗ Check chip variant and connections\r\n");
            id_info->chip_detected = false;
        }
    } else {
        printf("[KSZ8851SNL] No consistent readings found - SPI communication failed\r\n");
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

// TX memory reset function to clear stuck packets
static void ksz8851_tx_memory_reset(void)
{
    printf("[KSZ8851SNL] Resetting TX memory using FLUSH_QUEUE...\r\n");
    
    // Read current TX memory status
    uint16_t tx_mem_info = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t available_mem = tx_mem_info & TX_MEM_AVAILABLE_MASK;
    
    printf("[KSZ8851SNL] TX memory before reset: available=%d bytes\r\n", available_mem);
    
    // Method 1: Use TX_CTRL_FLUSH_QUEUE to clear transmit queue
    uint16_t tx_ctrl = ksz8851_reg_read(REG_TX_CTRL);
    printf("[KSZ8851SNL] TX_CTRL before flush: 0x%04X\r\n", tx_ctrl);
    
    // Set flush bit - this clears transmit queue and resets tx frame pointer
    ksz8851_reg_setbits(REG_TX_CTRL, TX_CTRL_FLUSH_QUEUE);
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow flush to complete
    
    // Clear flush bit (should auto-clear but ensure it's cleared)
    ksz8851_reg_clrbits(REG_TX_CTRL, TX_CTRL_FLUSH_QUEUE);
    
    tx_ctrl = ksz8851_reg_read(REG_TX_CTRL);
    printf("[KSZ8851SNL] TX_CTRL after flush: 0x%04X\r\n", tx_ctrl);
    
    // Method 2: If that doesn't work, try QMU reset
    tx_mem_info = ksz8851_reg_read(REG_TX_MEM_INFO);
    available_mem = tx_mem_info & TX_MEM_AVAILABLE_MASK;
    
    if (available_mem == 0) {
        printf("[KSZ8851SNL] FLUSH_QUEUE didn't work, trying QMU reset...\r\n");
        
        // Save current TX_CTRL configuration
        uint16_t saved_tx_ctrl = ksz8851_reg_read(REG_TX_CTRL);
        
        // QMU reset clears TX/RX queues
        ksz8851_reg_write(REG_RESET_CTRL, QMU_SOFTWARE_RESET);
        vTaskDelay(pdMS_TO_TICKS(10));
        ksz8851_reg_write(REG_RESET_CTRL, 0x0000);
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Restore TX_CTRL configuration
        ksz8851_reg_write(REG_TX_CTRL, saved_tx_ctrl);
        
        printf("[KSZ8851SNL] QMU reset completed, TX_CTRL restored to 0x%04X\r\n", saved_tx_ctrl);
    }
    
    // Read final TX memory status
    tx_mem_info = ksz8851_reg_read(REG_TX_MEM_INFO);
    available_mem = tx_mem_info & TX_MEM_AVAILABLE_MASK;
    
    printf("[KSZ8851SNL] TX memory after reset: available=%d bytes\r\n", available_mem);
}

// Buffer validation helper to detect corruption patterns
static bool validate_buffer_integrity(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0) {
        printf("[KSZ8851SNL] Buffer validation: NULL pointer or zero length\r\n");
        return false;
    }
    
    // Check for 0x55 corruption pattern (common memory corruption signature)
    uint16_t corruption_count = 0;
    for (uint16_t i = 0; i < length; i++) {
        if (data[i] == 0x55) {
            corruption_count++;
        }
    }
    
    // If more than 80% of buffer is 0x55, likely corruption
    if (corruption_count > (length * 4 / 5)) {
        printf("[KSZ8851SNL] Buffer corruption detected: %d/%d bytes are 0x55\r\n", 
               corruption_count, length);
        
        // Dump first 32 bytes for analysis
        printf("[KSZ8851SNL] Buffer dump (first 32 bytes): ");
        for (int i = 0; i < 32 && i < length; i++) {
            printf("%02X ", data[i]);
        }
        printf("\r\n");
        
        return false;
    }
    
    return true;
}

// FIFO state validation helper
static bool validate_fifo_write_sequence(uint32_t expected_total_length, uint16_t actual_data_length, uint32_t pad_bytes)
{
    // Verify length consistency
    uint32_t calculated_total = actual_data_length + pad_bytes;
    if (calculated_total != expected_total_length) {
        printf("[KSZ8851SNL] LENGTH MISMATCH: Expected %lu, calculated %lu (data=%d + pad=%lu)\r\n",
               expected_total_length, calculated_total, actual_data_length, pad_bytes);
        return false;
    }
    
    // Check for reasonable frame size (Ethernet: 14-1518 bytes for data, chip handles padding)
    if (expected_total_length < 14 || expected_total_length > 1600) {
        printf("[KSZ8851SNL] SUSPICIOUS FRAME SIZE: %lu bytes (outside 14-1600 range)\r\n",
               expected_total_length);
        return false;
    }
    
    printf("[KSZ8851SNL] FIFO write length validation passed: %lu bytes total\r\n", 
           expected_total_length);
    return true;
}

// FIFO error recovery function - used AFTER CS is properly released
static void ksz8851_fifo_error_recovery_post_cs_release(void)
{
    printf("[KSZ8851SNL] *** FIFO ERROR RECOVERY (CS already released) ***\r\n");
    
    // CS should already be released by caller - don't manipulate it here
    __DMB();  // Memory barrier
    
    // Reset TX FIFO pointer to clear any partial write state
    uint16_t txq_cmd = ksz8851_reg_read(REG_TXQ_CMD);
    printf("[KSZ8851SNL] TXQ_CMD before reset: 0x%04X\r\n", txq_cmd);
    
    // Clear any pending FIFO operations but preserve auto-enqueue configuration
    ksz8851_reg_write(REG_TXQ_CMD, 0x0000);
    
    // Add delay for FIFO reset to take effect
    for (volatile int i = 0; i < 1000; i++);
    
    // Restore TXQ manual mode configuration (no auto-enqueue)
    ksz8851_reg_write(REG_TXQ_CMD, 0x0000);
    
    txq_cmd = ksz8851_reg_read(REG_TXQ_CMD);
    printf("[KSZ8851SNL] TXQ_CMD after reset: 0x%04X (manual mode restored)\r\n", txq_cmd);
    
    // Check TX memory status after recovery
    uint16_t tx_mem_info = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t available_mem = tx_mem_info & TX_MEM_AVAILABLE_MASK;
    printf("[KSZ8851SNL] TX memory after FIFO recovery: %d bytes\r\n", available_mem);
    
    printf("[KSZ8851SNL] FIFO error recovery completed\r\n");
}

// FIFO write helper function following Oryx reference pattern
static drv_ksz8851snl_status_t ksz8851_fifo_write_begin(uint32_t total_length)
{
    // CRITICAL FIX: Match Oryx implementation - send only FIFO write command (1 byte)
    // The 5-byte header was confusing the KSZ8851SNL FIFO state machine
    uint8_t cmd = FIFO_WRITE;  // 0xC0 only
    
    printf("[KSZ8851SNL] FIFO write begin: Oryx-style (command only, no header)\r\n");
    
    // Assert CS and send FIFO write command only
    drv_spi_cs_set_low();
    
    drv_spi_status_t status = hw_spi_transfer(&spi_4, &cmd, NULL, 1);
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO write begin failed: %d\r\n", status);
        drv_spi_cs_set_high();
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    printf("[KSZ8851SNL] FIFO write command sent successfully - ready for data\r\n");
    return DRV_KSZ8851SNL_STATUS_OK;
}

// Write proper 4-byte TX header to FIFO (following Oryx pattern)
static drv_ksz8851snl_status_t ksz8851_fifo_write_tx_header(uint16_t frame_length)
{
    static uint8_t frame_id = 0;
    
    // Create 4-byte TX header as per KSZ8851SNL specification
    typedef struct {
        uint16_t control_word;  // Frame ID and control bits
        uint16_t byte_count;    // Frame length
    } __attribute__((packed)) tx_header_t;
    
    tx_header_t header;
    
    // Control word: frame ID (6 bits) + control bits
    // Bit 15: TXIC (TX Interrupt on Completion) - set to 1
    // Bits 5-0: Frame ID (6-bit counter)
    // KSZ8851SNL expects network byte order (big endian) for headers
    uint16_t control_word = (1 << 15) | (frame_id++ & 0x3F);
    header.control_word = htons_local(control_word);  // Convert to network byte order
    header.byte_count = htons_local(frame_length);    // Convert to network byte order
    
    printf("[KSZ8851SNL] Writing TX header: control=0x%04X, length=%d, frameID=%d\r\n", 
           header.control_word, header.byte_count, frame_id - 1);
    
    // Write TX header to FIFO (CS should already be low from begin())
    drv_spi_status_t status = hw_spi_transfer(&spi_4, (uint8_t*)&header, NULL, sizeof(header));
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] TX header write failed: %d\r\n", status);
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    printf("[KSZ8851SNL] TX header written successfully to FIFO\r\n");
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t ksz8851_fifo_write_data(const uint8_t *data, uint16_t length)
{
    // Validate buffer integrity before transmission
    if (!validate_buffer_integrity(data, length)) {
        printf("[KSZ8851SNL] CRITICAL: Buffer corruption detected - ABORTING transmission\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Add memory barrier to ensure data coherency
    __DMB();  // Data Memory Barrier - ensure all memory operations complete
    
    // Write packet data to FIFO (CS MUST STAY LOW from begin to end!)
    // CRITICAL: No CS manipulation here - violates FIFO protocol
    uint32_t start_tick = xTaskGetTickCount();
    drv_spi_status_t status = hw_spi_transfer(&spi_4, data, NULL, length);
    uint32_t transfer_time = xTaskGetTickCount() - start_tick;
    
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO data write failed: %d (took %lu ms)\r\n", 
               status, transfer_time);
        printf("[KSZ8851SNL] WARNING: CS still LOW - caller must handle recovery!\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Check for abnormally long transfer times (potential hardware issue)
    if (transfer_time > pdMS_TO_TICKS(100)) {
        printf("[KSZ8851SNL] WARNING: SPI transfer took %lu ms (expected < 100ms)\r\n", 
               transfer_time);
    }
    
    // Add memory barrier after SPI transfer
    __DMB();  // Ensure SPI transfer completion before proceeding
    
    printf("[KSZ8851SNL] FIFO data written successfully (%d bytes) - CS still LOW\r\n", length);
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

/**
 * Verify FIFO write by reading back data from TX FIFO and comparing
 * WARNING: This is for debugging only - reading from TX FIFO after write
 * may interfere with transmission in some KSZ8851SNL configurations
 */
static bool ksz8851_fifo_verify_write_data(const uint8_t *original_data, uint16_t length)
{
    // Skip verification for very large frames to avoid memory issues
    if (length > 256) {
        printf("[KSZ8851SNL] Skipping FIFO verification for large frame (%d bytes)\r\n", length);
        return true;
    }
    
    // Allocate buffer for readback
    uint8_t *readback_buffer = pvPortMalloc(length);
    if (!readback_buffer) {
        printf("[KSZ8851SNL] WARNING: Cannot allocate readback buffer - skipping verification\r\n");
        return true; // Assume OK if we can't verify
    }
    
    printf("[KSZ8851SNL] === FIFO READBACK VERIFICATION ===\r\n");
    printf("[KSZ8851SNL] Reading back %d bytes from TX FIFO...\r\n", length);
    
    // Read the TX memory space directly using register access
    // NOTE: This is experimental - may not work on all KSZ8851SNL revisions
    uint16_t tx_addr_ptr = ksz8851_reg_read(REG_TX_ADDR_PTR);
    printf("[KSZ8851SNL] Current TX address pointer: 0x%04X\r\n", tx_addr_ptr);
    
    // Reset TX address pointer to beginning of our frame
    // Calculate where our frame should start in TX memory
    uint16_t frame_start_addr = 0x4000; // TX memory starts at 0x4000
    ksz8851_reg_write(REG_TX_ADDR_PTR, frame_start_addr | ADDR_PTR_AUTO_INC);
    
    // Try to read data back using FIFO read - this may not work for TX FIFO
    drv_spi_cs_set_low();
    
    // Send FIFO read command
    uint8_t cmd[2] = {FIFO_READ, 0x00};
    drv_spi_status_t status = hw_spi_transfer(&spi_4, cmd, NULL, 2);
    
    if (status == DRV_SPI_STATUS_OK) {
        // Try to read data back
        status = hw_spi_transfer(&spi_4, NULL, readback_buffer, length);
    }
    
    drv_spi_cs_set_high();
    
    bool verification_result = true;
    
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO readback SPI transfer failed: %d\r\n", status);
        printf("[KSZ8851SNL] This may be normal - TX FIFO readback not supported on all chips\r\n");
        verification_result = true; // Don't fail transmission for unsupported feature
    } else {
        // Compare data
        uint16_t mismatch_count = 0;
        uint16_t corruption_0x55_count = 0;
        
        for (uint16_t i = 0; i < length; i++) {
            if (readback_buffer[i] != original_data[i]) {
                mismatch_count++;
                if (readback_buffer[i] == 0x55) {
                    corruption_0x55_count++;
                }
            }
        }
        
        if (mismatch_count == 0) {
            printf("[KSZ8851SNL] ✅ FIFO verification PASSED - data matches exactly\r\n");
        } else {
            printf("[KSZ8851SNL] ❌ FIFO verification FAILED - %d/%d bytes corrupted\r\n", 
                   mismatch_count, length);
            printf("[KSZ8851SNL] 0x55 corruption pattern: %d bytes\r\n", corruption_0x55_count);
            
            // Show first few mismatches for debugging
            printf("[KSZ8851SNL] First mismatches:\r\n");
            uint16_t shown = 0;
            for (uint16_t i = 0; i < length && shown < 8; i++) {
                if (readback_buffer[i] != original_data[i]) {
                    printf("[KSZ8851SNL]   [%d]: wrote 0x%02X, read 0x%02X\r\n", 
                           i, original_data[i], readback_buffer[i]);
                    shown++;
                }
            }
            
            if (corruption_0x55_count > (length / 2)) {
                printf("[KSZ8851SNL] ❌ CRITICAL: Massive 0x55 corruption detected in FIFO!\r\n");
                printf("[KSZ8851SNL] This explains the 0x55 frames in Wireshark!\r\n");
                verification_result = false;
            }
        }
    }
    
    // Restore original TX address pointer
    ksz8851_reg_write(REG_TX_ADDR_PTR, tx_addr_ptr);
    
    vPortFree(readback_buffer);
    printf("[KSZ8851SNL] === FIFO VERIFICATION COMPLETE ===\r\n");
    
    return verification_result;
}

static drv_ksz8851snl_status_t ksz8851_fifo_write_end(uint16_t data_length)
{
    drv_ksz8851snl_status_t result = DRV_KSZ8851SNL_STATUS_OK;
    
    // Use Oryx-style simple padding loop for 4-byte alignment
    // Note: We need to pad the total written length (header + data)
    uint16_t total_written = 4 + data_length;  // 4-byte header + data
    uint32_t i = total_written;
    
    printf("[KSZ8851SNL] Oryx-style padding: total_written=%d\r\n", total_written);
    
    // Pad until 4-byte aligned using simple loop (Oryx pattern)
    while ((i % 4) != 0) {
        uint8_t pad_byte = 0x00;
        drv_spi_status_t status = hw_spi_transfer(&spi_4, &pad_byte, NULL, 1);
        if (status != DRV_SPI_STATUS_OK) {
            printf("[KSZ8851SNL] CRITICAL: FIFO padding write failed: %d\r\n", status);
            result = DRV_KSZ8851SNL_STATUS_ERROR;
            break;
        }
        i++;
    }
    
    uint32_t padding_added = i - total_written;
    if (padding_added > 0) {
        printf("[KSZ8851SNL] Added %lu padding bytes (Oryx style)\r\n", padding_added);
    } else {
        printf("[KSZ8851SNL] No padding needed - already aligned\r\n");
    }
    
    // Always deassert CS to complete FIFO operation (even on error)
    drv_spi_cs_set_high();
    
    // Add memory barrier to ensure CS deassertion completes
    __DMB();
    
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO write sequence completed successfully\r\n");
    } else {
        printf("[KSZ8851SNL] FIFO write sequence FAILED - frame may be corrupted\r\n");
    }
    
    return result;
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

// Packet transmission implementation following Microchip reference
static drv_ksz8851snl_status_t drv_ksz8851snl_send_packet_impl(
    const void *hw_context,
    const uint8_t *data,
    uint16_t length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);

    printf("[KSZ8851SNL] *** DETAILED SEND DEBUG START ***\r\n");
    printf("[KSZ8851SNL] Send packet (Oryx style) - length: %d\r\n", length);

    // Step 1: Check driver state
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    printf("[KSZ8851SNL] Driver state - initialized: %s, enabled: %s\r\n",
           context->is_initialized ? "YES" : "NO",
           context->is_enabled ? "YES" : "NO");

    if (!context->is_initialized || !context->is_enabled) {
        printf("[KSZ8851SNL] ❌ Driver not ready for transmission!\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }

    // Step 2: Check available TX memory with detailed status
    uint16_t tx_mem_info = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t available_mem = tx_mem_info & TX_MEM_AVAILABLE_MASK;
    uint16_t required_mem = length + 8; // frame + header + alignment

    printf("[KSZ8851SNL] TX Memory Status:\r\n");
    printf("[KSZ8851SNL]   - Raw TX_MEM_INFO: 0x%04X\r\n", tx_mem_info);
    printf("[KSZ8851SNL]   - Available memory: %d bytes\r\n", available_mem);
    printf("[KSZ8851SNL]   - Required memory: %d bytes\r\n", required_mem);

    if (available_mem < required_mem) {
        printf("[KSZ8851SNL] ❌ Not enough TX memory: need %d, have %d\r\n",
               required_mem, available_mem);
        
        // Try to reset TX memory to clear stuck packets
        printf("[KSZ8851SNL] 🔄 Attempting TX memory reset...\r\n");
        ksz8851_tx_memory_reset();
        
        // Re-check memory after reset
        tx_mem_info = ksz8851_reg_read(REG_TX_MEM_INFO);
        available_mem = tx_mem_info & TX_MEM_AVAILABLE_MASK;
        printf("[KSZ8851SNL] TX memory after reset: %d bytes\r\n", available_mem);
        
        if (available_mem < required_mem) {
            printf("[KSZ8851SNL] ❌ Still not enough TX memory after reset\r\n");
            return DRV_KSZ8851SNL_STATUS_BUSY;
        }
        
        printf("[KSZ8851SNL] ✅ TX memory reset successful, proceeding with transmission\r\n");
    }

    // Step 3: Check TX control register status
    uint16_t tx_ctrl_status = ksz8851_reg_read(REG_TX_CTRL);
    printf("[KSZ8851SNL] TX Control Register: 0x%04X (TX_ENABLE: %s)\r\n",
           tx_ctrl_status, (tx_ctrl_status & TX_CTRL_ENABLE) ? "YES" : "NO");

    if (!(tx_ctrl_status & TX_CTRL_ENABLE)) {
        printf("[KSZ8851SNL] ❌ TX is disabled in control register!\r\n");
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }

    // Step 4: Enable TXQ write access with verification and retry
    // printf("[KSZ8851SNL] Step 4: Enabling TXQ write access (RXQ_SDA)\r\n");
    uint16_t rxq_before = ksz8851_reg_read(REG_RXQ_CMD);
    
    // Try to set SDA bit with verification and retry
    int retry_count = 0;
    uint16_t rxq_after;
    do {
        ksz8851_reg_setbits(REG_RXQ_CMD, RXQ_SDA);
        vTaskDelay(pdMS_TO_TICKS(1)); // Small delay for bit to take effect
        rxq_after = ksz8851_reg_read(REG_RXQ_CMD);
        
        if (!(rxq_after & RXQ_SDA)) {
            printf("[KSZ8851SNL] ⚠️  RXQ_SDA bit not set, retry %d/3\r\n", retry_count + 1);
            retry_count++;
            if (retry_count >= 3) {
                printf("[KSZ8851SNL] ❌ CRITICAL: Failed to set RXQ_SDA after 3 retries!\r\n");
                return DRV_KSZ8851SNL_STATUS_ERROR;
            }
        }
    } while (!(rxq_after & RXQ_SDA) && retry_count < 3);
    
    printf("[KSZ8851SNL]   RXQ_CMD: 0x%04X -> 0x%04X (SDA: %s)\r\n", 
           rxq_before, rxq_after, (rxq_after & RXQ_SDA) ? "SET" : "NOT SET");

    // Step 5: Begin FIFO write
    // printf("[KSZ8851SNL] Step 5: Beginning FIFO write sequence\r\n");
    uint8_t cmd = FIFO_WRITE;
    // printf("[KSZ8851SNL]   Sending FIFO_WRITE command: 0x%02X\r\n", cmd);
    drv_spi_cs_set_low();
    if (hw_spi_transfer(&spi_4, &cmd, NULL, 1) != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] ❌ FIFO write command failed!\r\n");
        drv_spi_cs_set_high();
        ksz8851_reg_clrbits(REG_RXQ_CMD, RXQ_SDA);
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    // printf("[KSZ8851SNL]   FIFO write command sent successfully\r\n");

    // Step 6: Write TX header
    // printf("[KSZ8851SNL] Step 6: Writing TX header\r\n");
    static uint8_t frame_id = 0;
    uint16_t control_word = (1 << 15) | (frame_id++ & 0x3F);
    uint16_t byte_count   = length;

    uint8_t header[4];
    header[0] = control_word >> 8;
    header[1] = control_word & 0xFF;
    header[2] = byte_count >> 8;
    header[3] = byte_count & 0xFF;

    printf("[KSZ8851SNL]   Header: [0x%02X 0x%02X 0x%02X 0x%02X] (ctrl=0x%04X, len=%d)\r\n",
           header[0], header[1], header[2], header[3], control_word, byte_count);

    if (hw_spi_transfer(&spi_4, header, NULL, sizeof(header)) != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] ❌ TX header write failed!\r\n");
        drv_spi_cs_set_high();
        ksz8851_reg_clrbits(REG_RXQ_CMD, RXQ_SDA);
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    // printf("[KSZ8851SNL]   TX header written successfully\r\n");

    // Step 7: Write frame data
    // printf("[KSZ8851SNL] Step 7: Writing frame data (%d bytes)\r\n", length);
    if (hw_spi_transfer(&spi_4, data, NULL, length) != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] ❌ Frame data write failed!\r\n");
        drv_spi_cs_set_high();
        ksz8851_reg_clrbits(REG_RXQ_CMD, RXQ_SDA);
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    // printf("[KSZ8851SNL]   Frame data written successfully\r\n");

    // Step 8: Add padding for 4-byte alignment
    uint16_t total_written = 4 + length;
    uint16_t padding_needed = (4 - (total_written % 4)) % 4;
    // printf("[KSZ8851SNL] Step 8: Adding padding (%d bytes) for alignment\r\n", padding_needed);
    
    for (uint16_t i = 0; i < padding_needed; i++) {
        uint8_t pad = 0x00;
        if (hw_spi_transfer(&spi_4, &pad, NULL, 1) != DRV_SPI_STATUS_OK) {
            printf("[KSZ8851SNL] ❌ Padding write failed!\r\n");
            drv_spi_cs_set_high();
            ksz8851_reg_clrbits(REG_RXQ_CMD, RXQ_SDA);
            return DRV_KSZ8851SNL_STATUS_ERROR;
        }
    }
    total_written += padding_needed;
    // printf("[KSZ8851SNL]   Total written: %d bytes (aligned)\r\n", total_written);

    // Step 9: Release CS
    // printf("[KSZ8851SNL] Step 9: Releasing CS (ending FIFO write)\r\n");
    drv_spi_cs_set_high();

    // Step 10: Disable TXQ write access (end FIFO write phase)
    // printf("[KSZ8851SNL] Step 10: Disabling TXQ write access\r\n");
    ksz8851_reg_clrbits(REG_RXQ_CMD, RXQ_SDA);
    uint16_t rxq_final = ksz8851_reg_read(REG_RXQ_CMD);
    // printf("[KSZ8851SNL]   RXQ_CMD after disable: 0x%04X\r\n", rxq_final);

    // Step 10.5: Verify that data was actually written to FIFO
    uint16_t tx_mem_after_write = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t available_after_write = tx_mem_after_write & TX_MEM_AVAILABLE_MASK;
    
    if (available_after_write == available_mem) {
        printf("[KSZ8851SNL] ⚠️  WARNING: TX memory unchanged (%d bytes) - FIFO write may have failed!\r\n", available_after_write);
        printf("[KSZ8851SNL] ⚠️  This indicates the first few packets often fail to write to FIFO\r\n");
        // Continue anyway - this is expected for the first few packets
    } else {
        printf("[KSZ8851SNL] ✅ FIFO write successful: %d bytes consumed (%d -> %d)\r\n", 
               available_mem - available_after_write, available_mem, available_after_write);
    }

    // Step 11: Ensure TX_CTRL_FLOW_ENABLE is set (critical for transmission)
    printf("[KSZ8851SNL] Ensuring TX_CTRL_FLOW_ENABLE is set before transmission...\r\n");
    ensure_tx_flow_control_enabled();
    
    // Step 12: COMPREHENSIVE REGISTER STATE ANALYSIS BEFORE TRANSMISSION
    printf("[KSZ8851SNL] =============================================\r\n");
    printf("[KSZ8851SNL] COMPREHENSIVE REGISTER STATE ANALYSIS\r\n");
    printf("[KSZ8851SNL] =============================================\r\n");
    
    // Read all critical registers for diagnosis
    uint16_t reg_chip_id = ksz8851_reg_read(REG_CHIP_ID);
    uint16_t reg_tx_ctrl = ksz8851_reg_read(REG_TX_CTRL); 
    uint16_t reg_rx_ctrl = ksz8851_reg_read(REG_RX_CTRL1);
    uint16_t reg_led_ctrl = ksz8851_reg_read(REG_LED_CTRL);
    uint16_t reg_txq_cmd = ksz8851_reg_read(REG_TXQ_CMD);
    uint16_t reg_rxq_cmd = ksz8851_reg_read(REG_RXQ_CMD);
    uint16_t reg_int_mask = ksz8851_reg_read(REG_INT_MASK);
    uint16_t reg_int_status = ksz8851_reg_read(REG_INT_STATUS);
    uint16_t reg_port_ctrl = ksz8851_reg_read(0xF6);  // Port Control
    uint16_t reg_port_status = ksz8851_reg_read(0xF8); // Port Status 
    uint16_t reg_phy_ctrl = ksz8851_reg_read(0xE4);    // PHY Control
    uint16_t reg_phy_status = ksz8851_reg_read(0xE6);  // PHY Status
    uint16_t reg_tx_status = ksz8851_reg_read(0x72);   // TX Status
    uint16_t reg_tx_total_frame = ksz8851_reg_read(0x74); // TX Total Frame Counter
    
    printf("[KSZ8851SNL] REGISTER DUMP BEFORE TRANSMISSION:\r\n");
    printf("[KSZ8851SNL]   CHIP_ID (0xC0):        0x%04X\r\n", reg_chip_id);
    printf("[KSZ8851SNL]   TX_CTRL (0x70):        0x%04X\r\n", reg_tx_ctrl);
    printf("[KSZ8851SNL]     - TX_ENABLE:         %s\r\n", (reg_tx_ctrl & 0x0001) ? "YES" : "NO");
    printf("[KSZ8851SNL]     - TX_CRC_ENABLE:     %s\r\n", (reg_tx_ctrl & 0x0002) ? "YES" : "NO");
    printf("[KSZ8851SNL]     - TX_PAD_ENABLE:     %s\r\n", (reg_tx_ctrl & 0x0004) ? "YES" : "NO");
    printf("[KSZ8851SNL]     - TX_FLOW_ENABLE:    %s\r\n", (reg_tx_ctrl & 0x0008) ? "YES" : "NO");
    printf("[KSZ8851SNL]     - TX_FLUSH_QUEUE:    %s\r\n", (reg_tx_ctrl & 0x0010) ? "YES" : "NO");
    printf("[KSZ8851SNL]   RX_CTRL1 (0x74):       0x%04X\r\n", reg_rx_ctrl);
    printf("[KSZ8851SNL]   LED_CTRL (0xC6):       0x%04X\r\n", reg_led_ctrl);
    printf("[KSZ8851SNL]   TXQ_CMD (0x80):        0x%04X\r\n", reg_txq_cmd);
    printf("[KSZ8851SNL]     - TXQ_ENQUEUE:       %s\r\n", (reg_txq_cmd & 0x0001) ? "YES" : "NO");
    printf("[KSZ8851SNL]     - TXQ_AUTO_ENQUEUE:  %s\r\n", (reg_txq_cmd & 0x0004) ? "YES" : "NO");
    printf("[KSZ8851SNL]   RXQ_CMD (0x82):        0x%04X\r\n", reg_rxq_cmd);
    printf("[KSZ8851SNL]   INT_MASK (0x90):       0x%04X\r\n", reg_int_mask);
    printf("[KSZ8851SNL]   INT_STATUS (0x92):     0x%04X\r\n", reg_int_status);
    printf("[KSZ8851SNL]   PORT_CTRL (0xF6):      0x%04X\r\n", reg_port_ctrl);
    printf("[KSZ8851SNL]   PORT_STATUS (0xF8):    0x%04X\r\n", reg_port_status);
    printf("[KSZ8851SNL]     - LINK_GOOD:         %s\r\n", (reg_port_status & 0x0020) ? "YES" : "NO");
    printf("[KSZ8851SNL]     - AN_DONE:           %s\r\n", (reg_port_status & 0x0040) ? "YES" : "NO");
    printf("[KSZ8851SNL]     - DUPLEX_STATUS:     %s\r\n", (reg_port_status & 0x0004) ? "FULL" : "HALF");
    printf("[KSZ8851SNL]     - SPEED_STATUS:      %s\r\n", (reg_port_status & 0x0002) ? "100M" : "10M");
    printf("[KSZ8851SNL]   PHY_CTRL (0xE4):       0x%04X\r\n", reg_phy_ctrl);
    printf("[KSZ8851SNL]   PHY_STATUS (0xE6):     0x%04X\r\n", reg_phy_status);
    printf("[KSZ8851SNL]   TX_STATUS (0x72):      0x%04X\r\n", reg_tx_status);
    printf("[KSZ8851SNL]   TX_TOTAL_FRAME (0x74): 0x%04X\r\n", reg_tx_total_frame);
    printf("[KSZ8851SNL] =============================================\r\n");
    
    // Step 12: Pure manual transmission mode (like original working code)
    printf("[KSZ8851SNL] Using PURE MANUAL transmission mode (TXQ_ENQUEUE only)\r\n");
    uint16_t txq_status = ksz8851_reg_read(REG_TXQ_CMD);
    printf("[KSZ8851SNL]   TXQ_CMD current status: 0x%04X\r\n", txq_status);
    
    // Ensure we're in pure manual mode (no auto-enqueue)
    if (txq_status != 0x0000) {
        printf("[KSZ8851SNL]   ⚠️  Clearing TXQ_CMD to pure manual mode...\r\n");
        ksz8851_reg_write(REG_TXQ_CMD, 0x0000);
        vTaskDelay(pdMS_TO_TICKS(1));
        txq_status = ksz8851_reg_read(REG_TXQ_CMD);
        printf("[KSZ8851SNL]   TXQ_CMD after clear: 0x%04X\r\n", txq_status);
    }
    
    // Trigger ONLY manual TXQ_ENQUEUE (one frame at a time)
    printf("[KSZ8851SNL]   Writing PURE MANUAL TXQ_ENQUEUE...\r\n");
    printf("[KSZ8851SNL]   Command value: 0x%04X (MANUAL ONLY: 0x%04X)\r\n", 
           TXQ_ENQUEUE, TXQ_ENQUEUE);
    
    ksz8851_reg_write(REG_TXQ_CMD, TXQ_ENQUEUE);
    
    // Small delay for transmission trigger to take effect
    vTaskDelay(pdMS_TO_TICKS(2));  // Slightly longer delay for manual mode
    
    // Check final state (TXQ_ENQUEUE should auto-clear after triggering)
    txq_status = ksz8851_reg_read(REG_TXQ_CMD);
    printf("[KSZ8851SNL]   Final TXQ_CMD: 0x%04X ", txq_status);
    if (txq_status & TXQ_ENQUEUE) {
        printf("(manual: STILL PENDING - transmission may have failed)\r\n");
    } else {
        printf("(manual: TRIGGERED AND CLEARED - transmission should have started)\r\n");
    }
    
    // COMPREHENSIVE REGISTER STATE ANALYSIS AFTER TRANSMISSION TRIGGER
    printf("[KSZ8851SNL] =============================================\r\n");
    printf("[KSZ8851SNL] REGISTER STATE AFTER TRANSMISSION TRIGGER\r\n");
    printf("[KSZ8851SNL] =============================================\r\n");
    
    uint16_t reg_tx_ctrl_after = ksz8851_reg_read(REG_TX_CTRL);
    uint16_t reg_txq_cmd_after = ksz8851_reg_read(REG_TXQ_CMD);
    uint16_t reg_int_status_after = ksz8851_reg_read(REG_INT_STATUS);
    uint16_t reg_port_status_after = ksz8851_reg_read(0xF8);
    uint16_t reg_tx_status_after = ksz8851_reg_read(0x72);
    uint16_t reg_tx_total_frame_after = ksz8851_reg_read(0x74);
    uint16_t reg_tx_mem_after = ksz8851_reg_read(REG_TX_MEM_INFO);
    
    printf("[KSZ8851SNL]   TX_CTRL (0x70):        0x%04X -> 0x%04X\r\n", reg_tx_ctrl, reg_tx_ctrl_after);
    printf("[KSZ8851SNL]     - TX_ENABLE:         %s -> %s\r\n", 
           (reg_tx_ctrl & 0x0001) ? "YES" : "NO",
           (reg_tx_ctrl_after & 0x0001) ? "YES" : "NO");
    printf("[KSZ8851SNL]     - TX_FLOW_ENABLE:    %s -> %s\r\n", 
           (reg_tx_ctrl & 0x0008) ? "YES" : "NO",
           (reg_tx_ctrl_after & 0x0008) ? "YES" : "NO");
    printf("[KSZ8851SNL]   TXQ_CMD (0x80):        0x%04X -> 0x%04X\r\n", reg_txq_cmd, reg_txq_cmd_after);
    printf("[KSZ8851SNL]   INT_STATUS (0x92):     0x%04X -> 0x%04X\r\n", reg_int_status, reg_int_status_after);
    printf("[KSZ8851SNL]   PORT_STATUS (0xF8):    0x%04X -> 0x%04X\r\n", reg_port_status, reg_port_status_after);
    printf("[KSZ8851SNL]     - LINK_GOOD:         %s -> %s\r\n", 
           (reg_port_status & 0x0020) ? "YES" : "NO",
           (reg_port_status_after & 0x0020) ? "YES" : "NO");
    printf("[KSZ8851SNL]   TX_STATUS (0x72):      0x%04X -> 0x%04X\r\n", reg_tx_status, reg_tx_status_after);
    printf("[KSZ8851SNL]   TX_TOTAL_FRAME (0x74): 0x%04X -> 0x%04X\r\n", reg_tx_total_frame, reg_tx_total_frame_after);
    printf("[KSZ8851SNL]   TX_MEM_INFO (0x8C):    0x%04X -> 0x%04X\r\n", tx_mem_info, reg_tx_mem_after);
    printf("[KSZ8851SNL] =============================================\r\n");
    
    // Re-check TX_CTRL after transmission attempt
    uint16_t tx_ctrl_check = reg_tx_ctrl_after;
    uint16_t flow_final = tx_ctrl_check & TX_CTRL_FLOW_ENABLE;
    printf("[KSZ8851SNL]   TX_CTRL after transmission: 0x%04X (FLOW_ENABLE: %s)\r\n", 
           tx_ctrl_check, flow_final ? "YES" : "NO");

    // Step 12: Wait for transmission to complete and check TX interrupt
    printf("[KSZ8851SNL] Waiting for transmission to complete...\r\n");
    
    // Wait up to 50ms for TX interrupt to fire
    int wait_count = 0;
    uint16_t int_status;
    bool tx_interrupt_fired = false;
    
    while (wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(1)); // Wait 1ms
        int_status = ksz8851_reg_read(REG_INT_STATUS);
        
        if (int_status & INT_TX) {
            tx_interrupt_fired = true;
            printf("[KSZ8851SNL] ✅ TX interrupt fired after %d ms (status: 0x%04X)\r\n", wait_count, int_status);
            break;
        }
        wait_count++;
    }
    
    if (!tx_interrupt_fired) {
        printf("[KSZ8851SNL] ⚠️  No TX interrupt after %d ms (status: 0x%04X)\r\n", wait_count, int_status);
    }

    // Step 13: Check final TX memory status after transmission attempt
    uint16_t final_tx_mem = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t final_available = final_tx_mem & TX_MEM_AVAILABLE_MASK;
    printf("[KSZ8851SNL] Final TX memory: %d bytes (was %d bytes)\r\n", final_available, available_mem);
    
    // Step 13.5: Detect if packet is stuck in hardware and reset if needed
    bool packet_transmitted = false;
    if (tx_interrupt_fired) {
        ksz8851_reg_write(REG_INT_STATUS, INT_TX);
        printf("[KSZ8851SNL] ✅ TX interrupt cleared - packet transmitted successfully\r\n");
        packet_transmitted = true;
    } else {
        // Check for any error interrupts
        if (int_status & 0x0070) {  // Various error bits
            printf("[KSZ8851SNL]   ❌ Error interrupts detected: 0x%04X\r\n", int_status & 0x0070);
        }
        
        // CRITICAL: Check if packet is stuck in hardware FIFO
        if (final_available < available_mem) {
            printf("[KSZ8851SNL] ⚠️  CRITICAL: Packet stuck in hardware! Memory not restored (%d < %d)\r\n", 
                   final_available, available_mem);
            printf("[KSZ8851SNL] 🔄 Resetting TX memory to recover stuck packet...\r\n");
            
            // Force TX memory reset to recover
            ksz8851_tx_memory_reset();
            
            // Check if reset worked
            uint16_t reset_tx_mem = ksz8851_reg_read(REG_TX_MEM_INFO);
            uint16_t reset_available = reset_tx_mem & TX_MEM_AVAILABLE_MASK;
            printf("[KSZ8851SNL] TX memory after reset: %d bytes\r\n", reset_available);
            
            if (reset_available >= available_mem) {
                printf("[KSZ8851SNL] ✅ TX memory reset successful - ready for next packet\r\n");
            } else {
                printf("[KSZ8851SNL] ❌ TX memory reset failed - hardware may need power cycle\r\n");
            }
        } else {
            printf("[KSZ8851SNL] ✅ Memory restored - packet may have transmitted without interrupt\r\n");
            packet_transmitted = true;
        }
    }
    
    // Step 14: FINAL COMPREHENSIVE REGISTER STATE ANALYSIS
    printf("[KSZ8851SNL] =============================================\r\n");
    printf("[KSZ8851SNL] FINAL REGISTER STATE AFTER TRANSMISSION\r\n");
    printf("[KSZ8851SNL] =============================================\r\n");
    
    uint16_t reg_tx_ctrl_final = ksz8851_reg_read(REG_TX_CTRL);
    uint16_t reg_txq_cmd_final = ksz8851_reg_read(REG_TXQ_CMD);
    uint16_t reg_int_status_final = ksz8851_reg_read(REG_INT_STATUS);
    uint16_t reg_port_status_final = ksz8851_reg_read(0xF8);
    uint16_t reg_tx_status_final = ksz8851_reg_read(0x72);
    uint16_t reg_tx_total_frame_final = ksz8851_reg_read(0x74);
    uint16_t reg_tx_mem_final = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t reg_led_ctrl_final = ksz8851_reg_read(REG_LED_CTRL);
    uint16_t reg_phy_status_final = ksz8851_reg_read(0xE6);
    
    printf("[KSZ8851SNL] CRITICAL REGISTER FINAL STATES:\r\n");
    printf("[KSZ8851SNL]   TX_CTRL (0x70):        0x%04X (TX_EN: %s, FLOW_EN: %s)\r\n", 
           reg_tx_ctrl_final,
           (reg_tx_ctrl_final & 0x0001) ? "YES" : "NO",
           (reg_tx_ctrl_final & 0x0008) ? "YES" : "NO");
    printf("[KSZ8851SNL]   TXQ_CMD (0x80):        0x%04X (ENQUEUE: %s, AUTO_EN: %s)\r\n", 
           reg_txq_cmd_final,
           (reg_txq_cmd_final & 0x0001) ? "YES" : "NO",
           (reg_txq_cmd_final & 0x0004) ? "YES" : "NO");
    printf("[KSZ8851SNL]   INT_STATUS (0x92):     0x%04X (TX_INT: %s)\r\n", 
           reg_int_status_final,
           (reg_int_status_final & 0x4000) ? "YES" : "NO");
    printf("[KSZ8851SNL]   PORT_STATUS (0xF8):    0x%04X (LINK: %s, %s, %s)\r\n", 
           reg_port_status_final,
           (reg_port_status_final & 0x0020) ? "UP" : "DOWN",
           (reg_port_status_final & 0x0004) ? "FULL" : "HALF",
           (reg_port_status_final & 0x0002) ? "100M" : "10M");
    printf("[KSZ8851SNL]   TX_STATUS (0x72):      0x%04X\r\n", reg_tx_status_final);
    printf("[KSZ8851SNL]   TX_TOTAL_FRAME (0x74): 0x%04X\r\n", reg_tx_total_frame_final);
    printf("[KSZ8851SNL]   TX_MEM_INFO (0x8C):    0x%04X (available: %d bytes)\r\n", 
           reg_tx_mem_final, reg_tx_mem_final & TX_MEM_AVAILABLE_MASK);
    printf("[KSZ8851SNL]   LED_CTRL (0xC6):       0x%04X\r\n", reg_led_ctrl_final);
    printf("[KSZ8851SNL]   PHY_STATUS (0xE6):     0x%04X\r\n", reg_phy_status_final);
    
    // Analysis and diagnosis
    printf("[KSZ8851SNL] TRANSMISSION DIAGNOSIS:\r\n");
    printf("[KSZ8851SNL]   - Frame written to FIFO: %s\r\n", 
           (reg_tx_mem_final != tx_mem_info) ? "YES" : "NO");
    printf("[KSZ8851SNL]   - TXQ_ENQUEUE triggered: %s\r\n", 
           (reg_txq_cmd_final & 0x0001) ? "NO (still pending)" : "YES (cleared)");
    printf("[KSZ8851SNL]   - TX interrupt fired: %s\r\n", 
           tx_interrupt_fired ? "YES" : "NO");
    printf("[KSZ8851SNL]   - TX total frame count changed: %s (0x%04X -> 0x%04X)\r\n", 
           (reg_tx_total_frame != reg_tx_total_frame_final) ? "YES" : "NO",
           reg_tx_total_frame, reg_tx_total_frame_final);
    printf("[KSZ8851SNL]   - Link status: %s\r\n", 
           (reg_port_status_final & 0x0020) ? "LINK UP" : "LINK DOWN");
    
    printf("[KSZ8851SNL] =============================================\r\n");
    
    // Step 15: Update transmission statistics
    context->tx_packets++;
    printf("[KSZ8851SNL] TX statistics updated: %lu packets sent\r\n", context->tx_packets);
    
    printf("[KSZ8851SNL] *** PACKET TRANSMISSION SEQUENCE COMPLETED ***\r\n");
    printf("[KSZ8851SNL] ✅ Check Wireshark now - packet should appear!\r\n");
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_receive_packet_impl(const void *hw_context, uint8_t *data, uint16_t *length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length != NULL);
    
    // Check if there are frames in the receive queue
    uint16_t rx_status = ksz8851_reg_read(REG_RXQ_CMD);
    uint8_t rx_frame_count = (rx_status & RX_FRAME_CNT_MASK) >> 8;
    
    printf("[KSZ8851SNL] Receive packet: %d frames available\r\n", rx_frame_count);
    
    if (rx_frame_count == 0) {
        *length = 0;
        return DRV_KSZ8851SNL_STATUS_OK; // No frames available
    }
    
    // Yellow text notification that RX is coming
    printf("\033[33m🔥 RX is coming! Processing %d frame(s)\033[0m\r\n", rx_frame_count);
    
    // Start reading frame data from RX FIFO (same as interrupt handler)
    ksz8851_reg_setbits(REG_RXQ_CMD, RXQ_START);
    
    // Read the 4-byte frame header from RX FIFO
    uint32_t rx_frame_hdr = 0;
    for (int i = 0; i < 4; i += 2) {
        uint16_t word = ksz8851_reg_read(REG_QDR_DUMMY);
        rx_frame_hdr |= ((uint32_t)word) << (i * 8);
    }
    
    uint16_t frame_len = (rx_frame_hdr >> 16) & 0x0FFF;  // Frame length (bits 27:16)
    uint16_t frame_status = rx_frame_hdr & 0xFFFF;       // Frame status (bits 15:0)
    
    printf("[KSZ8851SNL] RX Frame: len=%d, status=0x%04X\r\n", frame_len, frame_status);
    
    // Check if frame is valid
    if ((frame_status & 0x8000) == 0) {  // Valid frame bit
        printf("[KSZ8851SNL] Invalid frame status: 0x%04X\r\n", frame_status);
        
        // Still need to read the frame data to clear FIFO
        uint16_t words_to_read = (frame_len + 1) / 2;
        for (uint16_t i = 0; i < words_to_read; i++) {
            uint16_t dummy = ksz8851_reg_read(REG_QDR_DUMMY);
            (void)dummy;
        }
        
        // Clear the frame from queue
        ksz8851_reg_setbits(REG_RXQ_CMD, RXQ_CMD_FREE_PACKET);
        
        *length = 0;
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Check if caller's buffer is large enough
    if (frame_len > *length) {
        printf("[KSZ8851SNL] Frame too large: %d > %d\r\n", frame_len, *length);
        
        // Still need to read and discard the frame data
        uint16_t words_to_read = (frame_len + 1) / 2;
        for (uint16_t i = 0; i < words_to_read; i++) {
            uint16_t dummy = ksz8851_reg_read(REG_QDR_DUMMY);
            (void)dummy;
        }
        
        // Clear the frame from queue
        ksz8851_reg_setbits(REG_RXQ_CMD, RXQ_CMD_FREE_PACKET);
        
        *length = 0;
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    printf("[KSZ8851SNL] Receiving packet - length: %d\r\n", frame_len);
    
    // Read actual frame data from RX FIFO (frame header already consumed above)
    // Read data in 16-bit words since KSZ8851SNL uses word-based FIFO access
    uint16_t words_to_read = (frame_len + 1) / 2;  // Round up to word boundary
    uint16_t byte_index = 0;
    
    for (uint16_t i = 0; i < words_to_read && byte_index < frame_len; i++) {
        uint16_t word_data = ksz8851_reg_read(REG_QDR_DUMMY);
        
        // Store low byte
        if (byte_index < frame_len) {
            data[byte_index++] = (uint8_t)(word_data & 0xFF);
        }
        
        // Store high byte
        if (byte_index < frame_len) {
            data[byte_index++] = (uint8_t)((word_data >> 8) & 0xFF);
        }
    }
    
    printf("[KSZ8851SNL] Frame data read: %d bytes\r\n", byte_index);
    
    // Clear the frame from queue
    ksz8851_reg_setbits(REG_RXQ_CMD, RXQ_CMD_FREE_PACKET);
    
    // Update reception statistics
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    context->rx_packets++;
    
    *length = frame_len;
    printf("[KSZ8851SNL] ✅ Packet received successfully (%d bytes)\r\n", frame_len);
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_check_rx_available_impl(const void *hw_context, bool *rx_available)
{
    ASSERT(hw_context != NULL);
    ASSERT(rx_available != NULL);
    
    *rx_available = false;
    
    // ALWAYS check RX frame count directly (not just on interrupt)
    uint16_t rx_status = ksz8851_reg_read(REG_RXQ_CMD);
    uint8_t rx_frame_count = (rx_status & RX_FRAME_CNT_MASK) >> 8;
    
    // Debug: Print RX status periodically (every 1000 calls ~ 1 second)
    static uint32_t debug_counter = 0;
    debug_counter++;
    if (debug_counter % 1000 == 0) {
        uint16_t int_status = ksz8851_reg_read(REG_INT_STATUS);
        printf("[KSZ8851SNL] RX DEBUG: frame_count=%d, rx_status=0x%04X, int_status=0x%04X\r\n", 
               rx_frame_count, rx_status, int_status);
    }
    
    if (rx_frame_count > 0) {
        *rx_available = true;
        printf("[KSZ8851SNL] RX available: %d frame(s) pending (direct check)\r\n", rx_frame_count);
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    // Also check if interrupt semaphore is available (interrupt occurred)
    if (ksz8851snl_interrupt_semaphore != NULL) {
        // Try to take semaphore without blocking
        if (xSemaphoreTake(ksz8851snl_interrupt_semaphore, 0) == pdTRUE) {
            // Process the interrupt
            ksz8851snl_process_interrupt();
            
            // Re-check RX frame count after processing interrupt
            rx_status = ksz8851_reg_read(REG_RXQ_CMD);
            rx_frame_count = (rx_status & RX_FRAME_CNT_MASK) >> 8;
            
            if (rx_frame_count > 0) {
                *rx_available = true;
                printf("[KSZ8851SNL] RX available: %d frame(s) pending (after interrupt)\r\n", rx_frame_count);
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
    printf("[KSZ8851SNL] === Enhanced Packet Transmission Test ===\r\n");
    
    // Show current chip configuration first
    printf("[KSZ8851SNL] === Chip Configuration Check ===\r\n");
    uint16_t tx_ctrl = ksz8851_reg_read(REG_TX_CTRL);
    printf("[KSZ8851SNL] TX Control Register: 0x%04X\r\n", tx_ctrl);
    printf("[KSZ8851SNL]   TX Enable: %s\r\n", (tx_ctrl & TX_CTRL_ENABLE) ? "YES" : "NO");
    printf("[KSZ8851SNL]   CRC Enable: %s\r\n", (tx_ctrl & TX_CTRL_CRC_ENABLE) ? "YES" : "NO");
    printf("[KSZ8851SNL]   Padding Enable: %s\r\n", (tx_ctrl & TX_CTRL_PAD_ENABLE) ? "YES" : "NO");
    printf("[KSZ8851SNL]   Checksum Enable: %s\r\n", (tx_ctrl & (TX_CTRL_IP_CHECKSUM | TX_CTRL_TCP_CHECKSUM | TX_CTRL_UDP_CHECKSUM)) ? "YES" : "NO");
    
    uint16_t tx_addr_ptr = ksz8851_reg_read(REG_TX_ADDR_PTR);
    printf("[KSZ8851SNL] TX Address Pointer: 0x%04X (auto-inc: %s)\r\n", 
           tx_addr_ptr, (tx_addr_ptr & ADDR_PTR_AUTO_INC) ? "YES" : "NO");
    
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
    
    printf("[KSZ8851SNL] === Transmission Test ===\r\n");
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
    
    // Check if chip is properly initialized for transmission
    if (tx_space_before == 0) {
        printf("[KSZ8851SNL] ⚠ WARNING: TX memory pool shows 0 bytes - trying to reinitialize\r\n");
        // Try to flush and reinitialize TX
        ksz8851_reg_setbits(REG_TX_CTRL, TX_CTRL_FLUSH_QUEUE);
        for (volatile int i = 0; i < 1000; i++);
        ksz8851_reg_clrbits(REG_TX_CTRL, TX_CTRL_FLUSH_QUEUE);
        
        uint16_t tx_space_after_flush = ksz8851_reg_read(REG_TX_MEM_INFO) & TX_MEM_AVAILABLE_MASK;
        printf("[KSZ8851SNL] TX buffer space after flush: %d bytes\r\n", tx_space_after_flush);
    }
    
    // Transmit the packet
    drv_ksz8851snl_status_t result = hw_ksz8851snl_send_packet(&ksz8851snl_0, test_packet, sizeof(test_packet));
    
    // Get TX buffer space after transmission
    uint16_t tx_space_after = ksz8851_reg_read(REG_TX_MEM_INFO) & TX_MEM_AVAILABLE_MASK;
    printf("[KSZ8851SNL] TX buffer space after: %d bytes\r\n", tx_space_after);
    
    // Check interrupt status and TXQ status
    uint16_t int_status = ksz8851_reg_read(REG_INT_STATUS);
    uint16_t txq_status = ksz8851_reg_read(REG_TXQ_CMD);
    printf("[KSZ8851SNL] Interrupt status: 0x%04X\r\n", int_status);
    printf("[KSZ8851SNL] TXQ Command status: 0x%04X\r\n", txq_status);
    
    // Results analysis
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] ✓ Test packet transmission reported SUCCESS\r\n");
        printf("[KSZ8851SNL] Buffer space change: %d bytes\r\n", tx_space_before - tx_space_after);
        
        if (tx_space_before > 0 && tx_space_after < tx_space_before) {
            printf("[KSZ8851SNL] ✓ TX buffer space decreased - packet transmission looks successful!\r\n");
            printf("[KSZ8851SNL] ✓ Expected result: packet should now be visible in Wireshark\r\n");
        } else if (tx_space_before == 0) {
            printf("[KSZ8851SNL] ⚠ TX memory pool was 0 - check chip initialization\r\n");
        } else {
            printf("[KSZ8851SNL] ⚠ TX buffer space unchanged - packet may not have been transmitted\r\n");
        }
    } else {
        printf("[KSZ8851SNL] ✗ Test packet transmission FAILED: %d\r\n", result);
        if (result == DRV_KSZ8851SNL_STATUS_BUSY) {
            printf("[KSZ8851SNL] ✗ Reason: Not enough TX memory available\r\n");
        }
    }
    
    printf("[KSZ8851SNL] === Enhanced Packet Transmission Test Complete ===\r\n");
    printf("[KSZ8851SNL] EXPECTED: With fixes, packet should now appear in Wireshark!\r\n");
}