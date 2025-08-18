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
#include "bsp_ksz8851snl.h"
#include <string.h>

// External reference to DoIP driver instance
extern drv_doip_t doip_0;

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
    bool discovery_active;
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
 * \brief Discovery callback - handles vehicle identification responses
 * \param type Callback type
 * \param data Pointer to received data (enhanced UDP context)
 * \param data_len Length of received data
 */
static void discovery_callback(drv_doip_cb_type_t type, const void *data, size_t data_len)
{
    if (type != DRV_DOIP_CB_RAW_PACKET_RECEIVED || !discovered_ecus.discovery_active) {
        return;
    }
    
    if (data == NULL || data_len < 8) {
        printf("DOIP Discovery: Invalid packet - data=%p, len=%zu\r\n", data, data_len);
        return; // Invalid packet
    }
    
    const uint8_t *packet = (const uint8_t *)data;
    
    printf("DOIP Discovery: Received packet %zu bytes: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           data_len, packet[0], packet[1], packet[2], packet[3], 
           packet[4], packet[5], packet[6], packet[7]);
    
    // Validate DoIP protocol version
    if (packet[0] != 0x02 || packet[1] != 0xFD) {
        printf("DOIP Discovery: Invalid protocol version: 0x%02X 0x%02X\r\n", packet[0], packet[1]);
        return;
    }
    
    // Parse DoIP header
    uint16_t payload_type = (packet[2] << 8) | packet[3];
    uint32_t payload_length = (packet[4] << 24) | (packet[5] << 16) | (packet[6] << 8) | packet[7];
    
    printf("DOIP Discovery: Payload type=0x%04X, length=%lu\r\n", payload_type, payload_length);
    
    // Check if this is a vehicle identification response
    if (payload_type == 0x0004) {
        if (data_len < 8 + payload_length) {
            printf("DOIP Discovery: Incomplete packet: need %lu bytes, got %zu\r\n", 
                   8 + payload_length, data_len);
            return;
        }
        
        if (payload_length < 25) {
            printf("DOIP Discovery: Vehicle ID response too short: %lu bytes\r\n", payload_length);
            return;
        }
        
        if (discovered_ecus.count >= MAX_DISCOVERED_ECUS) {
            printf("DOIP Discovery: Maximum ECU limit reached\r\n");
            return;
        }
        
        drv_doip_vehicle_info_t vehicle;
        memset(&vehicle, 0, sizeof(vehicle));
        
        // Parse VIN (17 bytes) - ensure it's printable
        memcpy(vehicle.vin, &packet[8], 17);
        vehicle.vin[17] = '\0';
        
        // Clean up non-printable characters in VIN
        for (int i = 0; i < 17; i++) {
            if (vehicle.vin[i] < 32 || vehicle.vin[i] > 126) {
                vehicle.vin[i] = '?'; // Replace unprintable chars
            }
        }
        
        // Parse Logical Address (2 bytes at offset 8+17=25)
        vehicle.logical_address = (packet[8 + 17] << 8) | packet[8 + 18];
        
        // Parse Entity ID (6 bytes at offset 8+19=27)
        if (payload_length >= 25) {
            memcpy(vehicle.entity_id, &packet[8 + 19], 6);
        }
        
        // Get real source IP address from driver (this is the ECU's actual IP!)
        // IMPORTANT: Get IP immediately while it's still the current packet source
        vehicle.ip_address = hw_doip_get_last_source_ip(&doip_0);
        vehicle.tcp_port = 13400;
        
        // DEBUG: Show driver IP that will be stored
        uint32_t driver_ip = hw_doip_get_last_source_ip(&doip_0);
        printf("DOIP Discovery: Using driver IP=%lu.%lu.%lu.%lu\r\n",
               driver_ip & 0xFF, (driver_ip >> 8) & 0xFF, (driver_ip >> 16) & 0xFF, (driver_ip >> 24) & 0xFF);
        
        printf("DOIP Discovery: Parsed - VIN='%s', LA=0x%04X, IP=%lu.%lu.%lu.%lu\r\n", 
               vehicle.vin, vehicle.logical_address,
               vehicle.ip_address & 0xFF, 
               (vehicle.ip_address >> 8) & 0xFF,
               (vehicle.ip_address >> 16) & 0xFF, 
               (vehicle.ip_address >> 24) & 0xFF);
        
        // Check for duplicates
        bool already_found = false;
        for (uint8_t i = 0; i < discovered_ecus.count; i++) {
            if (discovered_ecus.vehicles[i].logical_address == vehicle.logical_address) {
                already_found = true;
                printf("DOIP Discovery: Duplicate ECU 0x%04X ignored\r\n", vehicle.logical_address);
                break;
            }
        }
        
        if (!already_found) {
            // Add new ECU to discovery list
            memcpy(&discovered_ecus.vehicles[discovered_ecus.count], &vehicle, sizeof(drv_doip_vehicle_info_t));
            discovered_ecus.count++;
            
            ecu_type_t ecu_type = get_ecu_type_from_address(vehicle.logical_address);
            printf("DOIP Client: Discovered ECU %d - %s (0x%04X): VIN=%s, IP=%lu.%lu.%lu.%lu:%d\r\n", 
                   discovered_ecus.count, get_ecu_type_name(ecu_type), 
                   vehicle.logical_address, vehicle.vin,
                   vehicle.ip_address & 0xFF, 
                   (vehicle.ip_address >> 8) & 0xFF,
                   (vehicle.ip_address >> 16) & 0xFF, 
                   (vehicle.ip_address >> 24) & 0xFF,
                   vehicle.tcp_port);
        }
    } else {
        printf("DOIP Discovery: Ignoring non-vehicle-ID packet type 0x%04X\r\n", payload_type);
    }
}

/**
 * \brief Discover all available ECUs in the vehicle using callback-based approach
 * \param handle DOIP driver handle
 * \return Number of ECUs discovered
 */
static uint8_t doip_discover_all_ecus(drv_doip_t *handle)
{
    printf("DOIP Client: Starting callback-based ECU discovery...\r\n");
    
    // Reset discovery state
    discovered_ecus.count = 0;
    discovered_ecus.discovery_timestamp = xTaskGetTickCount();
    discovered_ecus.discovery_active = true;
    
    // Register discovery callback
    drv_doip_status_t status = hw_doip_register_callback(handle, DRV_DOIP_CB_RAW_PACKET_RECEIVED, discovery_callback);
    if (status != DRV_DOIP_STATUS_OK) {
        printf("DOIP Client: Failed to register discovery callback\r\n");
        discovered_ecus.discovery_active = false;
        return 0;
    }
    
    // Send single discovery broadcast
    drv_doip_vehicle_info_t dummy_vehicle; // Not used by new driver
    status = hw_doip_discover_vehicles(handle, &dummy_vehicle);
    if (status != DRV_DOIP_STATUS_OK) {
        printf("DOIP Client: Failed to send discovery broadcast\r\n");
        discovered_ecus.discovery_active = false;
        return 0;
    }
    
    printf("DOIP Client: Discovery broadcast sent, waiting for responses...\r\n");
    
    // Wait for responses via callback
    TickType_t discovery_start = xTaskGetTickCount();
    uint8_t last_count = 0;
    
    while ((xTaskGetTickCount() - discovery_start) < pdMS_TO_TICKS(ECU_DISCOVERY_TIMEOUT_MS)) {
        if (discovered_ecus.count > last_count) {
            last_count = discovered_ecus.count;
            printf("DOIP Client: Found %d ECUs so far...\r\n", discovered_ecus.count);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
        
        // Stop early if we found maximum ECUs
        if (discovered_ecus.count >= MAX_DISCOVERED_ECUS) {
            printf("DOIP Client: Maximum ECU limit reached\r\n");
            break;
        }
    }
    
    // Stop discovery
    discovered_ecus.discovery_active = false;
    
    printf("DOIP Client: Discovery completed - found %d ECUs in %lu ms\r\n", 
           discovered_ecus.count, 
           (xTaskGetTickCount() - discovery_start) * portTICK_PERIOD_MS);
    
    
    // Display summary of discovered ECUs
    if (discovered_ecus.count > 0) {
        printf("\r\n=== Discovered Vehicle ECUs ===\r\n");
        for (uint8_t i = 0; i < discovered_ecus.count; i++) {
            ecu_type_t ecu_type = get_ecu_type_from_address(discovered_ecus.vehicles[i].logical_address);
            printf("  ECU %d: %s (0x%04X) - VIN: %s - IP: %lu.%lu.%lu.%lu\r\n", 
                   i + 1, get_ecu_type_name(ecu_type), 
                   discovered_ecus.vehicles[i].logical_address, 
                   discovered_ecus.vehicles[i].vin,
                   discovered_ecus.vehicles[i].ip_address & 0xFF,
                   (discovered_ecus.vehicles[i].ip_address >> 8) & 0xFF,
                   (discovered_ecus.vehicles[i].ip_address >> 16) & 0xFF,
                   (discovered_ecus.vehicles[i].ip_address >> 24) & 0xFF);
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

// Forward declarations
static void generate_test_pattern_with_seed(uint8_t *buffer, size_t size, size_t seed);

// Test configuration structure for easy customization
typedef struct {
    bool enable_extended_testing;    // Full test suite for all ECUs
    bool enable_early_termination;   // Stop ECU testing after failures
    bool enable_adaptive_timing;     // Adjust delays based on results
    uint16_t max_failures_per_ecu;   // Max failures before skipping ECU
    uint32_t base_delay_ms;          // Base delay between tests
    uint32_t failure_delay_ms;       // Extended delay after failures
    bool verbose_logging;            // Detailed per-test logging
    uint32_t fragment_timeout_ms;    // Per-fragment timeout (same as 8KB tests)
    uint32_t overall_timeout_ms;     // Overall sustained transfer timeout
} large_message_test_config_t;

// Default test configuration
static const large_message_test_config_t default_test_config = {
    .enable_extended_testing = false,   // Use tiered approach by default
    .enable_early_termination = true,   // Stop after failures
    .enable_adaptive_timing = true,     // Smart delays
    .max_failures_per_ecu = 3,          // 3 strikes rule
    .base_delay_ms = 200,               // Fast base timing
    .failure_delay_ms = 1000,           // Recovery time after failures
    .verbose_logging = false,           // Summary mode by default
    .fragment_timeout_ms = 10000,       // 10s per fragment (same as 8KB tests)
    .overall_timeout_ms = 60000         // 60s total (reasonable for 256KB)
};

/**
 * \brief Generate test pattern data for large message testing
 * \param buffer Pointer to buffer to fill with test pattern
 * \param size Size of buffer in bytes
 */
static void generate_test_pattern(uint8_t *buffer, size_t size)
{
    generate_test_pattern_with_seed(buffer, size, 0);
}

/**
 * \brief Generate test pattern data with seed for unique fragments
 * \param buffer Pointer to buffer to fill with test pattern
 * \param size Size of buffer in bytes
 * \param seed Seed value for unique pattern generation
 */
static void generate_test_pattern_with_seed(uint8_t *buffer, size_t size, size_t seed)
{
    if (buffer == NULL || size == 0) {
        return;
    }
    
    // Create a 256-byte repeating pattern (0x00 to 0xFF) with seed offset
    const size_t pattern_size = 256;
    
    // Fill buffer with repeating pattern, offset by seed
    for (size_t i = 0; i < size; i++) {
        buffer[i] = (uint8_t)((i + seed) % pattern_size);
    }
    
    // Add unique signature: fragment number (seed) and size
    if (size >= 8) {
        // Last 8 bytes: seed (4 bytes) + size (4 bytes) 
        uint32_t seed_signature = (uint32_t)seed;
        uint32_t size_signature = (uint32_t)size;
        
        buffer[size - 8] = (uint8_t)((seed_signature >> 24) & 0xFF);
        buffer[size - 7] = (uint8_t)((seed_signature >> 16) & 0xFF);
        buffer[size - 6] = (uint8_t)((seed_signature >> 8) & 0xFF);
        buffer[size - 5] = (uint8_t)(seed_signature & 0xFF);
        
        buffer[size - 4] = (uint8_t)((size_signature >> 24) & 0xFF);
        buffer[size - 3] = (uint8_t)((size_signature >> 16) & 0xFF);
        buffer[size - 2] = (uint8_t)((size_signature >> 8) & 0xFF);
        buffer[size - 1] = (uint8_t)(size_signature & 0xFF);
    } else if (size >= 4) {
        // Fallback for smaller buffers: just use size
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
        // RED FLAG ERROR ALERT
        printf("\r\n");
        printf("🚩🚩🚩 RED FLAG ERROR DETECTED 🚩🚩🚩\r\n");
        printf("======================================\r\n");
        printf("❌ DIAGNOSTIC READ FAILURE ❌\r\n");
        printf("ECU: %s\r\n", ecu_name);
        printf("DID: 0x%04X (%s)\r\n", did, name);
        printf("Status Code: %d\r\n", status);
        printf("======================================\r\n");
        printf("\r\n");
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
    
    // Print comprehensive metrics report after large message testing
    printf("=== DoIP Performance Metrics After Large Message Testing ===\r\n");
    hw_doip_print_metrics(&doip_0);
    
    printf("Waiting for ECU emulator to stabilize after large message tests...\r\n");
    
    // Give ECU emulator extended time to recover from intensive large message testing
    // This prevents connection failures due to resource exhaustion
    vTaskDelay(pdMS_TO_TICKS(1000)); // 10 second recovery period for heavily stressed emulator
    
    printf("Extended emulator stabilization complete, proceeding with ECU parameter reading...\r\n");
    
    // Test emulator health with a simple connection attempt before proceeding
    printf("Testing emulator connectivity...\r\n");
    drv_doip_status_t health_check = hw_doip_connect_to_vehicle(handle, &discovered_ecus.vehicles[0]);
    if (health_check == DRV_DOIP_STATUS_OK) {
        printf("✅ Emulator connectivity test passed\r\n");
        hw_doip_disconnect(handle);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Small delay after health check
    } else {
        printf("⚠️  Emulator connectivity test failed - proceeding with retries\r\n");
    }
    
    // Test reading same DID from multiple ECUs to show differentiation
    uint16_t test_dids[] = {DID_VIN, DID_VEHICLE_SPEED_INFORMATION, DID_BATTERY_VOLTAGE_INFORMATION};
    const char* test_names[] = {"VIN", "Vehicle Speed", "Battery Voltage"};
    const char* test_formats[] = {"string", "uint16_kmh", "uint16_mv"};
    
    for (int did_idx = 0; did_idx < 3; did_idx++) {
        printf("\r\n--- Reading %s from all ECUs ---\r\n", test_names[did_idx]);
        
        for (uint8_t ecu_idx = 0; ecu_idx < discovered_ecus.count; ecu_idx++) {
            // Connect to specific ECU with retry logic for emulator stability
            drv_doip_status_t connect_status = DRV_DOIP_STATUS_ERROR;
            ecu_type_t ecu_type = get_ecu_type_from_address(discovered_ecus.vehicles[ecu_idx].logical_address);
            const char *ecu_name = get_ecu_type_name(ecu_type);
            
            for (int retry = 0; retry < 3; retry++) {
                connect_status = hw_doip_connect_to_vehicle(handle, &discovered_ecus.vehicles[ecu_idx]);
                if (connect_status == DRV_DOIP_STATUS_OK) {
                    break; // Connection successful
                }
                
                // Connection failed, wait before retry
                if (retry < 2) { // Don't wait after last attempt
                    printf("  [%s] Connection failed, retrying in 2 seconds... (attempt %d/3)\r\n", 
                           ecu_name, retry + 1);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            }
            
            if (connect_status == DRV_DOIP_STATUS_OK) {
                // Read DID from this specific ECU
                doip_read_did_from_ecu_and_display(handle, &discovered_ecus.vehicles[ecu_idx], 
                                                   test_dids[did_idx], test_names[did_idx], test_formats[did_idx]);
                
                // Disconnect from this ECU
                hw_doip_disconnect(handle);
                
                // Increased delay between ECU connections to allow proper cleanup
                vTaskDelay(pdMS_TO_TICKS(1000)); // Longer delay for emulator stability
            } else {
                printf("  [%s] Connection failed after 3 attempts - ECU may be unavailable\r\n", ecu_name);
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
    printf("  Connecting to ECU %s at %lu.%lu.%lu.%lu:%d...\r\n", ecu_name,
           ecu_info->ip_address & 0xFF,
           (ecu_info->ip_address >> 8) & 0xFF,
           (ecu_info->ip_address >> 16) & 0xFF,
           (ecu_info->ip_address >> 24) & 0xFF,
           ecu_info->tcp_port);
    drv_doip_status_t connect_status = hw_doip_connect_to_vehicle(handle, ecu_info);
    if (connect_status != DRV_DOIP_STATUS_OK) {
        // RED FLAG ERROR ALERT
        printf("\r\n");
        printf("  🚩🚩🚩 RED FLAG ERROR DETECTED 🚩🚩🚩\r\n");
        printf("  ======================================\r\n");
        printf("  ❌ DOIP CONNECTION FAILURE ❌\r\n");
        printf("  ECU: %s\r\n", ecu_name);
        printf("  Status Code: %d\r\n", connect_status);
        printf("  ======================================\r\n");
        if (connect_status == DRV_DOIP_STATUS_TIMEOUT) {
            printf("  INFO: This is expected if no DOIP server is running\r\n");
        }
        printf("\r\n");
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
    
    // Mark static buffer as in use
    large_message_buffer_in_use = true;
    
    // Fill buffer with test pattern
    large_message_test_buffer[0] = UDS_LARGE_MESSAGE_TEST;
    generate_test_pattern(&large_message_test_buffer[1], test_size);
    
    // Send large message test via raw message API
    printf("  Sending %zu byte message (static buffer)...\r\n", buffer_size);
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
        // RED FLAG ERROR ALERT
        printf("\r\n");
        printf("  🚩🚩🚩 RED FLAG ERROR DETECTED 🚩🚩🚩\r\n");
        printf("  ======================================\r\n");
        printf("  ❌ LARGE MESSAGE SEND FAILURE ❌\r\n");
        printf("  ECU: %s\r\n", ecu_name);
        printf("  Status Code: %d\r\n", status);
        printf("  Message Size: %zu bytes\r\n", buffer_size);
        printf("  ======================================\r\n");
        printf("\r\n");
        // Disconnect before returning error
        printf("  Disconnecting from ECU %s after error...\r\n", ecu_name);
        hw_doip_disconnect(handle);
        
        // Extended recovery delay after errors to ensure TCP cleanup
        printf("  Extended error recovery delay...\r\n");
        vTaskDelay(pdMS_TO_TICKS(1500)); // 1.5 second recovery after errors
        
        return TEST_RESULT_FAILED;
    }
    
    measure_transfer_performance("Send", buffer_size, send_start, send_end);
    
    // Enhanced performance logging for very large messages
    if (buffer_size >= 65536) { // 64KB+
        uint32_t elapsed_ms = (send_end - send_start) * portTICK_PERIOD_MS;
        if (elapsed_ms > 0) {
            float mb_per_sec = ((float)buffer_size / (1024.0f * 1024.0f)) / ((float)elapsed_ms / 1000.0f);
            uint32_t estimated_chunks = (buffer_size + 1199) / 1200; // DOIP_SAFE_CHUNK_SIZE = 1200
            printf("  📊 Performance: %.2f MB/s, ~%lu chunks (1200 bytes each)\r\n", 
                   mb_per_sec, estimated_chunks);
        }
    }
    
    printf("  ✅ Large message test with ECU %s: SUCCESS\r\n", ecu_name);
    printf("  Successfully sent %zu byte large message via unified DoIP API\r\n", buffer_size);
    
    // Disconnect from ECU after successful test
    printf("  Disconnecting from ECU %s...\r\n", ecu_name);
    drv_doip_status_t disconnect_status = hw_doip_disconnect(handle);
    if (disconnect_status != DRV_DOIP_STATUS_OK) {
        printf("  WARNING: Failed to disconnect from ECU %s: status %d\r\n", ecu_name, disconnect_status);
    }
    
    // Enhanced recovery delay for TCP buffer cleanup - especially after large messages
    printf("  Waiting for TCP buffer cleanup...\r\n");
    if (buffer_size >= 8192) {
        // Longer delay after large messages to ensure TCP buffers are fully drained
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second recovery delay for large messages
        printf("  Extended recovery completed for large message\r\n");
    } else {
        vTaskDelay(pdMS_TO_TICKS(500)); // 500ms recovery delay for smaller messages
    }
    
    return TEST_RESULT_PASSED;
}

/**
 * \brief Test sustained large message transfers (multiple 8KB messages)
 * \param handle DOIP driver handle
 * \param ecu_info Target ECU information  
 * \param total_size Target total size to transfer (will be rounded to 8KB multiples)
 * \param fast_mode Enable fast mode (minimal delays, reduced logging)
 * \param config Test configuration (NULL for default timeouts)
 * \return test_result_t indicating test result
 */
static test_result_t doip_test_sustained_large_messages(drv_doip_t *handle, 
                                                       const drv_doip_vehicle_info_t *ecu_info,
                                                       size_t total_size,
                                                       bool fast_mode,
                                                       const large_message_test_config_t *config)
{
    if (handle == NULL || ecu_info == NULL || total_size == 0) {
        return TEST_RESULT_FAILED;
    }
    
    if (large_message_buffer_in_use) {
        printf("  SKIP: Large message buffer is in use by another test\r\n");
        return TEST_RESULT_SKIPPED;
    }
    
    ecu_type_t ecu_type = get_ecu_type_from_address(ecu_info->logical_address);
    const char *ecu_name = get_ecu_type_name(ecu_type);
    
    // Calculate number of 8KB fragments needed
    const size_t fragment_size = LARGE_MESSAGE_TEST_BUFFER_SIZE - 1; // Leave 1 byte for service ID
    const size_t num_fragments = (total_size + fragment_size - 1) / fragment_size; // Round up
    const size_t actual_total_size = num_fragments * fragment_size;
    
    printf("  Testing sustained transfer: %zu bytes (%zu x %zu-byte fragments) with ECU %s\r\n", 
           actual_total_size, num_fragments, fragment_size, ecu_name);
    
    // Mark buffer as in use
    large_message_buffer_in_use = true;
    
    TickType_t overall_start = xTaskGetTickCount();
    size_t total_bytes_sent = 0;
    uint32_t successful_fragments = 0;
    
    // Use provided config or default timeouts (same as 8KB message tests)
    const large_message_test_config_t *test_config = config ? config : &default_test_config;
    const TickType_t fragment_timeout_ticks = pdMS_TO_TICKS(test_config->fragment_timeout_ms);
    const TickType_t overall_timeout_ticks = pdMS_TO_TICKS(test_config->overall_timeout_ms);
    
    printf("  Timeout protection: %lu seconds per fragment, %lu seconds overall\r\n",
           test_config->fragment_timeout_ms / 1000, test_config->overall_timeout_ms / 1000);
    
    // Pre-fill buffer with base pattern to optimize performance
    large_message_test_buffer[0] = UDS_LARGE_MESSAGE_TEST;
    generate_test_pattern_with_seed(&large_message_test_buffer[1], fragment_size, 0);
    
    for (size_t fragment = 0; fragment < num_fragments; fragment++) {
        // Overall timeout check (same as 8KB message test approach)
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - overall_start) > overall_timeout_ticks) {
            printf("    ⏰ TIMEOUT: Sustained transfer exceeded %lu seconds limit\r\n", 
                   test_config->overall_timeout_ms / 1000);
            printf("    Completed %u/%zu fragments before timeout\r\n", successful_fragments, num_fragments);
            break;
        }
        
        // Update fragment-specific signature only (last 8 bytes)
        if (fragment_size >= 8) {
            uint32_t fragment_signature = (uint32_t)fragment;
            size_t sig_offset = 1 + fragment_size - 8; // After service ID, at end of data
            large_message_test_buffer[sig_offset] = (uint8_t)((fragment_signature >> 24) & 0xFF);
            large_message_test_buffer[sig_offset + 1] = (uint8_t)((fragment_signature >> 16) & 0xFF);
            large_message_test_buffer[sig_offset + 2] = (uint8_t)((fragment_signature >> 8) & 0xFF);
            large_message_test_buffer[sig_offset + 3] = (uint8_t)(fragment_signature & 0xFF);
        }
        
        size_t buffer_size = fragment_size + 1; // Service ID + data
        
        // Conditional logging based on fast_mode
        if (!fast_mode || (fragment % 10 == 0) || (fragment == num_fragments - 1)) {
            printf("    Fragment %zu/%zu: Sending %zu bytes...\r\n", 
                   fragment + 1, num_fragments, buffer_size);
        }
        
        TickType_t fragment_start = xTaskGetTickCount();
        
        drv_doip_status_t status = hw_doip_send_raw_message(
            handle, 
            DOIP_DIAGNOSTIC_MESSAGE,
            large_message_test_buffer, 
            buffer_size,
            true  // use_static_buffer = true
        );
        
        TickType_t fragment_end = xTaskGetTickCount();
        uint32_t fragment_ms = (fragment_end - fragment_start) * portTICK_PERIOD_MS;
        
        // Per-fragment timeout check (same as individual 8KB message tests)
        if ((fragment_end - fragment_start) > fragment_timeout_ticks) {
            printf("    ⏰ TIMEOUT: Fragment %zu exceeded %lu seconds limit (%lums actual)\r\n", 
                   fragment + 1, test_config->fragment_timeout_ms / 1000, fragment_ms);
            break;
        }
        
        if (status == DRV_DOIP_STATUS_OK) {
            total_bytes_sent += buffer_size;
            successful_fragments++;
            
            float fragment_kbps = 0.0f;
            if (fragment_ms > 0) {
                fragment_kbps = ((float)buffer_size * 8.0f * 1000.0f) / ((float)fragment_ms * 1024.0f);
            }
            
            // Reduced logging in fast mode
            if (!fast_mode || (fragment % 10 == 0) || (fragment == num_fragments - 1)) {
                printf("    Fragment %zu: ✅ SUCCESS (%zu bytes in %lums, %.1f Kbps)\r\n", 
                       fragment + 1, buffer_size, fragment_ms, fragment_kbps);
            }
        } else {
            printf("    Fragment %zu: ❌ FAILED (status=%d)\r\n", fragment + 1, status);
            
            // Same error handling as 8KB message tests
            if (status == DRV_DOIP_STATUS_TIMEOUT) {
                printf("    ⏰ Driver reported timeout for fragment %zu\r\n", fragment + 1);
            } else if (status == DRV_DOIP_STATUS_ERROR) {
                printf("    💥 Driver reported error for fragment %zu\r\n", fragment + 1);
            } else if (status == DRV_DOIP_STATUS_NO_VEHICLE) {
                printf("    🚗 No vehicle connection for fragment %zu\r\n", fragment + 1);
            }
            
            break;
        }
        
        // Adaptive inter-fragment delay based on performance and mode
        uint32_t fragment_delay_ms;
        
        if (fast_mode) {
            // Fast mode: minimal delays for maximum throughput
            fragment_delay_ms = 1; // 1ms minimal delay
            if (fragment_ms > 100) { // Only add delay if ECU is very slow
                fragment_delay_ms = 10;
            }
        } else {
            // Normal mode: conservative delays for stability
            fragment_delay_ms = 10; // Reduced from 100ms to 10ms (90% faster)
            
            // Dynamic delay adjustment based on transmission speed
            if (fragment_ms > 50) { // If fragment took >50ms, ECU might be slow
                fragment_delay_ms = 50; // Give it more recovery time
            } else if (fragment_ms < 20) { // Fast transmission
                fragment_delay_ms = 5;  // Minimal delay for fast ECUs
            }
        }
        
        // Skip delay for last fragment
        if (fragment < num_fragments - 1) {
            vTaskDelay(pdMS_TO_TICKS(fragment_delay_ms));
        }
    }
    
    TickType_t overall_end = xTaskGetTickCount();
    large_message_buffer_in_use = false;
    
    // Calculate overall performance
    uint32_t overall_ms = (overall_end - overall_start) * portTICK_PERIOD_MS;
    float overall_mbps = 0.0f;
    if (overall_ms > 0) {
        overall_mbps = ((float)total_bytes_sent / (1024.0f * 1024.0f)) / ((float)overall_ms / 1000.0f);
    }
    
    printf("  📊 Sustained Transfer Results:\r\n");
    printf("      Total sent: %zu bytes (%zu KB)\r\n", total_bytes_sent, total_bytes_sent / 1024);
    printf("      Successful fragments: %u/%zu\r\n", successful_fragments, num_fragments);
    printf("      Total time: %lums\r\n", overall_ms);
    printf("      Sustained throughput: %.2f MB/s\r\n", overall_mbps);
    
    // Timeout-aware result evaluation (same logic as 8KB message tests)
    bool timed_out = (overall_ms >= test_config->overall_timeout_ms) || (successful_fragments < num_fragments);
    
    if (successful_fragments == num_fragments) {
        printf("  ✅ Sustained large message test: SUCCESS\r\n");
        printf("      All fragments completed within timeout limits\r\n");
        return TEST_RESULT_PASSED;
    } else if (timed_out) {
        printf("  ⏰ Sustained large message test: TIMEOUT\r\n");
        printf("      %u/%zu fragments completed before timeout\r\n", successful_fragments, num_fragments);
        if (successful_fragments > (num_fragments * 75 / 100)) { // >75% success
            printf("      Partial success - %u%% completion rate\r\n", 
                   (successful_fragments * 100) / (uint32_t)num_fragments);
            return TEST_RESULT_SKIPPED; // Treat as partial success
        } else {
            return TEST_RESULT_FAILED;
        }
    } else {
        printf("  ❌ Sustained large message test: FAILED\r\n");
        printf("      %u/%zu fragments failed due to errors\r\n", 
               (uint32_t)num_fragments - successful_fragments, num_fragments);
        return TEST_RESULT_FAILED;
    }
}

/**
 * \brief Test large messages with all discovered ECUs (optimized version)
 * \param handle DOIP driver handle
 * \param config Test configuration (NULL for default)
 */
static void doip_test_large_messages_all_ecus_optimized(drv_doip_t *handle, 
                                                      const large_message_test_config_t *config)
{
    if (handle == NULL || discovered_ecus.count == 0) {
        printf("DOIP Large Test: No ECUs available for testing\r\n");
        return;
    }
    
    // Use default config if none provided
    const large_message_test_config_t *test_config = config ? config : &default_test_config;
    
    printf("\r\n=== DOIP Large Message Test Suite (Optimized) ===\r\n");
    printf("Testing large message capabilities with %d ECUs\r\n", discovered_ecus.count);
    printf("Using 8KB static buffer (no heap allocation)\r\n");
    printf("Available heap: %zu bytes\r\n", xPortGetFreeHeapSize());
    printf("Configuration: %s testing, %s timing, %s termination\r\n",
           test_config->enable_extended_testing ? "Extended" : "Tiered",
           test_config->enable_adaptive_timing ? "Adaptive" : "Fixed",
           test_config->enable_early_termination ? "Early" : "Complete");
    
    // Strategic test sizes for optimal coverage with minimal redundancy
    const size_t basic_test_sizes[] = {
        256,       // Small message - basic connectivity
        1400,      // MTU boundary test (1460 - 60 TCP/IP headers)
        1600,      // Above MTU - fragmentation test  
        4096,      // 4KB - chunking boundary test
        8191,      // Maximum static buffer size
    };
    
    const size_t extended_test_sizes[] = {
        256, 512, 1024, 1400, 1600, 2048, 4096, 6144, 8191  // Full coverage for primary ECU
    };
    
    const size_t num_basic_sizes = sizeof(basic_test_sizes) / sizeof(basic_test_sizes[0]);
    const size_t num_extended_sizes = sizeof(extended_test_sizes) / sizeof(extended_test_sizes[0]);
    
    uint16_t total_tests = 0;
    uint16_t passed_tests = 0;
    uint16_t skipped_tests = 0;
    
    printf("=== Tiered Testing Strategy ===\r\n");
    printf("Primary ECU: Full test suite (%zu sizes)\r\n", num_extended_sizes);
    printf("Other ECUs: Core test suite (%zu sizes)\r\n", num_basic_sizes);
    printf("====================================\r\n\r\n");
    
    // Iterate through ECUs with different test strategies
    for (uint8_t ecu_idx = 0; ecu_idx < discovered_ecus.count; ecu_idx++) {
        ecu_type_t ecu_type = get_ecu_type_from_address(discovered_ecus.vehicles[ecu_idx].logical_address);
        const char *ecu_name = get_ecu_type_name(ecu_type);
        bool is_primary = (ecu_idx == 0);
        
        // Choose test suite based on configuration and ECU priority
        const size_t *test_sizes;
        size_t num_test_sizes;
        const char *suite_name;
        
        if (test_config->enable_extended_testing || is_primary) {
            test_sizes = extended_test_sizes;
            num_test_sizes = num_extended_sizes;
            suite_name = "Extended";
        } else {
            test_sizes = basic_test_sizes;
            num_test_sizes = num_basic_sizes;
            suite_name = "Core";
        }
        
        printf("\r\n--- Testing ECU %d: %s (%s test suite) ---\r\n", 
               ecu_idx + 1, ecu_name, suite_name);
        
        uint16_t ecu_failures = 0;
        const uint16_t max_ecu_failures = test_config->max_failures_per_ecu;
        size_t size_idx = 0;
        
        for (size_idx = 0; size_idx < num_test_sizes; size_idx++) {
            size_t test_size = test_sizes[size_idx];
            
            // Skip remaining tests for this ECU if too many failures
            if (ecu_failures >= max_ecu_failures && test_config->enable_early_termination) {
                printf("  Skipping remaining tests for %s (too many failures: %d)\r\n", 
                       ecu_name, ecu_failures);
                skipped_tests += (num_test_sizes - size_idx);
                break;
            }
            
            total_tests++;
            
            printf("    Test %zu/%zu: %zu bytes (%s)...\r\n", 
                   size_idx + 1, num_test_sizes, test_size, 
                   test_size >= 1024 ? (test_size >= 4096 ? "large" : "medium") : "small");
            
            test_result_t result = doip_test_large_message_single_ecu(handle, 
                                                                    &discovered_ecus.vehicles[ecu_idx], 
                                                                    test_size);
            
            if (result == TEST_RESULT_PASSED) {
                passed_tests++;
            } else if (result == TEST_RESULT_SKIPPED) {
                skipped_tests++;
            } else {
                ecu_failures++;
                printf("    ❌ Failure %d/%d for ECU %s\r\n", ecu_failures, max_ecu_failures, ecu_name);
            }
            
            // Adaptive delay based on configuration, test result and size
            uint32_t delay_ms = test_config->base_delay_ms;
            
            if (test_config->enable_adaptive_timing) {
                if (result != TEST_RESULT_PASSED) {
                    delay_ms = test_config->failure_delay_ms; // Longer delay after failures
                } else if (test_size >= 4096) {
                    delay_ms = test_config->base_delay_ms * 2; // Medium delay for large tests
                }
            }
            
            if (delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            }
        }
        
        // ECU summary
        uint16_t ecu_tests = num_test_sizes;
        if (ecu_failures >= max_ecu_failures && test_config->enable_early_termination) {
            ecu_tests = size_idx; // Only count tests that were actually attempted
        }
        uint16_t ecu_passed = ecu_tests - ecu_failures;
        
        printf("  ECU %s Results: %d/%d passed", ecu_name, ecu_passed, ecu_tests);
        if (ecu_failures == 0) {
            printf(" ✅ PERFECT\r\n");
        } else if (ecu_failures <= 2) {
            printf(" ⚠️  GOOD\r\n");
        } else {
            printf(" ❌ POOR\r\n");
        }
    }
    
    // Display summary
    uint16_t failed_tests = total_tests - passed_tests - skipped_tests;
    
    printf("\r\n=== Large Message Test Results ===\r\n");
    printf("Total Tests: %d\r\n", total_tests);
    printf("Passed: %d\r\n", passed_tests);
    
    // RED FLAG ALERT FOR FAILED TESTS
    if (failed_tests > 0) {
        printf("\r\n");
        printf("🚩🚩🚩 RED FLAG: %d TESTS FAILED 🚩🚩🚩\r\n", failed_tests);
        printf("============================================\r\n");
        printf("❌ FAILED TESTS: %d\r\n", failed_tests);
        if (failed_tests > 10) {
            printf("⚠️  WARNING: HIGH FAILURE RATE DETECTED\r\n");
        } else if (failed_tests > 5) {
            printf("⚠️  WARNING: MODERATE FAILURE RATE\r\n");
        }
        printf("============================================\r\n");
        printf("\r\n");
    } else {
        printf("✅ Failed: %d (Perfect!)\r\n", failed_tests);
    }
    
    printf("Skipped: %d (memory constraints)\r\n", skipped_tests);
    if (total_tests > 0) {
        float success_rate = (float)passed_tests * 100.0f / (total_tests - skipped_tests);
        printf("Success Rate: %.1f%% (%d/%d executed tests)", 
               success_rate, passed_tests, total_tests - skipped_tests);
        
        // Success rate color coding
        if (success_rate >= 95.0f) {
            printf(" ✅ EXCELLENT\r\n");
        } else if (success_rate >= 85.0f) {
            printf(" ⚠️  GOOD\r\n");
        } else if (success_rate >= 70.0f) {
            printf(" ⚠️  NEEDS IMPROVEMENT\r\n");
        } else {
            printf(" 🚩 POOR PERFORMANCE\r\n");
        }
    }
    printf("Final heap: %zu bytes\r\n", xPortGetFreeHeapSize());
    printf("======================================\r\n\r\n");
    
    // Add delay before continuing with other tests
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/**
 * \brief Create test configuration for different scenarios
 * \param scenario Configuration scenario (0=default, 1=fast, 2=thorough, 3=debug)
 * \return Test configuration structure
 */
__attribute__((unused))
static large_message_test_config_t create_test_config(int scenario)
{
    large_message_test_config_t config = default_test_config;
    
    switch (scenario) {
        case 1: // Fast testing - minimal delays, early termination
            config.enable_extended_testing = false;
            config.enable_early_termination = true;
            config.max_failures_per_ecu = 2;
            config.base_delay_ms = 100;
            config.failure_delay_ms = 500;
            config.verbose_logging = false;
            break;
            
        case 2: // Thorough testing - all ECUs get full test suite
            config.enable_extended_testing = true;
            config.enable_early_termination = false;
            config.max_failures_per_ecu = 10;
            config.base_delay_ms = 300;
            config.failure_delay_ms = 1500;
            config.verbose_logging = true;
            break;
            
        case 3: // Debug mode - extensive logging and recovery
            config.enable_extended_testing = false;
            config.enable_early_termination = false;
            config.max_failures_per_ecu = 5;
            config.base_delay_ms = 500;
            config.failure_delay_ms = 2000;
            config.verbose_logging = true;
            break;
            
        default: // scenario 0 or unknown - use defaults
            break;
    }
    
    return config;
}

/**
 * \brief Legacy wrapper for backward compatibility
 * \param handle DOIP driver handle
 */
static void doip_test_large_messages_all_ecus(drv_doip_t *handle)
{
    doip_test_large_messages_all_ecus_optimized(handle, NULL);
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
	
	printf("🔍 [DEBUG] DOIP Client Task started\r\n");
	printf("🔍 [DEBUG] Driver handle: %p\r\n", doip_handle);
	
	// Initialize DOIP driver
	printf("🔍 [DEBUG] Initializing DOIP driver...\r\n");
	if (hw_doip_init(doip_handle) != DRV_DOIP_STATUS_OK) {
		printf("❌ [DEBUG] Driver initialization failed\r\n");
		vTaskDelete(NULL);
		return;
	}
	
	printf("✅ [DEBUG] DOIP driver initialized successfully\r\n");
	
	// Reset metrics for clean session start
	hw_doip_reset_metrics(doip_handle);
	printf("✅ [DEBUG] Performance metrics reset for clean session\r\n");
	
	// Demonstrate configurable timeout functionality
	drv_doip_config_t current_config;
	hw_doip_get_config(doip_handle, &current_config);
	printf("🔍 [DEBUG] Current configuration:\r\n");
	printf("  Discovery timeout: %lu ms\r\n", current_config.discovery_timeout_ms);
	printf("  TCP connect timeout: %lu ms\r\n", current_config.tcp_connect_timeout_ms);
	printf("  Safe chunk size: %u bytes\r\n", current_config.safe_chunk_size);
	printf("  Buffer wait timeout: %lu ms\r\n", current_config.buffer_wait_timeout_ms);
	
	printf("✅ [DEBUG] Driver initialized successfully\r\n");
	
	// Small delay to ensure driver is ready
	vTaskDelay(pdMS_TO_TICKS(100));
	
	// Wait a bit for network to be fully ready
	printf("🔍 [DEBUG] Waiting for network to be ready...\r\n");
	vTaskDelay(pdMS_TO_TICKS(2000));
	
	while (1) {
		// Only proceed if we're in idle state (not connected)
		drv_doip_state_t current_state = hw_doip_get_status(doip_handle);
		printf("🔍 [DEBUG] Current DOIP state: %d\r\n", current_state);
		
		if (current_state == DRV_DOIP_STATE_IDLE) {
			printf("✅ [DEBUG] DOIP ready - now starting ECU discovery\r\n");
			
			// Perform DoIP vehicle discovery
			printf("=== DoIP Vehicle Discovery ===\r\n");
			printf("🔍 Broadcasting DoIP vehicle identification request...\r\n");
			
			uint8_t discovered_ecu_count = doip_discover_all_ecus(doip_handle);
			
			if (discovered_ecu_count > 0) {
				printf("✅ DoIP Discovery successful! Found %d ECUs\r\n", discovered_ecu_count);
				printf("🔍 Discovered ECUs:\r\n");
				
				// Print discovered ECU information
				for (int i = 0; i < discovered_ecu_count && i < MAX_DISCOVERED_ECUS; i++) {
					printf("  ECU %d: IP=%d.%d.%d.%d, TCP Port=%d, Logical Address=0x%04X\r\n",
					       i + 1,
					       (discovered_ecus.vehicles[i].ip_address >> 0) & 0xFF,
					       (discovered_ecus.vehicles[i].ip_address >> 8) & 0xFF,
					       (discovered_ecus.vehicles[i].ip_address >> 16) & 0xFF,
					       (discovered_ecus.vehicles[i].ip_address >> 24) & 0xFF,
					       discovered_ecus.vehicles[i].tcp_port,
					       discovered_ecus.vehicles[i].logical_address);
				}
			} else {
				printf("❌ DoIP Discovery failed or no ECUs found\r\n");
				printf("🔍 Check network connection and ensure ECUs are available\r\n");
			}
			
			printf("✅ DoIP Discovery cycle completed\r\n");
			
			// Wait before next discovery cycle
			printf("🔍 Waiting 10 seconds before next discovery...\r\n");
			vTaskDelay(pdMS_TO_TICKS(10000));
			
		} else {
			// Wait a bit and try again
			printf("🔍 [DEBUG] DOIP not ready (state: %d), waiting...\r\n", current_state);
			vTaskDelay(pdMS_TO_TICKS(1000));
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
		// CRITICAL RED FLAG ERROR
		printf("\r\n");
		printf("🚩🚩🚩 CRITICAL RED FLAG ERROR 🚩🚩🚩\r\n");
		printf("=========================================\r\n");
		printf("💥 FATAL: DOIP TASK CREATION FAILED 💥\r\n");
		printf("Task: DOIPClient\r\n");
		printf("Stack Size: %d bytes\r\n", DOIP_CLIENT_TASK_STACK_SIZE);
		printf("SYSTEM HALTED - RECOVERY REQUIRED\r\n");
		printf("=========================================\r\n");
		printf("\r\n");
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
            vTaskDelay(pdMS_TO_TICKS(1000)); // Report every 10 seconds
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
        // CRITICAL RED FLAG ERROR
        printf("\r\n");
        printf("🚩🚩🚩 CRITICAL RED FLAG ERROR 🚩🚩🚩\r\n");
        printf("=========================================\r\n");
        printf("💥 FATAL: DIAGNOSTIC TASK CREATION FAILED 💥\r\n");
        printf("Task: DiagProcessor\r\n");
        printf("Stack Size: %d bytes\r\n", DOIP_CLIENT_TASK_STACK_SIZE);
        printf("SYSTEM HALTED - RECOVERY REQUIRED\r\n");
        printf("=========================================\r\n");
        printf("\r\n");
        while (1) {
            ;
        }
    }
    
    printf("Diagnostic Processor: Task created successfully\r\n");
}

