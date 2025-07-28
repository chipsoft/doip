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
#include <hal_mac_async.h>
#include "bsp_led.h"
#include "bsp_ethernet.h"
#include "eth_ipstack_main.h"
#include "webserver_tasks.h"
#include "doip_client.h" 
#include "bsp_net.h"  // New universal network driver
#include "FreeRTOS.h"
#include "task.h"

/* RTT printf integration */
extern void rtt_printf_init(void);

/* Peripheral descriptors */
struct mac_async_descriptor COMMUNICATION_IO;

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
	
	// Configure network parameters
	drv_net_config_t net_config = {
		.mac_addr = {0x00, 0x00, 0x00, 0x00, 0x20, 0x76},
		.use_dhcp = false,
		.static_ip = "192.168.100.2",
		.static_netmask = "255.255.255.0", 
		.static_gateway = "192.168.100.1",
		.hostname = "same54-doip",
		.dhcp_timeout_ms = 30000,
	};
	
	// Initialize network stack
	printf("Initializing network stack...\r\n");
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
	
	printf("Network stack initialized successfully\r\n");
	
	// Start DOIP client now that network is ready
	doip_client_start_task();
	
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
	
	// Initialize Ethernet using universal driver (now handles pins/clocks internally)
	hw_eth_init(&eth_communication);
		
	/* Initialize SEGGER RTT for debug output */
	rtt_printf_init();

	/* Initialize DOIP client */
	doip_client_init();

	/* Create application tasks */
	task_led_create();
	
	/* Start Ethernet link monitoring through driver API */
	hw_eth_start_link_monitor(&eth_communication);

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
