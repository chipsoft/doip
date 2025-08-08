/*
 * NetworkBufferManagement.c - FreeRTOS-Plus-TCP Buffer Management
 * This file implements network buffer allocation and management for FreeRTOS-Plus-TCP
 */

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"
#include "printf.h"

#include <string.h>

/* Buffer management using heap allocation */

/*
 * Allocate a NetworkBufferDescriptor_t and a buffer of the requested size
 */
NetworkBufferDescriptor_t *pxGetNetworkBufferWithDescriptor(size_t xRequestedSizeBytes,
                                                             TickType_t xBlockTimeTicks)
{
    NetworkBufferDescriptor_t *pxReturn = NULL;
    uint8_t *pucEthernetBuffer;

    /* Ensure the requested size is at least large enough to hold an Ethernet header */
    if (xRequestedSizeBytes < sizeof(EthernetHeader_t)) {
        xRequestedSizeBytes = sizeof(EthernetHeader_t);
    }

    /* Round up to nearest 32-byte boundary for alignment */
    if ((xRequestedSizeBytes & 0x1fUL) != 0U) {
        xRequestedSizeBytes = (xRequestedSizeBytes | 0x1fUL) + 1U;
    }

    /* Block time is not used in this simple implementation */
    (void)xBlockTimeTicks;

    /* Allocate memory for the descriptor */
    pxReturn = (NetworkBufferDescriptor_t *)pvPortMalloc(sizeof(NetworkBufferDescriptor_t));

    if (pxReturn != NULL) {
        /* Allocate memory for the Ethernet buffer */
        pucEthernetBuffer = (uint8_t *)pvPortMalloc(xRequestedSizeBytes + ipBUFFER_PADDING);

        if (pucEthernetBuffer != NULL) {
            /* Initialize the buffer descriptor */
            pxReturn->pucEthernetBuffer = pucEthernetBuffer;
            pxReturn->xDataLength = xRequestedSizeBytes;

            /* Add padding offset to ensure proper alignment */
            pxReturn->pucEthernetBuffer += ipBUFFER_PADDING;

            #if (ipconfigUSE_LINKED_RX_MESSAGES != 0)
            {
                pxReturn->pxNextBuffer = NULL;
            }
            #endif
        } else {
            /* Failed to allocate buffer memory, free the descriptor */
            vPortFree(pxReturn);
            pxReturn = NULL;
            printf("[NET-BUF] ERROR: Failed to allocate buffer memory (%d bytes)\r\n", 
                   xRequestedSizeBytes + ipBUFFER_PADDING);
        }
    } else {
        printf("[NET-BUF] ERROR: Failed to allocate buffer descriptor\r\n");
    }

    return pxReturn;
}

/*
 * Release a NetworkBufferDescriptor_t and associated buffer
 */
void vReleaseNetworkBufferAndDescriptor(NetworkBufferDescriptor_t * const pxNetworkBuffer)
{
    if (pxNetworkBuffer != NULL) {
        if (pxNetworkBuffer->pucEthernetBuffer != NULL) {
            /* Remove padding offset before freeing */
            uint8_t *pucBuffer = pxNetworkBuffer->pucEthernetBuffer - ipBUFFER_PADDING;
            vPortFree(pucBuffer);
        }

        vPortFree(pxNetworkBuffer);
    }
}

/*
 * Get a free network buffer (just the buffer, not the descriptor)
 */
uint8_t *pxGetNetworkBuffer(size_t *pxRequestedSizeBytes)
{
    uint8_t *pucReturn = NULL;
    size_t xRequestedSize = *pxRequestedSizeBytes;

    /* Ensure minimum size */
    if (xRequestedSize < sizeof(EthernetHeader_t)) {
        xRequestedSize = sizeof(EthernetHeader_t);
    }

    /* Round up to nearest 32-byte boundary */
    if ((xRequestedSize & 0x1fUL) != 0U) {
        xRequestedSize = (xRequestedSize | 0x1fUL) + 1U;
    }

    /* Allocate buffer with padding */
    pucReturn = (uint8_t *)pvPortMalloc(xRequestedSize + ipBUFFER_PADDING);

    if (pucReturn != NULL) {
        /* Add padding offset */
        pucReturn += ipBUFFER_PADDING;
        *pxRequestedSizeBytes = xRequestedSize;
    } else {
        printf("[NET-BUF] ERROR: Failed to allocate network buffer (%d bytes)\r\n", 
               xRequestedSize + ipBUFFER_PADDING);
        *pxRequestedSizeBytes = 0;
    }

    return pucReturn;
}

/*
 * Release a network buffer (not a descriptor)
 */
void vReleaseNetworkBuffer(uint8_t *pucBuffer)
{
    if (pucBuffer != NULL) {
        /* Remove padding offset before freeing */
        uint8_t *pucActualBuffer = pucBuffer - ipBUFFER_PADDING;
        vPortFree(pucActualBuffer);
    }
}

/*
 * Get the number of free network buffers
 * This is a simple implementation that doesn't track buffer counts
 */
UBaseType_t uxGetNumberOfFreeNetworkBuffers(void)
{
    /* In this heap-based implementation, we don't track buffer counts */
    /* Return a reasonable estimate based on available heap */
    size_t xFreeHeapSize = xPortGetFreeHeapSize();
    UBaseType_t uxFreeBuffers = (UBaseType_t)(xFreeHeapSize / (ipconfigNETWORK_BUFFER_SIZE + sizeof(NetworkBufferDescriptor_t)));
    
    /* Ensure we don't return more than the configured maximum */
    if (uxFreeBuffers > ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS) {
        uxFreeBuffers = ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS;
    }
    
    return uxFreeBuffers;
}

/*
 * Get the minimum number of free network buffers
 * This is used for debugging and monitoring
 */
UBaseType_t uxGetMinimumFreeNetworkBuffers(void)
{
    /* In this simple implementation, return current free count */
    return uxGetNumberOfFreeNetworkBuffers();
}

/*
 * Initialize the network buffer management system
 * This function is called during FreeRTOS-Plus-TCP initialization
 */
BaseType_t xNetworkBuffersInitialise(void)
{
    printf("[NET-BUF] Initializing network buffer management (heap-based)\r\n");
    printf("[NET-BUF] Buffer size: %d bytes\r\n", ipconfigNETWORK_BUFFER_SIZE);
    printf("[NET-BUF] Maximum buffers: %d\r\n", ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS);
    
    /* No specific initialization needed for heap-based allocation */
    return pdTRUE;
}