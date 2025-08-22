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
#include <stdlib.h>

// Include existing register definitions
#include "app_libs/FreeRTOS-Plus-TCP/source/portable/NetworkInterface/ksz8851snl/ksz8851snl_reg.h"

// CRITICAL MISSING DEFINITION: TXQ Access Control bit (from working commit d39de28)
#define RXQ_SDA                   0x0008    /* Enable TXQ write access (SDA = Start DMA Access) - bit 3 */

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
    uint8_t mac_address[6];  // Current MAC address
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
    .mac_address = {0x00, 0x00, 0x00, 0x00, 0x20, 0x76},  // Default MAC address
};

// Interrupt handling variables
static SemaphoreHandle_t ksz8851snl_interrupt_semaphore = NULL;
static bool ksz8851snl_irq_initialized = false;
static TaskHandle_t ksz8851snl_irq_task_handle = NULL;

// Interrupt statistics and debugging
typedef struct {
    uint32_t total_interrupts;
    uint32_t rx_interrupts;
    uint32_t tx_interrupts;
    uint32_t phy_interrupts;
    uint32_t unknown_interrupts;
    uint32_t empty_interrupts;
    uint32_t semaphore_timeouts;
    uint32_t last_interrupt_status;
    uint32_t gpio_pin_state;
    TickType_t last_interrupt_time;
} ksz8851snl_irq_stats_t;

static ksz8851snl_irq_stats_t irq_stats = {0};

// SPI register access functions forward declarations (must appear before first use)
static uint16_t ksz8851_reg_read(uint16_t reg);
static void ksz8851_reg_write(uint16_t reg, uint16_t wrdata);

// KSZ8851SNL interrupt handler callback
static void ksz8851snl_irq_handler(void)
{
    // Update interrupt statistics
    irq_stats.total_interrupts++;
    irq_stats.gpio_pin_state = gpio_get_pin_level(KSZ8851SNL_INT_PIN);
    
    printf("[KSZ8851SNL] IRQ #%lu! PIN=%s\r\n", 
           irq_stats.total_interrupts, 
           irq_stats.gpio_pin_state ? "HIGH" : "LOW");
    
    // Read interrupt status register
    uint16_t int_status = ksz8851_reg_read(REG_INT_STATUS);
    irq_stats.last_interrupt_status = int_status;
    
    printf("[KSZ8851SNL] INT_STATUS: 0x%04X\r\n", int_status);
    
    // Handle RX interrupt
    if (int_status & INT_RX) {
        irq_stats.rx_interrupts++;
        
        // Read RX frame count from queue status register
        uint16_t rx_frame_count_reg = ksz8851_reg_read(REG_RX_FRAME_CNT_THRES);
        uint8_t frame_count = (rx_frame_count_reg & RX_FRAME_CNT_MASK) >> 8;
        
        // Read RX queue command register status
        uint16_t rxq_status = ksz8851_reg_read(REG_RXQ_CMD);
        
        printf("[KSZ8851SNL] RX IRQ! Frames in queue: %d, RXQ_STATUS: 0x%04X\r\n", 
               frame_count, rxq_status);
        
        // If multiple frames are queued, show additional info
        if (frame_count > 1) {
            printf("[KSZ8851SNL] Multiple frames queued (%d) - processing...\r\n", frame_count);
        }
    }
    
    // Handle TX interrupt
    if (int_status & INT_TX) {
        irq_stats.tx_interrupts++;
        printf("[KSZ8851SNL] 🚀 TX IRQ! Packet transmission completed (IRQ #%lu)\r\n", irq_stats.tx_interrupts);
        
        // Read TX status register for transmission details
        uint16_t tx_status = ksz8851_reg_read(REG_TX_STATUS);
        printf("[KSZ8851SNL] TX_STATUS in IRQ: 0x%04X\r\n", tx_status);
        
        if (tx_status & TX_STAT_ERRORS) {
            printf("[KSZ8851SNL] TX errors in IRQ: 0x%04X\r\n", tx_status & TX_STAT_ERRORS);
        }
    }
    
    // Handle PHY interrupt
    if (int_status & INT_PHY) {
        irq_stats.phy_interrupts++;
        printf("[KSZ8851SNL] PHY IRQ!\r\n");
    }
    
    // Handle SPI errors specifically
    if (int_status & INT_RX_SPI_ERROR) {
        printf("[KSZ8851SNL] ⚠️ SPI ERROR in interrupt handler! This indicates SPI communication issues.\r\n");
        printf("[KSZ8851SNL] SPI errors can prevent FIFO writes from working properly.\r\n");
        // Don't increment error counter here - this is just notification
    }
    
    // Clear handled interrupts
    if (int_status != 0) {
        ksz8851_reg_write(REG_INT_STATUS, int_status);
        printf("[KSZ8851SNL] Cleared IRQ: 0x%04X\r\n", int_status);
    }
}

// Forward declarations (implementations after SPI functions)
static void ksz8851snl_process_interrupt(void);
static void ksz8851snl_interrupt_task(void *param);
static void ksz8851snl_test_registers(void);
static void ksz8851snl_print_irq_stats(void);
static void ksz8851snl_gpio_test(void);
static void ksz8851snl_force_interrupt_test(void);

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
    
    // Set interrupt priority for FreeRTOS compatibility
    // Priority 5 is safe for FreeRTOS API calls (between configMAX_SYSCALL_INTERRUPT_PRIORITY=4 and configLIBRARY_LOWEST_INTERRUPT_PRIORITY=7)
    NVIC_SetPriority(EIC_7_IRQn, 5);
    uint32_t actual_priority = NVIC_GetPriority(EIC_7_IRQn);
    printf("[KSZ8851SNL] Set EIC_7_IRQn priority to 5, actual priority: %lu\r\n", actual_priority);
    
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

    // Create deferred interrupt handler task (processes KSZ IRQ in task context)
    if (ksz8851snl_irq_task_handle == NULL) {
        BaseType_t task_created = xTaskCreate(
            ksz8851snl_interrupt_task,
            "KSZ8851_INT",
            configMINIMAL_STACK_SIZE + 256,
            NULL,
            tskIDLE_PRIORITY + 2,
            &ksz8851snl_irq_task_handle
        );
        if (task_created != pdPASS) {
            printf("[KSZ8851SNL] Failed to create interrupt task\r\n");
            ext_irq_deinit();
            vSemaphoreDelete(ksz8851snl_interrupt_semaphore);
            ksz8851snl_interrupt_semaphore = NULL;
            ksz8851snl_irq_task_handle = NULL;
            return DRV_KSZ8851SNL_STATUS_ERROR;
        }
        printf("[KSZ8851SNL] Interrupt task created\r\n");
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
static drv_ksz8851snl_status_t drv_ksz8851snl_set_mac_address_impl(const void *hw_context, const uint8_t mac_addr[6]);
static drv_ksz8851snl_status_t drv_ksz8851snl_get_mac_address_impl(const void *hw_context, uint8_t mac_addr[6]);

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
    .set_mac_address = drv_ksz8851snl_set_mac_address_impl,
    .get_mac_address = drv_ksz8851snl_get_mac_address_impl,
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
    
    // if (status != DRV_SPI_STATUS_OK) {
    //     printf("[KSZ8851SNL] SPI write error: %d\r\n", status);
    // } else {
    //     printf("[KSZ8851SNL] Register write completed successfully\r\n");
    // }
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
    temp &= ~bits_to_clr;  // Fixed: keep as uint16_t, no cast to uint32_t
    ksz8851_reg_write(reg, temp);
}

// Process KSZ8851SNL interrupt in task context
static void ksz8851snl_process_interrupt(void)
{
    // Read interrupt status register
    uint16_t int_status = ksz8851_reg_read(REG_INT_STATUS);
    
    // Update statistics
    irq_stats.last_interrupt_status = int_status;
    
    if (int_status == 0) {
        irq_stats.empty_interrupts++;
        printf("[KSZ8851SNL] Empty interrupt (status: 0x%04X)\r\n", int_status);
        return; // No interrupts pending
    }
    
    printf("[KSZ8851SNL] Interrupt status: 0x%04X (GPIO PB7: %s)\r\n", 
           int_status, irq_stats.gpio_pin_state ? "HIGH" : "LOW");
    
    // Handle RX interrupt
    if (int_status & INT_RX) {
        irq_stats.rx_interrupts++;
        printf("[KSZ8851SNL] RX interrupt - packet received\r\n");
        // TODO: Signal network stack that packet is available
        // This will be implemented when we update packet processing
    }
    
    // Handle TX interrupt
    if (int_status & INT_TX) {
        irq_stats.tx_interrupts++;
        printf("[KSZ8851SNL] TX interrupt - packet transmitted\r\n");
        // TODO: Signal that TX buffer is available
    }
    
    // Handle PHY link change interrupt
    if (int_status & INT_PHY) {
        irq_stats.phy_interrupts++;
        printf("[KSZ8851SNL] PHY interrupt - link status changed\r\n");
        // TODO: Update link status and notify network stack
    }
    
    // Check for unknown interrupts
    uint16_t known_interrupts = INT_RX | INT_TX | INT_PHY;
    if (int_status & ~known_interrupts) {
        irq_stats.unknown_interrupts++;
        printf("[KSZ8851SNL] Unknown interrupt bits: 0x%04X\r\n", int_status & ~known_interrupts);
    }
    
    // Clear handled interrupts
    ksz8851_reg_write(REG_INT_STATUS, int_status);
}

// Deferred interrupt handler task: not used in simple mode
static void ksz8851snl_interrupt_task(void *param)
{
    (void)param;
    // Not used - interrupt processing is done directly in ISR
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Debug and test functions
static void ksz8851snl_print_irq_stats(void)
{
    printf("\r\n=== KSZ8851SNL Interrupt Statistics ===\r\n");
    printf("Total interrupts:     %lu\r\n", irq_stats.total_interrupts);
    printf("RX interrupts:        %lu\r\n", irq_stats.rx_interrupts);
    printf("TX interrupts:        %lu\r\n", irq_stats.tx_interrupts);
    printf("PHY interrupts:       %lu\r\n", irq_stats.phy_interrupts);
    printf("Unknown interrupts:   %lu\r\n", irq_stats.unknown_interrupts);
    printf("Empty interrupts:     %lu\r\n", irq_stats.empty_interrupts);
    printf("Semaphore timeouts:   %lu\r\n", irq_stats.semaphore_timeouts);
    printf("Last interrupt status: 0x%04lX\r\n", irq_stats.last_interrupt_status);
    printf("Last GPIO PB7 state:  %s\r\n", irq_stats.gpio_pin_state ? "HIGH" : "LOW");
    printf("Last interrupt time:   %lu ticks\r\n", irq_stats.last_interrupt_time);
    printf("Current GPIO PB7:     %s\r\n", gpio_get_pin_level(KSZ8851SNL_INT_PIN) ? "HIGH" : "LOW");
    printf("========================================\r\n\r\n");
}

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

static void ksz8851snl_force_interrupt_test(void)
{
    printf("\r\n=== Force Interrupt Test ===\r\n");
    
    // Read current interrupt status
    uint16_t int_status_before = ksz8851_reg_read(REG_INT_STATUS);
    printf("Interrupt status before: 0x%04X\r\n", int_status_before);
    
    // Clear all pending interrupts to potentially trigger new ones
    if (int_status_before != 0) {
        printf("Clearing pending interrupts...\r\n");
        ksz8851_reg_write(REG_INT_STATUS, int_status_before);
        
        // Wait a moment
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Read status after clear
    uint16_t int_status_after = ksz8851_reg_read(REG_INT_STATUS);
    printf("Interrupt status after:  0x%04X\r\n", int_status_after);
    
    // Try to trigger PHY interrupt by reading PHY registers
    printf("Reading PHY registers to potentially trigger events...\r\n");
    uint16_t phy_status = ksz8851_reg_read(0xE6);
    uint16_t port_status = ksz8851_reg_read(0xF8);
    printf("PHY Status: 0x%04X, Port Status: 0x%04X\r\n", phy_status, port_status);
    
    printf("==============================\r\n\r\n");
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
    
    // Configure RX Control Register 1 (RXCR1) - enable required RX modes
    printf("[KSZ8851SNL] Configuring RX Control Register 1 (RXCR1)\r\n");
    uint16_t rx_ctrl1 = RX_CTRL_ENABLE | RX_CTRL_BROADCAST | 
                        RX_CTRL_UNICAST | RX_CTRL_FLOW_ENABLE;
    ksz8851_reg_write(REG_RX_CTRL1, rx_ctrl1);
    printf("[KSZ8851SNL] RX Control 1 configured: 0x%04X\r\n", rx_ctrl1);
    
    // Configure RX Control Register 2 (RXCR2) - set burst length for SPI
    printf("[KSZ8851SNL] Configuring RX Control Register 2 (RXCR2)\r\n");
    uint16_t rx_ctrl2 = RX_CTRL_BURST_LEN_FRAME;
    ksz8851_reg_write(REG_RX_CTRL2, rx_ctrl2);
    printf("[KSZ8851SNL] RX Control 2 configured: 0x%04X\r\n", rx_ctrl2);
    
    // Configure RX Queue Control Register (RXQCR) - enable frame count interrupt and auto dequeue
    printf("[KSZ8851SNL] Configuring RX Queue Control Register (RXQCR)\r\n");
    uint16_t rx_queue_ctrl = RXQ_FRAME_CNT_INT | RXQ_AUTO_DEQUEUE;
    ksz8851_reg_write(REG_RXQ_CMD, rx_queue_ctrl);
    printf("[KSZ8851SNL] RX Queue Control configured: 0x%04X\r\n", rx_queue_ctrl);
    
    // Enable auto-increment for RX frame data pointer
    printf("[KSZ8851SNL] Configuring RX Frame Data Pointer (RXFDPR)\r\n");
    ksz8851_reg_write(REG_RX_ADDR_PTR, ADDR_PTR_AUTO_INC);
    printf("[KSZ8851SNL] RX Frame Pointer configured with auto-increment\r\n");
    
    // Set RX frame count threshold to 1 frame
    printf("[KSZ8851SNL] Configuring RX Frame Count Threshold\r\n");
    ksz8851_reg_write(REG_RX_FRAME_CNT_THRES, 1);
    printf("[KSZ8851SNL] RX Frame Count Threshold set to 1 frame\r\n");
    
    // Configure TX Control Register (TXCR) - enable transmission features
    printf("[KSZ8851SNL] Configuring TX Control Register (TXCR)\r\n");
    uint16_t tx_ctrl = TX_CTRL_ENABLE |           // Enable transmit
                       TX_CTRL_CRC_ENABLE |       // Enable CRC generation
                       TX_CTRL_PAD_ENABLE |       // Enable padding for short frames
                       TX_CTRL_FLOW_ENABLE |      // Enable flow control
                       TX_CTRL_IP_CHECKSUM |      // Enable IP checksum generation
                       TX_CTRL_TCP_CHECKSUM |     // Enable TCP checksum generation
                       TX_CTRL_UDP_CHECKSUM |     // Enable UDP checksum generation
                       TX_CTRL_ICMP_CHECKSUM;     // Enable ICMP checksum generation
    ksz8851_reg_write(REG_TX_CTRL, tx_ctrl);
    printf("[KSZ8851SNL] TX Control configured: 0x%04X\r\n", tx_ctrl);
    
    // Configure TX Queue Control Register (TXQCR) 
    // Try manual enqueue mode instead of auto-enqueue for better control
    printf("[KSZ8851SNL] Configuring TX Queue Control Register (TXQCR)\r\n");
    uint16_t tx_queue_ctrl = TXQ_MEM_AVAILABLE_INT; // Enable TX memory available interrupt only
    ksz8851_reg_write(REG_TXQ_CMD, tx_queue_ctrl);
    printf("[KSZ8851SNL] TX Queue Control configured: 0x%04X (MANUAL_ENQUEUE + MEM_INT)\r\n", tx_queue_ctrl);
    
    // Set MAC address from configuration
    if (config != NULL && hw_ksz8851snl_is_mac_valid(config->mac_addr)) {
        printf("[KSZ8851SNL] Setting MAC address from configuration\r\n");
        drv_ksz8851snl_status_t mac_status = drv_ksz8851snl_set_mac_address_impl(hw_context, config->mac_addr);
        if (mac_status != DRV_KSZ8851SNL_STATUS_OK) {
            printf("[KSZ8851SNL] Warning: Failed to set MAC address: %d\r\n", mac_status);
        }
    } else {
        printf("[KSZ8851SNL] Using default MAC address\r\n");
    }
    
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

// TX FIFO write helper functions
static void ksz8851_fifo_write_begin(void)
{
    // CRITICAL FIX: Send only 1-byte FIFO_WRITE command (0xC0) as per working commit d39de28
    // The extra dummy byte was confusing the KSZ8851SNL FIFO state machine!
    uint8_t cmd = FIFO_WRITE;  // 0xC0 only
    
    printf("[KSZ8851SNL] FIFO_WRITE_BEGIN: Sending command 0x%02X (1 byte only)\r\n", cmd);
    
    // Start SPI transaction for FIFO write
    drv_spi_cs_set_low();
    
    // Send FIFO write command (1 byte only!)
    drv_spi_status_t status = hw_spi_transfer(&spi_4, &cmd, NULL, 1);
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851SNL] FIFO write begin failed: %d\r\n", status);
        drv_spi_cs_set_high();
    } else {
        printf("[KSZ8851SNL] FIFO_WRITE_BEGIN: Command sent successfully - ready for data\r\n");
    }
    // Note: CS remains low for continued data transfer
}

static void ksz8851_fifo_write_end(void)
{
    // End SPI transaction for FIFO write
    printf("[KSZ8851SNL] FIFO_WRITE_END: Ending SPI transaction\r\n");
    drv_spi_cs_set_high();
}

static drv_ksz8851snl_status_t ksz8851_fifo_write_data(const uint8_t *data, uint16_t length)
{
    ASSERT(data != NULL);
    ASSERT(length > 0);
    
    printf("[KSZ8851SNL] FIFO_WRITE_DATA: Writing %d bytes\r\n", length);
    
    // Write data in chunks that are compatible with SPI transfer
    uint16_t bytes_written = 0;
    const uint16_t max_chunk_size = 256; // Reasonable chunk size for SPI
    
    while (bytes_written < length) {
        uint16_t chunk_size = length - bytes_written;
        if (chunk_size > max_chunk_size) {
            chunk_size = max_chunk_size;
        }
        
        printf("[KSZ8851SNL] FIFO_WRITE_DATA: Chunk at offset %d, size %d\r\n", 
               bytes_written, chunk_size);
        
        // Create dummy response buffer for SPI transfer
        uint8_t *dummy_resp = malloc(chunk_size);
        if (dummy_resp == NULL) {
            printf("[KSZ8851SNL] Failed to allocate memory for SPI transfer\r\n");
            return DRV_KSZ8851SNL_STATUS_ERROR;
        }
        
        drv_spi_status_t status = hw_spi_transfer(&spi_4, &data[bytes_written], dummy_resp, chunk_size);
        
        free(dummy_resp);
        
        if (status != DRV_SPI_STATUS_OK) {
            printf("[KSZ8851SNL] FIFO write data failed at offset %d: %d\r\n", bytes_written, status);
            return DRV_KSZ8851SNL_STATUS_ERROR;
        }
        
        bytes_written += chunk_size;
    }
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t ksz8851_check_tx_space(uint16_t required_bytes)
{
    // Read TX memory info register to check available space
    uint16_t tx_mem_info = ksz8851_reg_read(REG_TX_MEM_INFO);
    uint16_t available_bytes = tx_mem_info & TX_MEM_AVAILABLE_MASK;
    
    printf("[KSZ8851SNL] TX memory available: %d bytes, required: %d bytes\r\n", 
           available_bytes, required_bytes);
    
    if (available_bytes < required_bytes) {
        printf("[KSZ8851SNL] Insufficient TX memory space\r\n");
        return DRV_KSZ8851SNL_STATUS_BUSY;
    }
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

// Packet transmission implementation
static drv_ksz8851snl_status_t drv_ksz8851snl_send_packet_impl(const void *hw_context, const uint8_t *data, uint16_t length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);
    
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    
    printf("[KSZ8851SNL] Sending packet - length: %d bytes\r\n", length);
    
    // Validate packet length (Ethernet frame size limits)
    if (length < 60 || length > 1518) {
        printf("[KSZ8851SNL] Invalid packet length: %d (must be 60-1518 bytes)\r\n", length);
        context->tx_errors++;
        return DRV_KSZ8851SNL_STATUS_INVALID_PARAM;
    }
    
    // Calculate required TX memory: packet + 4-byte TX header + alignment
    uint16_t required_bytes = length + 4;  // 4 bytes for TX control header
    if (required_bytes & 1) {
        required_bytes++;  // Ensure even byte count for proper alignment
    }
    
    // Check link status first - don't try to send if link is down
    drv_ksz8851snl_status_info_t status_info;
    drv_ksz8851snl_status_t link_status = drv_ksz8851snl_get_status_impl(hw_context, &status_info);
    if (link_status == DRV_KSZ8851SNL_STATUS_OK && !status_info.link_up) {
        printf("[KSZ8851SNL] ⚠️ Cannot send packet: Link is DOWN\r\n");
        context->tx_errors++;
        return DRV_KSZ8851SNL_STATUS_NO_LINK;
    }
    
    // Check TX memory availability
    drv_ksz8851snl_status_t space_status = ksz8851_check_tx_space(required_bytes);
    if (space_status != DRV_KSZ8851SNL_STATUS_OK) {
        context->tx_errors++;
        return space_status;
    }
    
    // Prepare TX control header (4 bytes)
    // Based on KSZ8851SNL datasheet: [Control Word][Byte Count]
    uint8_t tx_header[4];
    
    // TX Control Word (bits 15-0):
    // Bit 15: TXIC (TX Interrupt on Completion) - set to enable TX interrupt
    // Bits 14-0: Reserved/frame ID
    uint16_t tx_control = TX_CTRL_INTERRUPT_ON; // Enable TX completion interrupt
    
    // TX Byte Count (packet length)
    uint16_t tx_byte_count = length;
    
    // Pack header in little-endian format (as expected by KSZ8851SNL)
    tx_header[0] = tx_control & 0xFF;
    tx_header[1] = (tx_control >> 8) & 0xFF;
    tx_header[2] = tx_byte_count & 0xFF;
    tx_header[3] = (tx_byte_count >> 8) & 0xFF;
    
    printf("[KSZ8851SNL] TX header: [0x%02X 0x%02X 0x%02X 0x%02X] (ctrl=0x%04X, len=%d)\r\n",
           tx_header[0], tx_header[1], tx_header[2], tx_header[3], tx_control, tx_byte_count);
    
    // Check TX FIFO state before writing
    uint16_t tx_mem_before = ksz8851_reg_read(REG_TX_MEM_INFO);
    printf("[KSZ8851SNL] TX memory before FIFO write: %d bytes\r\n", tx_mem_before & TX_MEM_AVAILABLE_MASK);
    
    // Clear any existing SPI errors before FIFO write
    uint16_t pre_int_status = ksz8851_reg_read(REG_INT_STATUS);
    if (pre_int_status & INT_RX_SPI_ERROR) {
        printf("[KSZ8851SNL] Clearing pre-existing SPI error before FIFO write\r\n");
        ksz8851_reg_write(REG_INT_STATUS, INT_RX_SPI_ERROR);
    }
    
    // CRITICAL FIX: Enable TXQ write access (RXQ_SDA) as per working commit d39de28
    printf("[KSZ8851SNL] Step 1: Enabling TXQ write access (RXQ_SDA)\r\n");
    uint16_t rxq_before = ksz8851_reg_read(REG_RXQ_CMD);
    ksz8851_reg_setbits(REG_RXQ_CMD, RXQ_SDA);
    uint16_t rxq_after = ksz8851_reg_read(REG_RXQ_CMD);
    printf("[KSZ8851SNL]   RXQ_CMD: 0x%04X -> 0x%04X (RXQ_SDA enabled)\r\n", rxq_before, rxq_after);
    
    // Step 2: FIFO write sequence (CS low for entire operation)
    printf("[KSZ8851SNL] Step 2: Beginning FIFO write sequence\r\n");
    ksz8851_fifo_write_begin();
    
    // Write TX header to FIFO
    drv_ksz8851snl_status_t header_status = ksz8851_fifo_write_data(tx_header, 4);
    if (header_status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] Failed to write TX header\r\n");
        ksz8851_fifo_write_end();
        context->tx_errors++;
        return header_status;
    }
    
    // Write packet data to FIFO
    drv_ksz8851snl_status_t data_status = ksz8851_fifo_write_data(data, length);
    if (data_status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] Failed to write packet data\r\n");
        ksz8851_fifo_write_end();
        context->tx_errors++;
        return data_status;
    }
    
    // If odd packet length, write one padding byte for alignment
    if (length & 1) {
        uint8_t padding = 0x00;
        drv_ksz8851snl_status_t pad_status = ksz8851_fifo_write_data(&padding, 1);
        if (pad_status != DRV_KSZ8851SNL_STATUS_OK) {
            printf("[KSZ8851SNL] Failed to write padding byte\r\n");
            ksz8851_fifo_write_end();
            context->tx_errors++;
            return pad_status;
        }
        printf("[KSZ8851SNL] Added padding byte for alignment\r\n");
    }
    
    ksz8851_fifo_write_end();
    printf("[KSZ8851SNL] FIFO write operation completed\r\n");
    
    // CRITICAL FIX: Disable TXQ write access (RXQ_SDA) as per working commit d39de28
    printf("[KSZ8851SNL] Step 3: Disabling TXQ write access (RXQ_SDA)\r\n");
    ksz8851_reg_clrbits(REG_RXQ_CMD, RXQ_SDA);
    uint16_t rxq_final = ksz8851_reg_read(REG_RXQ_CMD);
    printf("[KSZ8851SNL]   RXQ_CMD after disable: 0x%04X (RXQ_SDA cleared)\r\n", rxq_final);
    
    // Check for SPI errors immediately after FIFO write
    uint16_t post_fifo_status = ksz8851_reg_read(REG_INT_STATUS);
    if (post_fifo_status & INT_RX_SPI_ERROR) {
        printf("[KSZ8851SNL] ⚠️ SPI ERROR occurred during FIFO write!\r\n");
        ksz8851_reg_write(REG_INT_STATUS, INT_RX_SPI_ERROR);
        context->tx_errors++;
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Check TX FIFO state after writing
    uint16_t tx_mem_after = ksz8851_reg_read(REG_TX_MEM_INFO);
    printf("[KSZ8851SNL] TX memory after FIFO write: %d bytes\r\n", tx_mem_after & TX_MEM_AVAILABLE_MASK);
    printf("[KSZ8851SNL] TX memory used: %d bytes\r\n", (tx_mem_before & TX_MEM_AVAILABLE_MASK) - (tx_mem_after & TX_MEM_AVAILABLE_MASK));
    
    // Manual enqueue mode - trigger transmission with ENQUEUE bit
    printf("[KSZ8851SNL] Triggering transmission (MANUAL_ENQUEUE mode)...\r\n");
    
    // Read current TXQ command register
    uint16_t current_txq = ksz8851_reg_read(REG_TXQ_CMD);
    printf("[KSZ8851SNL] Current TXQ_CMD: 0x%04X\r\n", current_txq);
    
    // First, verify TX is enabled in TXCR register
    uint16_t tx_ctrl_check = ksz8851_reg_read(REG_TX_CTRL);
    printf("[KSZ8851SNL] TX_CTRL register: 0x%04X (TX enabled: %s)\r\n", 
           tx_ctrl_check, (tx_ctrl_check & TX_CTRL_ENABLE) ? "YES" : "NO");
    
    // Use ONLY TXQ_ENQUEUE (0x0001) as per working commit d39de28
    uint16_t enqueue_cmd = TXQ_ENQUEUE;
    printf("[KSZ8851SNL] Writing ENQUEUE command: 0x%04X (TXQ_ENQUEUE only)\r\n", enqueue_cmd);
    ksz8851_reg_write(REG_TXQ_CMD, enqueue_cmd);
    
    // Small delay for register write to take effect
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // Read back to confirm
    uint16_t new_txq = ksz8851_reg_read(REG_TXQ_CMD);
    printf("[KSZ8851SNL] TXQ_CMD after ENQUEUE: 0x%04X\r\n", new_txq);
    
    // If ENQUEUE bit is still there, it means the command didn't execute
    if (new_txq & TXQ_ENQUEUE) {
        printf("[KSZ8851SNL] ⚠️ ENQUEUE bit still set - command not executed\r\n");
    } else if (new_txq == 0x0000) {
        printf("[KSZ8851SNL] ⚠️ TXQ register cleared - possible transmission started\r\n");
    }
    
    // Check interrupt status immediately after transmission trigger
    uint16_t int_status_before = ksz8851_reg_read(REG_INT_STATUS);
    printf("[KSZ8851SNL] INT_STATUS before transmission: 0x%04X\r\n", int_status_before);
    
    // Longer delay to allow transmission to complete
    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms should be enough for 100Mbps transmission
    
    // Check interrupt status after short delay
    uint16_t int_status_after = ksz8851_reg_read(REG_INT_STATUS);
    printf("[KSZ8851SNL] INT_STATUS after transmission: 0x%04X\r\n", int_status_after);
    
    // Check for SPI errors specifically
    if (int_status_after & INT_RX_SPI_ERROR) {
        printf("[KSZ8851SNL] ⚠️ SPI ERROR detected (0x0002) - this may explain FIFO write failures!\r\n");
        // Clear SPI error
        ksz8851_reg_write(REG_INT_STATUS, INT_RX_SPI_ERROR);
        printf("[KSZ8851SNL] SPI error cleared\r\n");
    }
    
    // Also check TX status register for detailed transmission info
    uint16_t tx_status = ksz8851_reg_read(REG_TX_STATUS);
    printf("[KSZ8851SNL] TX_STATUS register: 0x%04X\r\n", tx_status);
    
    // Check TX memory info again to see if packet was consumed
    uint16_t tx_mem_final = ksz8851_reg_read(REG_TX_MEM_INFO);
    printf("[KSZ8851SNL] TX memory final: %d bytes (vs %d before)\r\n", 
           tx_mem_final & TX_MEM_AVAILABLE_MASK, tx_mem_before & TX_MEM_AVAILABLE_MASK);
    
    // Check interrupt mask to ensure TX interrupts are enabled
    uint16_t int_mask = ksz8851_reg_read(REG_INT_MASK);
    printf("[KSZ8851SNL] INT_MASK register: 0x%04X (TX enabled: %s)\r\n", 
           int_mask, (int_mask & INT_TX) ? "YES" : "NO");
    
    if (int_status_after & INT_TX) {
        printf("[KSZ8851SNL] ✅ TX interrupt detected in status register!\r\n");
        // Clear the TX interrupt
        ksz8851_reg_write(REG_INT_STATUS, INT_TX);
        printf("[KSZ8851SNL] TX interrupt cleared\r\n");
    } else {
        printf("[KSZ8851SNL] ⚠️  TX interrupt NOT detected in status register\r\n");
        
        // Check if any other interrupts are set that might be masking TX
        if (int_status_after != 0) {
            printf("[KSZ8851SNL] Other interrupts present: 0x%04X\r\n", int_status_after);
        }
        
        // Additional debugging - try reading TX status multiple times
        printf("[KSZ8851SNL] Extended TX debugging:\r\n");
        for (int i = 0; i < 5; i++) {
            vTaskDelay(pdMS_TO_TICKS(5)); // 5ms delay
            uint16_t int_check = ksz8851_reg_read(REG_INT_STATUS);
            uint16_t tx_check = ksz8851_reg_read(REG_TX_STATUS);
            uint16_t txq_check = ksz8851_reg_read(REG_TXQ_CMD);
            printf("[KSZ8851SNL]   [%d] INT: 0x%04X, TX_STATUS: 0x%04X, TXQ: 0x%04X\r\n", 
                   i, int_check, tx_check, txq_check);
            
            if (int_check & INT_TX) {
                printf("[KSZ8851SNL] ✅ TX interrupt detected on check %d!\r\n", i);
                ksz8851_reg_write(REG_INT_STATUS, INT_TX);
                break;
            }
        }
    }
    
    if (tx_status & TX_STAT_ERRORS) {
        printf("[KSZ8851SNL] ❌ TX errors detected: 0x%04X\r\n", tx_status & TX_STAT_ERRORS);
        context->tx_errors++;
    } else {
        context->tx_packets++;
    }
    
    printf("[KSZ8851SNL] Packet transmission completed\r\n");
    printf("[KSZ8851SNL] TX Statistics: %lu packets sent, %lu errors\r\n", 
           context->tx_packets, context->tx_errors);
    
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

static drv_ksz8851snl_status_t drv_ksz8851snl_set_mac_address_impl(const void *hw_context, const uint8_t mac_addr[6])
{
    ASSERT(hw_context != NULL);
    ASSERT(mac_addr != NULL);
    
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    
    printf("[KSZ8851SNL] Setting MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    
    // Validate MAC address
    if (!hw_ksz8851snl_is_mac_valid(mac_addr)) {
        printf("[KSZ8851SNL] ERROR: Invalid MAC address\r\n");
        return DRV_KSZ8851SNL_STATUS_INVALID_MAC;
    }
    
    // Write MAC address to KSZ8851SNL registers
    // The KSZ8851SNL stores MAC address in registers 0x10-0x15 (REG_MAC_ADDR_0 to REG_MAC_ADDR_5)
    // The format is: MARL (0x10,0x11), MARM (0x12,0x13), MARH (0x14,0x15)
    // Each register pair stores 2 bytes in little-endian format
    
    ksz8851_reg_write(REG_MAC_ADDR_0, (mac_addr[1] << 8) | mac_addr[0]);  // MARL
    ksz8851_reg_write(REG_MAC_ADDR_2, (mac_addr[3] << 8) | mac_addr[2]);  // MARM
    ksz8851_reg_write(REG_MAC_ADDR_4, (mac_addr[5] << 8) | mac_addr[4]);  // MARH
    
    // Store MAC address in context
    memcpy(context->mac_address, mac_addr, 6);
    
    printf("[KSZ8851SNL] MAC address set successfully\r\n");
    return DRV_KSZ8851SNL_STATUS_OK;
}

static drv_ksz8851snl_status_t drv_ksz8851snl_get_mac_address_impl(const void *hw_context, uint8_t mac_addr[6])
{
    ASSERT(hw_context != NULL);
    ASSERT(mac_addr != NULL);
    
    drv_ksz8851snl_hw_context_t *context = (drv_ksz8851snl_hw_context_t *)hw_context;
    
    // Read MAC address from KSZ8851SNL registers
    uint16_t marl = ksz8851_reg_read(REG_MAC_ADDR_0);
    uint16_t marm = ksz8851_reg_read(REG_MAC_ADDR_2);
    uint16_t marh = ksz8851_reg_read(REG_MAC_ADDR_4);
    
    // Extract bytes from register pairs (little-endian format)
    mac_addr[0] = marl & 0xFF;
    mac_addr[1] = (marl >> 8) & 0xFF;
    mac_addr[2] = marm & 0xFF;
    mac_addr[3] = (marm >> 8) & 0xFF;
    mac_addr[4] = marh & 0xFF;
    mac_addr[5] = (marh >> 8) & 0xFF;
    
    // Update context with current MAC
    memcpy(context->mac_address, mac_addr, 6);
    
    printf("[KSZ8851SNL] Current MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    
    return DRV_KSZ8851SNL_STATUS_OK;
}

// Public MAC address configuration functions
drv_ksz8851snl_status_t bsp_ksz8851snl_set_mac_address(const uint8_t mac_addr[6])
{
    return hw_ksz8851snl_set_mac_address(&ksz8851snl_0, mac_addr);
}

drv_ksz8851snl_status_t bsp_ksz8851snl_get_mac_address(uint8_t mac_addr[6])
{
    return hw_ksz8851snl_get_mac_address(&ksz8851snl_0, mac_addr);
}

// Public debug and test functions
void ksz8851snl_debug_print_irq_stats(void)
{
    ksz8851snl_print_irq_stats();
}

void ksz8851snl_debug_test_registers(void)
{
    ksz8851snl_test_registers();
}

void ksz8851snl_debug_gpio_test(void)
{
    ksz8851snl_gpio_test();
}

void ksz8851snl_debug_force_interrupt_test(void)
{
    ksz8851snl_force_interrupt_test();
}