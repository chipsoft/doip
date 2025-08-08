/**
 * \file freertos_tcp_hooks.c
 *
 * \brief FreeRTOS-Plus-TCP callback implementations
 *
 * Provides required callback functions for FreeRTOS-Plus-TCP v4.x.
 */

#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "printf.h"

/**
 * \brief Network event hook for multi-endpoint configurations
 * 
 * This function is called when network events occur on any endpoint.
 * Required by newer versions of FreeRTOS-Plus-TCP with multi-endpoint support.
 */
void vApplicationIPNetworkEventHook_Multi(eIPCallbackEvent_t eNetworkEvent, struct xNetworkEndPoint *pxEndPoint)
{
    (void)pxEndPoint;
    
    switch (eNetworkEvent) {
        case eNetworkUp:
            printf("\r\n[FreeRTOS-Plus-TCP] Network is UP\r\n");
            printf("[TCP/IP] Stack ready for communication\r\n");
            
            /* Log network configuration */
            {
                uint32_t ulIPAddress, ulNetMask, ulGatewayAddress, ulDNSServerAddress;
                char cBuffer[16];
                
                /* Get network configuration */
                FreeRTOS_GetEndPointConfiguration(&ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress, NULL);
                
                printf("\r\n[CONFIG] Network Configuration:\r\n");
                FreeRTOS_inet_ntoa(ulIPAddress, cBuffer);
                printf("  IP Address: %s\r\n", cBuffer);
                FreeRTOS_inet_ntoa(ulNetMask, cBuffer);
                printf("  Subnet Mask: %s\r\n", cBuffer);
                FreeRTOS_inet_ntoa(ulGatewayAddress, cBuffer);
                printf("  Gateway: %s\r\n", cBuffer);
                FreeRTOS_inet_ntoa(ulDNSServerAddress, cBuffer);
                printf("  DNS Server: %s\r\n", cBuffer);
                printf("\r\n");
            }
            break;
            
        case eNetworkDown:
            printf("\r\n[FreeRTOS-Plus-TCP] Network is DOWN\r\n");
            printf("[TCP/IP] Connection lost\r\n");
            break;
            
        default:
            printf("[FreeRTOS-Plus-TCP] Unknown network event: %d\r\n", (int)eNetworkEvent);
            break;
    }
}

/**
 * \brief DHCP hook for multi-endpoint configurations
 * 
 * Called during DHCP processing to allow application to customize behavior.
 * Return eDHCPContinue to continue normal DHCP processing.
 */
eDHCPCallbackAnswer_t xApplicationDHCPHook_Multi(eDHCPCallbackPhase_t eDHCPPhase, 
                                                 struct xNetworkEndPoint *pxEndPoint, 
                                                 IP_Address_t *pxIPAddress)
{
    (void)pxEndPoint;
    (void)pxIPAddress;
    
    switch (eDHCPPhase) {
        case eDHCPPhasePreDiscover:
            printf("[DHCP] Starting DHCP discovery\r\n");
            break;
            
        case eDHCPPhasePreRequest:
            printf("[DHCP] Sending DHCP request\r\n");
            break;
            
        default:
            break;
    }
    
    return eDHCPContinue;
}

/**
 * \brief Ping reply hook
 * 
 * Called when a ping reply is received.
 */
void vApplicationPingReplyHook(ePingReplyStatus_t eStatus, uint16_t usIdentifier)
{
    switch (eStatus) {
        case eSuccess:
            printf("[PING] Reply received (ID: %u)\r\n", usIdentifier);
            break;
            
        case eInvalidChecksum:
            printf("[PING] Invalid checksum (ID: %u)\r\n", usIdentifier);
            break;
            
        case eInvalidData:
            printf("[PING] Invalid data (ID: %u)\r\n", usIdentifier);
            break;
    }
}

/**
 * \brief Random number generator
 * 
 * Required by FreeRTOS-Plus-TCP for security purposes.
 * This is a simple implementation - production code should use hardware RNG.
 */
BaseType_t xApplicationGetRandomNumber(uint32_t *pulNumber)
{
    /* Simple pseudo-random number generator */
    static uint32_t ulSeed = 0x12345678UL;
    
    ulSeed = (ulSeed * 1664525UL) + 1013904223UL;
    *pulNumber = ulSeed;
    
    return pdTRUE;
}

/**
 * \brief DNS clear function stub
 * 
 * Called to clear DNS cache when network goes down.
 */
void FreeRTOS_dnsclear(void)
{
    printf("[DNS] Cache cleared\r\n");
}

/**
 * \brief System call stub functions
 * 
 * Required by newlib nano for embedded systems.
 */
int _write(int file, char *data, int len)
{
    (void)file;
    (void)data;
    (void)len;
    return 0;
}

int _read(int file, char *data, int len)
{
    (void)file;
    (void)data;
    (void)len;
    return 0;
}