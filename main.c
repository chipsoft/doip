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
#include "lwip/sockets.h"  // Add socket includes
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include <peripheral_clk_config.h>
#include <utils.h>
#include <hal_init.h>
#include <hal_gpio.h>
#include "bsp_doip.h"  // Universal DOIP driver
#include "bsp_ksz8851snl.h"  // KSZ8851SNL driver with debug functions
#ifndef USE_KSZ8851SNL_INTERFACE
#include "bsp_net.h"   // Universal network driver (GMAC only)
#endif

// Network interface specific includes
#ifdef USE_KSZ8851SNL_INTERFACE
#include "bsp_ksz8851snl.h"
#include "ksz8851snl_config.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"
#include "ethif_ksz8851snl.h"
#include "lwip_macif_config.h"
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

#ifdef USE_KSZ8851SNL_INTERFACE
/* KSZ8851SNL LwIP network interface */
struct netif ksz8851snl_netif;
#endif

/* Task handles */
// static TaskHandle_t xCreatedEthernetBasicTask; // Removed - not used anymore

/* define to avoid compilation warning */
// #define LWIP_TIMEVAL_PRIVATE 0

void print_ipaddress(void)
{
	static char tmp_buff[16];
#ifdef USE_KSZ8851SNL_INTERFACE
	printf("IP_ADDR    : %s\r\n",
	       ipaddr_ntoa_r(netif_ip4_addr(&ksz8851snl_netif), tmp_buff, 16));
	printf("NET_MASK   : %s\r\n",
	       ipaddr_ntoa_r(netif_ip4_netmask(&ksz8851snl_netif), tmp_buff, 16));
	printf("GATEWAY_IP : %s\r\n", ipaddr_ntoa_r(netif_ip4_gw(&ksz8851snl_netif), tmp_buff, 16));
#else
	printf("IP_ADDR    : %s\r\n",
	       ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.ip_addr), tmp_buff, 16));
	printf("NET_MASK   : %s\r\n",
	       ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.netmask), tmp_buff, 16));
	printf("GATEWAY_IP : %s\r\n", ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.gw), tmp_buff, 16));
#endif
}

#ifdef USE_KSZ8851SNL_INTERFACE
// Packet reception task for KSZ8851SNL interface
static void ksz8851snl_packet_task(void *pvParameters)
{
	(void)pvParameters;
	
	printf("KSZ8851SNL packet reception task started\r\n");
	
	while (1) {
		// Check for incoming packets and process them
		ethif_ksz8851snl_input(&ksz8851snl_netif);
		
		// Yield to other tasks - polling interval of 1ms
		vTaskDelay(pdMS_TO_TICKS(1));
	}
}

// Link monitoring task for KSZ8851SNL interface
static void ksz8851snl_link_monitor_task(void *pvParameters)
{
	(void)pvParameters;
	
	printf("KSZ8851SNL link monitoring task started\r\n");
	
	bool previous_link_state = false;
	
	while (1) {
		// Check link status
		drv_ksz8851snl_status_info_t status_info;
		drv_ksz8851snl_status_t result = hw_ksz8851snl_get_status(&ksz8851snl_0, &status_info);
		
		if (result == DRV_KSZ8851SNL_STATUS_OK) {
			// Update network interface link status
			if (status_info.link_up != previous_link_state) {
				if (status_info.link_up) {
					printf("[LINK] Link UP - %d Mbps %s duplex\r\n", 
					       status_info.link_speed, 
					       status_info.full_duplex ? "Full" : "Half");
					netif_set_link_up(&ksz8851snl_netif);
				} else {
					printf("[LINK] Link DOWN\r\n");
					netif_set_link_down(&ksz8851snl_netif);
				}
				previous_link_state = status_info.link_up;
			}
		}
		
		// Check link status every 500ms
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}
#endif

// Raw TCP test function using KSZ8851SNL hardware directly (bypasses lwIP)
static void raw_tcp_test_send_packets(void)
{
    printf("\r\n=== Raw TCP Test: Direct KSZ8851SNL Transmission ===\r\n");
    printf("This will send raw TCP packets directly through KSZ8851SNL hardware\r\n");
    printf("Bypassing lwIP entirely to test raw packet transmission\r\n");
    
    // Wait a bit for KSZ8851SNL to be fully ready
    printf("🔍 Raw TCP Test: Waiting for KSZ8851SNL to be ready...\r\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // First, let's test with a simple broadcast packet to verify basic functionality
    printf("🔍 Raw TCP Test: Testing basic broadcast packet first...\r\n");
    
    // Check TX memory status before sending
    printf("🔍 Raw TCP Test: Checking TX memory status...\r\n");
    uint16_t tx_mem_info = 0;
    uint16_t available_mem = 0;
    
    // Read TX memory info register (we'll need to implement this)
    // For now, let's try to send a smaller packet first
    
    uint8_t broadcast_packet[64];
    uint16_t broadcast_length = 0;
    
    // Simple broadcast packet (64 bytes minimum for Ethernet)
    // Destination MAC (broadcast)
    broadcast_packet[0] = 0xFF; broadcast_packet[1] = 0xFF; broadcast_packet[2] = 0xFF;
    broadcast_packet[3] = 0xFF; broadcast_packet[4] = 0xFF; broadcast_packet[5] = 0xFF;
    // Source MAC (our device - use the configured MAC address)
    broadcast_packet[6] = 0x00; broadcast_packet[7] = 0x00; broadcast_packet[8] = 0x00;
    broadcast_packet[9] = 0x00; broadcast_packet[10] = 0x20; broadcast_packet[11] = 0x76;
    // EtherType (IPv4)
    broadcast_packet[12] = 0x08; broadcast_packet[13] = 0x00;
    // Simple payload (fill with test pattern) - REDUCE SIZE
    for (int i = 14; i < 32; i++) {  // Reduced from 64 to 32 bytes
        broadcast_packet[i] = i & 0xFF;
    }
    broadcast_length = 32;  // Reduced packet size
    
    printf("🔍 Raw TCP Test: Sending small broadcast test packet (%d bytes)...\r\n", broadcast_length);
    printf("🔍 Raw TCP Test: Broadcast packet starts with: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           broadcast_packet[0], broadcast_packet[1], broadcast_packet[2], broadcast_packet[3],
           broadcast_packet[4], broadcast_packet[5], broadcast_packet[6], broadcast_packet[7]);
    
    // Try to reset TX memory if it's full
    printf("🔍 Raw TCP Test: Attempting to reset TX memory...\r\n");
    // This would require implementing a TX reset function in the driver
    // For now, let's try to send the packet and see what happens
    
    extern drv_ksz8851snl_t ksz8851snl_0;
    drv_ksz8851snl_status_t broadcast_result = hw_ksz8851snl_send_packet(&ksz8851snl_0, broadcast_packet, broadcast_length);
    
    if (broadcast_result == DRV_KSZ8851SNL_STATUS_OK) {
        printf("✅ Raw TCP Test: Broadcast packet sent successfully!\r\n");
        printf("🔍 Raw TCP Test: Check Wireshark for broadcast packet\r\n");
    } else {
        printf("❌ Raw TCP Test: Broadcast packet failed: %d\r\n", broadcast_result);
    }
    
    // Wait a bit to see if broadcast packet appears in Wireshark
    printf("🔍 Raw TCP Test: Waiting 3 seconds to check broadcast packet in Wireshark...\r\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    uint32_t packet_count = 0;
    
    while (1) {
        packet_count++;
        printf("\r\n--- Raw TCP Test Packet #%lu ---\r\n", packet_count);
        
        // Create a raw TCP packet manually
        // TCP SYN packet structure for connection to 192.168.100.100:13400
        uint8_t raw_tcp_packet[60]; // Standard TCP packet size
        uint16_t packet_length = 0;
        
        // Ethernet header (14 bytes)
        uint8_t *eth_ptr = raw_tcp_packet;
        // Destination MAC (broadcast for testing)
        eth_ptr[0] = 0xFF; eth_ptr[1] = 0xFF; eth_ptr[2] = 0xFF;
        eth_ptr[3] = 0xFF; eth_ptr[4] = 0xFF; eth_ptr[5] = 0xFF;
        // Source MAC (our device - use the configured MAC address)
        eth_ptr[6] = 0x00; eth_ptr[7] = 0x00; eth_ptr[8] = 0x00;
        eth_ptr[9] = 0x00; eth_ptr[10] = 0x20; eth_ptr[11] = 0x76;
        // EtherType (IPv4)
        eth_ptr[12] = 0x08; eth_ptr[13] = 0x00;
        packet_length += 14;
        
        // IP header (20 bytes)
        uint8_t *ip_ptr = raw_tcp_packet + 14;
        // IP version and header length
        ip_ptr[0] = 0x45; // IPv4, header length 5 words
        // Type of Service
        ip_ptr[1] = 0x00;
        // Total length (will be filled)
        ip_ptr[2] = 0x00; ip_ptr[3] = 0x00;
        // Identification
        ip_ptr[4] = 0x00; ip_ptr[5] = 0x01;
        // Flags and fragment offset
        ip_ptr[6] = 0x40; ip_ptr[7] = 0x00; // Don't fragment
        // Time to live
        ip_ptr[8] = 0x40; // 64 hops
        // Protocol (TCP)
        ip_ptr[9] = 0x06;
        // Header checksum (will be filled)
        ip_ptr[10] = 0x00; ip_ptr[11] = 0x00;
        // Source IP (our device: 192.168.100.2)
        ip_ptr[12] = 192; ip_ptr[13] = 168; ip_ptr[14] = 100; ip_ptr[15] = 2;
        // Destination IP (target: 192.168.100.100)
        ip_ptr[16] = 192; ip_ptr[17] = 168; ip_ptr[18] = 100; ip_ptr[19] = 100;
        packet_length += 20;
        
        // TCP header (20 bytes)
        uint8_t *tcp_ptr = raw_tcp_packet + 34;
        // Source port (random high port)
        tcp_ptr[0] = 0x13; tcp_ptr[1] = 0x89; // 5001
        // Destination port (DOIP: 13400)
        tcp_ptr[2] = 0x34; tcp_ptr[3] = 0x58; // 13400
        // Sequence number
        tcp_ptr[4] = 0x00; tcp_ptr[5] = 0x00; tcp_ptr[6] = 0x00; tcp_ptr[7] = 0x01;
        // Acknowledgment number
        tcp_ptr[8] = 0x00; tcp_ptr[9] = 0x00; tcp_ptr[10] = 0x00; tcp_ptr[11] = 0x00;
        // Data offset and flags
        tcp_ptr[12] = 0x50; // 5 words, no flags
        tcp_ptr[13] = 0x02; // SYN flag
        // Window size
        tcp_ptr[14] = 0x20; tcp_ptr[15] = 0x00; // 8192
        // Checksum (will be filled)
        tcp_ptr[16] = 0x00; tcp_ptr[17] = 0x00;
        // Urgent pointer
        tcp_ptr[18] = 0x00; tcp_ptr[19] = 0x00;
        packet_length += 20;
        
        // TCP options (6 bytes - MSS)
        uint8_t *opt_ptr = raw_tcp_packet + 54;
        opt_ptr[0] = 0x02; // Option kind: MSS
        opt_ptr[1] = 0x04; // Option length: 4 bytes
        opt_ptr[2] = 0x05; opt_ptr[3] = 0xB4; // MSS: 1460
        opt_ptr[4] = 0x01; // Option kind: NOP
        opt_ptr[5] = 0x01; // Option kind: NOP
        packet_length += 6;
        
        // Update IP total length
        uint16_t ip_total_len = packet_length - 14; // Exclude Ethernet header
        ip_ptr[2] = (ip_total_len >> 8) & 0xFF;
        ip_ptr[3] = ip_total_len & 0xFF;
        
        // Calculate IP checksum
        uint32_t ip_checksum = 0;
        for (int i = 0; i < 20; i += 2) {
            ip_checksum += (ip_ptr[i] << 8) | ip_ptr[i + 1];
        }
        while (ip_checksum >> 16) {
            ip_checksum = (ip_checksum & 0xFFFF) + (ip_checksum >> 16);
        }
        ip_checksum = ~ip_checksum;
        ip_ptr[10] = (ip_checksum >> 8) & 0xFF;
        ip_ptr[11] = ip_checksum & 0xFF;
        
        // Calculate TCP checksum (pseudo-header + TCP header + data)
        uint32_t tcp_checksum = 0;
        
        // Pseudo-header
        for (int i = 12; i < 20; i += 2) { // Source IP
            tcp_checksum += (ip_ptr[i] << 8) | ip_ptr[i + 1];
        }
        for (int i = 16; i < 20; i += 2) { // Destination IP
            tcp_checksum += (ip_ptr[i] << 8) | ip_ptr[i + 1];
        }
        tcp_checksum += 0x0006; // Protocol (TCP)
        tcp_checksum += (ip_total_len - 20); // TCP length
        
        // TCP header and data
        for (int i = 0; i < (packet_length - 34); i += 2) {
            if (i + 1 < (packet_length - 34)) {
                tcp_checksum += (tcp_ptr[i] << 8) | tcp_ptr[i + 1];
            } else {
                tcp_checksum += (tcp_ptr[i] << 8);
            }
        }
        
        while (tcp_checksum >> 16) {
            tcp_checksum = (tcp_checksum & 0xFFFF) + (tcp_checksum >> 16);
        }
        tcp_checksum = ~tcp_checksum;
        tcp_ptr[16] = (tcp_checksum >> 8) & 0xFF;
        tcp_ptr[17] = tcp_checksum & 0xFF;
        
        printf("🔍 Raw TCP Test: Packet constructed (%lu bytes)\r\n", packet_length);
        printf("🔍 Raw TCP Test: Source: 192.168.100.2:5001\r\n");
        printf("🔍 Raw TCP Test: Destination: 192.168.100.100:13400\r\n");
        printf("🔍 Raw TCP Test: TCP SYN flag set\r\n");
        
        // Send the raw packet through KSZ8851SNL
        printf("🔍 Raw TCP Test: Sending raw packet through KSZ8851SNL...\r\n");
        
        // Debug: Print first few bytes of the packet
        printf("🔍 Raw TCP Test: Packet starts with: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               raw_tcp_packet[0], raw_tcp_packet[1], raw_tcp_packet[2], raw_tcp_packet[3],
               raw_tcp_packet[4], raw_tcp_packet[5], raw_tcp_packet[6], raw_tcp_packet[7]);
        
        // Use the existing KSZ8851SNL send function
        // This bypasses lwIP entirely and sends directly through the hardware
        extern drv_ksz8851snl_t ksz8851snl_0; // Reference to the driver instance
        drv_ksz8851snl_status_t send_result = hw_ksz8851snl_send_packet(&ksz8851snl_0, raw_tcp_packet, packet_length);
        
        if (send_result == DRV_KSZ8851SNL_STATUS_OK) {
            printf("✅ Raw TCP Test: Packet sent successfully through KSZ8851SNL!\r\n");
            printf("🔍 Raw TCP Test: Check Wireshark for raw TCP SYN packet\r\n");
        } else {
            printf("❌ Raw TCP Test: Failed to send packet: %d\r\n", send_result);
        }
        
        printf("🔍 Raw TCP Test: Packet #%lu completed\r\n", packet_count);
        printf("🔍 Raw TCP Test: Waiting 2 seconds before next packet...\r\n");
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds between packets
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
			
			// Run GPIO test
			ksz8851snl_debug_gpio_test();
			
			// Run register test
			ksz8851snl_debug_test_registers();
			
			// Run packet transmission test to validate fixes
			ksz8851snl_debug_test_packet_transmission();
			
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
	// For KSZ8851SNL: Initialize LwIP network stack with KSZ8851SNL interface
	printf("KSZ8851SNL network initialization starting...\r\n");
	
	// Initialize LwIP stack
	eth_ipstack_init();
	
	// Configure IP addresses
	ip4_addr_t ipaddr, netmask, gateway;
	
#if CONF_TCPIP_STACK_INTERFACE_0_DHCP
	// Use DHCP
	ip4_addr_set_zero(&ipaddr);
	ip4_addr_set_zero(&netmask);
	ip4_addr_set_zero(&gateway);
	printf("Using DHCP for IP configuration\r\n");
#else
	// Use static IP configuration
	ip4addr_aton(CONF_TCPIP_STACK_INTERFACE_0_IP, &ipaddr);
	ip4addr_aton(CONF_TCPIP_STACK_INTERFACE_0_NETMASK, &netmask);
	ip4addr_aton(CONF_TCPIP_STACK_INTERFACE_0_GATEWAY, &gateway);
	printf("Using static IP: %s\r\n", CONF_TCPIP_STACK_INTERFACE_0_IP);
	printf("Netmask: %s, Gateway: %s\r\n", CONF_TCPIP_STACK_INTERFACE_0_NETMASK, CONF_TCPIP_STACK_INTERFACE_0_GATEWAY);
#endif
	
	// Add network interface with KSZ8851SNL driver
	struct netif *netif_result = netif_add(&ksz8851snl_netif,
	                                       &ipaddr, &netmask, &gateway,
	                                       NULL,  // No state data needed
	                                       ethif_ksz8851snl_init,  // KSZ8851SNL init function
	                                       ethernet_input);        // Standard ethernet input
	
	if (netif_result == NULL) {
		printf("Failed to add KSZ8851SNL network interface\r\n");
		vTaskDelete(NULL);
		return;
	}
	
	// Set as default interface
	netif_set_default(&ksz8851snl_netif);
	
	// Bring interface up
	netif_set_up(&ksz8851snl_netif);
	
#if CONF_TCPIP_STACK_INTERFACE_0_DHCP
	// Start DHCP if enabled
	dhcp_start(&ksz8851snl_netif);
	printf("DHCP client started\r\n");
	
	// Wait for DHCP to assign an IP (with timeout)
	uint32_t dhcp_timeout = 30000; // 30 seconds
	uint32_t dhcp_start_time = xTaskGetTickCount();
	
	while (!netif_is_up(&ksz8851snl_netif) || ip4_addr_isany(netif_ip4_addr(&ksz8851snl_netif))) {
		vTaskDelay(pdMS_TO_TICKS(100));
		if ((xTaskGetTickCount() - dhcp_start_time) > pdMS_TO_TICKS(dhcp_timeout)) {
			printf("DHCP timeout - using link-local address\r\n");
			break;
		}
	}
#endif
	
	printf("KSZ8851SNL network stack initialized successfully\r\n");
	
	// Print network configuration
	print_ipaddress();
	
	// TCP test will be started in a separate task after network initialization
	
	// Create packet reception task for KSZ8851SNL
	if (xTaskCreate(ksz8851snl_packet_task,
	                "KSZ_RX",
	                512,  // Stack size
	                NULL,
	                (tskIDLE_PRIORITY + 2),  // Medium priority
	                NULL)
	    != pdPASS) {
		printf("Failed to create KSZ8851SNL packet reception task\r\n");
	} else {
		printf("KSZ8851SNL packet reception task created\r\n");
	}
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
	
	// Create TCP test task after network is fully initialized
	if (xTaskCreate(raw_tcp_test_task_wrapper,
	                "TCP_TEST",
	                1024,  // Stack size
	                NULL,
	                (tskIDLE_PRIORITY + 1),  // Medium priority
	                NULL)
	    != pdPASS) {
		printf("Failed to create TCP test task\r\n");
	} else {
		printf("TCP test task created successfully\r\n");
	}
	
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
// #ifdef USE_KSZ8851SNL_INTERFACE
// 	// Create KSZ8851SNL link monitoring task
// 	if (xTaskCreate(ksz8851snl_link_monitor_task,
// 	                "KSZ_LINK",
// 	                256,  // Stack size
// 	                NULL,
// 	                (tskIDLE_PRIORITY + 1),  // Low priority
// 	                NULL)
// 	    != pdPASS) {
// 		printf("Failed to create KSZ8851SNL link monitoring task\r\n");
// 	} else {
// 		printf("KSZ8851SNL link monitoring task created\r\n");
// 	}
// #else
// 	hw_eth_start_link_monitor(&eth_communication);
// #endif

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
