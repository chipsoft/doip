/*
 * NetworkInterface.c - FreeRTOS-Plus-TCP Network Interface Implementation
 * This file implements the required network interface functions for FreeRTOS-Plus-TCP
 * It delegates the actual work to the BSP-specific implementation
 */

#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"

// Forward declare the BSP functions that are implemented in bsp_net_freertos_tcp.c
extern BaseType_t xBSPNetworkInterfaceInitialise(void);
extern BaseType_t xBSPNetworkInterfaceOutput(NetworkBufferDescriptor_t * const pxDescriptor, 
                                             BaseType_t xReleaseAfterSend);
extern BaseType_t xBSPGetPhyLinkStatus(void);

/*
 * Initialize the network interface hardware
 * This function is called by FreeRTOS-Plus-TCP during IP stack initialization
 */
BaseType_t xNetworkInterfaceInitialise(void)
{
    // This function is implemented in bsp_net_freertos_tcp.c
    // It handles the hardware-specific initialization
    return xBSPNetworkInterfaceInitialise();
}

/*
 * Send a network packet
 * This function is called by FreeRTOS-Plus-TCP when it needs to send a packet
 */
BaseType_t xNetworkInterfaceOutput(NetworkBufferDescriptor_t * const pxDescriptor,
                                   BaseType_t xReleaseAfterSend)
{
    // This function is implemented in bsp_net_freertos_tcp.c
    // It handles the hardware-specific packet transmission
    return xBSPNetworkInterfaceOutput(pxDescriptor, xReleaseAfterSend);
}

/*
 * Get the PHY link status
 * This function is called by FreeRTOS-Plus-TCP to check if the network link is up
 */
BaseType_t xGetPhyLinkStatus(struct xNetworkInterface * pxInterface)
{
    // This function is implemented in bsp_net_freertos_tcp.c
    // It checks the hardware link status
    (void)pxInterface; // Ignore interface parameter for now
    return xBSPGetPhyLinkStatus();
}

/*
 * Application-defined hook to process network events
 * This weak implementation can be overridden by the application
 */
__attribute__((weak)) void vApplicationIPNetworkEventHook(eIPCallbackEvent_t eNetworkEvent)
{
    // Default implementation - can be overridden in application code
    (void)eNetworkEvent;
}

/*
 * Generate a randomized TCP sequence number
 * This weak implementation can be overridden by the application
 */
__attribute__((weak)) uint32_t ulApplicationGetNextSequenceNumber(uint32_t ulSourceAddress,
                                                                  uint16_t usSourcePort,
                                                                  uint32_t ulDestinationAddress,
                                                                  uint16_t usDestinationPort)
{
    (void)ulSourceAddress;
    (void)usSourcePort;
    (void)ulDestinationAddress;
    (void)usDestinationPort;
    
    // Simple PRNG for sequence numbers
    static uint32_t ulNextRand = 1;
    ulNextRand = (ulNextRand * 1103515245UL + 12345UL) & 0x7fffffffUL;
    return ulNextRand;
}

/*
 * Get current timestamp for TCP
 * This weak implementation can be overridden by the application
 */
__attribute__((weak)) uint32_t ulApplicationGetTimestamp(void)
{
    return (uint32_t)xTaskGetTickCount();
}