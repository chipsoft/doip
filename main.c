/**
 * \file
 *
 * \brief LwIP socket api application implementation
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
// #include "lwip/tcpip.h"
#include "printf.h"
// #include "network_events.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_led.h"
#include "bsp_ethernet.h"
#include "eth_ipstack_main.h"
#include <hal_mac_async.h>
#include "app_libs/asf4/hri/hri_gmac_e54.h"
#include "driver_doip.h"
#include <string.h>
#include <peripheral_clk_config.h>
#include <utils.h>
#include <hal_init.h>
#include <hal_gpio.h>
#include "bsp_ksz8851snl.h"  // KSZ8851SNL driver with debug functions


static void test_ksz8851snl_task(void *pvParameters)
{
    (void)pvParameters;
    // Initialize KSZ8851SNL
    drv_ksz8851snl_config_t config = {
        .mac_addr = {0x00, 0x00, 0x00, 0x00, 0x20, 0x76},
        .auto_negotiation = true,
        .link_speed = 100,
        .full_duplex = true
    };
    drv_ksz8851snl_status_t init_result = hw_ksz8851snl_init(&ksz8851snl_0, &config);
    if (init_result != DRV_KSZ8851SNL_STATUS_OK) {
        printf("🔍 KSZ8851SNL initialization failed: %d\r\n", init_result);
        ASSERT(0);
    }
    // Enable KSZ8851SNL
    drv_ksz8851snl_status_t enable_result = hw_ksz8851snl_enable(&ksz8851snl_0);
    if (enable_result != DRV_KSZ8851SNL_STATUS_OK) {
        printf("🔍 KSZ8851SNL enable failed: %d\r\n", enable_result);
        ASSERT(0);
    }
    printf("🔍 KSZ8851SNL task started\r\n");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every 1 second
        printf("🔍 KSZ8851SNL task running\r\n");
    }
}

/* RTT printf integration */
extern void rtt_printf_init(void);

// Raw TCP test function using KSZ8851SNL hardware directly (bypasses lwIP)
static void raw_tcp_test_send_packets(void)
{
    printf("\r\n=== SWITCHING TO MINIMAL PACKET TEST ===\r\n");
    printf("Running simplified test to isolate packet transmission issues\r\n");
    
    // Wait a bit for KSZ8851SNL to be fully ready
    printf("🔍 Waiting for KSZ8851SNL to be ready...\r\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Minimal packet tests disabled - DoIP discovery handles network validation
    printf("📡 Minimal packet tests disabled - DoIP client will handle network testing\r\n");
    printf("🔍 Monitor DoIP client task for vehicle discovery broadcasts\r\n");
    
    // Just wait - let DoIP client task handle everything
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); // Check every 30 seconds
        printf("🔍 TCP test task idle - DoIP client handling all network operations\r\n");
    }
}

// Task wrapper for raw TCP test
static void raw_tcp_test_task_wrapper(void *pvParameters)
{
    (void)pvParameters; // Unused parameter
    
    printf("🔍 Raw TCP Test Task: Starting...\r\n");
    
    // Run the raw TCP test
    raw_tcp_test_send_packets();
    
    // This should never reach here, but just in case
    printf("🔍 Raw TCP Test Task: Unexpected exit\r\n");
    vTaskDelete(NULL);
}

/*
 * NOTE:
 * If compilation results in error: "redefinition of 'struct timeval'",
 * perform below step to make compilation successful.
 *
 * Navigate to sockets.h file in $PROJECT_LOCAION$\lwip\lwip-1.4.0\src\include\lwip &
 * change the MACRO LWIP_TIMEVAL_PRIVATE like below
 *
 * #define LWIP_TIMEVAL_PRIVATE		1
 *				to
 * #define LWIP_TIMEVAL_PRIVATE		0
 *
 */

int main(void)
{
	/* Initialize system and peripherals */
	init_mcu();
	// Initialize LED using universal driver
	hw_led_init(&led_yellow);
	/* Initialize SEGGER RTT for debug output */
	rtt_printf_init();
	/* Create application tasks */
	task_led_create();

    if (xTaskCreate(test_ksz8851snl_task,
        	                "KSZ_LINK",
        	                256,  // Stack size
        	                NULL,
        	                (tskIDLE_PRIORITY + 1),  // Low priority
        	                NULL)
        	    != pdPASS) {
        		printf("Failed to create KSZ8851SNL task\r\n");
        	} else {
        		printf("KSZ8851SNL task created\r\n");
        	}    
	

	/* Start FreeRTOS scheduler */
	vTaskStartScheduler();
	/* Should never reach here */
	return 0;
}

/**
 * FreeRTOS heap protection function - provides random canary value
 * Required when configENABLE_HEAP_PROTECTOR is enabled
 */
void vApplicationGetRandomHeapCanary(portPOINTER_SIZE_TYPE *pxHeapCanary)
{
	/* Simple pseudo-random canary based on system tick and memory addresses
	 * In production, use hardware RNG if available */
	static uint32_t seed = 0x55AA55AA;
	
	/* Linear congruential generator for simple randomness */
	seed = (seed * 1664525UL + 1013904223UL);
	
	/* Mix in system tick for additional entropy */
	seed ^= (uint32_t)xTaskGetTickCount();
	
	/* Mix in stack address for additional entropy */
	volatile uint32_t stack_var;
	seed ^= (uint32_t)&stack_var;
	
	*pxHeapCanary = (portPOINTER_SIZE_TYPE)seed;
	
	printf("[HEAP_PROTECTOR] Generated canary: 0x%08lX\r\n", (unsigned long)*pxHeapCanary);
}

void vApplicationMallocFailedHook(void)
{
  configASSERT(0)
}

void vApplicationStackOverflowHook(xTaskHandle pxTask, char *pcTaskName)
{
  (void)pxTask;
  (void)pcTaskName;
  configASSERT(0)
}
