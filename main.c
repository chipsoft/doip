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
#include "minimal_packet_test.h"  // Simple TX test

// Example function to set custom MAC address
void set_custom_mac_address(void)
{
    // Example MAC address: 02:00:11:22:33:44 (locally administered)
    uint8_t custom_mac[6] = {0x02, 0x00, 0x11, 0x22, 0x33, 0x44};
    
    printf("[MAIN] Setting custom MAC address...\r\n");
    
    drv_ksz8851snl_status_t status = bsp_ksz8851snl_set_mac_address(custom_mac);
    if (status == DRV_KSZ8851SNL_STATUS_OK) {
        printf("[MAIN] MAC address set successfully!\r\n");
        
        // Verify by reading back the MAC address
        uint8_t read_mac[6];
        status = bsp_ksz8851snl_get_mac_address(read_mac);
        if (status == DRV_KSZ8851SNL_STATUS_OK) {
            printf("[MAIN] Verified MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   read_mac[0], read_mac[1], read_mac[2], read_mac[3], read_mac[4], read_mac[5]);
        }
    } else {
        printf("[MAIN] Failed to set MAC address, status: %d\r\n", status);
    }
}


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
    
    // Test MAC address configuration
    set_custom_mac_address();
    
    // Run minimal packet test to verify TX functionality with RXQ_SDA fix
    printf("🔍 Running minimal packet test with fixed TX implementation...\r\n");
    minimal_packet_test();
    printf("🔍 Minimal packet test completed, continuing with periodic transmission...\r\n");
    
    // Create a simple Ethernet frame for testing
    // Format: [Destination MAC][Source MAC][EtherType][Payload][Padding if needed]
    uint8_t test_ethernet_frame[] = {
        // Destination MAC (broadcast address)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        // Source MAC (our device MAC)
        0x00, 0x00, 0x00, 0x00, 0x20, 0x76,
        // EtherType (0x0800 = IPv4, using custom test type 0x88B7)
        0x88, 0xB7,
        // Test payload: "Hello KSZ8851SNL Test Frame!"
        'H', 'e', 'l', 'l', 'o', ' ', 'K', 'S', 'Z', '8', '8', '5', '1', 'S', 'N', 'L',
        ' ', 'T', 'e', 's', 't', ' ', 'F', 'r', 'a', 'm', 'e', '!', '\0',
        // Padding to reach minimum Ethernet frame size (60 bytes total)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    uint16_t frame_length = sizeof(test_ethernet_frame);
    uint32_t packet_counter = 0;
    
    printf("🔍 Test Ethernet frame prepared: %d bytes\r\n", frame_length);
    printf("🔍 Starting periodic frame transmission every 1 second\r\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second
        
        packet_counter++;
        printf("🔍 Sending test packet #%lu\r\n", packet_counter);
        
        // Send the test Ethernet frame
        drv_ksz8851snl_status_t send_result = hw_ksz8851snl_send_packet(&ksz8851snl_0, 
                                                                        test_ethernet_frame, 
                                                                        frame_length);
        
        if (send_result == DRV_KSZ8851SNL_STATUS_OK) {
            printf("✅ Test packet #%lu sent successfully\r\n", packet_counter);
        } else {
            printf("❌ Test packet #%lu send failed: %d\r\n", packet_counter, send_result);
        }
        
        // Check and display status every 10 packets
        if (packet_counter % 10 == 0) {
            drv_ksz8851snl_status_info_t status_info;
            drv_ksz8851snl_status_t status_result = hw_ksz8851snl_get_status(&ksz8851snl_0, &status_info);
            
            if (status_result == DRV_KSZ8851SNL_STATUS_OK) {
                printf("📊 Status after %lu packets:\r\n", packet_counter);
                printf("   Link: %s, Speed: %d Mbps, Duplex: %s\r\n",
                       status_info.link_up ? "UP" : "DOWN",
                       status_info.link_speed,
                       status_info.full_duplex ? "Full" : "Half");
                printf("   TX: %lu packets, %lu errors\r\n",
                       status_info.tx_packets, status_info.tx_errors);
                printf("   RX: %lu packets, %lu errors\r\n",
                       status_info.rx_packets, status_info.rx_errors);
            }
            
            // Also display interrupt statistics
            printf("🔔 Interrupt Statistics:\r\n");
            ksz8851snl_debug_print_irq_stats();
        }
    }
}

/* RTT printf integration */
extern void rtt_printf_init(void);



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
        	                "KSZ_TEST",
        	                512,  // Stack size
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

/**
 * FreeRTOS stack overflow hook - called when stack overflow is detected
 * Required when configCHECK_FOR_STACK_OVERFLOW is enabled
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	/* Critical error - stack overflow detected */
	printf("\r\n*** CRITICAL ERROR: Stack overflow detected! ***\r\n");
	printf("Task: %s (handle: %p)\r\n", pcTaskName ? pcTaskName : "Unknown", (void*)xTask);
	printf("This could be the source of 0x55 frame corruption!\r\n");
	printf("System halted for safety.\r\n");
	
	/* Disable interrupts and halt system */
	taskDISABLE_INTERRUPTS();
	
	/* Infinite loop to prevent further corruption */
	while(1) {
		/* Optional: Flash LED or other indication */
		/* In production, could trigger watchdog reset */
	}
}
