#include "bsp_eth_ksz8851.h"
#include "driver_eth_ksz8851.h"
#include "driver_spi.h"
#include "bsp_spi.h"
#include "utils_assert.h"
#include "printf.h"

// KSZ8851 Register Definitions (extracted from Oryx Embedded driver)
// Core registers
#define KSZ8851_CCR                                     0x08
#define KSZ8851_MARL                                    0x10
#define KSZ8851_MARM                                    0x12
#define KSZ8851_MARH                                    0x14
#define KSZ8851_OBCR                                    0x20
#define KSZ8851_EEPCR                                   0x22
#define KSZ8851_MBIR                                    0x24
#define KSZ8851_GRR                                     0x26
#define KSZ8851_WFCR                                    0x2A

// Transmit/Receive Control Registers
#define KSZ8851_TXCR                                    0x70
#define KSZ8851_TXSR                                    0x72
#define KSZ8851_RXCR1                                   0x74
#define KSZ8851_RXCR2                                   0x76
#define KSZ8851_TXMIR                                   0x78
#define KSZ8851_RXFHSR                                  0x7C
#define KSZ8851_RXFHBCR                                 0x7E
#define KSZ8851_TXQCR                                   0x80
#define KSZ8851_RXQCR                                   0x82
#define KSZ8851_TXFDPR                                  0x84
#define KSZ8851_RXFDPR                                  0x86
#define KSZ8851_RXDTTR                                  0x8C
#define KSZ8851_RXDBCTR                                 0x8E
#define KSZ8851_IER                                     0x90
#define KSZ8851_ISR                                     0x92

// QMU Receive Queue Watermark Control Registers
#define KSZ8851_FCLWR                                   0xB0
#define KSZ8851_FCHWR                                   0xB2
#define KSZ8851_FCOWR                                   0xB4

// Global Control Registers
#define KSZ8851_CIDER                                   0xC0
#define KSZ8851_CGCR                                    0xC6
#define KSZ8851_IACR                                    0xC8
#define KSZ8851_IADLR                                   0xD0
#define KSZ8851_IADHR                                   0xD2

// Power Management Control Registers
#define KSZ8851_PMECR                                   0xD4

// PHY Control Registers
#define KSZ8851_PHYRR                                   0xD8
#define KSZ8851_P1MBCR                                  0xE4
#define KSZ8851_P1MBSR                                  0xE6
#define KSZ8851_PHY1ILR                                 0xE8
#define KSZ8851_PHY1IHR                                 0xEA
#define KSZ8851_P1ANAR                                  0xEC
#define KSZ8851_P1ANLPR                                 0xEE
#define KSZ8851_P1SCLMD                                 0xF4
#define KSZ8851_P1CR                                    0xF6
#define KSZ8851_P1SR                                    0xF8

// SPI Command Definitions (based on KSZ8851 datasheet)
#define KSZ8851_CMD_READ                                0x00
#define KSZ8851_CMD_WRITE                               0x40
#define KSZ8851_CMD_FIFO_READ                           0x80
#define KSZ8851_CMD_FIFO_WRITE                          0xC0

// Address mask for register access
#define KSZ8851_ADDR_MASK                               0x3F

// Register Bit Definitions
#define KSZ8851_CCR_EEPROM                              0x0200
#define KSZ8851_CCR_SPI                                 0x0100
#define KSZ8851_CCR_8BIT                                0x0080
#define KSZ8851_CCR_16BIT                               0x0040
#define KSZ8851_CCR_32BIT                               0x0020

#define KSZ8851_GRR_QMU_MODULE_SOFT_RESET               0x0002
#define KSZ8851_GRR_GLOBAL_SOFT_RESET                   0x0001

#define KSZ8851_TXCR_ICMP_CHECKSUM                      0x0100
#define KSZ8851_TXCR_UDP_CHECKSUM                       0x0080
#define KSZ8851_TXCR_TCP_CHECKSUM                       0x0040
#define KSZ8851_TXCR_IP_CHECKSUM                        0x0020
#define KSZ8851_TXCR_FLUSH_QUEUE                        0x0010
#define KSZ8851_TXCR_FLOW_CONTROL_ENABLE                0x0008
#define KSZ8851_TXCR_PAD_ENABLE                         0x0004
#define KSZ8851_TXCR_CRC_ENABLE                         0x0002
#define KSZ8851_TXCR_ENABLE                             0x0001

#define KSZ8851_RXCR1_FLUSH_QUEUE                       0x8000
#define KSZ8851_RXCR1_UDP_CHECKSUM                      0x4000
#define KSZ8851_RXCR1_TCP_CHECKSUM                      0x2000
#define KSZ8851_RXCR1_IP_CHECKSUM                       0x1000
#define KSZ8851_RXCR1_MAC_FILTER                        0x0800
#define KSZ8851_RXCR1_FLOW_CONTROL_ENABLE               0x0400
#define KSZ8851_RXCR1_BAD_PACKET_ENABLE                 0x0200
#define KSZ8851_RXCR1_MULTICAST_ENABLE                  0x0100
#define KSZ8851_RXCR1_BROADCAST_ENABLE                  0x0080
#define KSZ8851_RXCR1_ALL_MULTICAST_ENABLE              0x0040
#define KSZ8851_RXCR1_UNICAST_ENABLE                    0x0020
#define KSZ8851_RXCR1_PROMISCUOUS_MODE                  0x0010
#define KSZ8851_RXCR1_STRIP_CRC                         0x0008
#define KSZ8851_RXCR1_INVERSE_FILTERING                 0x0002
#define KSZ8851_RXCR1_ENABLE                            0x0001

#define KSZ8851_RXQCR_TWOBYTE_OFFSET                    0x0200
#define KSZ8851_RXQCR_AUTO_DEQUEUE                      0x0010
#define KSZ8851_RXQCR_START                             0x0008
#define KSZ8851_RXQCR_FREE_PACKET                       0x0001

#define KSZ8851_TXQCR_ENQUEUE_PACKET                    0x0001

#define KSZ8851_IER_PHY                                 0x8000
#define KSZ8851_IER_TX                                  0x4000
#define KSZ8851_IER_RX                                  0x2000

#define KSZ8851_ISR_PHY                                 0x8000
#define KSZ8851_ISR_TX                                  0x4000
#define KSZ8851_ISR_RX                                  0x2000

#define KSZ8851_RXFHSR_VALID                            0x8000
#define KSZ8851_RXFHSR_ICMP_ERROR                       0x2000
#define KSZ8851_RXFHSR_IP_ERROR                         0x1000
#define KSZ8851_RXFHSR_TCP_ERROR                        0x0800
#define KSZ8851_RXFHSR_UDP_ERROR                        0x0400
#define KSZ8851_RXFHSR_BROADCAST_FRAME                  0x0080
#define KSZ8851_RXFHSR_MULTICAST_FRAME                  0x0040
#define KSZ8851_RXFHSR_UNICAST_FRAME                    0x0020
#define KSZ8851_RXFHSR_PHY_ERROR                        0x0010
#define KSZ8851_RXFHSR_ETHERNET_TYPE_FRAME              0x0008
#define KSZ8851_RXFHSR_TOO_LONG                         0x0004
#define KSZ8851_RXFHSR_RUNT_ERROR                       0x0002
#define KSZ8851_RXFHSR_CRC_ERROR                        0x0001

#define KSZ8851_P1MBSR_LINK_STATUS                      0x0004
#define KSZ8851_P1MBSR_AUTONEG_COMPLETE                 0x0020

#define KSZ8851_P1SR_LINK_GOOD                          0x0020
#define KSZ8851_P1SR_SPEED_100MBPS                      0x0400
#define KSZ8851_P1SR_FULL_DUPLEX                        0x0200

#define KSZ8851_PHYRR_PHY_RESET                         0x0001

#define KSZ8851_P1MBCR_RESTART_AUTONEG                  0x0200

// Chip ID
#define KSZ8851_CHIP_ID_MASK                            0xFFF0
#define KSZ8851_CHIP_ID_8851_16                         0x8870

// TX/RX Buffer sizes
#define KSZ8851_ETH_TX_BUFFER_SIZE                      1536
#define KSZ8851_ETH_RX_BUFFER_SIZE                      1536

// Hardware context structure
typedef struct {
    drv_spi_t *spi_handle;
    drv_spi_config_t spi_config;
    drv_eth_ksz8851_callback_t rx_callback;
    drv_eth_ksz8851_callback_t tx_callback;
    drv_eth_ksz8851_callback_t link_callback;
    drv_eth_ksz8851_callback_t error_callback;
    uint8_t mac_addr[6];
    bool is_ksz8851_init;
} drv_eth_ksz8851_hw_context_t;

// Static hardware context
static drv_eth_ksz8851_hw_context_t drv_eth_ksz8851_hw_context_0 = {
    .spi_handle = &spi_4,
    .spi_config = {
        .baudrate = 10000000,  // 10 MHz
        .clock_polarity = 0,   // CPOL = 0
        .clock_phase = 0,      // CPHA = 0
        .bits_per_transfer = 8,
        .cs_delay_before = 1,
        .cs_delay_after = 1,
    },
    .rx_callback = NULL,
    .tx_callback = NULL,
    .link_callback = NULL,
    .error_callback = NULL,
    .mac_addr = {0x00, 0x10, 0xA1, 0x86, 0x95, 0x11},  // Default MAC
    .is_ksz8851_init = false,
};

// Forward declarations
static drv_eth_ksz8851_status_t drv_eth_ksz8851_init_impl(const void *hw_context, const drv_eth_ksz8851_config_t *config);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_deinit_impl(const void *hw_context);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_enable_impl(const void *hw_context);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_disable_impl(const void *hw_context);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_read_reg_impl(const void *hw_context, uint16_t reg, uint16_t *value);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_write_reg_impl(const void *hw_context, uint16_t reg, uint16_t value);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_set_bits_impl(const void *hw_context, uint16_t reg, uint16_t mask);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_clear_bits_impl(const void *hw_context, uint16_t reg, uint16_t mask);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_send_packet_impl(const void *hw_context, const uint8_t *data, uint16_t length);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_receive_packet_impl(const void *hw_context, uint8_t *data, uint16_t max_length, uint16_t *actual_length);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_get_rx_status_impl(const void *hw_context, drv_eth_ksz8851_rx_status_t *rx_status);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_get_link_status_impl(const void *hw_context, bool *link_up);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_restart_autoneg_impl(const void *hw_context);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_get_link_speed_impl(const void *hw_context, bool *speed_100mbps, bool *full_duplex);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_enable_irq_impl(const void *hw_context);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_disable_irq_impl(const void *hw_context);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_irq_handler_impl(const void *hw_context);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_set_mac_addr_impl(const void *hw_context, const uint8_t mac_addr[6]);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_get_mac_addr_impl(const void *hw_context, uint8_t mac_addr[6]);
static drv_eth_ksz8851_status_t drv_eth_ksz8851_register_callback_impl(const void *hw_context, 
                                                                        drv_eth_ksz8851_cb_type_t type, 
                                                                        drv_eth_ksz8851_callback_t callback);

// Global driver instance
drv_eth_ksz8851_t ksz8851_eth_0 = {
    .is_init = false,
    .is_enabled = false,
    .hw_context = &drv_eth_ksz8851_hw_context_0,
    .init = drv_eth_ksz8851_init_impl,
    .deinit = drv_eth_ksz8851_deinit_impl,
    .enable = drv_eth_ksz8851_enable_impl,
    .disable = drv_eth_ksz8851_disable_impl,
    .read_reg = drv_eth_ksz8851_read_reg_impl,
    .write_reg = drv_eth_ksz8851_write_reg_impl,
    .set_bits = drv_eth_ksz8851_set_bits_impl,
    .clear_bits = drv_eth_ksz8851_clear_bits_impl,
    .send_packet = drv_eth_ksz8851_send_packet_impl,
    .receive_packet = drv_eth_ksz8851_receive_packet_impl,
    .get_rx_status = drv_eth_ksz8851_get_rx_status_impl,
    .get_link_status = drv_eth_ksz8851_get_link_status_impl,
    .restart_autoneg = drv_eth_ksz8851_restart_autoneg_impl,
    .get_link_speed = drv_eth_ksz8851_get_link_speed_impl,
    .enable_irq = drv_eth_ksz8851_enable_irq_impl,
    .disable_irq = drv_eth_ksz8851_disable_irq_impl,
    .irq_handler = drv_eth_ksz8851_irq_handler_impl,
    .set_mac_addr = drv_eth_ksz8851_set_mac_addr_impl,
    .get_mac_addr = drv_eth_ksz8851_get_mac_addr_impl,
    .register_callback = drv_eth_ksz8851_register_callback_impl,
};

// Helper function to convert SPI status to KSZ8851 status
static drv_eth_ksz8851_status_t convert_spi_error(drv_spi_status_t spi_status)
{
    switch (spi_status) {
        case DRV_SPI_STATUS_OK:
            return DRV_ETH_KSZ8851_STATUS_OK;
        case DRV_SPI_STATUS_BUSY:
            return DRV_ETH_KSZ8851_STATUS_BUSY;
        case DRV_SPI_STATUS_TIMEOUT:
            return DRV_ETH_KSZ8851_STATUS_TIMEOUT;
        case DRV_SPI_STATUS_INVALID_PARAM:
            return DRV_ETH_KSZ8851_STATUS_INVALID_PARAM;
        default:
            return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
}

// Low-level SPI register access functions
static drv_eth_ksz8851_status_t ksz8851_read_reg(drv_eth_ksz8851_hw_context_t *context, uint16_t reg, uint16_t *value)
{
    uint8_t tx_buffer[4];
    uint8_t rx_buffer[4];
    drv_spi_status_t status;
    
    // Prepare SPI command for register read (KSZ8851 format)
    // Command format: [CMD][ADDR][DUMMY][DUMMY] -> [DATA_H][DATA_L] received
    uint8_t addr = (reg >> 1) & KSZ8851_ADDR_MASK;  // Convert word address to byte address
    tx_buffer[0] = KSZ8851_CMD_READ | addr;
    tx_buffer[1] = 0x00;  // Dummy byte
    tx_buffer[2] = 0x00;  // Dummy byte  
    tx_buffer[3] = 0x00;  // Dummy byte
    
    printf("[KSZ8851] Reading reg 0x%02X: CMD=0x%02X, addr=0x%02X\r\n", reg, tx_buffer[0], addr);
    
    // Assert CS
    hw_spi_cs_set_low(context->spi_handle);
    
    // Perform SPI transfer
    status = hw_spi_transfer(context->spi_handle, tx_buffer, rx_buffer, 4);
    
    // Deassert CS
    hw_spi_cs_set_high(context->spi_handle);
    
    if (status == DRV_SPI_STATUS_OK) {
        // KSZ8851 returns data in bytes 2 and 3 (or 1 and 2 depending on alignment)
        *value = (rx_buffer[2] << 8) | rx_buffer[3];
        printf("[KSZ8851] RX: [0x%02X 0x%02X 0x%02X 0x%02X] -> value=0x%04X\r\n",
               rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3], *value);
        return DRV_ETH_KSZ8851_STATUS_OK;
    }
    
    printf("[KSZ8851] SPI transfer failed: %d\r\n", status);
    return convert_spi_error(status);
}

static drv_eth_ksz8851_status_t ksz8851_write_reg(drv_eth_ksz8851_hw_context_t *context, uint16_t reg, uint16_t value)
{
    uint8_t tx_buffer[4];
    drv_spi_status_t status;
    
    // Prepare SPI command for register write (KSZ8851 format)
    uint8_t addr = (reg >> 1) & KSZ8851_ADDR_MASK;  // Convert word address to byte address
    tx_buffer[0] = KSZ8851_CMD_WRITE | addr;
    tx_buffer[1] = 0x00;  // Dummy byte or additional addressing
    tx_buffer[2] = (uint8_t)(value >> 8);   // Data high byte
    tx_buffer[3] = (uint8_t)value;          // Data low byte
    
    printf("[KSZ8851] Writing reg 0x%02X: CMD=0x%02X, addr=0x%02X, value=0x%04X\r\n", 
           reg, tx_buffer[0], addr, value);
    
    // Assert CS
    hw_spi_cs_set_low(context->spi_handle);
    
    // Perform SPI transfer
    status = hw_spi_transfer(context->spi_handle, tx_buffer, NULL, 4);
    
    // Deassert CS
    hw_spi_cs_set_high(context->spi_handle);
    
    if (status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851] SPI write failed: %d\r\n", status);
    }
    
    return convert_spi_error(status);
}

// Implementation functions
static drv_eth_ksz8851_status_t drv_eth_ksz8851_init_impl(const void *hw_context, const drv_eth_ksz8851_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t chip_id;
    
    if (context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_OK;
    }
    
    printf("[KSZ8851] Initializing KSZ8851 Ethernet controller\r\n");
    
    // Initialize SPI interface
    drv_spi_status_t spi_status = hw_spi_init(context->spi_handle, &context->spi_config);
    if (spi_status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851] SPI initialization failed: %d\r\n", spi_status);
        return convert_spi_error(spi_status);
    }
    
    // Enable SPI interface
    spi_status = hw_spi_enable(context->spi_handle);
    if (spi_status != DRV_SPI_STATUS_OK) {
        printf("[KSZ8851] SPI enable failed: %d\r\n", spi_status);
        return convert_spi_error(spi_status);
    }
    
    // Read and verify chip ID
    result = ksz8851_read_reg(context, KSZ8851_CIDER, &chip_id);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        printf("[KSZ8851] Failed to read chip ID: %d\r\n", result);
        return result;
    }
    
    printf("[KSZ8851] Chip ID: 0x%04X\r\n", chip_id);
    
    if ((chip_id & KSZ8851_CHIP_ID_MASK) != KSZ8851_CHIP_ID_8851_16) {
        printf("[KSZ8851] Invalid chip ID: expected 0x%04X, got 0x%04X\r\n", 
               KSZ8851_CHIP_ID_8851_16, chip_id & KSZ8851_CHIP_ID_MASK);
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Perform global soft reset
    result = ksz8851_write_reg(context, KSZ8851_GRR, KSZ8851_GRR_GLOBAL_SOFT_RESET);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        printf("[KSZ8851] Global reset failed: %d\r\n", result);
        return result;
    }
    
    // Wait for reset to complete (small delay)
    // Note: In a real implementation, you might want to use a proper delay function
    for (volatile int i = 0; i < 10000; i++);
    
    // Copy MAC address from config if provided
    if (config->mac_addr[0] != 0 || config->mac_addr[1] != 0 || config->mac_addr[2] != 0 ||
        config->mac_addr[3] != 0 || config->mac_addr[4] != 0 || config->mac_addr[5] != 0) {
        for (int i = 0; i < 6; i++) {
            context->mac_addr[i] = config->mac_addr[i];
        }
    }
    
    // Set MAC address
    result = drv_eth_ksz8851_set_mac_addr_impl(hw_context, context->mac_addr);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        printf("[KSZ8851] MAC address setup failed: %d\r\n", result);
        return result;
    }
    
    // Configure transmit control
    uint16_t txcr = KSZ8851_TXCR_CRC_ENABLE | KSZ8851_TXCR_PAD_ENABLE | 
                    KSZ8851_TXCR_FLOW_CONTROL_ENABLE | KSZ8851_TXCR_IP_CHECKSUM |
                    KSZ8851_TXCR_TCP_CHECKSUM | KSZ8851_TXCR_UDP_CHECKSUM;
    result = ksz8851_write_reg(context, KSZ8851_TXCR, txcr);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        printf("[KSZ8851] TX control setup failed: %d\r\n", result);
        return result;
    }
    
    // Configure receive control
    uint16_t rxcr1 = KSZ8851_RXCR1_UNICAST_ENABLE | KSZ8851_RXCR1_BROADCAST_ENABLE |
                     KSZ8851_RXCR1_MULTICAST_ENABLE | KSZ8851_RXCR1_FLOW_CONTROL_ENABLE |
                     KSZ8851_RXCR1_STRIP_CRC | KSZ8851_RXCR1_IP_CHECKSUM |
                     KSZ8851_RXCR1_TCP_CHECKSUM | KSZ8851_RXCR1_UDP_CHECKSUM;
    result = ksz8851_write_reg(context, KSZ8851_RXCR1, rxcr1);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        printf("[KSZ8851] RX control setup failed: %d\r\n", result);
        return result;
    }
    
    // Configure receive queue control
    uint16_t rxqcr = KSZ8851_RXQCR_AUTO_DEQUEUE | KSZ8851_RXQCR_TWOBYTE_OFFSET;
    result = ksz8851_write_reg(context, KSZ8851_RXQCR, rxqcr);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        printf("[KSZ8851] RX queue control setup failed: %d\r\n", result);
        return result;
    }
    
    context->is_ksz8851_init = true;
    
    printf("[KSZ8851] KSZ8851 initialized successfully\r\n");
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_OK;
    }
    
    printf("[KSZ8851] Deinitializing KSZ8851\r\n");
    
    // Disable interrupts
    drv_eth_ksz8851_disable_irq_impl(hw_context);
    
    // Disable transmit and receive
    drv_eth_ksz8851_disable_impl(hw_context);
    
    // Perform global soft reset
    ksz8851_write_reg(context, KSZ8851_GRR, KSZ8851_GRR_GLOBAL_SOFT_RESET);
    
    // Disable SPI interface
    hw_spi_disable(context->spi_handle);
    
    context->is_ksz8851_init = false;
    
    printf("[KSZ8851] KSZ8851 deinitialized\r\n");
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_enable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    printf("[KSZ8851] Enabling KSZ8851\r\n");
    
    // Enable transmit
    result = drv_eth_ksz8851_set_bits_impl(hw_context, KSZ8851_TXCR, KSZ8851_TXCR_ENABLE);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Enable receive
    result = drv_eth_ksz8851_set_bits_impl(hw_context, KSZ8851_RXCR1, KSZ8851_RXCR1_ENABLE);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Start receive queue
    result = drv_eth_ksz8851_set_bits_impl(hw_context, KSZ8851_RXQCR, KSZ8851_RXQCR_START);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    printf("[KSZ8851] KSZ8851 enabled\r\n");
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_disable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    printf("[KSZ8851] Disabling KSZ8851\r\n");
    
    // Disable transmit
    result = drv_eth_ksz8851_clear_bits_impl(hw_context, KSZ8851_TXCR, KSZ8851_TXCR_ENABLE);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Disable receive
    result = drv_eth_ksz8851_clear_bits_impl(hw_context, KSZ8851_RXCR1, KSZ8851_RXCR1_ENABLE);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    printf("[KSZ8851] KSZ8851 disabled\r\n");
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_read_reg_impl(const void *hw_context, uint16_t reg, uint16_t *value)
{
    ASSERT(hw_context != NULL);
    ASSERT(value != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return ksz8851_read_reg(context, reg, value);
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_write_reg_impl(const void *hw_context, uint16_t reg, uint16_t value)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return ksz8851_write_reg(context, reg, value);
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_set_bits_impl(const void *hw_context, uint16_t reg, uint16_t mask)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t value;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Read current value
    result = ksz8851_read_reg(context, reg, &value);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Set bits and write back
    value |= mask;
    return ksz8851_write_reg(context, reg, value);
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_clear_bits_impl(const void *hw_context, uint16_t reg, uint16_t mask)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t value;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Read current value
    result = ksz8851_read_reg(context, reg, &value);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Clear bits and write back
    value &= ~mask;
    return ksz8851_write_reg(context, reg, value);
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_send_packet_impl(const void *hw_context, const uint8_t *data, uint16_t length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t tx_space;
    uint8_t tx_buffer[4];
    drv_spi_status_t spi_status;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Check available TX memory
    result = ksz8851_read_reg(context, KSZ8851_TXMIR, &tx_space);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    tx_space &= 0x1FFF;  // Mask to get available memory
    
    if (tx_space < (length + 4)) {  // +4 for header
        printf("[KSZ8851] Insufficient TX memory: need %d, have %d\r\n", length + 4, tx_space);
        return DRV_ETH_KSZ8851_STATUS_BUSY;
    }
    
    // Write TX frame header
    tx_buffer[0] = KSZ8851_CMD_FIFO_WRITE;
    tx_buffer[1] = 0x00;  // Reserved
    tx_buffer[2] = (uint8_t)(length >> 8);
    tx_buffer[3] = (uint8_t)length;
    
    // Assert CS and write header
    hw_spi_cs_set_low(context->spi_handle);
    spi_status = hw_spi_transfer(context->spi_handle, tx_buffer, NULL, 4);
    
    if (spi_status == DRV_SPI_STATUS_OK) {
        // Write packet data
        spi_status = hw_spi_transfer(context->spi_handle, data, NULL, length);
    }
    
    // Deassert CS
    hw_spi_cs_set_high(context->spi_handle);
    
    if (spi_status != DRV_SPI_STATUS_OK) {
        return convert_spi_error(spi_status);
    }
    
    // Enqueue packet for transmission
    result = ksz8851_write_reg(context, KSZ8851_TXQCR, KSZ8851_TXQCR_ENQUEUE_PACKET);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    printf("[KSZ8851] Packet sent: %d bytes\r\n", length);
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_receive_packet_impl(const void *hw_context, uint8_t *data, uint16_t max_length, uint16_t *actual_length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(actual_length != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t rx_status, rx_length;
    uint8_t rx_buffer[4];
    drv_spi_status_t spi_status;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    *actual_length = 0;
    
    // Check if frame is available
    result = ksz8851_read_reg(context, KSZ8851_RXFHSR, &rx_status);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    if (!(rx_status & KSZ8851_RXFHSR_VALID)) {
        return DRV_ETH_KSZ8851_STATUS_NO_PACKET;
    }
    
    // Get frame length
    result = ksz8851_read_reg(context, KSZ8851_RXFHBCR, &rx_length);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    rx_length &= 0x0FFF;  // Mask to get length
    
    if (rx_length > max_length) {
        printf("[KSZ8851] RX packet too large: %d > %d\r\n", rx_length, max_length);
        // Free the packet
        ksz8851_write_reg(context, KSZ8851_RXQCR, KSZ8851_RXQCR_FREE_PACKET);
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Read packet header
    rx_buffer[0] = KSZ8851_CMD_FIFO_READ;
    rx_buffer[1] = 0x00;
    rx_buffer[2] = 0x00;
    rx_buffer[3] = 0x00;
    
    // Assert CS and read header + data
    hw_spi_cs_set_low(context->spi_handle);
    
    // Send read command
    spi_status = hw_spi_transfer(context->spi_handle, rx_buffer, NULL, 4);
    
    if (spi_status == DRV_SPI_STATUS_OK) {
        // Read packet data
        spi_status = hw_spi_transfer(context->spi_handle, NULL, data, rx_length);
    }
    
    // Deassert CS
    hw_spi_cs_set_high(context->spi_handle);
    
    if (spi_status != DRV_SPI_STATUS_OK) {
        return convert_spi_error(spi_status);
    }
    
    *actual_length = rx_length;
    
    // Free the packet
    result = ksz8851_write_reg(context, KSZ8851_RXQCR, KSZ8851_RXQCR_FREE_PACKET);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    printf("[KSZ8851] Packet received: %d bytes\r\n", rx_length);
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_get_rx_status_impl(const void *hw_context, drv_eth_ksz8851_rx_status_t *rx_status)
{
    ASSERT(hw_context != NULL);
    ASSERT(rx_status != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t status_reg, length_reg;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Read frame header status
    result = ksz8851_read_reg(context, KSZ8851_RXFHSR, &status_reg);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Read frame length
    result = ksz8851_read_reg(context, KSZ8851_RXFHBCR, &length_reg);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Parse status
    rx_status->valid = (status_reg & KSZ8851_RXFHSR_VALID) != 0;
    rx_status->broadcast = (status_reg & KSZ8851_RXFHSR_BROADCAST_FRAME) != 0;
    rx_status->multicast = (status_reg & KSZ8851_RXFHSR_MULTICAST_FRAME) != 0;
    rx_status->unicast = (status_reg & KSZ8851_RXFHSR_UNICAST_FRAME) != 0;
    rx_status->crc_error = (status_reg & KSZ8851_RXFHSR_CRC_ERROR) != 0;
    rx_status->length_error = (status_reg & KSZ8851_RXFHSR_TOO_LONG) != 0;
    rx_status->phy_error = (status_reg & KSZ8851_RXFHSR_PHY_ERROR) != 0;
    rx_status->length = length_reg & 0x0FFF;
    
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_get_link_status_impl(const void *hw_context, bool *link_up)
{
    ASSERT(hw_context != NULL);
    ASSERT(link_up != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t status;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Read PHY status register
    result = ksz8851_read_reg(context, KSZ8851_P1SR, &status);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    *link_up = (status & KSZ8851_P1SR_LINK_GOOD) != 0;
    
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_restart_autoneg_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    printf("[KSZ8851] Restarting auto-negotiation\r\n");
    
    // Restart auto-negotiation
    return drv_eth_ksz8851_set_bits_impl(hw_context, KSZ8851_P1MBCR, KSZ8851_P1MBCR_RESTART_AUTONEG);
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_get_link_speed_impl(const void *hw_context, bool *speed_100mbps, bool *full_duplex)
{
    ASSERT(hw_context != NULL);
    ASSERT(speed_100mbps != NULL);
    ASSERT(full_duplex != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t status;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Read port status register
    result = ksz8851_read_reg(context, KSZ8851_P1SR, &status);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    *speed_100mbps = (status & KSZ8851_P1SR_SPEED_100MBPS) != 0;
    *full_duplex = (status & KSZ8851_P1SR_FULL_DUPLEX) != 0;
    
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_enable_irq_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    printf("[KSZ8851] Enabling interrupts\r\n");
    
    // Enable PHY, TX, and RX interrupts
    uint16_t interrupts = KSZ8851_IER_PHY | KSZ8851_IER_TX | KSZ8851_IER_RX;
    return ksz8851_write_reg(context, KSZ8851_IER, interrupts);
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_disable_irq_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    printf("[KSZ8851] Disabling interrupts\r\n");
    
    // Disable all interrupts
    return ksz8851_write_reg(context, KSZ8851_IER, 0x0000);
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_irq_handler_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    uint16_t int_status;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Read interrupt status
    result = ksz8851_read_reg(context, KSZ8851_ISR, &int_status);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Clear interrupts by writing back the status
    ksz8851_write_reg(context, KSZ8851_ISR, int_status);
    
    // Handle specific interrupts
    if (int_status & KSZ8851_ISR_PHY) {
        printf("[KSZ8851] PHY interrupt\r\n");
        if (context->link_callback) {
            context->link_callback();
        }
    }
    
    if (int_status & KSZ8851_ISR_TX) {
        printf("[KSZ8851] TX interrupt\r\n");
        if (context->tx_callback) {
            context->tx_callback();
        }
    }
    
    if (int_status & KSZ8851_ISR_RX) {
        printf("[KSZ8851] RX interrupt\r\n");
        if (context->rx_callback) {
            context->rx_callback();
        }
    }
    
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_set_mac_addr_impl(const void *hw_context, const uint8_t mac_addr[6])
{
    ASSERT(hw_context != NULL);
    ASSERT(mac_addr != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    drv_eth_ksz8851_status_t result;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    printf("[KSZ8851] Setting MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    
    // Set MAC address low register (bytes 0-1)
    uint16_t mac_low = (mac_addr[1] << 8) | mac_addr[0];
    result = ksz8851_write_reg(context, KSZ8851_MARL, mac_low);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Set MAC address middle register (bytes 2-3)
    uint16_t mac_mid = (mac_addr[3] << 8) | mac_addr[2];
    result = ksz8851_write_reg(context, KSZ8851_MARM, mac_mid);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Set MAC address high register (bytes 4-5)
    uint16_t mac_high = (mac_addr[5] << 8) | mac_addr[4];
    result = ksz8851_write_reg(context, KSZ8851_MARH, mac_high);
    if (result != DRV_ETH_KSZ8851_STATUS_OK) {
        return result;
    }
    
    // Store MAC address in context
    for (int i = 0; i < 6; i++) {
        context->mac_addr[i] = mac_addr[i];
    }
    
    printf("[KSZ8851] MAC address set successfully\r\n");
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_get_mac_addr_impl(const void *hw_context, uint8_t mac_addr[6])
{
    ASSERT(hw_context != NULL);
    ASSERT(mac_addr != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    // Return stored MAC address
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = context->mac_addr[i];
    }
    
    return DRV_ETH_KSZ8851_STATUS_OK;
}

static drv_eth_ksz8851_status_t drv_eth_ksz8851_register_callback_impl(const void *hw_context, 
                                                                        drv_eth_ksz8851_cb_type_t type, 
                                                                        drv_eth_ksz8851_callback_t callback)
{
    ASSERT(hw_context != NULL);
    
    drv_eth_ksz8851_hw_context_t *context = (drv_eth_ksz8851_hw_context_t *)hw_context;
    
    if (!context->is_ksz8851_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    switch (type) {
        case DRV_ETH_KSZ8851_CB_RX_PACKET:
            context->rx_callback = callback;
            break;
        case DRV_ETH_KSZ8851_CB_TX_COMPLETE:
            context->tx_callback = callback;
            break;
        case DRV_ETH_KSZ8851_CB_LINK_CHANGE:
            context->link_callback = callback;
            break;
        case DRV_ETH_KSZ8851_CB_ERROR:
            context->error_callback = callback;
            break;
        default:
            return DRV_ETH_KSZ8851_STATUS_INVALID_PARAM;
    }
    
    return DRV_ETH_KSZ8851_STATUS_OK;
}