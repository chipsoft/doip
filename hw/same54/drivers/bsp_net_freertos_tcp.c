#include "bsp_net.h"
#include "driver_net_freertos_tcp.h"
#include "bsp_ethernet.h"
#include "utils_assert.h"
#include "printf.h"

// FreeRTOS-Plus-TCP includes
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"

// Hardware includes  
#include "hal_mac_async.h"
#include "hal_gpio.h"

#include <string.h>

// Static hardware context for FreeRTOS-Plus-TCP
static drv_net_freertos_tcp_context_t drv_net_freertos_tcp_hw_context_0 = {
    .mac_address = {0x00, 0x00, 0x00, 0x00, 0x20, 0x76},
    .network_up = false,
    .link_up = false,
    .has_ip_address = false,
    .packets_sent = 0,
    .packets_received = 0,
    .send_errors = 0,
    .recv_errors = 0,
};

// Global driver instance for FreeRTOS-Plus-TCP
drv_net_t freertos_tcp_network_0 = {
    .is_init = false,
    .is_started = false,
    .stack_type = DRV_NET_STACK_FREERTOS_TCP,
    .hw_context = &drv_net_freertos_tcp_hw_context_0,
    
    // Function pointers to implementation
    .init = drv_net_freertos_tcp_init_impl,
    .deinit = drv_net_freertos_tcp_deinit_impl,
    .start = drv_net_freertos_tcp_start_impl,
    .stop = drv_net_freertos_tcp_stop_impl,
    .get_status = drv_net_freertos_tcp_get_status_impl,
    .wait_for_link = drv_net_freertos_tcp_wait_for_link_impl,
    .wait_for_ip = drv_net_freertos_tcp_wait_for_ip_impl,
    .print_network_info = drv_net_freertos_tcp_print_network_info_impl,
    .ping = drv_net_freertos_tcp_ping_impl,
    .register_callback = drv_net_freertos_tcp_register_callback_impl,
};

// External reference to Ethernet driver
extern drv_eth_t eth_communication;

/*
 * FreeRTOS-Plus-TCP Network Interface Implementation
 * These functions are required by FreeRTOS-Plus-TCP and are called by the stack
 */

// Network interface initialization function - called by FreeRTOS-Plus-TCP
BaseType_t xNetworkInterfaceInitialise(void)
{
    printf("[BSP-NET] Initializing network interface for FreeRTOS-Plus-TCP\r\n");
    
    // Initialize Ethernet driver if not already done
    if (!eth_communication.is_init) {
        drv_eth_status_t eth_result = hw_eth_init(&eth_communication);
        if (eth_result != DRV_ETH_STATUS_OK) {
            printf("[BSP-NET] ERROR: Failed to initialize Ethernet driver\r\n");
            return pdFAIL;
        }
    }
    
    // Enable Ethernet interface
    drv_eth_status_t eth_result = hw_eth_enable(&eth_communication);
    if (eth_result != DRV_ETH_STATUS_OK) {
        printf("[BSP-NET] ERROR: Failed to enable Ethernet interface\r\n");
        return pdFAIL;
    }
    
    printf("[BSP-NET] Network interface initialized successfully\r\n");
    return pdPASS;
}

// Network interface output function - called when FreeRTOS-Plus-TCP needs to send a packet
BaseType_t xNetworkInterfaceOutput(NetworkBufferDescriptor_t * const pxDescriptor, 
                                   BaseType_t xReleaseAfterSend)
{
    BaseType_t result = pdFAIL;
    
    if (pxDescriptor == NULL || pxDescriptor->pucEthernetBuffer == NULL) {
        printf("[BSP-NET] ERROR: Invalid buffer descriptor\r\n");
        return pdFAIL;
    }
    
    // Check if we can send the packet
    if (!eth_communication.is_enabled) {
        printf("[BSP-NET] ERROR: Ethernet interface not enabled\r\n");
        drv_net_freertos_tcp_hw_context_0.send_errors++;
        return pdFAIL;
    }
    
    // Send packet through Ethernet driver
    drv_eth_status_t eth_result = hw_eth_write(&eth_communication, 
                                               pxDescriptor->pucEthernetBuffer,
                                               pxDescriptor->xDataLength);
    
    if (eth_result == DRV_ETH_STATUS_OK) {
        result = pdPASS;
        drv_net_freertos_tcp_hw_context_0.packets_sent++;
        printf("[BSP-NET] Sent packet (%d bytes)\r\n", pxDescriptor->xDataLength);
    } else {
        printf("[BSP-NET] ERROR: Failed to send packet\r\n");
        drv_net_freertos_tcp_hw_context_0.send_errors++;
    }
    
    // Release the buffer if requested
    if (xReleaseAfterSend != pdFALSE) {
        vReleaseNetworkBufferAndDescriptor(pxDescriptor);
    }
    
    return result;
}

// Get PHY link status - called by FreeRTOS-Plus-TCP to check link status
BaseType_t xGetPhyLinkStatus(struct xNetworkInterface * pxInterface)
{
    (void)pxInterface; // Ignore interface parameter for now
    
    // Get link status from Ethernet driver
    bool link_up = false;
    drv_eth_status_t result = hw_eth_get_link_status(&eth_communication, &link_up);
    
    if (result == DRV_ETH_STATUS_OK && link_up) {
        drv_net_freertos_tcp_hw_context_0.link_up = true;
        return pdTRUE;
    } else {
        drv_net_freertos_tcp_hw_context_0.link_up = false;
        return pdFALSE;
    }
}

// Network interface input task - processes received packets
static void prvNetworkInterfaceInput(void *pvParameters)
{
    (void)pvParameters;
    
    printf("[BSP-NET] Network interface input task started (simplified)\r\n");
    
    while (1) {
        // For now, just idle - packet reception would be handled by 
        // interrupt service routines in a full implementation
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Start network interface input task
static BaseType_t prvStartNetworkInterfaceInputTask(void)
{
    static TaskHandle_t xNetworkInputTaskHandle = NULL;
    
    if (xNetworkInputTaskHandle == NULL) {
        BaseType_t result = xTaskCreate(
            prvNetworkInterfaceInput,
            "NetInput",
            512,  // Stack size
            NULL, // Parameters
            configMAX_PRIORITIES - 1, // High priority
            &xNetworkInputTaskHandle
        );
        
        if (result != pdPASS) {
            printf("[BSP-NET] ERROR: Failed to create network input task\r\n");
            return pdFAIL;
        }
        
        printf("[BSP-NET] Network input task created successfully\r\n");
    }
    
    return pdPASS;
}

// Application hook called when network goes up
void vApplicationIPNetworkEventHook(eIPCallbackEvent_t eNetworkEvent)
{
    drv_net_freertos_tcp_context_t *context = &drv_net_freertos_tcp_hw_context_0;
    
    switch (eNetworkEvent) {
        case eNetworkUp:
            printf("[BSP-NET] Network is up!\r\n");
            context->network_up = true;
            context->has_ip_address = true;
            
            // Start network input task
            prvStartNetworkInterfaceInputTask();
            
            // Call registered callback
            if (context->ip_acquired_callback != NULL) {
                context->ip_acquired_callback();
            }
            break;
            
        case eNetworkDown:
            printf("[BSP-NET] Network is down!\r\n");
            context->network_up = false;
            context->has_ip_address = false;
            
            // Call registered callback
            if (context->ip_lost_callback != NULL) {
                context->ip_lost_callback();
            }
            break;
            
        default:
            printf("[BSP-NET] Unknown network event: %d\r\n", eNetworkEvent);
            break;
    }
}

// Hook for generating random numbers (required by FreeRTOS-Plus-TCP)
uint32_t ulApplicationGetNextSequenceNumber(uint32_t ulSourceAddress,
                                            uint16_t usSourcePort,
                                            uint32_t ulDestinationAddress,
                                            uint16_t usDestinationPort)
{
    (void)ulSourceAddress;
    (void)usSourcePort;
    (void)ulDestinationAddress;
    (void)usDestinationPort;
    
    // Simple random number generation
    // In production, this should use a proper random number generator
    static uint32_t ulNextRand = 1;
    ulNextRand = (ulNextRand * 1103515245UL + 12345UL) & 0x7fffffffUL;
    return ulNextRand;
}

// Hook for getting time (required for TCP timestamps)
uint32_t ulApplicationGetTimestamp(void)
{
    // Return current tick count as timestamp
    return (uint32_t)xTaskGetTickCount();
}

// BSP wrapper functions called by NetworkInterface.c
BaseType_t xBSPNetworkInterfaceInitialise(void)
{
    return xNetworkInterfaceInitialise();
}

BaseType_t xBSPNetworkInterfaceOutput(NetworkBufferDescriptor_t * const pxDescriptor,
                                      BaseType_t xReleaseAfterSend)
{
    return xNetworkInterfaceOutput(pxDescriptor, xReleaseAfterSend);
}

BaseType_t xBSPGetPhyLinkStatus(void)
{
    return xGetPhyLinkStatus(NULL);
}