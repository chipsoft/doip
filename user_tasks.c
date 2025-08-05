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

uint16_t led_blink_rate = BLINK_NORMAL;

static TaskHandle_t xLed_Task;
static TaskHandle_t xDoip_Client_Task;

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
					
					// For socket implementation, we wait a bit to listen for alive check responses
					if (hw_doip_get_status(doip_handle) == DRV_DOIP_STATE_ACTIVATED) {
						printf("\r\n--- Testing Alive Check ---\r\n");
						printf("DOIP Client: Connection established, listening for ECU messages...\r\n");
						
						// Listen period for incoming messages (alive checks, etc.)
						vTaskDelay(pdMS_TO_TICKS(3000)); // 3 second listening period
						
						printf("DOIP Client: Alive check testing completed\r\n");
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

