/**
 * Simple KSZ8851SNL Test - No LwIP Dependencies
 * 
 * This test verifies basic KSZ8851SNL functionality by:
 * 1. Initializing the KSZ chip with IP 192.168.100.2
 * 2. Setting MAC address
 * 3. Checking link status
 * 4. Sending simple UDP packets in a loop
 * 
 * Single FreeRTOS task design to avoid race conditions.
 */

#include "ksz_simple_test.h"
#include "FreeRTOS.h"
#include "task.h"
#include "printf.h"
#include "bsp_ksz8851snl.h"
#include "driver_ksz8851snl.h"
#include <string.h>

// Network configuration
#define TEST_IP_ADDR        {192, 168, 100, 2}
#define TEST_MAC_ADDR       {0x00, 0x00, 0x00, 0x00, 0x20, 0x76}
#define TARGET_IP_ADDR      {192, 168, 100, 100}
#define TARGET_PORT         1234
#define SOURCE_PORT         5000

// Packet buffer
static uint8_t tx_packet_buffer[256];

/**
 * Calculate IP checksum
 */
static uint16_t calculate_ip_checksum(const uint8_t* ip_header, uint8_t length)
{
    uint32_t sum = 0;
    
    // Sum all 16-bit words
    for (int i = 0; i < length; i += 2) {
        if (i + 1 < length) {
            sum += (ip_header[i] << 8) | ip_header[i + 1];
        } else {
            sum += ip_header[i] << 8;
        }
    }
    
    // Add carry
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/**
 * Calculate UDP checksum (with IP pseudo-header)
 */
static uint16_t calculate_udp_checksum(const uint8_t* ip_src, const uint8_t* ip_dst, 
                                      const uint8_t* udp_header, uint16_t udp_length)
{
    uint32_t sum = 0;
    
    // IP pseudo-header
    sum += (ip_src[0] << 8) | ip_src[1];
    sum += (ip_src[2] << 8) | ip_src[3];
    sum += (ip_dst[0] << 8) | ip_dst[1];
    sum += (ip_dst[2] << 8) | ip_dst[3];
    sum += 0x0011; // UDP protocol
    sum += udp_length;
    
    // UDP header and data
    for (int i = 0; i < udp_length; i += 2) {
        if (i + 1 < udp_length) {
            sum += (udp_header[i] << 8) | udp_header[i + 1];
        } else {
            sum += udp_header[i] << 8;
        }
    }
    
    // Add carry
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/**
 * Create and send a simple UDP packet
 */
static void send_test_udp_packet(uint32_t packet_count)
{
    uint8_t src_ip[] = TEST_IP_ADDR;
    uint8_t dst_ip[] = TARGET_IP_ADDR;
    uint8_t src_mac[] = TEST_MAC_ADDR;
    uint8_t dst_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast for simplicity
    
    // Create test payload
    char payload[64];
    snprintf(payload, sizeof(payload), "KSZ Test Packet #%lu from 192.168.100.2", packet_count);
    uint16_t payload_len = strlen(payload);
    
    uint16_t packet_len = 0;
    uint8_t* ptr = tx_packet_buffer;
    
    // Ethernet Header (14 bytes)
    memcpy(ptr, dst_mac, 6);        // Destination MAC
    ptr += 6;
    memcpy(ptr, src_mac, 6);        // Source MAC  
    ptr += 6;
    *ptr++ = 0x08; *ptr++ = 0x00;   // EtherType (IPv4)
    packet_len += 14;
    
    // IP Header (20 bytes)
    uint8_t* ip_start = ptr;
    *ptr++ = 0x45;                  // Version (4) + Header Length (5 words)
    *ptr++ = 0x00;                  // Type of Service
    uint16_t ip_length = 20 + 8 + payload_len; // IP + UDP + payload
    *ptr++ = (ip_length >> 8) & 0xFF;
    *ptr++ = ip_length & 0xFF;
    *ptr++ = 0x00; *ptr++ = 0x01;   // Identification
    *ptr++ = 0x40; *ptr++ = 0x00;   // Flags (Don't Fragment) + Fragment Offset
    *ptr++ = 64;                    // TTL
    *ptr++ = 17;                    // Protocol (UDP)
    *ptr++ = 0x00; *ptr++ = 0x00;   // Checksum (will be filled)
    memcpy(ptr, src_ip, 4);         // Source IP
    ptr += 4;
    memcpy(ptr, dst_ip, 4);         // Destination IP
    ptr += 4;
    packet_len += 20;
    
    // Calculate and insert IP checksum
    uint16_t ip_checksum = calculate_ip_checksum(ip_start, 20);
    ip_start[10] = (ip_checksum >> 8) & 0xFF;
    ip_start[11] = ip_checksum & 0xFF;
    
    // UDP Header (8 bytes)
    uint8_t* udp_start = ptr;
    uint16_t udp_length = 8 + payload_len;
    *ptr++ = (SOURCE_PORT >> 8) & 0xFF; *ptr++ = SOURCE_PORT & 0xFF;  // Source Port
    *ptr++ = (TARGET_PORT >> 8) & 0xFF; *ptr++ = TARGET_PORT & 0xFF;  // Destination Port
    *ptr++ = (udp_length >> 8) & 0xFF;  *ptr++ = udp_length & 0xFF;   // Length
    *ptr++ = 0x00; *ptr++ = 0x00;       // Checksum (will be filled)
    packet_len += 8;
    
    // UDP Payload
    memcpy(ptr, payload, payload_len);
    ptr += payload_len;
    packet_len += payload_len;
    
    // Calculate and insert UDP checksum
    uint16_t udp_checksum = calculate_udp_checksum(src_ip, dst_ip, udp_start, udp_length);
    udp_start[6] = (udp_checksum >> 8) & 0xFF;
    udp_start[7] = udp_checksum & 0xFF;
    
    printf("[KSZ_TEST] Packet #%lu: %d bytes, payload: \"%.32s\"\r\n", 
           packet_count, packet_len, payload);
    printf("[KSZ_TEST] UDP %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d\r\n",
           src_ip[0], src_ip[1], src_ip[2], src_ip[3], SOURCE_PORT,
           dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], TARGET_PORT);
    
    // Send packet through KSZ8851SNL
    drv_ksz8851snl_status_t result = hw_ksz8851snl_send_packet(&ksz8851snl_0, tx_packet_buffer, packet_len);
    
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ_TEST] ✓ Packet sent successfully\r\n");
    } else {
        printf("[KSZ_TEST] ✗ Packet send failed: %d\r\n", result);
    }
}

/**
 * Main KSZ simple test task
 */
static void ksz_simple_test_task(void *pvParameters)
{
    (void)pvParameters;
    
    printf("\r\n=== KSZ8851SNL Simple Test Started ===\r\n");
    printf("[KSZ_TEST] No LwIP - Direct hardware access only\r\n");
    printf("[KSZ_TEST] Single task design to avoid race conditions\r\n");
    printf("[KSZ_TEST] Debugging corrupted 0x55 frame issue\r\n");
    
    // Wait longer before initializing to avoid startup issues
    printf("[KSZ_TEST] Waiting 5 seconds for system stabilization...\r\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    printf("[KSZ_TEST] ===========================================\r\n");
    printf("[KSZ_TEST] IMPORTANT: Monitor Wireshark Now\r\n");
    printf("[KSZ_TEST] Look for corrupted frames during initialization\r\n");
    printf("[KSZ_TEST] ===========================================\r\n");
    
    // Configure KSZ8851SNL
    drv_ksz8851snl_config_t config = {
        .mac_addr = TEST_MAC_ADDR,
        .auto_negotiation = true,
        .link_speed = 100,
        .full_duplex = true
    };
    
    printf("[KSZ_TEST] Starting KSZ8851SNL initialization...\r\n");
    printf("[KSZ_TEST] >>> Corrupted frames may appear in Wireshark now <<<\r\n");
    drv_ksz8851snl_status_t init_result = hw_ksz8851snl_init(&ksz8851snl_0, &config);
    
    if (init_result != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ_TEST] ✗ KSZ8851SNL initialization failed: %d\r\n", init_result);
        printf("[KSZ_TEST] Check SPI connections and power\r\n");
        vTaskDelete(NULL);
        return;
    }
    
    printf("[KSZ_TEST] ✓ KSZ8851SNL initialized successfully\r\n");
    printf("[KSZ_TEST] >>> Initialization complete - corrupted frames should stop <<<\r\n");
    
    // Verify chip detection
    drv_ksz8851snl_id_info_t id_info;
    drv_ksz8851snl_status_t chip_result = hw_ksz8851snl_get_chip_id(&ksz8851snl_0, &id_info);
    
    if (chip_result != DRV_KSZ8851SNL_STATUS_OK || !id_info.chip_detected) {
        printf("[KSZ_TEST] ✗ KSZ8851SNL chip detection failed: %d\r\n", chip_result);
        vTaskDelete(NULL);
        return;
    }
    
    printf("[KSZ_TEST] ✓ Chip ID: 0x%04X, Revision: %d\r\n", id_info.chip_id, id_info.revision_id);
    printf("[KSZ_TEST] ✓ SPI communication working\r\n");
    
    // Enable KSZ8851SNL
    printf("[KSZ_TEST] Enabling KSZ8851SNL...\r\n");
    drv_ksz8851snl_status_t enable_result = hw_ksz8851snl_enable(&ksz8851snl_0);
    
    if (enable_result != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ_TEST] ✗ KSZ8851SNL enable failed: %d\r\n", enable_result);
        vTaskDelete(NULL);
        return;
    }
    
    printf("[KSZ_TEST] ✓ KSZ8851SNL enabled\r\n");
    
    // Wait for link negotiation
    printf("[KSZ_TEST] Waiting for link negotiation...\r\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Check link status
    drv_ksz8851snl_status_info_t status;
    drv_ksz8851snl_status_t status_result = hw_ksz8851snl_get_status(&ksz8851snl_0, &status);
    
    if (status_result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ_TEST] Link Status: %s\r\n", status.link_up ? "UP" : "DOWN");
        if (status.link_up) {
            printf("[KSZ_TEST] Link Speed: %d Mbps, %s duplex\r\n", 
                   status.link_speed, status.full_duplex ? "Full" : "Half");
        }
    } else {
        printf("[KSZ_TEST] ✗ Failed to read link status: %d\r\n", status_result);
    }
    
    // Extra delay after initialization to let hardware settle
    printf("[KSZ_TEST] Waiting additional 5 seconds for hardware to settle...\r\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    printf("[KSZ_TEST] ===========================================\r\n");
    printf("[KSZ_TEST] Starting UDP packet transmission test\r\n");
    printf("[KSZ_TEST] Device IP: 192.168.100.2\r\n");
    printf("[KSZ_TEST] Target IP: 192.168.100.100:1234\r\n");
    printf("[KSZ_TEST] MAC Address: 00:00:00:00:20:76\r\n");
    printf("[KSZ_TEST] >>> Clean UDP packets should appear now <<<\r\n");
    printf("[KSZ_TEST] Check Wireshark for UDP packets (not corrupted)\r\n");
    printf("[KSZ_TEST] ===========================================\r\n");
    
    uint32_t packet_count = 0;
    
    // Main packet transmission loop
    while (1) {
        packet_count++;
        
        printf("\r\n[KSZ_TEST] --- Packet #%lu ---\r\n", packet_count);
        
        // Send test UDP packet
        send_test_udp_packet(packet_count);
        
        // Check link status periodically
        if (packet_count % 5 == 0) {
            hw_ksz8851snl_get_status(&ksz8851snl_0, &status);
            printf("[KSZ_TEST] Link: %s, TX: %lu, RX: %lu\r\n", 
                   status.link_up ? "UP" : "DOWN", status.tx_packets, status.rx_packets);
        }
        
        printf("[KSZ_TEST] Waiting 2 seconds...\r\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * Create and start the KSZ simple test task
 */
void ksz_simple_test_start(void)
{
    printf("[KSZ_TEST] Creating simple test task...\r\n");
    
    if (xTaskCreate(ksz_simple_test_task,
                    "KSZ_SIMPLE",
                    1024,  // Stack size
                    NULL,
                    (tskIDLE_PRIORITY + 2),  // Medium priority
                    NULL) != pdPASS) {
        printf("[KSZ_TEST] ✗ Failed to create KSZ simple test task\r\n");
    } else {
        printf("[KSZ_TEST] ✓ KSZ simple test task created\r\n");
    }
}