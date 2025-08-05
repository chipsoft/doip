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

uint16_t led_blink_rate = BLINK_NORMAL;

static TaskHandle_t xLed_Task;
static TaskHandle_t xDoip_Client_Task;

/**
 * \brief Read DID from DOIP server and display formatted result
 * \param handle DOIP driver handle
 * \param did Data Identifier to read
 * \param name Human-readable name for display
 * \param format Format string for data interpretation
 * \return Driver status
 */
static drv_doip_status_t doip_read_did_and_display(drv_doip_t *handle, uint16_t did, const char *name, const char *format)
{
    if (handle == NULL || name == NULL || format == NULL) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    uint8_t response[256];
    size_t actual_len = 0;
    
    drv_doip_status_t status = hw_doip_send_diagnostic_request(handle, UDS_READ_DATA_BY_IDENTIFIER, 
                                                              did, response, sizeof(response), &actual_len);
    
    if (status != DRV_DOIP_STATUS_OK) {
        printf("DOIP: Failed to read DID 0x%04X (%s): status %d\r\n", did, name, status);
        return status;
    }
    
    if (actual_len < 3) {
        printf("DOIP: Invalid response length for DID 0x%04X (%s): %zu bytes\r\n", did, name, actual_len);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Check positive response (service_id + 0x40)
    if (response[0] != (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK)) {
        printf("DOIP: Negative response for DID 0x%04X (%s): 0x%02X\r\n", did, name, response[0]);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Verify DID echo
    uint16_t response_did = (response[1] << 8) | response[2];
    if (response_did != did) {
        printf("DOIP: DID mismatch for %s: requested 0x%04X, got 0x%04X\r\n", name, did, response_did);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Display data based on format
    printf("  %-25s: ", name);
    
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
	drv_doip_vehicle_info_t vehicle_info;
	
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
			printf("DOIP Client: Starting vehicle discovery...\r\n");
			
			// Discover vehicles
			if (hw_doip_discover_vehicles(doip_handle, &vehicle_info) == DRV_DOIP_STATUS_OK) {
				printf("DOIP Client: Vehicle discovered, attempting connection...\r\n");
				
				// Connect to discovered vehicle
				if (hw_doip_connect_to_vehicle(doip_handle, &vehicle_info) == DRV_DOIP_STATUS_OK) {
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
					printf("DOIP Client: Connection failed\r\n");
					vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds before retry
				}
			} else {
				printf("DOIP Client: No vehicles discovered\r\n");
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

