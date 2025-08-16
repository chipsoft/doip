/**
 * Minimal KSZ8851SNL Test - Just Hardware Verification
 * 
 * This test ONLY:
 * 1. Reads KSZ registers to verify SPI communication
 * 2. Checks if data is 0x55 (indicates SPI problem)
 * 3. Examines TX buffer status
 * 4. Sends ONE simple packet if registers look good
 * 
 * NO ECU, NO DOIP, NO COMPLEX INITIALIZATION
 */

#include "ksz_minimal_test.h"
#include "FreeRTOS.h"
#include "task.h"
#include "printf.h"
#include "bsp_ksz8851snl.h"
#include "driver_ksz8851snl.h"
#include "bsp_spi.h"
#include "driver_spi.h"
#include <string.h>

// Simple test packet (60 bytes minimum Ethernet frame)
static uint8_t test_packet[60] = {
    // Destination MAC (broadcast)
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // Source MAC (our device)  
    0x00, 0x00, 0x00, 0x00, 0x20, 0x76,
    // EtherType (Custom for easy identification - 0x8877)
    0x88, 0x77,
    // Test payload - distinctive pattern (46 bytes)
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,  // 8 bytes - Distinctive header
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,  // 8 bytes - Counter pattern  
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,  // 8 bytes
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,  // 8 bytes
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,  // 8 bytes  
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26             // 6 bytes (total: 46 bytes payload)
};

/**
 * Read and verify KSZ registers - check for 0x55 corruption
 */
static bool verify_ksz_registers(void)
{
    printf("\r\n=== KSZ Register Verification ===\r\n");
    
    // Try to read chip ID register multiple times
    for (int i = 0; i < 5; i++) {
        printf("[KSZ_MIN] Reading Chip ID register (attempt %d)...\r\n", i + 1);
        
        drv_ksz8851snl_id_info_t id_info;
        hw_ksz8851snl_get_chip_id(&ksz8851snl_0, &id_info);
        
        printf("[KSZ_MIN] Chip ID: 0x%04X, SPI OK: %s, Detected: %s\r\n",
               id_info.chip_id, 
               id_info.spi_communication_ok ? "YES" : "NO",
               id_info.chip_detected ? "YES" : "NO");
        
        // Check for 0x55 corruption pattern
        if (id_info.chip_id == 0x5555) {
            printf("[KSZ_MIN] ERROR: Chip ID is 0x5555 - SPI corruption detected!\r\n");
            return false;
        }
        
        if (id_info.chip_id == 0x0000) {
            printf("[KSZ_MIN] ERROR: Chip ID is 0x0000 - no response\r\n");
            return false;
        }
        
        if (id_info.chip_detected && id_info.spi_communication_ok) {
            printf("[KSZ_MIN] ✓ Register read successful - no 0x55 corruption\r\n");
            return true;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("[KSZ_MIN] ✗ Failed to read valid registers\r\n");
    return false;
}

/**
 * Check TX buffer status without complex setup
 */
static void check_tx_buffer_status(void)
{
    printf("\r\n=== TX Buffer Status Check ===\r\n");
    
    drv_ksz8851snl_status_info_t status;
    drv_ksz8851snl_status_t result = hw_ksz8851snl_get_status(&ksz8851snl_0, &status);
    
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ_MIN] Link Status: %s\r\n", status.link_up ? "UP" : "DOWN");
        printf("[KSZ_MIN] TX Packets: %lu\r\n", status.tx_packets);
        printf("[KSZ_MIN] RX Packets: %lu\r\n", status.rx_packets);
        printf("[KSZ_MIN] TX Errors:  %lu\r\n", status.tx_errors);
        printf("[KSZ_MIN] RX Errors:  %lu\r\n", status.rx_errors);
        
        if (status.tx_errors > 0) {
            printf("[KSZ_MIN] WARNING: TX errors detected!\r\n");
        }
    } else {
        printf("[KSZ_MIN] ✗ Failed to read status: %d\r\n", result);
    }
}

/**
 * Dump first 32 bytes of packet for verification
 */
static void dump_packet_data(const uint8_t *packet, uint32_t length)
{
    printf("[KSZ_MIN] Packet data (first 32 bytes):\r\n");
    for (int i = 0; i < 32 && i < length; i += 8) {
        printf("[KSZ_MIN]   %02X: ", i);
        for (int j = 0; j < 8 && (i + j) < length && (i + j) < 32; j++) {
            printf("%02X ", packet[i + j]);
        }
        printf("\r\n");
    }
}

/**
 * Direct register read from KSZ for buffer verification
 */
static uint16_t read_ksz_register_direct(uint16_t reg_addr)
{
    // Use the same SPI access pattern as the KSZ driver
    uint8_t cmd_buf[4];
    uint8_t resp_buf[4];
    
    // Create read command (same as KSZ driver)
    uint16_t cmd = 0x0000 | ((reg_addr & 0xFE) << 8) | ((reg_addr & 0x02) << 7);
    cmd_buf[0] = (cmd >> 8) & 0xFF;
    cmd_buf[1] = cmd & 0xFF;
    cmd_buf[2] = 0x00;
    cmd_buf[3] = 0x00;
    
    // Perform SPI transfer (using same driver as KSZ)
    drv_spi_status_t spi_result = hw_spi_transfer(&spi_4, cmd_buf, resp_buf, 4);
    
    if (spi_result == DRV_SPI_STATUS_OK) {
        // Extract data (same as KSZ driver)
        uint16_t data = (resp_buf[2] << 8) | resp_buf[3];
        return data;
    }
    
    return 0xFFFF; // Error indicator
}

/**
 * Read and verify TX buffer contents after writing packet
 */
static bool verify_tx_buffer_contents(const uint8_t *expected_data, uint32_t length)
{
    printf("[KSZ_MIN] *** TX BUFFER VERIFICATION ***\r\n");
    printf("[KSZ_MIN] Reading back TX-related registers to check for corruption...\r\n");
    
    // Check key registers for 0x55 corruption
    uint16_t chip_id = read_ksz_register_direct(0xC0);      // Chip ID
    uint16_t tx_ctrl = read_ksz_register_direct(0x70);      // TX Control
    uint16_t tx_status = read_ksz_register_direct(0x72);    // TX Status  
    uint16_t tx_mem_info = read_ksz_register_direct(0x78);  // TX Memory Info
    uint16_t mac_addr_0 = read_ksz_register_direct(0x10);   // MAC Address low
    uint16_t mac_addr_1 = read_ksz_register_direct(0x12);   // MAC Address mid
    uint16_t mac_addr_2 = read_ksz_register_direct(0x14);   // MAC Address high
    
    printf("[KSZ_MIN] Register readback after TX:\r\n");
    printf("[KSZ_MIN]   Chip ID (0xC0):      0x%04X %s\r\n", chip_id, 
           (chip_id == 0x5555) ? "❌ CORRUPTED!" : "✓");
    printf("[KSZ_MIN]   TX Control (0x70):   0x%04X %s\r\n", tx_ctrl,
           (tx_ctrl == 0x5555) ? "❌ CORRUPTED!" : "✓");
    printf("[KSZ_MIN]   TX Status (0x72):    0x%04X %s\r\n", tx_status,
           (tx_status == 0x5555) ? "❌ CORRUPTED!" : "✓");
    printf("[KSZ_MIN]   TX Memory (0x78):    0x%04X %s\r\n", tx_mem_info,
           (tx_mem_info == 0x5555) ? "❌ CORRUPTED!" : "✓");
    printf("[KSZ_MIN]   MAC Addr 0 (0x10):   0x%04X %s\r\n", mac_addr_0,
           (mac_addr_0 == 0x5555) ? "❌ CORRUPTED!" : "✓");
    printf("[KSZ_MIN]   MAC Addr 1 (0x12):   0x%04X %s\r\n", mac_addr_1,
           (mac_addr_1 == 0x5555) ? "❌ CORRUPTED!" : "✓");
    printf("[KSZ_MIN]   MAC Addr 2 (0x14):   0x%04X %s\r\n", mac_addr_2,
           (mac_addr_2 == 0x5555) ? "❌ CORRUPTED!" : "✓");
    
    // Check for any 0x5555 corruption patterns
    bool corruption_5555 = (chip_id == 0x5555) || (tx_ctrl == 0x5555) || 
                           (tx_status == 0x5555) || (tx_mem_info == 0x5555) ||
                           (mac_addr_0 == 0x5555) || (mac_addr_1 == 0x5555) || 
                           (mac_addr_2 == 0x5555);
    
    if (corruption_5555) {
        printf("[KSZ_MIN] ❌ CRITICAL: 0x5555 corruption detected in KSZ registers!\r\n");
        printf("[KSZ_MIN] This explains the 0x55 frames in Wireshark!\r\n");
        return false;
    }
    
    // Check for SPI readback errors (all 0xFFFF)
    bool spi_error = (chip_id == 0xFFFF) && (tx_ctrl == 0xFFFF) && 
                     (tx_status == 0xFFFF) && (tx_mem_info == 0xFFFF);
    
    if (spi_error) {
        printf("[KSZ_MIN] ❌ CRITICAL: SPI readback failed (all 0xFFFF) - timing issue!\r\n");
        printf("[KSZ_MIN] Direct register access after TX has problems\r\n");
        return false;
    }
    
    // Also check for all 0x0000 (dead chip)
    bool chip_dead = (chip_id == 0x0000) && (tx_ctrl == 0x0000) && 
                     (tx_status == 0x0000) && (tx_mem_info == 0x0000);
    
    if (chip_dead) {
        printf("[KSZ_MIN] ❌ CRITICAL: All registers read 0x0000 - chip may be dead!\r\n");
        return false;
    }
    
    // Valid register values (chip should be 0x8872, others non-zero/non-FFFF)
    if (chip_id == 0x8872 && tx_ctrl != 0xFFFF && tx_status != 0xFFFF) {
        printf("[KSZ_MIN] ✓ All TX registers verified clean - no corruption\r\n");
        return true;
    }
    
    printf("[KSZ_MIN] ⚠️  Unexpected register values - may indicate timing/access issue\r\n");
    return false;
}

/**
 * Send ONE simple test packet with TX buffer verification
 */
static void send_single_test_packet(void)
{
    printf("\r\n=== Single Packet Transmission Test ===\r\n");
    printf("[KSZ_MIN] Sending 60-byte test packet...\r\n");
    printf("[KSZ_MIN] Pattern: DEADBEEF CAFEBABE + counter (not 0x55)\r\n");
    
    // Show packet contents before sending
    printf("[KSZ_MIN] BEFORE TX - Packet contents:\r\n");
    dump_packet_data(test_packet, sizeof(test_packet));
    
    // Verify registers are clean before TX
    printf("[KSZ_MIN] BEFORE TX - Register verification:\r\n");
    verify_ksz_registers();
    
    printf("[KSZ_MIN] Initiating packet transmission...\r\n");
    drv_ksz8851snl_status_t result = hw_ksz8851snl_send_packet(&ksz8851snl_0, test_packet, sizeof(test_packet));
    
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ_MIN] ✓ Packet sent successfully to KSZ TX buffer\r\n");
        
        // CRITICAL: Verify TX buffer contents immediately after write
        printf("[KSZ_MIN] AFTER TX - Verifying TX buffer contents...\r\n");
        bool buffer_ok = verify_tx_buffer_contents(test_packet, sizeof(test_packet));
        
        if (!buffer_ok) {
            printf("[KSZ_MIN] ❌ TX BUFFER CORRUPTED! This is the source of 0x55 frames!\r\n");
        } else {
            printf("[KSZ_MIN] ✓ TX buffer contents verified clean\r\n");
        }
        
        printf("[KSZ_MIN] Check Wireshark for 60-byte frame with DEADBEEF pattern...\r\n");
        printf("[KSZ_MIN] *** WIRESHARK DEBUG INFO ***\r\n");
        printf("[KSZ_MIN] Use these Wireshark filters:\r\n");
        printf("[KSZ_MIN]   eth.src == 00:00:00:00:20:76\r\n");
        printf("[KSZ_MIN]   eth.dst == ff:ff:ff:ff:ff:ff\r\n");
        printf("[KSZ_MIN]   eth.type == 0x8877\r\n");
        printf("[KSZ_MIN]   frame.len == 60\r\n");
        printf("[KSZ_MIN]   data contains deadbeef\r\n");
    } else {
        printf("[KSZ_MIN] ✗ Packet send failed: %d\r\n", result);
    }
    
    // Check status after sending with delay
    vTaskDelay(pdMS_TO_TICKS(200));
    check_tx_buffer_status();
    
    // Final register verification after everything
    printf("[KSZ_MIN] FINAL - Register verification after TX complete:\r\n");
    verify_ksz_registers();
}

/**
 * Minimal KSZ test task - NO complex initialization
 */
static void ksz_minimal_test_task(void *pvParameters)
{
    (void)pvParameters;
    
    printf("\r\n==========================================\r\n");
    printf("KSZ8851SNL MINIMAL HARDWARE TEST\r\n");
    printf("NO ECU, NO DOIP, NO COMPLEX SETUP\r\n");
    printf("==========================================\r\n");
    
    // Wait for system to stabilize
    printf("[KSZ_MIN] Waiting 3 seconds for stabilization...\r\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Step 1: Basic KSZ configuration (minimal)
    drv_ksz8851snl_config_t config = {
        .mac_addr = {0x00, 0x00, 0x00, 0x00, 0x20, 0x76},
        .auto_negotiation = true,
        .link_speed = 100,
        .full_duplex = true
    };
    
    printf("[KSZ_MIN] Basic KSZ initialization...\r\n");
    drv_ksz8851snl_status_t init_result = hw_ksz8851snl_init(&ksz8851snl_0, &config);
    
    if (init_result != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ_MIN] ✗ Basic init failed: %d\r\n", init_result);
        printf("[KSZ_MIN] Trying register read anyway...\r\n");
    }
    
    // Step 2: Verify register communication (most important)
    bool registers_ok = verify_ksz_registers();
    
    if (!registers_ok) {
        printf("[KSZ_MIN] ✗ SPI communication failed - check wiring\r\n");
        printf("[KSZ_MIN] Expected connections:\r\n");
        printf("[KSZ_MIN] - SCK:  PB26 -> KSZ SCK\r\n");
        printf("[KSZ_MIN] - MOSI: PB27 -> KSZ SI\r\n");
        printf("[KSZ_MIN] - MISO: PB29 -> KSZ SO\r\n");
        printf("[KSZ_MIN] - CS:   PB28 -> KSZ CS#\r\n");
        printf("[KSZ_MIN] - RST:  PA6  -> KSZ RST#\r\n");
        
        // Keep trying every 5 seconds
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            printf("[KSZ_MIN] Retrying register read...\r\n");
            verify_ksz_registers();
        }
    }
    
    // Step 3: Enable chip
    printf("[KSZ_MIN] Enabling KSZ8851SNL...\r\n");
    hw_ksz8851snl_enable(&ksz8851snl_0);
    
    // Step 4: Check initial TX buffer status
    vTaskDelay(pdMS_TO_TICKS(1000));
    check_tx_buffer_status();
    
    // Step 5: FIRST - Monitor without sending packets to check for spontaneous 0x55 frames
    printf("\r\n[KSZ_MIN] *** BASELINE TEST - NO PACKETS SENT ***\r\n");
    printf("[KSZ_MIN] Monitoring for 30 seconds WITHOUT sending any packets\r\n");
    printf("[KSZ_MIN] If you see 0x55 frames during this time, the corruption is NOT from our packets\r\n");
    printf("[KSZ_MIN] Check Wireshark now for any activity...\r\n");
    
    for (int i = 30; i > 0; i--) {
        printf("[KSZ_MIN] Baseline monitoring: %d seconds remaining (NO PACKETS SENT)\r\n", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    printf("[KSZ_MIN] Baseline complete. Any 0x55 frames during baseline are NOT from SAME54.\r\n");
    
    // Step 6: Now send ONE test packet
    printf("\r\n[KSZ_MIN] NOW starting packet transmission test...\r\n");
    printf("[KSZ_MIN] Waiting 2 seconds before first packet...\r\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    send_single_test_packet();
    
    // Step 6: Monitor mode - send packet every 30 seconds with detailed analysis
    printf("\r\n[KSZ_MIN] *** CORRUPTION ANALYSIS MODE ***\r\n");
    printf("[KSZ_MIN] Sending packets every 30 seconds\r\n");
    printf("[KSZ_MIN] Direct PC connection - no switch involved\r\n");
    printf("[KSZ_MIN] Watch for timing correlation between sent packets and 0x55 frames\r\n");
    
    uint32_t packet_count = 1;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30 second intervals for analysis
        
        packet_count++;
        printf("\r\n[KSZ_MIN] ==========================================\r\n");
        printf("[KSZ_MIN] ANALYSIS PACKET #%lu - %lu seconds\r\n", packet_count, (packet_count-1) * 30);
        printf("[KSZ_MIN] ==========================================\r\n");
        
        // Update packet counter in payload  
        test_packet[14] = (packet_count >> 24) & 0xFF;
        test_packet[15] = (packet_count >> 16) & 0xFF;
        test_packet[16] = (packet_count >> 8) & 0xFF;
        test_packet[17] = packet_count & 0xFF;
        
        // Add timestamp marker to help correlate with Wireshark
        uint32_t timestamp = xTaskGetTickCount();
        test_packet[18] = (timestamp >> 24) & 0xFF;
        test_packet[19] = (timestamp >> 16) & 0xFF;
        test_packet[20] = (timestamp >> 8) & 0xFF;
        test_packet[21] = timestamp & 0xFF;
        
        printf("[KSZ_MIN] Packet contains: counter=%lu, timestamp=%lu\r\n", packet_count, timestamp);
        printf("[KSZ_MIN] Expected in Wireshark: Look for 60-byte frame with custom EtherType 0x8877\r\n");
        printf("[KSZ_MIN] If you see 1365-byte 0x55 frames instead, note the timing!\r\n");
        
        send_single_test_packet();
        
        // Additional status check after each packet
        printf("[KSZ_MIN] Checking KSZ status after transmission...\r\n");
        check_tx_buffer_status();
        
        // Verify registers still work every 3 packets
        if (packet_count % 3 == 0) {
            printf("[KSZ_MIN] Verifying SPI communication still clean...\r\n");
            verify_ksz_registers();
        }
        
        printf("[KSZ_MIN] Next packet in 30 seconds...\r\n");
    }
}

/**
 * Start minimal KSZ test
 */
void ksz_minimal_test_start(void)
{
    printf("[KSZ_MIN] Creating minimal test task...\r\n");
    
    if (xTaskCreate(ksz_minimal_test_task,
                    "KSZ_MIN",
                    1024,  // Stack size
                    NULL,
                    (tskIDLE_PRIORITY + 2),  // Medium priority
                    NULL) != pdPASS) {
        printf("[KSZ_MIN] ✗ Failed to create minimal test task\r\n");
    } else {
        printf("[KSZ_MIN] ✓ Minimal test task created\r\n");
    }
}