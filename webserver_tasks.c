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

#include "atmel_start.h"
#include "webserver_tasks.h"
#include "semphr.h"
#include "lwip/tcpip.h"
#include "printf.h"
#include "network_events.h"
#include "ethernet_phy_main.h"
#include "FreeRTOS.h"
#include "task.h"

uint16_t led_blink_rate = BLINK_NORMAL;

gmac_device          gs_gmac_dev;
static bool          link_up   = false;
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
		gpio_toggle_pin_level(LED_0);
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
	ethernet_phy_get_link_status(&ETHERNET_PHY_0_desc, &previous_link_state);
	
	for (;;) {
		/* Check current link status */
		int32_t phy_result = ethernet_phy_get_link_status(&ETHERNET_PHY_0_desc, &current_link_state);
		if (phy_result == ERR_NONE) {
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
				ETHERNET_PHY_0_init();
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
 * \brief Invoked after completion of TCP/IP init
 */
void tcpip_init_done(void *arg)
{
	sys_sem_t *sem;
	sem         = (sys_sem_t *)arg;
	u8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x20, 0x76};
	
	/* Initialize network event logging */
	network_events_init();
	log_lwip_init(ERR_OK);
	
	mac_async_register_callback(&COMMUNICATION_IO, MAC_ASYNC_RECEIVE_CB, gmac_handler_cb);
	hri_gmac_set_IMR_RCOMP_bit(COMMUNICATION_IO.dev.hw);

	printf("[INIT] Waiting for Ethernet link...\r\n");
	
	/* Give PHY more time to initialize and establish link */
	int link_attempts = 0;
	const int max_link_attempts = 100;  /* 10 seconds at 100ms intervals */
	
	while (link_attempts < max_link_attempts) {
		int32_t phy_status = ethernet_phy_get_link_status(&ETHERNET_PHY_0_desc, &link_up);
		
		if (phy_status == ERR_NONE && link_up) {
			printf("[INIT] PHY link established after %d attempts\r\n", link_attempts);
			break;
		}
		
		if (phy_status != ERR_NONE) {
			printf("[INIT] PHY read error: %d (attempt %d)\r\n", phy_status, link_attempts);
		} else {
			printf("[INIT] PHY link still down (attempt %d)\r\n", link_attempts);
		}
		
		link_attempts++;
		vTaskDelay(100);  /* Wait 100ms between attempts */
	}
	
	if (!link_up) {
		printf("[INIT] WARNING: PHY link not established after %d attempts\r\n", max_link_attempts);
		printf("[INIT] Continuing with network stack initialization...\r\n");
	}

	printf("[INIT] Ethernet link detected\r\n");

	/* Enable NVIC GMAC interrupt. */
	/* Interrupt priorities. (lowest value = highest priority) */
	/* ISRs using FreeRTOS *FromISR APIs must have priorities below or equal to */
	/* configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY. */
	/* Setting to 5 (numerically higher than 4, so lower priority) */
	NVIC_SetPriority(GMAC_IRQn, 5);
	NVIC_EnableIRQ(GMAC_IRQn);
	mac_async_enable(&COMMUNICATION_IO);

	printf("[INIT] Initializing network interface...\r\n");
	TCPIP_STACK_INTERFACE_0_init(mac);

	TCPIP_STACK_INTERFACE_0_desc.input = tcpip_input;

	gs_gmac_dev.netif = &TCPIP_STACK_INTERFACE_0_desc;

	sys_thread_t id;

	/* Incoming packet notification semaphore. */
	if (sys_sem_new(&gs_gmac_dev.rx_sem, 0) != ERR_OK) {
		LWIP_ASSERT("Failed to create semaphore", 0);
	}

	id = sys_thread_new("GMAC", gmac_task, &gs_gmac_dev, netifINTERFACE_TASK_STACK_SIZE, netifINTERFACE_TASK_PRIORITY);
	LWIP_ASSERT("ethernetif_init: GMAC Task allocation ERROR!\n", (id != 0));

	printf("[INIT] Setting up network interface callbacks...\r\n");
	netif_set_default(&TCPIP_STACK_INTERFACE_0_desc);
	
	/* Register network event callbacks */
	netif_set_status_callback(&TCPIP_STACK_INTERFACE_0_desc, netif_status_callback);
	netif_set_link_callback(&TCPIP_STACK_INTERFACE_0_desc, netif_link_callback);
	
	/* Log initial MAC address */
	log_mac_address(&TCPIP_STACK_INTERFACE_0_desc);
	
	/* Log initial link status */
	log_link_status_change(&TCPIP_STACK_INTERFACE_0_desc, link_up);
	
	/* Manually update lwIP link status since link monitoring task hasn't started yet */
	if (link_up) {
		printf("[INIT] Setting initial link status to UP in lwIP\r\n");
		netif_set_link_up(&TCPIP_STACK_INTERFACE_0_desc);
	} else {
		printf("[INIT] Setting initial link status to DOWN in lwIP\r\n");
		netif_set_link_down(&TCPIP_STACK_INTERFACE_0_desc);
	}

#if CONF_TCPIP_STACK_INTERFACE_0_DHCP
	/* DHCP mode. */
	printf("[INIT] Starting DHCP client...\r\n");
	if (ERR_OK != dhcp_start(&TCPIP_STACK_INTERFACE_0_desc)) {
		log_dhcp_error(&TCPIP_STACK_INTERFACE_0_desc, "Failed to start DHCP client");
		LWIP_ASSERT("ERR_OK != dhcp_start", 0);
	} else {
		printf("[DHCP] Client started successfully\r\n");
	}
#else
	printf("[INIT] Using static IP configuration...\r\n");
	netif_set_up(&TCPIP_STACK_INTERFACE_0_desc);
	log_network_config(&TCPIP_STACK_INTERFACE_0_desc);
#endif

	printf("[INIT] Network initialization complete\r\n");
	sys_sem_signal(sem); /* Signal the waiting thread that the TCP/IP init is done. */
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
