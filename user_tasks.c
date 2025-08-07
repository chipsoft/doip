/**
 * \file
 *
 * \brief Starts Ethernet, GMAC and TCP tasks
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

#include "user_tasks.h"
#include "semphr.h"
#include "lwip/tcpip.h"
#include "printf.h"
#include "network_events.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_led.h"
#include "bsp_ethernet.h"
#include "eth_ipstack_main.h"
#include <hal_mac_async.h>
#include "app_libs/asf4/hri/hri_gmac_e54.h"
#include "ethif_mac.h"
#include "driver_doip.h"
#include <string.h>

// Multi-ECU support constants
#define MAX_DISCOVERED_ECUS 8
#define ECU_DISCOVERY_TIMEOUT_MS 10000

// ECU Type definitions based on logical addresses
typedef enum {
    ECU_TYPE_UNKNOWN = 0,
    ECU_TYPE_ENGINE = 1,      // Logical Address 0x0001
    ECU_TYPE_TRANSMISSION = 2, // Logical Address 0x0002
    ECU_TYPE_ABS = 3,         // Logical Address 0x0003
    ECU_TYPE_BCM = 4,         // Logical Address 0x0004
} ecu_type_t;

// Multi-ECU discovery structure
typedef struct {
    drv_doip_vehicle_info_t vehicles[MAX_DISCOVERED_ECUS];
    uint8_t count;
    uint32_t discovery_timestamp;
} multi_ecu_discovery_t;

static multi_ecu_discovery_t discovered_ecus = {0};

uint16_t led_blink_rate = BLINK_NORMAL;

static TaskHandle_t xLed_Task;
static TaskHandle_t xDoip_Client_Task;

/**
 * \brief Get ECU type from logical address
 * \param logical_address ECU logical address
 * \return ECU type enumeration
 */
static ecu_type_t get_ecu_type_from_address(uint16_t logical_address)
{
    switch (logical_address) {
        case 0x0001: return ECU_TYPE_ENGINE;
        case 0x0002: return ECU_TYPE_TRANSMISSION;
        case 0x0003: return ECU_TYPE_ABS;
        case 0x0004: return ECU_TYPE_BCM;
        default:     return ECU_TYPE_UNKNOWN;
    }
}

/**
 * \brief Get ECU type name string
 * \param ecu_type ECU type enumeration
 * \return String representation of ECU type
 */
static const char* get_ecu_type_name(ecu_type_t ecu_type)
{
    switch (ecu_type) {
        case ECU_TYPE_ENGINE:       return "ENGINE";
        case ECU_TYPE_TRANSMISSION: return "TRANSMISSION";
        case ECU_TYPE_ABS:          return "ABS";
        case ECU_TYPE_BCM:          return "BCM";
        default:                    return "UNKNOWN";
    }
}

/**
 * \brief Discover all available ECUs in the vehicle
 * \param handle DOIP driver handle
 * \return Number of ECUs discovered
 */
static uint8_t doip_discover_all_ecus(drv_doip_t *handle)
{
    printf("DOIP Client: Starting multi-ECU discovery...\r\n");
    
    discovered_ecus.count = 0;
    discovered_ecus.discovery_timestamp = xTaskGetTickCount();
    
    // Discover ECUs with extended timeout for multiple responses
    drv_doip_vehicle_info_t temp_vehicle;
    TickType_t discovery_start = xTaskGetTickCount();
    
    while (discovered_ecus.count < MAX_DISCOVERED_ECUS) {
        drv_doip_status_t status = hw_doip_discover_vehicles(handle, &temp_vehicle);
        
        if (status == DRV_DOIP_STATUS_OK) {
            // Check if this ECU is already discovered (avoid duplicates)
            bool already_found = false;
            for (uint8_t i = 0; i < discovered_ecus.count; i++) {
                if (discovered_ecus.vehicles[i].logical_address == temp_vehicle.logical_address) {
                    already_found = true;
                    break;
                }
            }
            
            if (!already_found) {
                // Add new ECU to discovery list
                memcpy(&discovered_ecus.vehicles[discovered_ecus.count], &temp_vehicle, sizeof(drv_doip_vehicle_info_t));
                discovered_ecus.count++;
                
                ecu_type_t ecu_type = get_ecu_type_from_address(temp_vehicle.logical_address);
                printf("DOIP Client: Discovered ECU %d - %s (0x%04X): VIN=%s\r\n", 
                       discovered_ecus.count, get_ecu_type_name(ecu_type), 
                       temp_vehicle.logical_address, temp_vehicle.vin);
            }
        }
        
        // Check timeout for discovery process
        if ((xTaskGetTickCount() - discovery_start) > pdMS_TO_TICKS(ECU_DISCOVERY_TIMEOUT_MS)) {
            printf("DOIP Client: Discovery timeout reached\r\n");
            break;
        }
        
        // Small delay between discovery attempts
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    printf("DOIP Client: Multi-ECU discovery completed - found %d ECUs\r\n", discovered_ecus.count);
    
    // Display summary of discovered ECUs
    if (discovered_ecus.count > 0) {
        printf("\r\n=== Discovered Vehicle ECUs ===\r\n");
        for (uint8_t i = 0; i < discovered_ecus.count; i++) {
            ecu_type_t ecu_type = get_ecu_type_from_address(discovered_ecus.vehicles[i].logical_address);
            printf("  ECU %d: %s (0x%04X) - VIN: %s\r\n", 
                   i + 1, get_ecu_type_name(ecu_type), 
                   discovered_ecus.vehicles[i].logical_address, 
                   discovered_ecus.vehicles[i].vin);
        }
        printf("\r\n");
    }
    
    return discovered_ecus.count;
}

// ==================== LARGE MESSAGE TEST SUPPORT ====================

// Static buffer for large message testing - avoids heap fragmentation
#define LARGE_MESSAGE_TEST_BUFFER_SIZE 8192  // 8KB static buffer
static uint8_t large_message_test_buffer[LARGE_MESSAGE_TEST_BUFFER_SIZE];
static bool large_message_buffer_in_use = false;

/**
 * \brief Generate test pattern data for large message testing
 * \param buffer Pointer to buffer to fill with test pattern
 * \param size Size of buffer in bytes
 */
static void generate_test_pattern(uint8_t *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return;
    }
    
    // Create a 256-byte repeating pattern (0x00 to 0xFF)
    const size_t pattern_size = 256;
    
    // Fill buffer with repeating pattern
    for (size_t i = 0; i < size; i++) {
        buffer[i] = (uint8_t)(i % pattern_size);
    }
    
    // Add unique signature at the end (last 4 bytes = message size as big-endian uint32)
    if (size >= 4) {
        uint32_t signature = (uint32_t)size;
        buffer[size - 4] = (uint8_t)((signature >> 24) & 0xFF);
        buffer[size - 3] = (uint8_t)((signature >> 16) & 0xFF);
        buffer[size - 2] = (uint8_t)((signature >> 8) & 0xFF);
        buffer[size - 1] = (uint8_t)(signature & 0xFF);
    }
}

/**
 * \brief Validate echo response from large message test
 * \param sent_data Pointer to originally sent data
 * \param sent_size Size of originally sent data
 * \param response_data Pointer to received response data
 * \param response_size Size of received response data
 * \return true if validation passes, false otherwise
 * \note Currently unused - for future implementation when BSP layer supports responses
 */
__attribute__((unused))
static bool validate_echo_response(const uint8_t *sent_data, size_t sent_size,
                                  const uint8_t *response_data, size_t response_size)
{
    if (sent_data == NULL || response_data == NULL) {
        printf("DOIP Large Test: Invalid data pointers for validation\r\n");
        return false;
    }
    
    // Response format: Service Response (1) + Length (4) + Echo Data
    if (response_size < 5) {
        printf("DOIP Large Test: Response too short: %zu bytes (min 5)\r\n", response_size);
        return false;
    }
    
    // Check positive response
    if (response_data[0] != (UDS_LARGE_MESSAGE_TEST + UDS_POSITIVE_RESPONSE_MASK)) {
        printf("DOIP Large Test: Negative response: 0x%02X\r\n", response_data[0]);
        return false;
    }
    
    // Extract echoed length (big-endian)
    uint32_t echoed_length = ((uint32_t)response_data[1] << 24) |
                            ((uint32_t)response_data[2] << 16) |
                            ((uint32_t)response_data[3] << 8) |
                            ((uint32_t)response_data[4]);
    
    // Check length matches
    if (echoed_length != sent_size) {
        printf("DOIP Large Test: Length mismatch - sent %zu, echoed %u\r\n", 
               sent_size, (unsigned int)echoed_length);
        return false;
    }
    
    // Check echoed data size
    size_t expected_response_size = 5 + sent_size;  // 1 + 4 + data
    if (response_size != expected_response_size) {
        printf("DOIP Large Test: Response size mismatch - got %zu, expected %zu\r\n",
               response_size, expected_response_size);
        return false;
    }
    
    // Verify echoed data matches sent data
    const uint8_t *echoed_data = &response_data[5];
    if (memcmp(sent_data, echoed_data, sent_size) != 0) {
        printf("DOIP Large Test: Echoed data does not match sent data\r\n");
        return false;
    }
    
    return true;
}

/**
 * \brief Measure transfer performance and display results
 * \param operation_name Name of the operation (e.g., "Send", "Receive")
 * \param data_size Size of data transferred in bytes
 * \param start_tick Start time in FreeRTOS ticks
 * \param end_tick End time in FreeRTOS ticks
 */
static void measure_transfer_performance(const char *operation_name, size_t data_size,
                                       TickType_t start_tick, TickType_t end_tick)
{
    if (operation_name == NULL) {
        return;
    }
    
    TickType_t duration_ticks = end_tick - start_tick;
    uint32_t duration_ms = duration_ticks * 1000 / configTICK_RATE_HZ;
    
    if (duration_ms > 0) {
        uint32_t transfer_rate_bps = (data_size * 1000) / duration_ms;  // bytes per second
        uint32_t transfer_rate_kbps = transfer_rate_bps / 1024;         // KB per second
        
        printf("  %s: %zu bytes in %u ms (%.1f KB/s)\r\n",
               operation_name, data_size, (unsigned int)duration_ms, 
               (float)transfer_rate_kbps);
    } else {
        printf("  %s: %zu bytes in <1 ms (very fast)\r\n",
               operation_name, data_size);
    }
}

/**
 * \brief Read DID from specific ECU and display formatted result
 * \param handle DOIP driver handle
 * \param ecu_info Target ECU information
 * \param did Data Identifier to read
 * \param name Human-readable name for display
 * \param format Format string for data interpretation
 * \return Driver status
 */
static drv_doip_status_t doip_read_did_from_ecu_and_display(drv_doip_t *handle, const drv_doip_vehicle_info_t *ecu_info, 
                                                           uint16_t did, const char *name, const char *format)
{
    if (handle == NULL || ecu_info == NULL || name == NULL || format == NULL) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    ecu_type_t ecu_type = get_ecu_type_from_address(ecu_info->logical_address);
    const char *ecu_name = get_ecu_type_name(ecu_type);
    
    uint8_t response[256];
    size_t actual_len = 0;
    
    drv_doip_status_t status = hw_doip_send_diagnostic_request(handle, UDS_READ_DATA_BY_IDENTIFIER, 
                                                              did, NULL, 0, response, sizeof(response), &actual_len);
    
    if (status != DRV_DOIP_STATUS_OK) {
        printf("DOIP: [%s] Failed to read DID 0x%04X (%s): status %d\r\n", ecu_name, did, name, status);
        return status;
    }
    
    if (actual_len == 0) {
        printf("  [%s] %-20s: Not supported by this ECU\r\n", ecu_name, name);
        return DRV_DOIP_STATUS_OK; // Not an error, just not supported
    }
    
    if (actual_len < 3) {
        printf("DOIP: [%s] Invalid response length for DID 0x%04X (%s): %zu bytes\r\n", ecu_name, did, name, actual_len);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Check positive response (service_id + 0x40)
    if (response[0] != (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK)) {
        printf("DOIP: [%s] Negative response for DID 0x%04X (%s): 0x%02X\r\n", ecu_name, did, name, response[0]);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Verify DID echo
    uint16_t response_did = (response[1] << 8) | response[2];
    if (response_did != did) {
        printf("DOIP: [%s] DID mismatch for %s: requested 0x%04X, got 0x%04X\r\n", ecu_name, name, did, response_did);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Display data based on format with ECU identification
    printf("  [%s] %-20s: ", ecu_name, name);
    
    if (strcmp(format, "string") == 0) {
        // Display as ASCII string (null-terminated or truncated)
        char temp_str[128];
        size_t data_len = actual_len - 3;
        size_t copy_len = (data_len < sizeof(temp_str) - 1) ? data_len : sizeof(temp_str) - 1;
        memcpy(temp_str, &response[3], copy_len);
        temp_str[copy_len] = '\0';
        printf("%s\r\n", temp_str);
        
    } else if (strcmp(format, "uint8") == 0) {
        if (actual_len >= 4) {
            printf("%u\r\n", response[3]);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else if (strcmp(format, "uint16") == 0) {
        if (actual_len >= 5) {
            uint16_t value = (response[3] << 8) | response[4];
            printf("%u\r\n", value);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else if (strcmp(format, "uint16_kmh") == 0) {
        if (actual_len >= 5) {
            uint16_t value = (response[3] << 8) | response[4];
            printf("%u km/h\r\n", value);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else if (strcmp(format, "uint16_rpm") == 0) {
        if (actual_len >= 5) {
            uint16_t value = (response[3] << 8) | response[4];
            printf("%u RPM\r\n", value);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else if (strcmp(format, "uint16_mv") == 0) {
        if (actual_len >= 5) {
            uint16_t value = (response[3] << 8) | response[4];
            printf("%.1f V\r\n", value / 1000.0f);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else if (strcmp(format, "int16_temp") == 0) {
        if (actual_len >= 5) {
            int16_t value = (response[3] << 8) | response[4];
            printf("%.1f°C\r\n", value / 10.0f);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else if (strcmp(format, "uint32") == 0) {
        if (actual_len >= 7) {
            uint32_t value = (response[3] << 24) | (response[4] << 16) | (response[5] << 8) | response[6];
            printf("%lu\r\n", value);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else if (strcmp(format, "uint32_hours") == 0) {
        if (actual_len >= 7) {
            uint32_t value = (response[3] << 24) | (response[4] << 16) | (response[5] << 8) | response[6];
            printf("%lu hours\r\n", value);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else if (strcmp(format, "uint8_percent") == 0) {
        if (actual_len >= 4) {
            printf("%u%%\r\n", response[3]);
        } else {
            printf("Invalid data length\r\n");
        }
        
    } else {
        // Default: display as hex bytes
        printf("0x");
        for (size_t i = 3; i < actual_len; i++) {
            printf("%02X", response[i]);
        }
        printf("\r\n");
    }
    
    return DRV_DOIP_STATUS_OK;
}

/**
 * \brief Legacy wrapper function for backward compatibility
 * \param handle DOIP driver handle
 * \param did Data Identifier to read
 * \param name Human-readable name for display
 * \param format Format string for data interpretation
 * \return Driver status
 */
static drv_doip_status_t doip_read_did_and_display(drv_doip_t *handle, uint16_t did, const char *name, const char *format)
{
    // Use first discovered ECU for legacy compatibility
    if (discovered_ecus.count > 0) {
        return doip_read_did_from_ecu_and_display(handle, &discovered_ecus.vehicles[0], did, name, format);
    } else {
        printf("DOIP: No ECUs discovered for legacy DID read\r\n");
        return DRV_DOIP_STATUS_NO_VEHICLE;
    }
}

/**
 * \brief Test concurrent requests to multiple ECUs
 * \param handle DOIP driver handle
 */
static void doip_test_concurrent_ecu_requests(drv_doip_t *handle)
{
    if (discovered_ecus.count < 2) {
        printf("DOIP Client: Need at least 2 ECUs for concurrent testing (found %d)\r\n", discovered_ecus.count);
        return;
    }
    
    printf("\r\n=== Testing Concurrent ECU Requests ===\r\n");
    
    // Test reading same DID from multiple ECUs to show differentiation
    uint16_t test_dids[] = {DID_VIN, DID_VEHICLE_SPEED_INFORMATION, DID_BATTERY_VOLTAGE_INFORMATION};
    const char* test_names[] = {"VIN", "Vehicle Speed", "Battery Voltage"};
    const char* test_formats[] = {"string", "uint16_kmh", "uint16_mv"};
    
    for (int did_idx = 0; did_idx < 3; did_idx++) {
        printf("\r\n--- Reading %s from all ECUs ---\r\n", test_names[did_idx]);
        
        for (uint8_t ecu_idx = 0; ecu_idx < discovered_ecus.count; ecu_idx++) {
            // Connect to specific ECU
            if (hw_doip_connect_to_vehicle(handle, &discovered_ecus.vehicles[ecu_idx]) == DRV_DOIP_STATUS_OK) {
                // Read DID from this specific ECU
                doip_read_did_from_ecu_and_display(handle, &discovered_ecus.vehicles[ecu_idx], 
                                                   test_dids[did_idx], test_names[did_idx], test_formats[did_idx]);
                
                // Disconnect from this ECU
                hw_doip_disconnect(handle);
                
                // Small delay between ECU connections
                vTaskDelay(pdMS_TO_TICKS(100));
            } else {
                ecu_type_t ecu_type = get_ecu_type_from_address(discovered_ecus.vehicles[ecu_idx].logical_address);
                printf("  [%s] Connection failed\r\n", get_ecu_type_name(ecu_type));
            }
        }
    }
}

// Test result enumeration
typedef enum {
    TEST_RESULT_PASSED = 0,
    TEST_RESULT_FAILED = 1,
    TEST_RESULT_SKIPPED = 2,
} test_result_t;

/**
 * \brief Test large message capability with a single ECU (static buffer version)
 * \param handle DOIP driver handle
 * \param ecu_info Target ECU information
 * \param test_size Size of test message in bytes
 * \return test_result_t indicating test result
 */
static test_result_t doip_test_large_message_single_ecu(drv_doip_t *handle, 
                                                       const drv_doip_vehicle_info_t *ecu_info,
                                                       size_t test_size)
{
    if (handle == NULL || ecu_info == NULL || test_size == 0) {
        return TEST_RESULT_FAILED;
    }
    
    ecu_type_t ecu_type = get_ecu_type_from_address(ecu_info->logical_address);
    const char *ecu_name = get_ecu_type_name(ecu_type);
    
    printf("  Testing %zu bytes with ECU %s (0x%04X)...\r\n", 
           test_size, ecu_name, ecu_info->logical_address);
    
    // First, ensure we're connected to this ECU
    printf("  Connecting to ECU %s...\r\n", ecu_name);
    drv_doip_status_t connect_status = hw_doip_connect_to_vehicle(handle, ecu_info);
    if (connect_status != DRV_DOIP_STATUS_OK) {
        printf("  ERROR: Failed to connect to ECU %s: status %d\r\n", ecu_name, connect_status);
        if (connect_status == DRV_DOIP_STATUS_TIMEOUT) {
            printf("  INFO: This is expected if no DOIP server is running\r\n");
        }
        return TEST_RESULT_FAILED;
    }
    printf("  Successfully connected to ECU %s\r\n", ecu_name);
    
    // Check if buffer is available
    if (large_message_buffer_in_use) {
        printf("  SKIP: Large message buffer is in use by another test\r\n");
        return TEST_RESULT_SKIPPED;
    }
    
    // Check if test size fits in static buffer (need space for service ID)
    size_t buffer_size = 1 + test_size;  // Service ID + test data
    if (buffer_size > LARGE_MESSAGE_TEST_BUFFER_SIZE) {
        printf("  SKIP: Message too large - need %zu bytes, buffer is %d bytes\r\n", 
               buffer_size, LARGE_MESSAGE_TEST_BUFFER_SIZE);
        return TEST_RESULT_SKIPPED;
    }
    
    // Mark buffer as in use
    large_message_buffer_in_use = true;
    
    // Use static buffer - no allocation needed!
    large_message_test_buffer[0] = UDS_LARGE_MESSAGE_TEST;
    generate_test_pattern(&large_message_test_buffer[1], test_size);
    
    // Send large message test via raw message API
    printf("  Sending %zu byte message (using static buffer)...\r\n", buffer_size);
    TickType_t send_start = xTaskGetTickCount();
    
    drv_doip_status_t status = hw_doip_send_raw_message(
        handle, 
        DOIP_DIAGNOSTIC_MESSAGE,
        large_message_test_buffer, 
        buffer_size,
        true  // use_static_buffer = true
    );
    
    TickType_t send_end = xTaskGetTickCount();
    
    // Release buffer
    large_message_buffer_in_use = false;
    
    if (status != DRV_DOIP_STATUS_OK) {
        printf("  ERROR: Failed to send large message: status %d\r\n", status);
        // Disconnect before returning error
        printf("  Disconnecting from ECU %s after error...\r\n", ecu_name);
        hw_doip_disconnect(handle);
        return TEST_RESULT_FAILED;
    }
    
    measure_transfer_performance("Send", buffer_size, send_start, send_end);
    
    printf("  ✅ Large message test with ECU %s: SUCCESS\r\n", ecu_name);
    printf("  Successfully sent %zu byte large message via unified DoIP API\r\n", buffer_size);
    
    // Disconnect from ECU after successful test
    printf("  Disconnecting from ECU %s...\r\n", ecu_name);
    drv_doip_status_t disconnect_status = hw_doip_disconnect(handle);
    if (disconnect_status != DRV_DOIP_STATUS_OK) {
        printf("  WARNING: Failed to disconnect from ECU %s: status %d\r\n", ecu_name, disconnect_status);
    }
    
    return TEST_RESULT_PASSED;
}

/**
 * \brief Test large messages with all discovered ECUs
 * \param handle DOIP driver handle
 */
static void doip_test_large_messages_all_ecus(drv_doip_t *handle)
{
    if (handle == NULL || discovered_ecus.count == 0) {
        printf("DOIP Large Test: No ECUs available for testing\r\n");
        return;
    }
    
    printf("\r\n=== DOIP Large Message Test Suite ===\r\n");
    printf("Testing large message capabilities with %d ECUs\r\n", discovered_ecus.count);
    printf("Using 8KB static buffer (no heap allocation)\r\n");
    printf("Available heap: %zu bytes\r\n", xPortGetFreeHeapSize());
    
    // Test sizes - up to 8KB using static buffer
    const size_t test_sizes[] = {
        256,       // 256B - Small message
        512,       // 512B - Medium message  
        1024,      // 1KB - Basic functionality
        1400,      // Close to MTU boundary  
        1600,      // Above MTU - fragmentation test
        2048,      // 2KB - Reasonable large message
        3072,      // 3KB - Larger test
        4096,      // 4KB - Large message test
        5120,      // 5KB - Very large message
        6144,      // 6KB - Extra large message
        7168,      // 7KB - Near maximum message  
        8191,      // 8KB-1 - Maximum size (leave 1 byte for service ID)
    };
    
    const size_t num_test_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    uint16_t total_tests = 0;
    uint16_t passed_tests = 0;
    uint16_t skipped_tests = 0;
    
    // Test each size with each ECU
    for (size_t size_idx = 0; size_idx < num_test_sizes; size_idx++) {
        size_t test_size = test_sizes[size_idx];
        
        printf("\r\n--- Testing %zu bytes (%zu KB) ---\r\n", 
               test_size, test_size / 1024);
        
        for (uint8_t ecu_idx = 0; ecu_idx < discovered_ecus.count; ecu_idx++) {
            total_tests++;
            
            test_result_t result = doip_test_large_message_single_ecu(handle, 
                                                                    &discovered_ecus.vehicles[ecu_idx], 
                                                                    test_size);
            
            if (result == TEST_RESULT_PASSED) {
                passed_tests++;
            } else if (result == TEST_RESULT_SKIPPED) {
                skipped_tests++;
            }
            
            // Small delay between tests to avoid overwhelming the network
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    // Display summary
    uint16_t failed_tests = total_tests - passed_tests - skipped_tests;
    
    printf("\r\n=== Large Message Test Results ===\r\n");
    printf("Total Tests: %d\r\n", total_tests);
    printf("Passed: %d\r\n", passed_tests);
    printf("Failed: %d\r\n", failed_tests);
    printf("Skipped: %d (memory constraints)\r\n", skipped_tests);
    if (total_tests > 0) {
        printf("Success Rate: %.1f%% (%d/%d executed tests)\r\n", 
               (float)passed_tests * 100.0f / (total_tests - skipped_tests),
               passed_tests, total_tests - skipped_tests);
    }
    printf("Final heap: %zu bytes\r\n", xPortGetFreeHeapSize());
    printf("======================================\r\n\r\n");
    
    // Add delay before continuing with other tests
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/**
 * OS task that blinks LED
 */
static void led_task(void *p)
{
	(void)p;
	for (;;) {
		hw_led_toggle(&led_yellow);
		vTaskDelay(led_blink_rate);
	}
}

/**
 * OS task that handles DOIP client operations
 */
static void doip_client_task(void *pvParameters)
{
	drv_doip_t *doip_handle = (drv_doip_t *)pvParameters;
	
	if (doip_handle == NULL) {
		printf("DOIP Client: Invalid driver handle passed to task\r\n");
		vTaskDelete(NULL);
		return;
	}
	
	printf("DOIP Client: Task started\r\n");
	
	// Initialize DOIP driver
	printf("DOIP Client: Initializing driver...\r\n");
	if (hw_doip_init(doip_handle) != DRV_DOIP_STATUS_OK) {
		printf("DOIP Client: Driver initialization failed\r\n");
		vTaskDelete(NULL);
		return;
	}
	
	printf("DOIP Client: Driver initialized successfully\r\n");
	
	// Small delay to ensure driver is ready
	vTaskDelay(pdMS_TO_TICKS(100));
	
	// Wait a bit for network to be fully ready
	vTaskDelay(pdMS_TO_TICKS(2000));
	
	while (1) {
		// Only proceed if we're in idle state (not connected)
		if (hw_doip_get_status(doip_handle) == DRV_DOIP_STATE_IDLE) {
			// Discover all available ECUs
			uint8_t ecu_count = doip_discover_all_ecus(doip_handle);
			
			if (ecu_count > 0) {
				printf("DOIP Client: Found %d ECUs, testing multi-ECU communication...\r\n", ecu_count);
				
				// Test large message capabilities with all ECUs
				doip_test_large_messages_all_ecus(doip_handle);
				
				// Test concurrent ECU requests
				doip_test_concurrent_ecu_requests(doip_handle);
				
				// Connect to primary ECU (first discovered) for detailed analysis
				printf("\r\n=== Connecting to Primary ECU for Detailed Analysis ===\r\n");
				if (hw_doip_connect_to_vehicle(doip_handle, &discovered_ecus.vehicles[0]) == DRV_DOIP_STATUS_OK) {
					printf("\r\n--- DOIP Communication Complete ---\r\n");
					
					// Read and display system information after successful connection
					if (hw_doip_get_status(doip_handle) == DRV_DOIP_STATE_ACTIVATED) {
						printf("\r\n=== ECU System Information ===\r\n");
						
						// Basic identification
						doip_read_did_and_display(doip_handle, DID_VIN, "VIN", "string");
						doip_read_did_and_display(doip_handle, DID_ECU_SOFTWARE_VERSION, "ECU SW Version", "string");
						doip_read_did_and_display(doip_handle, DID_ECU_HARDWARE_VERSION, "ECU HW Version", "string");
						doip_read_did_and_display(doip_handle, DID_ECU_SERIAL_NUMBER, "ECU Serial Number", "string");
						
						printf("\r\n=== System Details ===\r\n");
						doip_read_did_and_display(doip_handle, DID_ACTIVE_DIAGNOSTIC_SESSION, "Diagnostic Session", "uint8");
						doip_read_did_and_display(doip_handle, DID_VEHICLE_MANUFACTURER_SPARE_PART_NUMBER, "Spare Part Number", "string");
						doip_read_did_and_display(doip_handle, DID_SYSTEM_SUPPLIER_IDENTIFIER, "System Supplier", "string");
						doip_read_did_and_display(doip_handle, DID_ECU_MANUFACTURING_DATE, "Manufacturing Date", "string");
						
						printf("\r\n=== Network Information ===\r\n");
						doip_read_did_and_display(doip_handle, DID_VEHICLE_MANUFACTURER_ECU_NETWORK_NAME, "Network Name", "string");
						doip_read_did_and_display(doip_handle, DID_VEHICLE_MANUFACTURER_ECU_NETWORK_ADDRESS, "Network Address", "string");
						
						printf("\r\n=== Runtime Monitoring ===\r\n");
						doip_read_did_and_display(doip_handle, DID_ECU_OPERATING_HOURS, "Operating Hours", "uint32_hours");
						doip_read_did_and_display(doip_handle, DID_VEHICLE_SPEED_INFORMATION, "Vehicle Speed", "uint16_kmh");
						doip_read_did_and_display(doip_handle, DID_ENGINE_RPM_INFORMATION, "Engine RPM", "uint16_rpm");
						doip_read_did_and_display(doip_handle, DID_BATTERY_VOLTAGE_INFORMATION, "Battery Voltage", "uint16_mv");
						doip_read_did_and_display(doip_handle, DID_TEMPERATURE_SENSOR_DATA, "Temperature", "int16_temp");
						doip_read_did_and_display(doip_handle, DID_FUEL_LEVEL_INFORMATION, "Fuel Level", "uint8_percent");
						
						printf("\r\n=== Diagnostic Status ===\r\n");
						doip_read_did_and_display(doip_handle, DID_ERROR_MEMORY_STATUS, "Error Memory Status", "uint8");
						doip_read_did_and_display(doip_handle, DID_LAST_RESET_REASON, "Last Reset Reason", "uint8");
						doip_read_did_and_display(doip_handle, DID_BOOT_SOFTWARE_IDENTIFICATION, "Boot Software ID", "string");
						
						printf("\r\n--- Testing Alive Check ---\r\n");
						printf("DOIP Client: Connection established, listening for ECU messages...\r\n");
						
						// Listen period for incoming messages (alive checks, etc.)
						vTaskDelay(pdMS_TO_TICKS(3000)); // 3 second listening period
						
						printf("DOIP Client: Alive check testing completed\r\n");
						
						// Periodic monitoring of dynamic data
						printf("\r\n--- Periodic Runtime Monitoring ---\r\n");
						printf("DOIP Client: Monitoring dynamic data for 60 seconds...\r\n");
						
						for (int cycle = 0; cycle < 4; cycle++) { // 4 cycles = 60 seconds
							printf("\r\n--- Monitoring Cycle %d ---\r\n", cycle + 1);
							
							// Read dynamic runtime values
							doip_read_did_and_display(doip_handle, DID_VEHICLE_SPEED_INFORMATION, "Vehicle Speed", "uint16_kmh");
							doip_read_did_and_display(doip_handle, DID_ENGINE_RPM_INFORMATION, "Engine RPM", "uint16_rpm");
							doip_read_did_and_display(doip_handle, DID_BATTERY_VOLTAGE_INFORMATION, "Battery Voltage", "uint16_mv");
							doip_read_did_and_display(doip_handle, DID_TEMPERATURE_SENSOR_DATA, "Temperature", "int16_temp");
							doip_read_did_and_display(doip_handle, DID_FUEL_LEVEL_INFORMATION, "Fuel Level", "uint8_percent");
							doip_read_did_and_display(doip_handle, DID_ECU_OPERATING_HOURS, "Operating Hours", "uint32_hours");
							
							if (cycle < 3) { // Don't wait after last cycle
								vTaskDelay(pdMS_TO_TICKS(15000)); // Wait 15 seconds between readings
							}
						}
						
						printf("DOIP Client: Periodic monitoring completed\r\n");
					}
					
					// Disconnect after communication
					hw_doip_disconnect(doip_handle);
					
					// Wait before next cycle
					vTaskDelay(pdMS_TO_TICKS(30000)); // 30 seconds
				} else {
					printf("DOIP Client: Primary ECU connection failed\r\n");
					vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds before retry
				}
			} else {
				printf("DOIP Client: No ECUs discovered in multi-ECU scan\r\n");
				vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds before retry
			}
		} else {
			// If in connected state, perform periodic monitoring
			vTaskDelay(pdMS_TO_TICKS(5000));
		}
	}
}


/**
 * \brief Create OS task for LED blinking
 */
void task_led_create(void)
{
	/* Create task to make led blink */
	if (xTaskCreate(led_task, "Led", TASK_LED_STACK_SIZE, NULL, TASK_LED_TASK_PRIORITY, &xLed_Task) != pdPASS) {
		while (1) {
			;
		}
	}
}

/**
 * \brief Create OS task for DOIP client operations
 * \param doip_handle Pointer to the DOIP driver instance to use
 */
void task_doip_client_create(drv_doip_t *doip_handle)
{
	if (doip_handle == NULL) {
		printf("DOIP Client: Cannot create task with NULL driver handle\r\n");
		return;
	}
	
	/* Create task for DOIP client operations */
	if (xTaskCreate(doip_client_task, "DOIPClient", DOIP_CLIENT_TASK_STACK_SIZE, 
	                (void *)doip_handle, DOIP_CLIENT_TASK_PRIORITY, &xDoip_Client_Task) != pdPASS) {
		printf("DOIP Client: Failed to create DOIP client task\r\n");
		while (1) {
			;
		}
	}
	
	printf("DOIP Client: Task created successfully\r\n");
}

// Task handle for diagnostic processor
static TaskHandle_t xDiagnostic_Processor_Task;

// Diagnostic packet callback for raw packet processing
static void diagnostic_packet_callback(const drv_doip_raw_packet_t *packet)
{
    if (packet == NULL) {
        return;
    }
    
    printf("Diagnostic: Raw packet received - Type: 0x%04X, Length: %lu, IP: 0x%08lX\r\n",
           packet->payload_type, packet->payload_length, packet->source_ip_address);
    
    // Detailed packet analysis based on payload type
    switch (packet->payload_type) {
        case DOIP_VEHICLE_IDENTIFICATION_RESPONSE:
            printf("Diagnostic: Vehicle identification response received\r\n");
            if (packet->payload_length >= 17) {
                printf("Diagnostic: VIN: %.17s\r\n", packet->payload);
            }
            break;
            
        case DOIP_ROUTING_ACTIVATION_RESPONSE:
            printf("Diagnostic: Routing activation response received\r\n");
            if (packet->payload_length >= 9) {
                uint16_t source_addr = (packet->payload[0] << 8) | packet->payload[1];
                uint16_t target_addr = (packet->payload[2] << 8) | packet->payload[3];
                uint8_t response_code = packet->payload[4];
                printf("Diagnostic: SA=0x%04X, TA=0x%04X, Code=0x%02X\r\n", 
                       source_addr, target_addr, response_code);
            }
            break;
            
        case DOIP_DIAGNOSTIC_MESSAGE:
            printf("Diagnostic: Diagnostic message received\r\n");
            if (packet->payload_length >= 4) {
                uint16_t source_addr = (packet->payload[0] << 8) | packet->payload[1];
                uint16_t target_addr = (packet->payload[2] << 8) | packet->payload[3];
                printf("Diagnostic: SA=0x%04X, TA=0x%04X\r\n", source_addr, target_addr);
                
                // Parse UDS data if available
                if (packet->payload_length > 4) {
                    printf("Diagnostic: UDS Data: ");
                    for (uint32_t i = 4; i < packet->payload_length && i < 20; i++) {
                        printf("0x%02X ", packet->payload[i]);
                    }
                    if (packet->payload_length > 20) {
                        printf("... (truncated)");
                    }
                    printf("\r\n");
                }
            }
            break;
            
        case DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK:
            printf("Diagnostic: Positive ACK received\r\n");
            break;
            
        case DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK:
            printf("Diagnostic: Negative ACK received\r\n");
            if (packet->payload_length >= 5) {
                uint8_t nack_code = packet->payload[4];
                printf("Diagnostic: NACK Code: 0x%02X\r\n", nack_code);
            }
            break;
            
        case DOIP_ALIVE_CHECK_REQUEST:
            printf("Diagnostic: Alive check request received\r\n");
            break;
            
        case DOIP_ALIVE_CHECK_RESPONSE:
            printf("Diagnostic: Alive check response received\r\n");
            break;
            
        default:
            printf("Diagnostic: Unknown/Unsupported payload type: 0x%04X\r\n", packet->payload_type);
            break;
    }
    
    // Fragment handling information
    if (packet->is_fragmented) {
        printf("Diagnostic: Fragment %d/%d (total message: %lu bytes)\r\n",
               packet->fragment_index + 1, packet->total_fragments, packet->total_message_length);
    }
    
    printf("Diagnostic: Packet timestamp: %lu ms\r\n", packet->timestamp_ms);
    printf("Diagnostic: ------- End of packet analysis -------\r\n\r\n");
}

// Main diagnostic processing task
static void diagnostic_processor_task(void *pvParameters)
{
    drv_doip_t *doip_handle = (drv_doip_t *)pvParameters;
    
    printf("Diagnostic Processor: Task started\r\n");
    
    // Wait for system initialization
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Wait for DOIP driver to be initialized (with timeout)
    printf("Diagnostic Processor: Waiting for DOIP driver initialization...\r\n");
    uint32_t wait_count = 0;
    const uint32_t max_wait_cycles = 20; // 10 seconds total wait time
    while (!doip_handle->is_init && wait_count < max_wait_cycles) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_count++;
        if (wait_count % 4 == 0) {  // Print every 2 seconds
            printf("Diagnostic Processor: Still waiting for DOIP driver init (%lu/10s)...\r\n", wait_count / 2);
        }
    }
    
    if (!doip_handle->is_init) {
        printf("Diagnostic Processor: Timeout waiting for DOIP driver initialization\r\n");
        vTaskDelete(NULL);
        return;
    }
    
    printf("Diagnostic Processor: DOIP driver initialized, proceeding...\r\n");
    
    // Register packet callback for raw DOIP packet processing
    drv_doip_status_t status = hw_doip_register_packet_callback(doip_handle, diagnostic_packet_callback);
    if (status != DRV_DOIP_STATUS_OK) {
        printf("Diagnostic Processor: Raw packet functionality not available (status: %d)\r\n", status);
        printf("Diagnostic Processor: Running in traditional DOIP mode without raw packet analysis\r\n");
        
        // Continue running but in a simpler mode without raw packet analysis
        while (1) {
            printf("Diagnostic Processor: Traditional DOIP mode - monitoring connection state\r\n");
            drv_doip_state_t current_state = hw_doip_get_status(doip_handle);
            printf("Diagnostic Processor: Current DOIP state: %d\r\n", current_state);
            vTaskDelay(pdMS_TO_TICKS(10000)); // Report every 10 seconds
        }
    }
    
    printf("Diagnostic Processor: Packet callback registered successfully\r\n");
    
    // Configure packet listener
    drv_doip_packet_listener_config_t listener_config = {
        .is_enabled = true,
        .timeout_ms = 5000,         // 5 second timeout
        .max_fragments = 50,        // Support up to 50 fragments for large messages
        .packet_callback = diagnostic_packet_callback
    };
    
    // Start packet listener after DOIP connection is established
    printf("Diagnostic Processor: Waiting for DOIP connection...\r\n");
    
    while (1) {
        drv_doip_state_t current_state = hw_doip_get_status(doip_handle);
        
        if (current_state == DRV_DOIP_STATE_CONNECTED || current_state == DRV_DOIP_STATE_ACTIVATED) {
            // Start packet listener if not already active
            status = hw_doip_start_packet_listener(doip_handle, &listener_config);
            if (status == DRV_DOIP_STATUS_OK) {
                printf("Diagnostic Processor: Packet listener started successfully\r\n");
                break;
            } else {
                printf("Diagnostic Processor: Failed to start packet listener, retrying...\r\n");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    // Main processing loop
    while (1) {
        drv_doip_state_t current_state = hw_doip_get_status(doip_handle);
        
        if (current_state == DRV_DOIP_STATE_IDLE || current_state == DRV_DOIP_STATE_ERROR) {
            printf("Diagnostic Processor: Connection lost, stopping packet listener\r\n");
            hw_doip_stop_packet_listener(doip_handle);
            
            // Wait for reconnection
            while (hw_doip_get_status(doip_handle) != DRV_DOIP_STATE_CONNECTED &&
                   hw_doip_get_status(doip_handle) != DRV_DOIP_STATE_ACTIVATED) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            
            // Restart packet listener
            hw_doip_start_packet_listener(doip_handle, &listener_config);
            printf("Diagnostic Processor: Packet listener restarted after reconnection\r\n");
        }
        
        // Demonstrate raw message sending capability
        static uint32_t demo_counter = 0;
        if (demo_counter % 30 == 0) {  // Every 30 seconds
            // Send a custom alive check request using raw messaging
            drv_doip_raw_packet_t raw_packet = {0};
            raw_packet.protocol_version = DOIP_PROTOCOL_VERSION;
            raw_packet.inverse_protocol_version = DOIP_INVERSE_PROTOCOL_VERSION;
            raw_packet.payload_type = DOIP_ALIVE_CHECK_REQUEST;
            raw_packet.payload_length = 2;
            raw_packet.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
            raw_packet.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
            
            status = hw_doip_send_raw_message(doip_handle, raw_packet.payload_type, 
                                             raw_packet.payload, raw_packet.payload_length, false);
            if (status == DRV_DOIP_STATUS_OK) {
                printf("Diagnostic Processor: Sent custom alive check request via raw messaging\r\n");
            }
        }
        
        demo_counter++;
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1 second interval
    }
}

/**
 * \brief Create diagnostic processor task for raw DOIP packet analysis
 * \param doip_handle Pointer to the DOIP driver instance to use
 */
void task_diagnostic_processor_create(drv_doip_t *doip_handle)
{
    if (doip_handle == NULL) {
        printf("Diagnostic Processor: Cannot create task with NULL driver handle\r\n");
        return;
    }
    
    /* Create task for diagnostic processing */
    if (xTaskCreate(diagnostic_processor_task, "DiagProcessor", DOIP_CLIENT_TASK_STACK_SIZE, 
                    (void *)doip_handle, DOIP_CLIENT_TASK_PRIORITY + 1, &xDiagnostic_Processor_Task) != pdPASS) {
        printf("Diagnostic Processor: Failed to create diagnostic processor task\r\n");
        while (1) {
            ;
        }
    }
    
    printf("Diagnostic Processor: Task created successfully\r\n");
}

