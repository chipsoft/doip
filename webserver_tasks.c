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

#include "webserver_tasks.h"
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

/* External peripheral descriptors */
extern struct mac_async_descriptor COMMUNICATION_IO;

uint16_t led_blink_rate = BLINK_NORMAL;

gmac_device          gs_gmac_dev;
bool                 link_up   = false;
volatile static bool recv_flag = false;

static TaskHandle_t xLed_Task;
static TaskHandle_t xLink_Monitor_Task;

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
 * \brief Link monitoring task
 * Periodically checks PHY link status and notifies lwIP of changes
 */
static void link_monitor_task(void *p)
{
	(void)p;
	bool current_link_state = false;
	bool previous_link_state = false;
	
	/* Wait for network initialization to complete */
	vTaskDelay(2000);
	
	/* Get initial link state */
	hw_eth_get_link_status(&eth_communication, &previous_link_state);
	
	for (;;) {
		/* Check current link status */
		drv_eth_status_t phy_result = hw_eth_get_link_status(&eth_communication, &current_link_state);
		if (phy_result == DRV_ETH_STATUS_OK) {
			/* Detect link status changes */
			if (current_link_state != previous_link_state) {
				printf("[LINK_MONITOR] Link state change detected: %s -> %s\r\n",
				       previous_link_state ? "UP" : "DOWN",
				       current_link_state ? "UP" : "DOWN");
				
				/* Update lwIP network interface link status */
				if (current_link_state) {
					netif_set_link_up(&TCPIP_STACK_INTERFACE_0_desc);
					printf("[LINK_MONITOR] Notified lwIP: Link UP\r\n");
					
					/* Don't restart auto-negotiation - let it stabilize naturally */
					printf("[LINK_MONITOR] Link UP - allowing stabilization\r\n");
				} else {
					netif_set_link_down(&TCPIP_STACK_INTERFACE_0_desc);
					printf("[LINK_MONITOR] Notified lwIP: Link DOWN\r\n");
				}
				
				/* Update global link state */
				link_up = current_link_state;
				previous_link_state = current_link_state;
			}
		} else {
			printf("[LINK_MONITOR] Failed to read PHY link status (error: %d)\r\n", phy_result);
			
			/* Try to reinitialize PHY if we can't read it */
			static int phy_error_count = 0;
			phy_error_count++;
			if (phy_error_count >= 10) {  /* After 10 consecutive errors (5 seconds) */
				printf("[LINK_MONITOR] Attempting PHY re-initialization\r\n");
				hw_eth_phy_init(&eth_communication);
				phy_error_count = 0;
			}
		}
		
		/* Check link status every 500ms */
		vTaskDelay(500);
	}
}

void mac_receive_cb(struct mac_async_descriptor *desc)
{
	recv_flag = true;
}

/**
 * \brief Callback for GMAC interrupt.
 * Give semaphore for which gmac_task waits
 */
void gmac_handler_cb(void)
{
	BaseType_t xGMACTaskWoken = pdFALSE;
	
	/* Use ISR-safe semaphore give function */
	xSemaphoreGiveFromISR(gs_gmac_dev.rx_sem.sem, &xGMACTaskWoken);
	
	/* Perform context switch if higher priority task was woken */
	portYIELD_FROM_ISR(xGMACTaskWoken);
}


/**
 * \brief Task for GMAC.
 * Waits for GMAC interrupt and begins processing of received packets
 */
void gmac_task(void *pvParameters)
{
	gmac_device *ps_gmac_dev = pvParameters;

	while (1) {
		/* Wait for the counting RX notification semaphore. */
		sys_sem_wait(&ps_gmac_dev->rx_sem);

		/* Process the incoming packet. */
		ethernetif_mac_input(ps_gmac_dev->netif);
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
 * \brief Create OS task for link monitoring
 */
void task_link_monitor_create(void)
{
	/* Create task to monitor link status */
	if (xTaskCreate(link_monitor_task, "LinkMon", TASK_LED_STACK_SIZE, NULL, TASK_LED_TASK_PRIORITY, &xLink_Monitor_Task) != pdPASS) {
		while (1) {
			;
		}
	}
}
