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

#include "printf.h"
#include <peripheral_clk_config.h>
#include <utils.h>
#include <hal_init.h>
#include <hal_gpio.h>
#include "bsp_led.h"
#include "eth_ipstack_main.h"
#include "user_tasks.h"
#include "bsp_doip.h"  // Universal DOIP driver
#include "bsp_ksz8851snl.h"  // KSZ8851SNL driver with debug functions
#ifndef USE_KSZ8851SNL_INTERFACE
#include "bsp_net.h"   // Universal network driver (GMAC only)
#endif
#include "FreeRTOS.h"
#include "task.h"

// Network interface specific includes
#ifdef USE_KSZ8851SNL_INTERFACE
#include "bsp_ksz8851snl.h"
#include "ksz8851snl_config.h"
#else
#include <hal_mac_async.h>
#include "bsp_ethernet.h"
#endif

/* RTT printf integration */
extern void rtt_printf_init(void);

/* Peripheral descriptors */
#ifndef USE_KSZ8851SNL_INTERFACE
struct mac_async_descriptor COMMUNICATION_IO;
#endif

/* Task handles */
// static TaskHandle_t xCreatedEthernetBasicTask; // Removed - not used anymore

/* define to avoid compilation warning */
// #define LWIP_TIMEVAL_PRIVATE 0

void print_ipaddress(void)
{
	static char tmp_buff[16];
	printf("IP_ADDR    : %s\r\n",
	       ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.ip_addr), tmp_buff, 16));
	printf("NET_MASK   : %s\r\n",
	       ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.netmask), tmp_buff, 16));
	printf("GATEWAY_IP : %s\r\n", ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.gw), tmp_buff, 16));
}

// Network initialization task - runs after scheduler starts
static void network_init_task(void *pvParameters)
{
	(void)pvParameters;
	
	printf("Network initialization task started\r\n");
	
#ifdef USE_KSZ8851SNL_INTERFACE
	printf("Using KSZ8851SNL SPI-Ethernet interface\r\n");
	
	// Initialize KSZ8851SNL driver first (this will initialize SPI)
	drv_ksz8851snl_config_t ksz_config = {
		.mac_addr = {0x00, 0x00, 0x00, 0x00, 0x20, 0x76},
		.auto_negotiation = true,
		.link_speed = 100,
		.full_duplex = true
	};
	
	printf("[MAIN] ================================================\r\n");
	printf("[MAIN] KSZ8851SNL Ethernet Controller Initialization\r\n");
	printf("[MAIN] ================================================\r\n");
	
	drv_ksz8851snl_status_t init_status = hw_ksz8851snl_init(&ksz8851snl_0, &ksz_config);
	if (init_status != DRV_KSZ8851SNL_STATUS_OK) {
		printf("[MAIN] KSZ8851SNL initialization failed: %d\r\n", init_status);
		printf("[MAIN] Check SPI configuration and connections\r\n");
		// Continue anyway for now - could be a configuration issue
	} else {
		printf("[MAIN] KSZ8851SNL driver initialized successfully\r\n");
		
		// Now test chip ID (comprehensive SPI communication test)
		drv_ksz8851snl_id_info_t id_info;
		drv_ksz8851snl_status_t chip_status = hw_ksz8851snl_get_chip_id(&ksz8851snl_0, &id_info);
		
		if (chip_status == DRV_KSZ8851SNL_STATUS_OK && id_info.chip_detected) {
			printf("[MAIN] ✓ KSZ8851SNL Chip ID: 0x%04X, Revision: %d\r\n", 
			       id_info.chip_id, id_info.revision_id);
			printf("[MAIN] ✓ SPI Communication: %s\r\n", 
			       id_info.spi_communication_ok ? "OK" : "FAILED");
			
			// Enable the KSZ8851SNL and test link status
			hw_ksz8851snl_enable(&ksz8851snl_0);
			
			// Give some time for link negotiation
			vTaskDelay(pdMS_TO_TICKS(2000));
			
			// Test link status detection
			drv_ksz8851snl_status_info_t status_info;
			drv_ksz8851snl_status_t status_result = hw_ksz8851snl_get_status(&ksz8851snl_0, &status_info);
			
			if (status_result == DRV_KSZ8851SNL_STATUS_OK) {
				printf("[MAIN] ✓ Link Status: %s\r\n", status_info.link_up ? "UP" : "DOWN");
				if (status_info.link_up) {
					printf("[MAIN] ✓ Link Speed: %d Mbps\r\n", status_info.link_speed);
					printf("[MAIN] ✓ Duplex Mode: %s\r\n", status_info.full_duplex ? "Full" : "Half");
				}
			} else {
				printf("[MAIN] ✗ Failed to read link status: %d\r\n", status_result);
			}
			
			// Test interrupt system
			printf("[MAIN] ================================================\r\n");
			printf("[MAIN] KSZ8851SNL Interrupt System Testing\r\n");
			printf("[MAIN] ================================================\r\n");
			
			// Run GPIO test
			ksz8851snl_debug_gpio_test();
			
			// Run register test
			ksz8851snl_debug_test_registers();
			
			// Print initial interrupt statistics
			ksz8851snl_debug_print_irq_stats();
			
			// Run force interrupt test
			ksz8851snl_debug_force_interrupt_test();
			
			// Test interrupt detection by monitoring for 10 seconds
			printf("[MAIN] ================================================\r\n");
			printf("[MAIN] Testing Interrupt Detection (10 seconds)...\r\n");
			printf("[MAIN] ================================================\r\n");
			
			for (int i = 0; i < 10; i++) {
				printf("[MAIN] Test %d/10: Waiting 1 second...\r\n", i+1);
				vTaskDelay(pdMS_TO_TICKS(1000));
				
				// Check for any new interrupts
				ksz8851snl_debug_print_irq_stats();
				
				// Also manually check if RX is available (this will process pending interrupts)
				bool rx_available = false;
				hw_ksz8851snl_check_rx_available(&ksz8851snl_0, &rx_available);
				if (rx_available) {
					printf("[MAIN] *** RX data available detected! ***\r\n");
				}
			}
			
			printf("[MAIN] ================================================\r\n");
			printf("[MAIN] KSZ8851SNL initialization and testing COMPLETE\r\n");
			printf("[MAIN] ================================================\r\n");
			
		} else {
			printf("[MAIN] ✗ KSZ8851SNL chip detection failed: %d\r\n", chip_status);
			printf("[MAIN] Check SPI wiring and connections\r\n");
			printf("[MAIN] Expected connections:\r\n");
			printf("[MAIN] - SCK:  PB26 -> KSZ8851SNL SCK\r\n");
			printf("[MAIN] - MOSI: PB27 -> KSZ8851SNL SI\r\n");
			printf("[MAIN] - MISO: PB29 -> KSZ8851SNL SO\r\n");
			printf("[MAIN] - CS:   PB28 -> KSZ8851SNL CS#\r\n");
			printf("[MAIN] - RST:  PA6  -> KSZ8851SNL RST#\r\n");
			printf("[MAIN] - INT:  PB7  -> KSZ8851SNL INT#\r\n");
		}
	}
#else
	printf("Using GMAC Ethernet interface\r\n");
#endif
	
	// Configure network parameters
#ifndef USE_KSZ8851SNL_INTERFACE
	drv_net_config_t net_config = {
		.mac_addr = {0x00, 0x00, 0x00, 0x00, 0x20, 0x76},
		.use_dhcp = false,
		.static_ip = "192.168.100.2",
		.static_netmask = "255.255.255.0", 
		.static_gateway = "192.168.100.1",
		.hostname = "same54-doip",
		.dhcp_timeout_ms = 30000,
	};
#endif
	
	// Initialize network stack
	printf("Initializing network stack...\r\n");
	
#ifdef USE_KSZ8851SNL_INTERFACE
	// For KSZ8851SNL: simplified initialization (TODO: implement proper network stack)
	printf("KSZ8851SNL network initialization - simplified version\r\n");
	printf("TODO: Implement full network stack integration for KSZ8851SNL\r\n");
	
	// For now, just verify that our drivers can be accessed
	printf("Network stack placeholder initialized\r\n");
#else
	// For GMAC: use universal network driver
	drv_net_status_t net_result = hw_net_init(&lwip_network_0);
	if (net_result != DRV_NET_STATUS_OK) {
		printf("Network initialization failed: %d\r\n", net_result);
		vTaskDelete(NULL);
		return;
	}
	
	net_result = hw_net_start(&lwip_network_0, &net_config);
	if (net_result != DRV_NET_STATUS_OK) {
		printf("Network start failed: %d\r\n", net_result);
		vTaskDelete(NULL);
		return;
	}
#endif
	
	printf("Network stack initialized successfully\r\n");
	
	// Start DOIP client now that network is ready
	task_doip_client_create(&doip_0);
	
	// Start diagnostic processor for raw DOIP packet analysis
	task_diagnostic_processor_create(&doip_0);
	
	// This task is done, delete itself
	printf("Network initialization complete, deleting init task\r\n");
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
	
	/* DoIP configuration validation */
	printf("\r\n=== DoIP Configuration Status ===\r\n");
	printf("DOIP Small Buffer Size: %d bytes\r\n", DOIP_SMALL_PAYLOAD_SIZE);
	printf("DOIP Static Send Buffer: %d bytes\r\n", DOIP_LARGE_SEND_BUFFER_SIZE);
	printf("DOIP Task Stack Size: %d bytes\r\n", DOIP_CLIENT_TASK_STACK_SIZE);
	printf("FreeRTOS Heap Size: %d bytes\r\n", configTOTAL_HEAP_SIZE);
	printf("Max send payload: %d bytes (static)\r\n", DOIP_LARGE_SEND_BUFFER_SIZE);
	printf("Max receive payload: %d bytes (stored)\r\n", DOIP_SMALL_PAYLOAD_SIZE);
	printf("Large message support: %s\r\n", DOIP_ENABLE_LARGE_MESSAGES ? "ENABLED" : "DISABLED");
	printf("==================================\r\n\r\n");


	/* Create application tasks */
	task_led_create();
	
	/* Start Ethernet link monitoring through driver API */
#ifdef USE_KSZ8851SNL_INTERFACE
	// TODO: Implement link monitoring for KSZ8851SNL
	printf("KSZ8851SNL link monitoring not yet implemented\r\n");
#else
	hw_eth_start_link_monitor(&eth_communication);
#endif

	/* Create network initialization task that will start DOIP client */
	if (xTaskCreate(network_init_task,
	                "NetInit",
	                512,  // Stack size
	                NULL,
	                (tskIDLE_PRIORITY + 3),  // Higher priority to run first
	                NULL)
	    != pdPASS) {
		printf("Failed to create network initialization task\r\n");
		while (1);
	}

	/* Start FreeRTOS scheduler */
	printf("\r\nStarting FreeRTOS scheduler\r\n");
	vTaskStartScheduler();

	/* Should never reach here */
	return 0;
}
