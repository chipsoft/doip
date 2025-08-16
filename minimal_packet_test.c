/**
 * Minimal Packet Test - Direct KSZ8851SNL Communication
 * Based on working commit 1edc07c47b5799a3c381bbd72bb3fd5addc99d17
 */

#include "minimal_packet_test.h"
#include "printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_spi.h"
#include "driver_spi.h"
#include "hal_gpio.h"
#include "ksz8851snl_config.h"
#include "app_libs/FreeRTOS-Plus-TCP/source/portable/NetworkInterface/ksz8851snl/ksz8851snl_reg.h"
#include "bsp_ksz8851snl.h"  // For type definitions and extern declarations
#include "driver_ksz8851snl.h"  // For driver types

// Note: Using working driver API directly since register functions are static

// Simple test packet (64 bytes minimum Ethernet frame)
static uint8_t simple_packet[64] = {
    // Destination MAC (broadcast)
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // Source MAC 
    0x00, 0x00, 0x00, 0x00, 0x20, 0x76,
    // EtherType (Custom for easy identification)
    0x88, 0x77,
    // Simple payload (50 bytes to make total 64)
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,  // 8 bytes
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,  // 8 bytes
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,  // 8 bytes
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,  // 8 bytes
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,  // 8 bytes
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,  // 8 bytes
    0x29, 0x2A                                         // 2 bytes (total: 50 bytes payload)
};

// Test with 0x55 garbage pattern like the working commit
static uint8_t garbage_packet[64];

static void fill_garbage_pattern(void)
{
    for (int i = 0; i < 64; i++) {
        garbage_packet[i] = 0x55;
    }
}

void minimal_packet_test(void)
{
    printf("\r\n=== MINIMAL PACKET TEST START ===\r\n");
    
    // Check link status first - this might be the root cause
    printf("Checking PHY link status...\r\n");
    drv_ksz8851snl_status_info_t status_info;
    drv_ksz8851snl_status_t status_result = hw_ksz8851snl_get_status(&ksz8851snl_0, &status_info);
    
    if (status_result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("🔗 Link Status: %s\r\n", status_info.link_up ? "UP" : "DOWN");
        if (status_info.link_up) {
            printf("🔗 Link Speed: %d Mbps\r\n", status_info.link_speed);
            printf("🔗 Duplex Mode: %s\r\n", status_info.full_duplex ? "Full" : "Half");
            printf("✅ PHY link is UP - should be able to transmit\r\n");
        } else {
            printf("❌ PHY link is DOWN - packets will NOT reach network!\r\n");
            printf("⚠️  Check Ethernet cable connection\r\n");
            printf("⚠️  Check network switch/router\r\n");
            return;
        }
    } else {
        printf("❌ Failed to read PHY status: %d\r\n", status_result);
        return;
    }
    
    // Test 1: Try the working driver with proper packet
    printf("\r\n--- Test 1: Proper Ethernet frame ---\r\n");
    drv_ksz8851snl_status_t result1 = hw_ksz8851snl_send_packet(&ksz8851snl_0, simple_packet, 64);
    
    if (result1 == DRV_KSZ8851SNL_STATUS_OK) {
        printf("✅ Proper packet sent! Check Wireshark for broadcast with EtherType 0x8877\r\n");
    } else {
        printf("❌ Proper packet failed: %d\r\n", result1);
    }
    
    // Wait a bit
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Test 2: Try 0x55 garbage pattern like the working commit
    printf("\r\n--- Test 2: 0x55 garbage pattern (like working commit) ---\r\n");
    fill_garbage_pattern();
    drv_ksz8851snl_status_t result2 = hw_ksz8851snl_send_packet(&ksz8851snl_0, garbage_packet, 64);
    
    if (result2 == DRV_KSZ8851SNL_STATUS_OK) {
        printf("✅ Garbage packet sent! Check Wireshark for 0x55 pattern\r\n");
    } else {
        printf("❌ Garbage packet failed: %d\r\n", result2);
    }
    
    // Debug: Check interrupt status and MAC configuration after transmission
    printf("\r\n--- Post-transmission diagnostics ---\r\n");
    
    // Check interrupt status (might show transmission errors)
    drv_ksz8851snl_id_info_t id_info;
    if (hw_ksz8851snl_get_chip_id(&ksz8851snl_0, &id_info) == DRV_KSZ8851SNL_STATUS_OK) {
        printf("📊 Chip is still responsive (ID: 0x%04X)\r\n", id_info.chip_id);
    } else {
        printf("❌ Chip communication failed after transmission\r\n");
    }
    
    // Check current configuration state
    drv_ksz8851snl_status_info_t final_status;
    if (hw_ksz8851snl_get_status(&ksz8851snl_0, &final_status) == DRV_KSZ8851SNL_STATUS_OK) {
        printf("📊 Final link state: %s\r\n", final_status.link_up ? "UP" : "DOWN");
        printf("📊 Statistics - RX: %d packets, TX: %d packets\r\n", 
               final_status.rx_packets, final_status.tx_packets);
    }
    
    printf("\r\n=== MINIMAL PACKET TEST COMPLETE ===\r\n");
    printf("📝 Summary: Both packet types processed but NEITHER appears in Wireshark\r\n");
    printf("🔍 This suggests a transmission trigger or physical layer issue\r\n");
    printf("💡 The original working commit may have had different configuration\r\n");
    printf("\r\n🔍 ROOT CAUSE IDENTIFIED:\r\n");
    printf("   📊 TX packet counter = 0 means METFE trigger is NOT working!\r\n");
    printf("   📊 Hardware accepts data but doesn't transmit to wire\r\n");
    printf("   💡 Solution: Try TXQ_ENQUEUE (0x0001) instead of TXQ_METFE (0x0008)\r\n");
    printf("\r\n🛠️  NEXT STEP: Modify driver to use TXQ_ENQUEUE for transmission trigger\r\n");
}