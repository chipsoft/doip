/**
 * \file
 *
 * \brief KSZ8851 Ethernet Driver Test Application
 *
 * Copyright (c) 2019 Microchip Technology Inc. and its subsidiaries.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Subject to your compliance with these terms, you may use Microchip
 * software and any derivatives exclusively with Microchip products.
 * It is your responsibility to comply with third party license terms applicable
 * to your use of third party software (including open source software) that
 * may accompany Microchip software.
 *
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE,
 * INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
 * AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP BE
 * LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL
 * LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
 * SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE
 * POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT
 * ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY
 * RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
 * THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 * \asf_license_stop
 *
 */

#include "printf.h"
#include <string.h>
#include <peripheral_clk_config.h>
#include <utils.h>
#include <hal_init.h>
#include <hal_gpio.h>
#include "bsp_led.h"
#include "bsp_eth_ksz8851.h"
#include "FreeRTOS.h"
#include "task.h"

/* RTT printf integration */
extern void rtt_printf_init(void);

// Test results tracking
typedef struct {
    bool driver_init_passed;
    bool spi_communication_passed;
    bool register_access_passed;
    bool chip_id_valid;
    bool mac_address_passed;
    bool link_status_readable;
    bool link_up;
    bool speed_100mbps;
    bool full_duplex;
    int total_tests;
    int passed_tests;
} ksz8851_test_results_t;

static ksz8851_test_results_t test_results = {0};

// Test helper functions
static void print_test_header(const char* test_name)
{
    printf("\r\n=== %s ===\r\n", test_name);
}

static void print_test_result(const char* test_name, bool passed, const char* details)
{
    test_results.total_tests++;
    if (passed) {
        test_results.passed_tests++;
        printf("✓ %s: PASS", test_name);
    } else {
        printf("✗ %s: FAIL", test_name);
    }
    
    if (details) {
        printf(" - %s", details);
    }
    printf("\r\n");
}

// Test 1: Driver Initialization (following Oryx patterns)
static bool test_driver_init(void)
{
    print_test_header("Driver Initialization Test");
    
    // Configure KSZ8851 driver with Microchip OUI MAC address
    drv_eth_ksz8851_config_t config = {
        .mac_addr = {0x00, 0x10, 0xA1, 0x86, 0x95, 0x11},  // Microchip OUI
        .interrupt_driven = false,  // Start with polling mode
        .auto_negotiation = true,
        .full_duplex = true,
        .speed_100mbps = true,
    };
    
    printf("Initializing KSZ8851 driver...\r\n");
    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           config.mac_addr[0], config.mac_addr[1], config.mac_addr[2],
           config.mac_addr[3], config.mac_addr[4], config.mac_addr[5]);
    
    drv_eth_ksz8851_status_t status = hw_eth_ksz8851_init(&ksz8851_eth_0, &config);
    
    if (status == DRV_ETH_KSZ8851_STATUS_OK) {
        test_results.driver_init_passed = true;
        print_test_result("Driver Initialization", true, "KSZ8851 driver initialized successfully");
        return true;
    } else {
        print_test_result("Driver Initialization", false, "Driver initialization failed");
        printf("Error code: %d\r\n", status);
        printf("Check SPI connections:\r\n");
        printf("  SCK:  PB26 -> KSZ8851 SCK\r\n");
        printf("  MOSI: PB27 -> KSZ8851 SI\r\n");
        printf("  MISO: PB29 -> KSZ8851 SO\r\n");
        printf("  CS:   PB28 -> KSZ8851 CS#\r\n");
        return false;
    }
}

// Test 2: SPI Communication Test (register read/write)
static bool test_spi_communication(void)
{
    print_test_header("SPI Communication Test");
    
    if (!test_results.driver_init_passed) {
        print_test_result("SPI Communication", false, "Driver not initialized");
        return false;
    }
    
    // Test 1: Read chip ID register (should be 0x8870 for KSZ8851-16MQL)
    uint16_t chip_id = 0;
    drv_eth_ksz8851_status_t status = hw_eth_ksz8851_read_reg(&ksz8851_eth_0, 0xC0, &chip_id);
    
    if (status != DRV_ETH_KSZ8851_STATUS_OK) {
        print_test_result("Chip ID Read", false, "Failed to read chip ID register");
        return false;
    }
    
    printf("Chip ID Register: 0x%04X\r\n", chip_id);
    
    // Verify chip ID (KSZ8851-16MQL should return 0x8870 masked with 0xFFF0)
    uint16_t expected_chip_id = 0x8870;
    uint16_t chip_id_masked = chip_id & 0xFFF0;
    
    if (chip_id_masked == expected_chip_id) {
        test_results.chip_id_valid = true;
        test_results.spi_communication_passed = true;
        print_test_result("Chip ID Validation", true, "KSZ8851-16MQL detected");
        printf("Chip ID: 0x%04X, Revision: %d\r\n", chip_id_masked, (chip_id & 0x000E) >> 1);
        return true;
    } else {
        print_test_result("Chip ID Validation", false, "Unexpected chip ID");
        printf("Expected: 0x%04X, Got: 0x%04X\r\n", expected_chip_id, chip_id_masked);
        
        // Still consider SPI working if we got a response
        if (chip_id != 0xFFFF && chip_id != 0x0000) {
            test_results.spi_communication_passed = true;
            print_test_result("SPI Communication", true, "SPI responding but wrong chip ID");
        } else {
            print_test_result("SPI Communication", false, "No SPI response");
        }
        return false;
    }
}

// Test 3: Register Access Test (read/write/modify operations)
static bool test_register_access(void)
{
    print_test_header("Register Access Test");
    
    if (!test_results.spi_communication_passed) {
        print_test_result("Register Access", false, "SPI communication not working");
        return false;
    }
    
    bool all_passed = true;
    
    // Test 1: MAC Address Write/Read Test
    uint8_t test_mac[6] = {0x00, 0x10, 0xA1, 0x86, 0x95, 0x11};
    uint8_t read_mac[6] = {0};
    
    drv_eth_ksz8851_status_t status = hw_eth_ksz8851_set_mac_addr(&ksz8851_eth_0, test_mac);
    if (status != DRV_ETH_KSZ8851_STATUS_OK) {
        print_test_result("MAC Address Write", false, "Failed to write MAC address");
        all_passed = false;
    } else {
        print_test_result("MAC Address Write", true, NULL);
    }
    
    status = hw_eth_ksz8851_get_mac_addr(&ksz8851_eth_0, read_mac);
    if (status != DRV_ETH_KSZ8851_STATUS_OK) {
        print_test_result("MAC Address Read", false, "Failed to read MAC address");
        all_passed = false;
    } else {
        // Verify MAC address matches
        bool mac_match = true;
        for (int i = 0; i < 6; i++) {
            if (test_mac[i] != read_mac[i]) {
                mac_match = false;
                break;
            }
        }
        
        if (mac_match) {
            test_results.mac_address_passed = true;
            print_test_result("MAC Address Readback", true, "MAC address matches");
            printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   read_mac[0], read_mac[1], read_mac[2], read_mac[3], read_mac[4], read_mac[5]);
        } else {
            print_test_result("MAC Address Readback", false, "MAC address mismatch");
            printf("Written: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   test_mac[0], test_mac[1], test_mac[2], test_mac[3], test_mac[4], test_mac[5]);
            printf("Read:    %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   read_mac[0], read_mac[1], read_mac[2], read_mac[3], read_mac[4], read_mac[5]);
            all_passed = false;
        }
    }
    
    // Test 2: Bit Set/Clear Operations
    uint16_t test_reg = 0x70;  // TX Control Register
    uint16_t original_value, modified_value, final_value;
    
    // Read original value
    status = hw_eth_ksz8851_read_reg(&ksz8851_eth_0, test_reg, &original_value);
    if (status != DRV_ETH_KSZ8851_STATUS_OK) {
        print_test_result("Register Read", false, "Failed to read test register");
        all_passed = false;
    } else {
        printf("TXCR Original Value: 0x%04X\r\n", original_value);
        
        // Set a test bit (bit 3 - flow control enable)
        status = hw_eth_ksz8851_set_bits(&ksz8851_eth_0, test_reg, 0x0008);
        if (status == DRV_ETH_KSZ8851_STATUS_OK) {
            status = hw_eth_ksz8851_read_reg(&ksz8851_eth_0, test_reg, &modified_value);
            if (status == DRV_ETH_KSZ8851_STATUS_OK && (modified_value & 0x0008)) {
                print_test_result("Bit Set Operation", true, "Bit set successfully");
                
                // Clear the test bit
                status = hw_eth_ksz8851_clear_bits(&ksz8851_eth_0, test_reg, 0x0008);
                if (status == DRV_ETH_KSZ8851_STATUS_OK) {
                    status = hw_eth_ksz8851_read_reg(&ksz8851_eth_0, test_reg, &final_value);
                    if (status == DRV_ETH_KSZ8851_STATUS_OK && !(final_value & 0x0008)) {
                        print_test_result("Bit Clear Operation", true, "Bit cleared successfully");
                    } else {
                        print_test_result("Bit Clear Operation", false, "Bit not cleared");
                        all_passed = false;
                    }
                } else {
                    print_test_result("Bit Clear Operation", false, "Clear operation failed");
                    all_passed = false;
                }
            } else {
                print_test_result("Bit Set Operation", false, "Bit not set");
                all_passed = false;
            }
        } else {
            print_test_result("Bit Set Operation", false, "Set operation failed");
            all_passed = false;
        }
    }
    
    if (all_passed) {
        test_results.register_access_passed = true;
    }
    
    return all_passed;
}

// Test 4: Link Status Test
static bool test_link_status(void)
{
    print_test_header("Link Status Test");
    
    if (!test_results.register_access_passed) {
        print_test_result("Link Status", false, "Register access not working");
        return false;
    }
    
    // Enable the KSZ8851 for link detection
    drv_eth_ksz8851_status_t status = hw_eth_ksz8851_enable(&ksz8851_eth_0);
    if (status != DRV_ETH_KSZ8851_STATUS_OK) {
        print_test_result("Driver Enable", false, "Failed to enable driver");
        return false;
    }
    print_test_result("Driver Enable", true, "KSZ8851 enabled");
    
    // Wait a moment for link negotiation
    printf("Waiting 3 seconds for link negotiation...\r\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Check link status
    bool link_up = false;
    status = hw_eth_ksz8851_get_link_status(&ksz8851_eth_0, &link_up);
    
    if (status != DRV_ETH_KSZ8851_STATUS_OK) {
        print_test_result("Link Status Read", false, "Failed to read link status");
        return false;
    }
    
    test_results.link_status_readable = true;
    test_results.link_up = link_up;
    
    print_test_result("Link Status Read", true, NULL);
    printf("Link Status: %s\r\n", link_up ? "UP" : "DOWN");
    
    if (link_up) {
        // Get link speed and duplex
        bool speed_100mbps = false, full_duplex = false;
        status = hw_eth_ksz8851_get_link_speed(&ksz8851_eth_0, &speed_100mbps, &full_duplex);
        
        if (status == DRV_ETH_KSZ8851_STATUS_OK) {
            test_results.speed_100mbps = speed_100mbps;
            test_results.full_duplex = full_duplex;
            
            printf("Link Speed: %d Mbps\r\n", speed_100mbps ? 100 : 10);
            printf("Duplex Mode: %s\r\n", full_duplex ? "Full" : "Half");
            print_test_result("Link Speed Detection", true, "Speed and duplex detected");
        } else {
            print_test_result("Link Speed Detection", false, "Failed to read speed/duplex");
        }
        
        print_test_result("Link Connection", true, "Ethernet cable connected");
        return true;
    } else {
        print_test_result("Link Connection", false, "No Ethernet cable detected");
        printf("Connect an Ethernet cable to test link detection\r\n");
        return false;
    }
}

// Test 5: Basic Packet Operations Test
static bool test_packet_operations(void)
{
    print_test_header("Packet Operations Test");
    
    if (!test_results.link_up) {
        print_test_result("Packet Operations", false, "No link - cannot test packets");
        printf("Connect Ethernet cable for packet testing\r\n");
        return false;
    }
    
    // Test 1: Check RX status functionality
    drv_eth_ksz8851_rx_status_t rx_status;
    drv_eth_ksz8851_status_t status = hw_eth_ksz8851_get_rx_status(&ksz8851_eth_0, &rx_status);
    
    if (status != DRV_ETH_KSZ8851_STATUS_OK) {
        print_test_result("RX Status Check", false, "Failed to read RX status");
        return false;
    }
    
    print_test_result("RX Status Check", true, "RX status readable");
    printf("RX Status: Valid=%d, Length=%d bytes\r\n", rx_status.valid, rx_status.length);
    
    // Test 2: Simple packet send test (ARP request)
    uint8_t test_packet[] = {
        // Ethernet Header (14 bytes)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination MAC (broadcast)
        0x00, 0x10, 0xA1, 0x86, 0x95, 0x11,  // Source MAC (our MAC)
        0x08, 0x06,                          // EtherType (ARP)
        
        // ARP Header (28 bytes)
        0x00, 0x01,  // Hardware type (Ethernet)
        0x08, 0x00,  // Protocol type (IPv4)
        0x06,        // Hardware address length
        0x04,        // Protocol address length
        0x00, 0x01,  // Operation (ARP request)
        0x00, 0x10, 0xA1, 0x86, 0x95, 0x11,  // Sender MAC
        192, 168, 1, 100,                     // Sender IP
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Target MAC (unknown)
        192, 168, 1, 1                       // Target IP (gateway)
    };
    
    printf("Sending test ARP packet (%d bytes)...\r\n", sizeof(test_packet));
    status = hw_eth_ksz8851_send_packet(&ksz8851_eth_0, test_packet, sizeof(test_packet));
    
    if (status == DRV_ETH_KSZ8851_STATUS_OK) {
        print_test_result("Packet Transmission", true, "ARP packet sent successfully");
    } else {
        print_test_result("Packet Transmission", false, "Failed to send packet");
        printf("Send error code: %d\r\n", status);
        return false;
    }
    
    // Test 3: Check for any received packets
    printf("Checking for received packets (5 second window)...\r\n");
    bool packet_received = false;
    
    for (int i = 0; i < 5; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        status = hw_eth_ksz8851_get_rx_status(&ksz8851_eth_0, &rx_status);
        if (status == DRV_ETH_KSZ8851_STATUS_OK && rx_status.valid) {
            printf("Packet detected! Length: %d bytes\r\n", rx_status.length);
            
            // Try to receive the packet
            uint8_t rx_buffer[1500];
            uint16_t actual_length = 0;
            
            status = hw_eth_ksz8851_receive_packet(&ksz8851_eth_0, rx_buffer, sizeof(rx_buffer), &actual_length);
            if (status == DRV_ETH_KSZ8851_STATUS_OK) {
                printf("Received packet: %d bytes\r\n", actual_length);
                printf("First 16 bytes: ");
                for (int j = 0; j < 16 && j < actual_length; j++) {
                    printf("%02X ", rx_buffer[j]);
                }
                printf("\r\n");
                packet_received = true;
                break;
            }
        }
        printf("  %d/5 seconds elapsed...\r\n", i + 1);
    }
    
    if (packet_received) {
        print_test_result("Packet Reception", true, "Packet received successfully");
    } else {
        print_test_result("Packet Reception", false, "No packets received");
        printf("This may be normal if no network traffic is present\r\n");
    }
    
    return true;
}

// Main KSZ8851 driver test function
static void ksz8851_driver_test(void)
{
    printf("\r\n");
    printf("===============================================\r\n");
    printf("        KSZ8851 Ethernet Driver Test\r\n");
    printf("===============================================\r\n");
    
    // Initialize test results
    memset(&test_results, 0, sizeof(test_results));
    
    // Run all tests in sequence
    test_driver_init();
    test_spi_communication();
    test_register_access();
    test_link_status();
    test_packet_operations();
    
    // Print final results
    printf("\r\n");
    printf("===============================================\r\n");
    printf("           Test Results Summary\r\n");
    printf("===============================================\r\n");
    printf("Total Tests: %d\r\n", test_results.total_tests);
    printf("Passed: %d\r\n", test_results.passed_tests);
    printf("Failed: %d\r\n", test_results.total_tests - test_results.passed_tests);
    printf("Success Rate: %d%%\r\n", 
           test_results.total_tests > 0 ? (test_results.passed_tests * 100) / test_results.total_tests : 0);
    
    printf("\r\nDetailed Status:\r\n");
    printf("  Driver Init:       %s\r\n", test_results.driver_init_passed ? "✓" : "✗");
    printf("  SPI Communication: %s\r\n", test_results.spi_communication_passed ? "✓" : "✗");
    printf("  Chip ID Valid:     %s\r\n", test_results.chip_id_valid ? "✓" : "✗");
    printf("  Register Access:   %s\r\n", test_results.register_access_passed ? "✓" : "✗");
    printf("  MAC Address:       %s\r\n", test_results.mac_address_passed ? "✓" : "✗");
    printf("  Link Status:       %s\r\n", test_results.link_status_readable ? "✓" : "✗");
    printf("  Link Connected:    %s\r\n", test_results.link_up ? "✓" : "✗");
    
    if (test_results.link_up) {
        printf("  Link Speed:        %d Mbps\r\n", test_results.speed_100mbps ? 100 : 10);
        printf("  Duplex Mode:       %s\r\n", test_results.full_duplex ? "Full" : "Half");
    }
    
    printf("===============================================\r\n");
    
    if (test_results.passed_tests == test_results.total_tests) {
        printf("🎉 ALL TESTS PASSED! KSZ8851 driver is working correctly.\r\n");
    } else if (test_results.driver_init_passed && test_results.spi_communication_passed) {
        printf("⚠️  Basic functionality works, but some advanced features failed.\r\n");
    } else {
        printf("❌ Critical failures detected. Check hardware connections.\r\n");
    }
    
    printf("===============================================\r\n\r\n");
}

// FreeRTOS test task
static void ksz8851_test_task(void *pvParameters)
{
    (void)pvParameters;
    
    // Run the KSZ8851 driver test
    ksz8851_driver_test();
    
    // Keep the task alive and blink LED to show we're done
    while (1) {
        hw_led_toggle(&led_yellow);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    /* Initialize system and peripherals */
    init_mcu();
    
    // Initialize LED using universal driver
    hw_led_init(&led_yellow);
    hw_led_on(&led_yellow);
    
    /* Initialize SEGGER RTT for debug output */
    rtt_printf_init();
    
    printf("\r\n");
    printf("===============================================\r\n");
    printf("     KSZ8851 Ethernet Driver Test Application\r\n");
    printf("===============================================\r\n");
    printf("Build Date: %s %s\r\n", __DATE__, __TIME__);
    printf("FreeRTOS Heap Size: %d bytes\r\n", configTOTAL_HEAP_SIZE);
    printf("===============================================\r\n");
    
    /* Create KSZ8851 test task */
    if (xTaskCreate(ksz8851_test_task,
                    "KSZ8851_Test",
                    1024,  // Stack size
                    NULL,
                    (tskIDLE_PRIORITY + 2),
                    NULL)
        != pdPASS) {
        printf("Failed to create KSZ8851 test task\r\n");
        while (1) {
            hw_led_toggle(&led_yellow);
            vTaskDelay(pdMS_TO_TICKS(100));  // Fast blink for error
        }
    }
    
    /* Start FreeRTOS scheduler */
    printf("Starting FreeRTOS scheduler...\r\n");
    vTaskStartScheduler();
    
    /* Should never reach here */
    printf("ERROR: Scheduler returned!\r\n");
    while (1) {
        hw_led_toggle(&led_yellow);
        vTaskDelay(pdMS_TO_TICKS(50));  // Very fast blink for critical error
    }
    
    return 0;
}